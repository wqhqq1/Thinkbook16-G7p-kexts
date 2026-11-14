#include "OhMyDSPACE.hpp"
#include "UserClient.hpp"
#include "AceCaptureTrial.hpp"
#include "AceCoexistence.hpp"
#include "AceAnalogPlayback.hpp"
#include "AppleHDAStreamLease.hpp"

#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMapper.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <kern/clock.h>

extern "C" const uint8_t thinkbookSofFirmware[];
extern "C" const uint8_t thinkbookSofFirmwareEnd[];
extern "C" const uint8_t thinkbookDmicConfig[];
extern "C" const uint8_t thinkbookDmicConfigEnd[];
extern "C" const uint8_t thinkbookHostConfig[];
extern "C" const uint8_t thinkbookHostConfigEnd[];

namespace {

#include "AcePlaybackProfiles.inc"

struct Mmio {
    IOMemoryMap *map;

    IOByteCount length() { return map->getLength(); }

    template <class T> volatile T &reg(uint32_t offset) {
        return *reinterpret_cast<volatile T *>(map->getVirtualAddress() + offset);
    }

    uint8_t read8(uint32_t offset) { return reg<uint8_t>(offset); }
    uint16_t read16(uint32_t offset) { return reg<uint16_t>(offset); }
    uint32_t read32(uint32_t offset) { return reg<uint32_t>(offset); }
    void write8(uint32_t offset, uint8_t value) { reg<uint8_t>(offset) = value; }
    void write16(uint32_t offset, uint16_t value) { reg<uint16_t>(offset) = value; }
    void write32(uint32_t offset, uint32_t value) { reg<uint32_t>(offset) = value; }
};

struct Delay {
    uint64_t nowMs() {
        uint64_t uptime, nanoseconds;
        clock_get_uptime(&uptime);
        absolutetime_to_nanoseconds(uptime, &nanoseconds);
        return nanoseconds / 1000000;
    }
    void sleepMs(unsigned ms) { IOSleep(ms); }
    void delayUs(unsigned us) { IODelay(us); }
};

struct BdlEntry {
    uint64_t address;
    uint32_t length;
    uint32_t flags;
};
static_assert(sizeof(BdlEntry) == 16, "Intel HDA BDLE size");

void flushDmaCache(uint8_t *buffer, uint32_t size) {
    // Intel HDA may be using non-snooped DMA. This machine has 64-byte cache lines.
    for (uint32_t offset = 0; offset < size; offset += 64)
        __builtin_ia32_clflush(buffer + offset);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

struct DmaImage {
    IOBufferMemoryDescriptor *memory = nullptr;
    uint64_t bdl = 0;
    uint32_t bytes = 0;
    uint16_t entries = 0;
    bool prepared = false;
    uint8_t *buffer = nullptr;

    ~DmaImage() {
        if (memory) {
            if (prepared)
                memory->complete();
            memory->release();
        }
    }

    const char *prepareFirmware() {
        constexpr uint32_t kExtendedManifestSize = 0xc14;
        constexpr uint32_t kPayloadSize = 1155072;
        const auto fileSize = reinterpret_cast<uintptr_t>(thinkbookSofFirmwareEnd) -
            reinterpret_cast<uintptr_t>(thinkbookSofFirmware);
        if (fileSize != kExtendedManifestSize + kPayloadSize ||
            memcmp(thinkbookSofFirmware, "$AE1", 4) != 0 ||
            memcmp(thinkbookSofFirmware + kExtendedManifestSize, "$CPD", 4) != 0)
            return "firmware-layout-mismatch";

        return prepare(kPayloadSize, 65536, kIODirectionOut, thinkbookSofFirmware + kExtendedManifestSize);
    }

    const char *prepare(uint32_t sizeBytes, uint32_t segmentBytes, IODirection direction,
                        const uint8_t *source = nullptr) {
        bytes = sizeBytes;
        const auto segments = (bytes + segmentBytes - 1) / segmentBytes;
        if (segments < 2 || segments > 256)
            return "dma-buffer-segment-count-unusable";
        entries = static_cast<uint16_t>(segments);
        const uint32_t bdlOffset = (bytes + 127U) & ~127U;
        const uint32_t size = bdlOffset + entries * sizeof(BdlEntry);
        memory = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
            direction | kIOMemoryPhysicallyContiguous | kIOMemoryMapperNone, size, 0xfffff000ULL);
        if (!memory || memory->prepare() != kIOReturnSuccess)
            return "dma-buffer-allocation-failed";
        prepared = true;
        IOByteCount span = 0;
        const uint64_t bus = memory->getPhysicalSegment(0, &span, kIOMemoryMapperNone);
        buffer = static_cast<uint8_t *>(memory->getBytesNoCopy());
        if (!buffer || !bus || (bus & 127) || span < size || bus > 0x100000000ULL - size)
            return "dma-buffer-address-unusable";

        memset(buffer, 0, size);
        if (source)
            memcpy(buffer, source, bytes);
        auto descriptors = reinterpret_cast<BdlEntry *>(buffer + bdlOffset);
        for (uint32_t i = 0, offset = 0; i < entries; ++i) {
            const uint32_t remaining = bytes - offset;
            const uint32_t length = remaining < segmentBytes ? remaining : segmentBytes;
            descriptors[i] = {bus + offset, length, 0};
            offset += length;
        }
        flushDmaCache(buffer, size);
        bdl = bus + bdlOffset;
        return nullptr;
    }
};

} // namespace

struct AceBackend {
    static_assert(ace::CaptureTimeline::bytes == ace::kCaptureBytes, "Capture clock and DMA ring geometry must match");
    static void *operator new(size_t size) { return IOMalloc(size); }
    static void operator delete(void *p, size_t size) { if (p) IOFree(p, size); }
    OhMyDSPACE *owner = nullptr;
    IOService *controller = nullptr;
    IOPCIDevice *pci = nullptr;
    IOWorkLoop *controllerLoop = nullptr;
    Mmio hda {nullptr}, dsp {nullptr};
    DmaImage firmware, capture;
    ace::Report boot;
    ace::CaptureBuffer buffer;
    ace::CaptureSession session;
    ace::DspPowerState power;
    ace::IpcResult dmi {"not-run"}, d0ix {"not-run"};
    ace::AnalogPlayback playback;
    AppleHDAStreamLease lease;
    const char *result = "ready-for-client";
    uint8_t pmCap = 0;
    bool resident = false, leaseHeld = false, poisoned = false, resumePending = false;
    uint64_t idleSinceMs = 0;
    bool idleObserved = false;
    enum Op { Start, Sample, Stop, Shutdown, ShutdownNow };
    static constexpr unsigned kPollMs = 5;
    static constexpr unsigned kShutdownIdleMs = 200;

    ~AceBackend() {
        // An unconfirmed DMA stop must never free memory still reachable by DMA.
        if (!boot.dmaSafe) { firmware.memory = nullptr; firmware.prepared = false; }
        if (!buffer.report.dmaSafe || leaseHeld) { capture.memory = nullptr; capture.prepared = false; }
        if (hda.map) hda.map->release();
        if (dsp.map) dsp.map->release();
        if (controllerLoop) controllerLoop->release();
    }
    bool setup(OhMyDSPACE *o, IOService *c, IOPCIDevice *p) {
        owner = o; controller = c; pci = p;
        controllerLoop = controller->getWorkLoop();
        if (!controllerLoop) return false;
        controllerLoop->retain();
        pci->findPCICapability(1, &pmCap);
        result = firmware.prepareFirmware();
        if (result) return false;
        result = capture.prepare(ace::kCaptureBytes, ace::kCapturePeriodBytes, kIODirectionIn);
        if (result) return false;
        if (thinkbookDmicConfigEnd - thinkbookDmicConfig != ace::kDmicPayloadBytes ||
            thinkbookHostConfigEnd - thinkbookHostConfig != ace::kHostPayloadBytes) return false;
        buffer.bdl = capture.bdl; buffer.samples = capture.buffer;
        buffer.dmic = thinkbookDmicConfig; buffer.host = thinkbookHostConfig;
        buffer.readBarrier = flushDmaCache;
        result = "ready-for-client";
        return true;
    }
    bool valid(Op op) {
        if ((owner->isInactive() && (op == Start || op == Sample)) || controller->isInactive() || pci->isInactive() ||
            owner->getProvider() != controller || controller->getProvider() != pci ||
            pci->configRead32(kIOPCIConfigVendorID) != 0x77288086 ||
            pci->configRead32(kIOPCIConfigSubSystemVendorID) != 0x384717aa ||
            pmCap < 0x40 || pmCap > 0xf8 || (pmCap & 3) || IOMapper::gSystem) return false;
        const auto command = pci->configRead16(kIOPCIConfigCommand);
        if (command == UINT16_MAX || (command & 6) != 6 ||
            (pci->configRead16(static_cast<uint8_t>(pmCap + 4)) & 3)) return false;
        if (!hda.map) hda.map = pci->mapDeviceMemoryWithRegister(0x10, kIOMapInhibitCache);
        if (!dsp.map) dsp.map = pci->mapDeviceMemoryWithRegister(0x20, kIOMapInhibitCache);
        return hda.map && dsp.map && hda.length() >= 0x4000 && dsp.length() >= ace::kCoreControl + 4;
    }
    bool ownsCapture() {
        if (!session.configured) return false;
        const auto &s = session.stream;
        const auto pp = hda.read32(s.ppctl);
        return s.index < ace::kInputs && hda.read16(0) == 0x9a01 &&
            pp != UINT32_MAX && (pp & (1U << s.index)) &&
            hda.read8(s.offset + 2) >> 4 == s.tag &&
            hda.read32(s.offset + 0x18) == static_cast<uint32_t>(buffer.bdl) &&
            hda.read32(s.offset + 0x1c) == static_cast<uint32_t>(buffer.bdl >> 32);
    }
    void releaseLease() {
        if (!leaseHeld) return;
        if (buffer.report.dmaSafe && buffer.report.streamRestored && lease.returnToController(controller)) {
            leaseHeld = false; buffer.report.leaseResult = "returned-to-applehda";
        } else {
            buffer.report.leaseResult = "retained-until-reboot"; poisoned = true;
        }
    }
    IOReturn execute(Op op) {
        if (!valid(op)) {
            result = "controller-not-ready";
            if (session.configured) { buffer.report.dmaSafe = false; poisoned = true; }
            return kIOReturnOffline;
        }
        Delay clock;
        if (resumePending) {
            resumePending = false;
            const auto ss = dsp.read32(ace::kSubsystem);
            if (!leaseHeld && !playback.touched && ss != UINT32_MAX && !(ss & (ace::kSubsystemPowered | ace::kSubsystemRequest))) {
                resident = false; power = {}; boot = {};
            } else if (resident) { result = "resume-requires-reboot"; poisoned = true; }
        }
        if (op == Start) {
            if (poisoned) return kIOReturnNotReady;
            const auto format = hda.read16(ace::AnalogPlayback::sd + 0x12);
            const ace::PlaybackProfile *profile = nullptr;
            for (const auto &candidate : kAnalogProfiles)
                if (candidate.hdaFormat == format) profile = &candidate;
            if (!profile) {
                result = "speaker-format-not-supported-by-topology";
                return kIOReturnUnsupported;
            }
            if (!resident) {
                boot = {}; power = {};
                boot.result = ace::run<Mmio, Delay>(hda, dsp, clock, firmware.bdl, firmware.bytes,
                    firmware.entries, boot, nullptr, nullptr, &power);
                result = boot.result;
                resident = power.coreTouched;
                if (!resident) {
                    poisoned = !boot.dmaSafe || !boot.streamRestored || !boot.dspRestored;
                    return boot.hardwareTouched ? kIOReturnIOError : kIOReturnBusy;
                }
                // Hold ACE's DMI link in L0 while the DSP is resident. AppleHDA's
                // coupled speaker streams are not registered in SOF's DMA inventory.
                if (!ace::holdDmiL0(dsp, clock, boot, dmi) || !ace::preventD0ix(dsp, clock, boot, d0ix)) {
                    result = "firmware-coexistence-policy-failed";
                    uint32_t running, sync;
                    if (ace::readHdaActivity(hda, running, sync) && !running && !(sync & ace::kStreamMask)) {
                        ace::shutdownDsp(dsp, clock, boot, power);
                        resident = !boot.dspRestored;
                    }
                    poisoned = true;
                    return kIOReturnIOError;
                }
            }
            const auto ss = dsp.read32(ace::kSubsystem);
            if (ss == UINT32_MAX || !(ss & ace::kSubsystemPowered)) {
                result = "resident-firmware-power-lost"; poisoned = true; return kIOReturnOffline;
            }
            if (!playback.prepare(hda, dsp, clock, boot, *profile)) {
                result = playback.status;
                if (playback.touched && !playback.restore(hda, dsp, clock, boot)) poisoned = true;
                return kIOReturnIOError;
            }
            buffer.report = {}; session = {};
            if (!lease.resolve(controller) || !lease.acquire(controller)) {
                result = "applehda-input-allocation-failed"; return kIOReturnBusy;
            }
            leaseHeld = true;
            buffer.report.leaseResult = "reserved-from-applehda";
            buffer.reservedIndex = static_cast<uint32_t>(lease.index);
            buffer.reservedTag = static_cast<uint32_t>(lease.tag);
            if (lease.kind != AppleHDAStreamLease::kInput) {
                result = "unexpected-stream-kind"; releaseLease(); return kIOReturnIOError;
            }
            boot.requirePlayback = true; // permit AppleHDA output while recording
            boot.ppControl = hda.read32(boot.ppOffset + 4);
            memset(capture.buffer, 0, ace::kCaptureBytes);
            flushDmaCache(capture.buffer, ace::kCaptureBytes);
            if (ace::beginCapture(hda, dsp, clock, boot, buffer, session)) {
                idleObserved = false; result = "capturing"; return kIOReturnSuccess;
            }
            result = buffer.report.result;
            ace::endCapture(hda, dsp, clock, boot, buffer, session);
            releaseLease(); session.configured = false;
            return kIOReturnIOError;
        }
        if (op == Sample || op == Stop) {
            if (!session.configured) return kIOReturnSuccess;
            if (!ownsCapture()) {
                result = "input-descriptor-ownership-lost";
                poisoned = true; buffer.report.dmaSafe = false; return kIOReturnOffline;
            }
            if (op == Sample) {
                if (!playback.observe(hda, dsp, clock, boot)) {
                    result = playback.status;
                    return kIOReturnIOError;
                }
                const auto ok = ace::pollCapture(hda, boot, buffer, session);
                if (!ok) result = buffer.report.result;
                return ok ? kIOReturnSuccess : kIOReturnIOError;
            }
            ace::endCapture(hda, dsp, clock, boot, buffer, session);
            releaseLease(); session.configured = false;
            poisoned = poisoned || !boot.ipcSafeForD3;
            result = poisoned ? "capture-stop-unconfirmed" : "capture-stopped";
            return poisoned ? kIOReturnIOError : kIOReturnSuccess;
        }
        if ((op == Shutdown || op == ShutdownNow) && resident && !leaseHeld && !poisoned) {
            uint32_t running, sync;
            if (!ace::readHdaActivity(hda, running, sync) || (sync & ace::kStreamMask)) {
                idleObserved = false; return kIOReturnBusy;
            }
            if (playback.ready && !playback.observe(hda, dsp, clock, boot)) {
                result = playback.status;
                poisoned = !boot.ipcSafeForD3;
                return kIOReturnIOError;
            }
            if (running) {
                idleObserved = false;
                return kIOReturnBusy;
            }
            const auto now = clock.nowMs();
            if (!idleObserved) {
                idleObserved = true;
                idleSinceMs = now;
            }
            if (op == Shutdown && now - idleSinceMs < kShutdownIdleMs) return kIOReturnBusy;
            if (!playback.restore(hda, dsp, clock, boot)) {
                result = "speaker-pipeline-cleanup-unconfirmed";
                poisoned = true;
                return kIOReturnIOError;
            }
            ace::shutdownDsp(dsp, clock, boot, power);
            idleObserved = false;
            resident = !boot.dspRestored;
            poisoned = resident || (boot.ready && !boot.d3Accepted);
            result = poisoned ? "dsp-shutdown-unconfirmed" : "idle-dsp-off";
            return poisoned ? kIOReturnIOError : kIOReturnSuccess;
        }
        return kIOReturnSuccess;
    }
    static IOReturn action(OSObject *, void *arg, void *op, void *, void *) {
        return static_cast<AceBackend *>(arg)->execute(static_cast<Op>(reinterpret_cast<uintptr_t>(op)));
    }
    IOReturn run(Op op) {
        return controllerLoop->runAction(action, owner, this, reinterpret_cast<void *>(static_cast<uintptr_t>(op)));
    }
};

OSDefineMetaClassAndStructors(OhMyDSPACE, IOService)

bool OhMyDSPACE::start(IOService *provider) {
    auto pci = provider ? OSDynamicCast(IOPCIDevice, provider->getProvider()) : nullptr;
    if (!pci || strcmp(provider->getMetaClass()->getClassName(), "AppleHDAController") ||
        pci->configRead32(kIOPCIConfigVendorID) != 0x77288086 ||
        pci->configRead32(kIOPCIConfigSubSystemVendorID) != 0x384717aa ||
        !IOService::start(provider)) return false;
    backend = new AceBackend;
    if (!backend || !backend->setup(this, provider, pci)) return false;
    loop = IOWorkLoop::workLoop();
    gate = IOCommandGate::commandGate(this);
    timer = IOTimerEventSource::timerEventSource(this, tick);
    ring = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, kIODirectionInOut | kIOMemoryKernelUserShared,
                                                      ace::kCaptureBytes, page_size);
    status = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, kIODirectionInOut | kIOMemoryKernelUserShared, 4096, page_size);
    if (!loop || !gate || !timer || !ring || !status || loop->addEventSource(gate) || loop->addEventSource(timer)) return false;
    memset(ring->getBytesNoCopy(), 0, ace::kCaptureBytes);
    memset(status->getBytesNoCopy(), 0, 4096);
    st = static_cast<OhMyDSPStatus *>(status->getBytesNoCopy());
    st->magic = OHMYDSP_STATUS_MAGIC; st->version = OHMYDSP_STATUS_VERSION;
    st->ringBytes = ace::kCaptureBytes; st->frameBytes = 8; st->rate = 48000; st->channels = 2;
    setProperty("DriverVersion", "0.3.7");
    setProperty("Firmware", "Intel-signed SOF 2.15.0.1 / ACE / IPC4");
    setProperty("StartupPassive", true);
    PMinit(); pmReady = true;
    static IOPMPowerState states[2] = {
        {1,0,0,0,0,0,0,0,0,0,0,0},
        {1,kIOPMPowerOn,kIOPMPowerOn,kIOPMPowerOn,0,0,0,0,0,0,0,0}
    };
    registerPowerDriver(this, states, 2); provider->joinPMtree(this);
    publishDiagnostics(); registerService();
    return true;
}
IOReturn OhMyDSPACE::newUserClient(task_t task, void *token, UInt32 type, OSDictionary *props, IOUserClient **handler) {
    if (!handler) return kIOReturnBadArgument;
    *handler = nullptr;
    auto client = new OhMyDSPACEUserClient;
    if (!client) return kIOReturnNoMemory;
    if (!client->initWithTask(task, token, type, props)) { client->release(); return kIOReturnNotPrivileged; }
    if (!client->attach(this)) { client->release(); return kIOReturnNotAttached; }
    if (!client->start(this)) { client->detach(this); client->release(); return kIOReturnExclusiveAccess; }
    *handler = client; return kIOReturnSuccess;
}
IOMemoryDescriptor *OhMyDSPACE::sharedMemory(UInt32 type) const {
    return type == OHMYDSP_MEM_RING ? ring : type == OHMYDSP_MEM_STATUS ? status : nullptr;
}
IOReturn OhMyDSPACE::gateStart(OSObject *o, void *, void *, void *, void *) { return static_cast<OhMyDSPACE *>(o)->begin(); }
IOReturn OhMyDSPACE::gateStop(OSObject *o, void *, void *, void *, void *) { return static_cast<OhMyDSPACE *>(o)->end(); }
IOReturn OhMyDSPACE::startCapture() { return gate && !isInactive() ? gate->runAction(gateStart) : kIOReturnOffline; }
IOReturn OhMyDSPACE::stopCapture() { return gate ? gate->runAction(gateStop) : kIOReturnSuccess; }

void OhMyDSPACE::publish(UInt32 state, UInt32 position, UInt64 completedLaps, UInt64 hostTime) {
    if (!st) return;
    st->seq++; __sync_synchronize();
    st->lpib = position; st->state = state; st->laps = completedLaps; st->lapHostTime = hostTime;
    st->epoch = epoch; st->faults = faults;
    __sync_synchronize(); st->seq++;
}
IOReturn OhMyDSPACE::begin() {
    if (active) return kIOReturnSuccess;
    if (suspended || !backend || backend->poisoned) return kIOReturnNotReady;
    timer->cancelTimeout();
    const auto result = backend->run(AceBackend::Start);
    publishDiagnostics();
    if (result != kIOReturnSuccess) {
        if (backend->resident && !backend->poisoned) timer->setTimeoutMS(AceBackend::kPollMs);
        return result;
    }
    memset(ring->getBytesNoCopy(), 0, ace::kCaptureBytes);
    active = true;
    uint64_t now, ns; clock_get_uptime(&now); absolutetime_to_nanoseconds(now, &ns);
    timeline.reset(ns); ++epoch;
    publish(OHMYDSP_STATE_RUNNING, 0, 0, now);
    timer->setTimeoutMS(AceBackend::kPollMs);
    return kIOReturnSuccess;
}
IOReturn OhMyDSPACE::end() {
    if (!backend) return kIOReturnSuccess;
    if (timer) timer->cancelTimeout();
    IOReturn result = kIOReturnSuccess;
    if (active || backend->session.configured) result = backend->run(AceBackend::Stop);
    active = false;
    if (ring) memset(ring->getBytesNoCopy(), 0, ace::kCaptureBytes);
    uint64_t lapTime; nanoseconds_to_absolutetime(timeline.lapNs, &lapTime);
    publish(OHMYDSP_STATE_STOPPED, timeline.position, timeline.laps, lapTime);
    if (backend->resident && !backend->poisoned && !suspended) timer->setTimeoutMS(AceBackend::kPollMs);
    publishDiagnostics();
    return result;
}
void OhMyDSPACE::tick(OSObject *o, IOTimerEventSource *) { static_cast<OhMyDSPACE *>(o)->poll(); }
void OhMyDSPACE::poll() {
    if (!backend || suspended) return;
    if (!active) {
        backend->run(AceBackend::Shutdown);
        publishDiagnostics();
        if (backend->resident && !backend->poisoned) timer->setTimeoutMS(AceBackend::kPollMs);
        return;
    }
    if (backend->run(AceBackend::Sample) != kIOReturnSuccess) { ++faults; end(); return; }
    uint64_t now, ns; clock_get_uptime(&now); absolutetime_to_nanoseconds(now, &ns);
    const auto pos = backend->buffer.report.lastPosition;
    const auto lastPosition = timeline.position;
    const auto update = timeline.observe(pos, ns);
    if (!update.valid || update.stalled) {
        backend->result = update.stalled ? "capture-dma-stalled" : "capture-position-invalid";
        ++faults; end(); return;
    }
    auto dst = static_cast<uint8_t *>(ring->getBytesNoCopy());
    // Only completed frames are copied. If a timer was delayed for a whole ring,
    // discard that interval and advertise a discontinuity rather than stale audio.
    flushDmaCache(backend->capture.buffer, ace::kCaptureBytes);
    if (update.gap) {
        memset(dst, 0, ace::kCaptureBytes); ++epoch; ++faults;
    } else if (pos != lastPosition) {
        if (pos < lastPosition) {
            memcpy(dst + lastPosition, backend->capture.buffer + lastPosition, ace::kCaptureBytes - lastPosition);
            memcpy(dst, backend->capture.buffer, pos);
        } else memcpy(dst + lastPosition, backend->capture.buffer + lastPosition, pos - lastPosition);
    }
    uint64_t lapTime; nanoseconds_to_absolutetime(timeline.lapNs, &lapTime);
    publish(OHMYDSP_STATE_RUNNING, pos, timeline.laps, lapTime);
    timer->setTimeoutMS(AceBackend::kPollMs);
}
IOReturn OhMyDSPACE::gatePower(OSObject *o, void *p, void *, void *, void *) {
    auto self = static_cast<OhMyDSPACE *>(o);
    if (!p) {
        self->suspended = true; self->end();
        if (self->backend && self->backend->resident && !self->backend->poisoned) {
            self->backend->run(AceBackend::ShutdownNow);
        }
    } else {
        if (self->suspended && self->backend) self->backend->resumePending = true;
        self->suspended = false;
    }
    return kIOReturnSuccess;
}
IOReturn OhMyDSPACE::setPowerState(unsigned long state, IOService *) {
    if (gate) gate->runAction(gatePower, reinterpret_cast<void *>(state));
    return IOPMAckImplied;
}
void OhMyDSPACE::publishDiagnostics() {
    if (!backend) return;
    auto &b = *backend;
    setProperty("ACEState", b.result ? b.result : "setup-failed");
    setProperty("ACEFirmwareResult", b.boot.result);
    setProperty("ACEDmiL0Result", b.dmi.status); setProperty("ACEDmiL0Reply", b.dmi.reply, 32);
    setProperty("ACED0ixResult", b.d0ix.status); setProperty("ACED0ixReply", b.d0ix.reply, 32);
    setProperty("ACEFirmwareResident", b.resident); setProperty("ACEBlockedUntilReboot", b.poisoned);
    setProperty("ACEInputLease", b.buffer.report.leaseResult);
    setProperty("ACELeaseStage", b.lease.stage);
    setProperty("ACELeaseAcquireResult", static_cast<uint32_t>(b.lease.acquireResult), 32);
    setProperty("ACELeaseReleaseResult", static_cast<uint32_t>(b.lease.releaseResult), 32);
    setProperty("ACELeaseMethodDelta", b.lease.methodDelta, 32);
    setProperty("ACELeaseImagePageOffset", b.lease.imagePageOffset, 32);
    setProperty("ACELeaseHeaderMagic", b.lease.headerMagic, 32);
    setProperty("ACELeaseHeaderType", b.lease.headerType, 32);
    setProperty("ACELeaseHeaderCommands", b.lease.headerCommands, 32);
    setProperty("ACELeaseUuidOffset", b.lease.uuidOffset, 32);
    setProperty("ACELeaseMetaOffset", b.lease.metaOffset, 64);
    setProperty("ACELeaseAllocPrologue", b.lease.allocPrologue, 64);
    setProperty("ACELeaseReleasePrologue", b.lease.releasePrologue, 64);
    setProperty("ACELeaseTag", static_cast<uint32_t>(b.lease.tag), 32);
    setProperty("ACELeaseKind", static_cast<uint32_t>(b.lease.kind), 32);
    setProperty("ACELeaseIndex", static_cast<uint32_t>(b.lease.index), 32);
    setProperty("ACECaptureResult", b.buffer.report.result);
    setProperty("ACECaptureFailedRequest", b.buffer.report.failedRequest, 32);
    setProperty("ACECaptureFailedReply", b.buffer.report.failedReply, 32);
    setProperty("ACECapturePositionChanges", b.buffer.report.positionChanges, 32);
    setProperty("ACEPlaybackPositionChanges", b.buffer.report.playbackPositionChanges, 32);
    setProperty("ACESpeakerState", b.playback.status);
    setProperty("ACESpeakerIPC", b.playback.ipc.status);
    setProperty("ACESpeakerRequest", b.playback.lastRequest, 32);
    setProperty("ACESpeakerReply", b.playback.ipc.reply, 32);
    setProperty("ACESpeakerFailedRequest", b.playback.failedRequest, 32);
    setProperty("ACESpeakerFailedReply", b.playback.failedReply, 32);
    setProperty("ACESpeakerRunPolls", b.playback.runningPolls, 32);
    setProperty("ACESpeakerPositionChanges", b.playback.positionChanges, 32);
    setProperty("ACESpeakerHostProgress", b.playback.hostProgress, 32);
    setProperty("ACESpeakerLinkProgress", b.playback.linkProgress, 32);
    setProperty("ACESpeakerStarts", b.playback.starts, 32);
    setProperty("ACESpeakerHostReinitialized", b.playback.hostReinitialized);
    setProperty("ACESpeakerHostResetUnconfirmed", b.playback.hostResetUnconfirmed);
    if (b.playback.runningPolls) {
        setProperty("ACESpeakerFirstTrace", &b.playback.first, sizeof(b.playback.first));
        setProperty("ACESpeakerLatestTrace", &b.playback.latest, sizeof(b.playback.latest));
    } else {
        removeProperty("ACESpeakerFirstTrace");
        removeProperty("ACESpeakerLatestTrace");
    }
    setProperty("ACESpeakerRestored", !b.playback.touched);
    setProperty("ACESpeakerHostControl", b.playback.hostControl, 32);
    setProperty("ACESpeakerHostBytes", b.playback.hostBytes, 32);
    setProperty("ACESpeakerFormat", b.playback.format, 16);
    setProperty("ACESpeakerCodecMask", b.playback.codecMask, 16);
    setProperty("ACESpeakerPPBefore", b.playback.ppBefore, 32);
    setProperty("ACEInputDmaSafe", b.buffer.report.dmaSafe);
    setProperty("ACELoaderDmaSafe", b.boot.dmaSafe);
    setProperty("ACEDspRestored", b.boot.dspRestored);
    setProperty("ACED3Result", b.boot.d3Result);
}
void OhMyDSPACE::stop(IOService *provider) {
    stopCapture();
    if (timer) timer->cancelTimeout();
    if (pmReady) { PMstop(); pmReady = false; }
    IOService::stop(provider);
}
void OhMyDSPACE::free() {
    if (timer) { timer->cancelTimeout(); if (loop) loop->removeEventSource(timer); timer->release(); }
    if (gate) { if (loop) loop->removeEventSource(gate); gate->release(); }
    if (loop) loop->release();
    if (ring) ring->release(); if (status) status->release();
    if (backend) delete backend;
    IOService::free();
}

