#pragma once

#include <stdint.h>

namespace ace {

// PCI 8086:7728 on this board uses the MTL ACE register layout and SOF IPC4.
constexpr uint32_t kHdaStreamBase = 0x80;
constexpr uint32_t kHdaStreamStride = 0x20;
constexpr uint32_t kHdaStreamRun = 1U << 1;
constexpr uint32_t kHdaStreamReset = 1U;
constexpr uint32_t kHdaStreamStatus = 0x1c;
constexpr uint32_t kPpGlobalEnable = 1U << 30;
constexpr uint32_t kPpInterruptEnable = 1U << 31;
constexpr uint32_t kInputs = 10;
constexpr uint32_t kOutputs = 9;
constexpr uint32_t kStreams = kInputs + kOutputs;
constexpr uint32_t kStreamMask = (1U << kStreams) - 1;
constexpr uint32_t kInputMask = (1U << kInputs) - 1;

constexpr uint32_t kSubsystem = 0x1000;
constexpr uint32_t kSubsystemRequest = 1U << 16;
constexpr uint32_t kSubsystemPowered = 1U << 24;
constexpr uint32_t kHostPowerControl = 0x1d18;
constexpr uint32_t kHostPowerStatus = 0x1d1c;
constexpr uint32_t kCoreControl = 0x178d04;
constexpr uint32_t kCoreRequest = 1U;
constexpr uint32_t kCorePowered = 1U << 8;
constexpr uint32_t kOwnerMask = 3U << 24;
constexpr uint32_t kHostOwner = 2U << 24;
constexpr uint32_t kHostIpcInterrupt = 0x1140;

constexpr uint32_t kRequest = 0x73210;
constexpr uint32_t kReplyAck = 0x73214;
constexpr uint32_t kTargetRequest = 0x73200;
constexpr uint32_t kTargetAck = 0x73204;
constexpr uint32_t kTargetExtension = 0x73300;
constexpr uint32_t kRequestExtension = 0x73380;
constexpr uint32_t kHostMailbox = 0x188000;
constexpr uint32_t kBusy = 1U << 31;
constexpr uint32_t kIpc4Reply = 1U << 29;
constexpr uint32_t kIpc4TypeTargetMask = 0x5f000000;
constexpr uint32_t kIpc4StatusMask = 0x00ffffff;
constexpr uint32_t kIpc4SetDx = 0x47000000;
constexpr uint32_t kRomStatus = 0x163200;
constexpr uint32_t kRomError = 0x163204;
constexpr uint32_t kRomStateMask = 0x00ffffff;
constexpr uint32_t kRomInitDone = 1;
constexpr uint32_t kRomFirmwareEntered = 5;
constexpr uint32_t kReadyNotificationMask = 0x7fff0000;
constexpr uint32_t kReadyNotification = 0x1b080000;
constexpr uint32_t kExceptionNotification = 0x1b0a0000;
constexpr uint32_t kResourceNotification = 0x1b050000;

struct Report {
    const char *result = "not-run";
    const char *streamSelection = "not-run";
    const char *d3Result = "not-run";
    const char *dspCleanupStage = "not-needed";
    uint32_t streamIndex = UINT32_MAX;
    uint32_t streamTag = 0;
    uint32_t runningMask = 0;
    uint32_t hdaIntctl = 0;
    uint32_t hdaSsync = 0;
    uint32_t ppOffset = 0;
    uint32_t spibOffset = 0;
    uint32_t ppControl = 0;
    uint32_t spibControl = 0;
    uint32_t candidateMask = 0;
    uint32_t rejection[kOutputs] = {};
    uint32_t controlStatus[kOutputs] = {};
    uint16_t format[kOutputs] = {};
    uint32_t romAck = 0;
    uint32_t romStatus = 0;
    uint32_t romError = 0;
    uint32_t dmaPosition = 0;
    uint32_t dmaStatus = 0;
    uint32_t notification = 0;
    uint32_t notificationExt = 0;
    uint32_t d3Reply = 0;
    uint32_t coreBeforeShutdown = UINT32_MAX;
    uint32_t coreAfterShutdown = UINT32_MAX;
    uint32_t hostStatusBeforeSubsystemOff = UINT32_MAX;
    uint32_t subsystemAfterShutdown = UINT32_MAX;
    bool d3Ack = false;
    bool d3Accepted = false;
    bool d3RequestIdle = false;
    bool ipcSafeForD3 = true;
    bool requirePlayback = false;
    bool hardwareTouched = false;
    bool romInitialized = false;
    bool entered = false;
    bool ready = false;
    bool dmaSafe = true;
    bool streamRestored = true;
    bool dspRestored = true;
};

struct Stream {
    uint32_t index = 0;
    uint32_t tag = 0;
    uint32_t offset = 0;
    uint32_t ppctl = 0;
    uint32_t spbctl = 0;
    uint32_t spib = 0;
    uint32_t savedPpctl = 0;
    uint32_t savedSpbctl = 0;
    uint32_t savedSpib = 0;
    uint32_t savedCbl = 0;
    uint16_t savedLvi = 0;
    uint16_t savedFmt = 0;
    uint8_t savedCtl2 = 0;
};

template <class Bar>
bool update32(Bar &bar, uint32_t reg, uint32_t mask, uint32_t value) {
    const auto old = bar.read32(reg);
    if (old == UINT32_MAX)
        return false;
    bar.write32(reg, (old & ~mask) | (value & mask));
    return true;
}

template <class Bar, class Clock>
bool wait32(Bar &bar, Clock &clock, uint32_t reg, uint32_t mask, uint32_t expected,
            unsigned limitMs) {
    const auto deadline = clock.nowMs() + limitMs;
    for (;;) {
        const auto value = bar.read32(reg);
        if (value == UINT32_MAX)
            return false;
        if ((value & mask) == expected)
            return true;
        if (clock.nowMs() >= deadline)
            return false;
        clock.sleepMs(1);
    }
}

template <class Bar>
bool acknowledgeTarget(Bar &dsp, uint32_t message) {
    dsp.write32(kTargetRequest, message);
    return update32(dsp, kTargetAck, kBusy, 0);
}

struct IpcResult {
    const char *status;
    uint32_t reply = 0;
    bool done = false;
    bool received = false;
    bool reusable = false;
    bool completed = false;

    bool accepted() const { return completed && !(reply & kIpc4StatusMask); }
};

template <class Bar, class Clock>
IpcResult sendIpc(Bar &dsp, Clock &clock, uint32_t primary, uint32_t extension,
                  const uint8_t *payload = nullptr, uint32_t bytes = 0,
                  unsigned timeoutMs = 500) {
    IpcResult result {"mailbox-unavailable"};
    if (bytes > 4096 || (bytes & 3) || (bytes && !payload) || dsp.length() < kHostMailbox + bytes)
        return result;
    result.status = "host-request-still-busy";
    if (!wait32(dsp, clock, kRequest, kBusy, 0, timeoutMs < 100 ? timeoutMs : 100))
        return result;
    const auto priorAck = dsp.read32(kReplyAck);
    auto priorTarget = dsp.read32(kTargetRequest);
    // Chain DMA can report a resource event while no IPC request is pending.
    // Acknowledge that notification without consuming an earlier command reply.
    if ((priorTarget & kBusy) && (priorTarget & kReadyNotificationMask) == kResourceNotification) {
        if (!acknowledgeTarget(dsp, priorTarget)) {
            result.status = "resource-notification-ack-failed";
            return result;
        }
        priorTarget = dsp.read32(kTargetRequest);
    }
    if (priorAck == UINT32_MAX || priorTarget == UINT32_MAX || (priorTarget & kBusy)) {
        result.status = "ipc-inbox-unavailable-or-pending";
        return result;
    }
    if (priorAck & kBusy)
        dsp.write32(kReplyAck, kBusy);

    for (uint32_t offset = 0; offset < bytes; offset += 4) {
        const auto *p = payload + offset;
        dsp.write32(kHostMailbox + offset, p[0] | (uint32_t(p[1]) << 8) |
            (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24));
    }
    dsp.write32(kRequestExtension, extension);
    dsp.write32(kRequest, kBusy | primary);
    const auto deadline = clock.nowMs() + timeoutMs;
    for (;;) {
        const auto ack = dsp.read32(kReplyAck);
        const auto target = dsp.read32(kTargetRequest);
        const auto request = dsp.read32(kRequest);
        if (ack == UINT32_MAX || target == UINT32_MAX || request == UINT32_MAX) {
            result.status = "ipc-registers-unavailable";
            return result;
        }
        if (ack & kBusy) {
            dsp.write32(kReplyAck, kBusy);
            result.done = true;
        }
        if (target & kBusy) {
            if (!acknowledgeTarget(dsp, target)) {
                result.status = "target-ack-failed";
                return result;
            }
            if (target & kIpc4Reply) {
                result.reply = target & ~kBusy;
                if (result.received ||
                    (target & kIpc4TypeTargetMask) != (primary & kIpc4TypeTargetMask)) {
                    result.status = "unexpected-reply";
                    return result;
                }
                result.received = true;
            } else if ((target & kReadyNotificationMask) == kExceptionNotification) {
                result.status = "firmware-exception";
                return result;
            }
        }
        // D3 is terminal. On this controller it can leave BUSY asserted
        // even after a successful reply. No further IPC follows D3.
        const bool requestIdle = !(request & kBusy);
        const bool terminalD3Reply = primary == kIpc4SetDx && !(result.reply & kIpc4StatusMask);
        if (result.received && (requestIdle || terminalD3Reply)) {
            result.reusable = requestIdle;
            result.completed = true;
            if (!result.accepted())
                result.status = "firmware-rejected";
            else if (!requestIdle)
                result.status = "accepted-terminal-reply";
            else
                result.status = result.done ? "accepted" : "accepted-reply-only";
            return result;
        }
        if (clock.nowMs() >= deadline) {
            result.status = result.received ? "request-busy-after-reply" : "reply-timeout";
            return result;
        }
        clock.sleepMs(1);
    }
}

template <class Bar, class Clock>
void prepareFirmwareD3(Bar &dsp, Clock &clock, Report &report) {
    constexpr uint8_t d3Payload[8] = {1};
    const auto result = sendIpc(dsp, clock, kIpc4SetDx, 0, d3Payload, sizeof(d3Payload));
    report.d3Result = result.status;
    report.d3Reply = result.reply;
    report.d3Ack = result.done;
    report.d3Accepted = result.accepted();
    report.d3RequestIdle = result.reusable;
    if (report.d3Accepted && !report.d3RequestIdle)
        clock.sleepMs(5);
}

template <class Bar, class Clock>
bool waitStream(Bar &hda, Clock &clock, uint32_t offset, uint8_t mask, uint8_t expected) {
    for (unsigned i = 0; i < 100; ++i) {
        const auto value = hda.read8(offset);
        if (value == UINT8_MAX)
            return false;
        if ((value & mask) == expected)
            return true;
        clock.delayUs(10);
    }
    return false;
}

template <class Bar>
bool findStream(Bar &hda, Stream &stream, Report &report) {
    report.streamSelection = "hda-controller-not-ready";
    const auto control = hda.read32(8);
    if (hda.read16(0) != 0x9a01 || control == UINT32_MAX || !(control & 1))
        return false;
    report.hdaIntctl = hda.read32(0x20);
    report.hdaSsync = hda.read32(0x34);
    if (report.hdaIntctl == UINT32_MAX || report.hdaSsync == UINT32_MAX)
        return false;

    uint32_t tags = 0;
    for (uint32_t i = 0; i < kStreams; ++i) {
        const auto offset = kHdaStreamBase + kHdaStreamStride * i;
        const auto ctl = hda.read32(offset);
        if (ctl == UINT32_MAX)
            return false;
        if (ctl & kHdaStreamRun)
            report.runningMask |= 1U << i;
        if (i >= kInputs) {
            report.controlStatus[i - kInputs] = ctl;
            tags |= 1U << ((ctl >> 20) & 0xf);
        }
    }
    report.streamSelection = "hda-stream-active";
    if (report.runningMask)
        return false;
    report.streamSelection = "hda-stream-sync-pending";
    if (report.hdaSsync & kStreamMask)
        return false;

    uint32_t next = hda.read16(0x14);
    for (unsigned i = 0; i < 10 && next >= kHdaStreamBase &&
         next + 4 <= hda.length() && !(next & 3); ++i) {
        const auto header = hda.read32(next);
        const auto id = (header >> 16) & 0xfff;
        if (id == 3 && next + 8 <= hda.length())
            report.ppOffset = next;
        if (id == 4 && next + 8 + kStreams * 8 <= hda.length())
            report.spibOffset = next;
        const auto following = header & 0xffff;
        if (following == next)
            break;
        next = following;
    }
    report.streamSelection = "processing-pipe-cap-unavailable";
    if (!report.ppOffset)
        return false;
    stream.ppctl = report.ppOffset + 4;
    stream.savedPpctl = hda.read32(stream.ppctl);
    report.ppControl = stream.savedPpctl;
    report.streamSelection = "processing-pipe-disabled-or-busy";
    if (stream.savedPpctl == UINT32_MAX || !(stream.savedPpctl & kPpGlobalEnable) ||
        (stream.savedPpctl & (kStreamMask | kPpInterruptEnable)))
        return false;

    report.streamSelection = "spib-cap-unavailable";
    if (!report.spibOffset)
        return false;
    stream.spbctl = report.spibOffset + 4;
    stream.savedSpbctl = hda.read32(stream.spbctl);
    report.spibControl = stream.savedSpbctl;
    if (stream.savedSpbctl == UINT32_MAX)
        return false;

    const uint32_t reserved = report.hdaIntctl | report.hdaSsync | stream.savedSpbctl;
    for (uint32_t i = kInputs; i < kStreams; ++i) {
        const auto n = i - kInputs;
        const auto offset = kHdaStreamBase + kHdaStreamStride * i;
        const auto ctl = report.controlStatus[n];
        const auto tag = n + 1;
        const auto spib = stream.spbctl + 4 + 8 * i;
        const auto spibValue = hda.read32(spib);
        const auto cbl = hda.read32(offset + 8);
        const auto lvi = hda.read16(offset + 0xc);
        const auto fmt = hda.read16(offset + 0x12);
        report.format[n] = fmt;
        // Ignore direction, traffic priority and striping when identifying
        // an idle stream. Save them and the descriptor defaults as read.
        uint32_t reason = 0;
        if (ctl & 0x1cf0001fU)
            reason |= 1U;
        if (hda.read32(offset + 0x18) || hda.read32(offset + 0x1c))
            reason |= 2U;
        if (reserved & (1U << i))
            reason |= 4U;
        if (tags & (1U << tag))
            reason |= 8U;
        if (spibValue == UINT32_MAX || cbl == UINT32_MAX || lvi == UINT16_MAX || fmt == UINT16_MAX)
            reason |= 16U;
        report.rejection[n] = reason;
        if (reason)
            continue;
        report.candidateMask |= 1U << i;
        if (!stream.tag) {
            stream.index = i;
            stream.tag = tag;
            stream.offset = offset;
            stream.spib = spib;
            stream.savedSpib = spibValue;
            stream.savedCbl = cbl;
            stream.savedLvi = lvi;
            stream.savedFmt = fmt;
            stream.savedCtl2 = static_cast<uint8_t>(ctl >> 16);
        }
    }
    report.streamSelection = stream.tag ? "idle-unbound-playback-stream" : "no-idle-unbound-playback-stream";
    return stream.tag != 0;
}

struct DspPowerState {
    uint32_t subsystem = 0;
    uint32_t savedCore = 0;
    uint32_t host = 0;
    bool coreTouched = false;
    bool hostTouched = false;
};

template <class Bar, class Clock>
void shutdownDsp(Bar &dsp, Clock &clock, Report &report, const DspPowerState &state) {
    report.dspRestored = true;
    if (state.coreTouched) {
        report.coreBeforeShutdown = dsp.read32(kCoreControl);
        if (report.coreBeforeShutdown != UINT32_MAX && (report.coreBeforeShutdown & kCorePowered)) {
            report.romStatus = dsp.read32(kRomStatus);
            report.romError = dsp.read32(kRomError);
            if (report.ready && report.dmaSafe && report.ipcSafeForD3)
                prepareFirmwareD3(dsp, clock, report);
        }
        report.dspCleanupStage = "primary-core-power-down";
        const bool requestedOff = update32(dsp, kCoreControl, kCoreRequest, 0);
        clock.sleepMs(1);
        report.dspRestored = requestedOff &&
            wait32(dsp, clock, kCoreControl, kCorePowered | kCoreRequest, 0, 50);
        report.coreAfterShutdown = dsp.read32(kCoreControl);
    }
    // Only gate the parent domain once the primary core has stopped. The
    // host-island status may stay asserted until the subsystem powers off.
    if (report.dspRestored) {
        const bool ownerRestored = !state.coreTouched ||
            (update32(dsp, kCoreControl, kOwnerMask, state.savedCore) &&
             wait32(dsp, clock, kCoreControl, kOwnerMask, state.savedCore & kOwnerMask, 5));
        const bool hostRestored = !state.hostTouched ||
            (update32(dsp, kHostPowerControl, 1, state.host) &&
             wait32(dsp, clock, kHostPowerControl, 1, state.host & 1, 5));
        if (state.hostTouched)
            report.hostStatusBeforeSubsystemOff = dsp.read32(kHostPowerStatus);
        report.dspCleanupStage = "subsystem-power-down";
        const bool requestedOff = update32(dsp, kSubsystem, kSubsystemRequest, state.subsystem);
        clock.sleepMs(1);
        const bool subsystemOff = requestedOff && wait32(dsp, clock, kSubsystem,
            kSubsystemPowered | kSubsystemRequest, 0, 50);
        report.subsystemAfterShutdown = dsp.read32(kSubsystem);
        report.dspRestored = ownerRestored && hostRestored && subsystemOff;
        if (subsystemOff)
            report.dspCleanupStage = report.dspRestored ? "complete" : "power-controls-unconfirmed";
    }
}

template <class Bar>
bool readHdaActivity(Bar &hda, uint32_t &running, uint32_t &sync) {
    running = 0;
    sync = hda.read32(0x34);
    if (hda.read16(0) != 0x9a01 || sync == UINT32_MAX)
        return false;
    for (uint32_t i = 0; i < kStreams; ++i) {
        const auto ctl = hda.read8(kHdaStreamBase + kHdaStreamStride * i);
        if (ctl == UINT8_MAX)
            return false;
        if (ctl & kHdaStreamRun)
            running |= 1U << i;
    }
    return true;
}

template <class Bar, class Clock>
bool shutdownResidentIfIdle(Bar &hda, Bar &dsp, Clock &clock, Report &report,
                            const DspPowerState &state) {
    uint32_t running, sync;
    if (!readHdaActivity(hda, running, sync) || running || (sync & kStreamMask))
        return false;
    shutdownDsp(dsp, clock, report, state);
    return true;
}

template <class Bar, class Clock>
const char *run(Bar &hda, Bar &dsp, Clock &clock, uint64_t bdlAddress,
                uint32_t bytes, uint16_t entries, Report &report,
                void (*afterBoot)(Bar &, Bar &, Clock &, Report &, void *) = nullptr,
                void *captureContext = nullptr, DspPowerState *resident = nullptr) {
    Stream stream;
    if (hda.length() < 0x4000 || dsp.length() < kCoreControl + 4)
        return "bar-too-short";
    if (!bdlAddress || (bdlAddress & 127) || !bytes || (bytes & 3) || entries < 2 || entries > 256)
        return "invalid-loader-descriptor";
    if (!findStream(hda, stream, report))
        return report.streamSelection;

    const auto subsystem = dsp.read32(kSubsystem);
    if (subsystem == UINT32_MAX || (subsystem & (kSubsystemRequest | kSubsystemPowered)))
        return "dsp-not-fully-off";
    report.streamIndex = stream.index;
    report.streamTag = stream.tag;
    const auto sd = stream.offset;
    const auto streamBit = 1U << stream.index;
    bool hostTouched = false;
    bool coreTouched = false;
    bool streamTouched = false;
    bool descriptorInstalled = false;
    uint32_t savedCore = 0;
    uint32_t host = 0;

    // Keep all new interrupt routes disabled. ROM and FW_READY are polled.
    const char *result = "subsystem-power-timeout";
    report.hardwareTouched = true;
    dsp.write32(kSubsystem, subsystem | kSubsystemRequest);
    clock.sleepMs(1);
    do {
        if (!wait32(dsp, clock, kSubsystem, kSubsystemPowered, kSubsystemPowered, 50))
            break;
        host = dsp.read32(kHostPowerControl);
        result = "host-island-not-fully-off";
        if (host == UINT32_MAX || (host & 1))
            break;
        result = "host-island-power-timeout";
        hostTouched = true;
        dsp.write32(kHostPowerControl, host | 1U);
        clock.sleepMs(1);
        if (!wait32(dsp, clock, kHostPowerStatus, 1, 1, 50))
            break;

        savedCore = dsp.read32(kCoreControl);
        result = "core-or-ipc-in-use";
        const auto ipcInterrupt = dsp.read32(kHostIpcInterrupt);
        if (savedCore == UINT32_MAX || (savedCore & (kCoreRequest | kCorePowered)) ||
            ipcInterrupt == UINT32_MAX || (ipcInterrupt & 1) ||
            (dsp.read32(kRequest) & kBusy) || (dsp.read32(kReplyAck) & kBusy) ||
            (dsp.read32(kTargetRequest) & kBusy))
            break;

        {
            streamTouched = true;
            hda.write32(stream.ppctl, stream.savedPpctl | streamBit);
            hda.write8(sd, kHdaStreamReset);
            result = "stream-reset-assert-timeout";
            if (!waitStream(hda, clock, sd, kHdaStreamReset, kHdaStreamReset))
                break;
            hda.write8(sd, 0);
            result = "stream-reset-release-timeout";
            if (!waitStream(hda, clock, sd, kHdaStreamReset, 0))
                break;

            hda.write8(sd + 2, stream.savedCtl2 | static_cast<uint8_t>(stream.tag << 4));
            hda.write32(sd + 8, bytes);
            hda.write16(sd + 0xc, entries - 1);
            hda.write16(sd + 0x12, 0x40);
            hda.write32(sd + 0x18, static_cast<uint32_t>(bdlAddress));
            hda.write32(sd + 0x1c, static_cast<uint32_t>(bdlAddress >> 32));
            hda.write32(stream.spbctl, stream.savedSpbctl | streamBit);
            hda.write32(stream.spib, bytes);
            descriptorInstalled = true;
            result = "stream-program-readback-failed";
            if (hda.read8(sd + 2) >> 4 != stream.tag ||
                hda.read32(sd + 8) != bytes || hda.read16(sd + 0xc) != entries - 1 ||
                hda.read16(sd + 0x12) != 0x40 || hda.read32(sd + 0x18) != static_cast<uint32_t>(bdlAddress) ||
                hda.read32(sd + 0x1c) != static_cast<uint32_t>(bdlAddress >> 32) ||
                hda.read32(stream.spib) != bytes || !(hda.read32(stream.ppctl) & streamBit) ||
                !(hda.read32(stream.spbctl) & streamBit))
                break;
        }

        // ACE ROM receives the purge command before the primary core is released.
        dsp.write32(kRequest, kBusy | 0x01004000U | ((stream.tag - 1) << 9));
        coreTouched = true;
        dsp.write32(kCoreControl, (savedCore & ~kOwnerMask) | kHostOwner);
        dsp.write32(kCoreControl, (savedCore & ~kOwnerMask) | kHostOwner | kCoreRequest);
        clock.sleepMs(1);
        result = "primary-core-power-timeout";
        if (!wait32(dsp, clock, kCoreControl, kCorePowered, kCorePowered, 50))
            break;

        result = "rom-purge-ack-timeout";
        if (!wait32(dsp, clock, kReplyAck, kBusy, kBusy, 500))
            break;
        report.romAck = dsp.read32(kReplyAck);
        dsp.write32(kReplyAck, kBusy);

        result = "rom-init-timeout";
        if (!wait32(dsp, clock, kRomStatus, kBusy | kRomStateMask, kRomInitDone, 300))
            break;
        report.romInitialized = true;
        hda.write8(sd, kHdaStreamRun);
        result = "dma-run-timeout";
        if (!waitStream(hda, clock, sd, kHdaStreamRun, kHdaStreamRun))
            break;

        result = "firmware-ready-timeout";
        const auto deadline = clock.nowMs() + 3000;
        while (clock.nowMs() < deadline) {
            const auto status = dsp.read32(kRomStatus);
            const auto message = dsp.read32(kTargetRequest);
            if (status == UINT32_MAX || message == UINT32_MAX) {
                result = "dsp-registers-lost";
                break;
            }
            if (status & kBusy) {
                result = "rom-or-firmware-halted";
                break;
            }
            if (!report.entered && (status & kRomStateMask) == kRomFirmwareEntered) {
                report.entered = true;
                // Do not allow the cyclic code-loader buffer to keep repeating.
                hda.write8(sd, 0);
            }
            if (message & kBusy) {
                report.notification = message;
                report.notificationExt = dsp.read32(kTargetExtension);
                if (!acknowledgeTarget(dsp, message)) {
                    result = "notification-ack-failed";
                    break;
                }
                const auto type = message & kReadyNotificationMask;
                if (type == kReadyNotification) {
                    report.entered = true;
                    report.ready = true;
                    result = "firmware-ready";
                    break;
                }
                if (type == kExceptionNotification) {
                    result = "firmware-exception";
                    break;
                }
            }
            clock.sleepMs(1);
        }
        report.dmaPosition = hda.read32(sd + 4);
        report.dmaStatus = hda.read8(sd + 3);
        if (!report.entered && (report.dmaStatus & 0x18))
            result = "loader-dma-error";
    } while (false);

    if (streamTouched) {
        hda.write8(sd, 0);
        const bool stopped = waitStream(hda, clock, sd, kHdaStreamRun | kHdaStreamReset, 0);
        if (stopped) {
            // After stopping RUN, reset the borrowed descriptor before
            // restoring fields whose writes are defined only after reset.
            hda.write8(sd, kHdaStreamReset);
            const bool reset = waitStream(hda, clock, sd, kHdaStreamReset, kHdaStreamReset);
            hda.write8(sd, 0);
            const bool released = waitStream(hda, clock, sd, kHdaStreamReset | kHdaStreamRun, 0);
            if (reset && released) {
                hda.write8(sd + 2, stream.savedCtl2);
                hda.write32(sd + 0x18, 0);
                hda.write32(sd + 0x1c, 0);
                hda.write32(sd + 8, stream.savedCbl);
                hda.write16(sd + 0xc, stream.savedLvi);
                hda.write16(sd + 0x12, stream.savedFmt);
                hda.write8(sd + 3, kHdaStreamStatus);
            }
            hda.write32(stream.spib, stream.savedSpib);
            hda.write32(stream.spbctl, stream.savedSpbctl);
            hda.write32(stream.ppctl, stream.savedPpctl);
            report.dmaSafe = !descriptorInstalled ||
                (released && hda.read32(sd + 0x18) == 0 && hda.read32(sd + 0x1c) == 0);
            report.streamRestored = reset && released && report.dmaSafe &&
                hda.read32(stream.spbctl) == stream.savedSpbctl &&
                hda.read32(stream.spib) == stream.savedSpib &&
                hda.read32(sd + 8) == stream.savedCbl && hda.read16(sd + 0xc) == stream.savedLvi &&
                hda.read16(sd + 0x12) == stream.savedFmt && hda.read8(sd + 2) == stream.savedCtl2 &&
                hda.read32(stream.ppctl) == stream.savedPpctl;
        } else {
            report.dmaSafe = !descriptorInstalled;
            report.streamRestored = false;
        }
    }
    if (afterBoot && report.ready && report.dmaSafe && report.streamRestored)
        afterBoot(hda, dsp, clock, report, captureContext);

    if (resident && report.ready && report.dmaSafe && report.streamRestored) {
        *resident = {subsystem, savedCore, host, coreTouched, hostTouched};
        report.dspRestored = false;
        return "firmware-resident";
    }
    shutdownDsp(dsp, clock, report, {subsystem, savedCore, host, coreTouched, hostTouched});
    if (!report.dmaSafe)
        return "dma-stop-unconfirmed-buffer-retained";
    if (!report.streamRestored || !report.dspRestored)
        return "cleanup-incomplete";
    if (report.ready && !report.d3Accepted)
        return "firmware-ready-d3-unconfirmed";
    return result;
}

} // namespace ace
