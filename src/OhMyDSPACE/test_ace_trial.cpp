#include "AceCaptureTrial.hpp"
#include "AceAnalogPlayback.hpp"
#include "AcePlaybackProfiles.inc"
#include "AceCoexistence.hpp"
#include "CaptureTimeline.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string_view>
#include <vector>

constexpr uint32_t SD = ace::kHdaStreamBase + 10 * ace::kHdaStreamStride;
constexpr uint32_t PPCTL = 0x804;
constexpr uint32_t SPBCTL = 0x904;
constexpr uint32_t SPIB = 0x958;

enum class Fault { None, Defaults, HdaBusy, HdaSyncPending, DspActive, BoundStreams, MissingSpib, Power, RomAck,
                   NoReady, DmaStuck, DescriptorStuck, CoreStuck, D3Rejected,
                   D3NoAck, D3NoReply, D3TerminalBusy, HostStatusFollowsSubsystem,
                   Capture, CaptureRunTimeout, CaptureDmaStuck, CaptureDuringPlayback, PolicyRejected, PolicyTimeout,
                   SpeakerResetStuck };

struct Bar {
    explicit Bar(size_t size) : regs(size) {}
    std::vector<uint8_t> regs;
    Bar *other = nullptr;
    Fault fault = Fault::None;
    bool started = false;
    bool sentD3 = false;
    uint32_t captureStep = 0;
    unsigned writes = 0;
    bool modelSpeakerReset = false;
    uint32_t rejectPlayback = 0;
    std::vector<uint32_t> playbackRequests;

    static bool isPlaybackRequest(uint32_t request) {
        using Playback = ace::AnalogPlayback;
        return request == Playback::create || request == Playback::hostInstance ||
               request == Playback::linkInstance || request == Playback::bind ||
               (request & 0xffff0000) == Playback::state || request == Playback::remove;
    }

    size_t length() const { return regs.size(); }
    template <typename T> T get(uint32_t off) const {
        assert(off + sizeof(T) <= length());
        T value;
        std::memcpy(&value, regs.data() + off, sizeof(value));
        return value;
    }
    template <typename T> void put(uint32_t off, T value) {
        assert(off + sizeof(T) <= length());
        std::memcpy(regs.data() + off, &value, sizeof(value));
    }
    uint8_t read8(uint32_t off) { return get<uint8_t>(off); }
    uint16_t read16(uint32_t off) { return get<uint16_t>(off); }
    uint32_t read32(uint32_t off) { return get<uint32_t>(off); }
    void write8(uint32_t off, uint8_t value) {
        ++writes;
        if (modelSpeakerReset && off == ace::AnalogPlayback::sd) {
            assert(!(value & ace::kHdaStreamRun));
            if (value & ace::kHdaStreamReset) {
                assert(read32(PPCTL) & ace::AnalogPlayback::ppBit);
                // SD resets the descriptor fields but leaves the software-owned BDL memory alone.
                std::memset(regs.data() + off, 0, ace::kHdaStreamStride);
            } else if (fault == Fault::SpeakerResetStuck) {
                value |= ace::kHdaStreamReset;
            }
        }
        if (off == 0x80 && read8(off) & ace::kHdaStreamRun && fault == Fault::CaptureDmaStuck)
            value |= ace::kHdaStreamRun;
        if (off >= ace::kHdaStreamBase && off < ace::kHdaStreamBase + ace::kStreams * ace::kHdaStreamStride &&
            (off - ace::kHdaStreamBase) % ace::kHdaStreamStride == 3) {
            put<uint8_t>(off, read8(off) & ~value);
            return;
        }
        if (off == SD && (value & ace::kHdaStreamRun)) {
            assert(read32(SD + 0x18) == 0x200000);
            assert(read32(SPIB) == 1155072 && (read32(SPBCTL) & (1U << 10)));
            assert(read32(PPCTL) & (1U << 10));
            started = true;
            other->put<uint32_t>(ace::kRomStatus, ace::kRomFirmwareEntered);
            if (fault != Fault::NoReady)
                other->put<uint32_t>(ace::kTargetRequest, ace::kBusy | ace::kReadyNotification);
            put<uint32_t>(SD + 4, 1155072);
        }
        if (off == SD && started && fault == Fault::DmaStuck)
            value |= ace::kHdaStreamRun;
        put(off, value);
    }
    void write16(uint32_t off, uint16_t value) {
        ++writes;
        put(off, value);
    }
    void write32(uint32_t off, uint32_t value) {
        ++writes;
        if (off == SPIB && !(read32(SPBCTL) & (1U << 10)))
            return;
        if (off == ace::kSubsystem) {
            value &= ~ace::kSubsystemPowered;
            if ((value & ace::kSubsystemRequest) && fault != Fault::Power)
                value |= ace::kSubsystemPowered;
            if (!(value & ace::kSubsystemRequest))
                put<uint32_t>(ace::kHostPowerStatus, 0);
        } else if (off == ace::kHostPowerControl) {
            if ((value & 1) || fault != Fault::HostStatusFollowsSubsystem)
                put<uint32_t>(ace::kHostPowerStatus, value & 1);
        } else if (off == ace::kCoreControl) {
            value &= ~ace::kCorePowered;
            if (value & ace::kCoreRequest) {
                assert(read32(ace::kRequest) == 0x81004000);
                value |= ace::kCorePowered;
                if (fault != Fault::RomAck) {
                    put<uint32_t>(ace::kReplyAck, ace::kBusy);
                    put<uint32_t>(ace::kRequest, 0);
                }
                put<uint32_t>(ace::kRomStatus, ace::kRomInitDone);
            } else if (fault == Fault::CoreStuck) {
                value |= ace::kCorePowered;
            }
        } else if (off == ace::kRequest && value == (ace::kBusy | ace::kIpc4SetDx)) {
            assert(!(read32(ace::kTargetRequest) & ace::kBusy));
            assert(read32(ace::kHostMailbox) == 1 && read32(ace::kHostMailbox + 4) == 0);
            assert(read32(ace::kRequestExtension) == 0);
            sentD3 = true;
            if (fault != Fault::D3NoAck && fault != Fault::D3TerminalBusy)
                put<uint32_t>(ace::kReplyAck, ace::kBusy);
            if (fault != Fault::D3NoReply) {
                const auto status = fault == Fault::D3Rejected ? 4U : 0U;
                put<uint32_t>(ace::kTargetRequest, ace::kBusy | ace::kIpc4SetDx | ace::kIpc4Reply | status);
            }
            if (fault != Fault::D3TerminalBusy)
                value = 0;
        } else if (off == ace::kRequest && ((value & ~ace::kBusy) == ace::kLargeConfigSetBase ||
                                          (value & ~ace::kBusy) == ace::kSetD0ix)) {
            const auto primary = value & ~ace::kBusy;
            if (primary == ace::kLargeConfigSetBase) {
                assert(read32(ace::kRequestExtension) == 0x3070000c);
                assert(read32(ace::kHostMailbox) == 28);
                assert(read32(ace::kHostMailbox + 4) == 4 && read32(ace::kHostMailbox + 8) == 1);
            } else assert(read32(ace::kRequestExtension) == 4);
            if (fault != Fault::PolicyTimeout) {
                put<uint32_t>(ace::kTargetRequest, ace::kBusy | primary | ace::kIpc4Reply |
                              (fault == Fault::PolicyRejected ? 4U : 0U));
                value = 0;
            }
        } else if (off == ace::kRequest && (value & ace::kBusy) && isPlaybackRequest(value & ~ace::kBusy)) {
            using Playback = ace::AnalogPlayback;
            const auto primary = value & ~ace::kBusy;
            const auto extension = read32(ace::kRequestExtension);
            if (primary == Playback::hostInstance || primary == Playback::linkInstance) {
                assert(extension == 0x10015);
                assert(read32(ace::kHostMailbox + ace::kHostNodeOffset) ==
                       (primary == Playback::hostInstance ? 0 : 0x800));
                assert(read32(ace::kHostMailbox + 16) == 48000);
                if (primary == Playback::hostInstance)
                    assert(read32(ace::kHostMailbox + 36) == 0x1802); // 24 valid bits, MSB integer
            } else if (primary == Playback::bind) {
                assert(extension == Playback::linkBinding);
            } else {
                assert(extension == 0);
            }
            playbackRequests.push_back(primary);
            if (fault != Fault::PolicyTimeout) {
                put<uint32_t>(ace::kTargetRequest, ace::kBusy | ace::kIpc4Reply |
                              (primary & ace::kIpc4TypeTargetMask) | (primary == rejectPlayback ? 4U : 0U));
                value = 0;
            }
        } else if (off == ace::kRequest && (value & ace::kBusy) && read32(ace::kCoreControl) & ace::kCorePowered) {
            constexpr uint32_t commands[] = {
                ace::kCreatePipeline, ace::kDmicCopier, ace::kHostCopier, ace::kBindCopiers,
                ace::kSetPipelineState | ace::kPipelinePaused, ace::kSetPipelineState | ace::kPipelineRunning,
                ace::kSetPipelineState | ace::kPipelinePaused, ace::kSetPipelineState | ace::kPipelineReset,
                ace::kDeletePipeline
            };
            assert(captureStep < sizeof(commands) / sizeof(*commands));
            const auto primary = value & ~ace::kBusy;
            assert(commands[captureStep++] == primary);
            const auto extension = read32(ace::kRequestExtension);
            if (primary == ace::kDmicCopier)
                assert(extension == ace::kDmicPayloadBytes / 4);
            if (primary == ace::kHostCopier) {
                assert(extension == ace::kHostPayloadBytes / 4);
                assert(read32(ace::kHostMailbox + ace::kHostNodeOffset) == 0x100);
            }
            if (primary == ace::kBindCopiers)
                assert(extension == 0x10005);
            if (captureStep == 6 && fault == Fault::CaptureRunTimeout) {
                put(off, value);
                return;
            }
            put<uint32_t>(ace::kTargetRequest, ace::kBusy | (primary & ace::kIpc4TypeTargetMask) | ace::kIpc4Reply);
            value = 0;
        } else if (off == ace::kReplyAck || off == ace::kTargetRequest) {
            value &= ~ace::kBusy;
        } else if (off == SD + 0x18 && !value && started && fault == Fault::DescriptorStuck) {
            return;
        }
        put(off, value);
    }
};

struct Delay {
    uint64_t elapsed = 0;
    uint64_t nowMs() { return elapsed; }
    void sleepMs(unsigned ms) { elapsed += ms; }
    void delayUs(unsigned) {}
};

void test(Fault fault, std::string_view outcome) {
    Bar hda(0x4000), dsp(0x200000);
    hda.other = &dsp;
    hda.fault = dsp.fault = fault;
    hda.put<uint16_t>(0, 0x9a01);
    hda.put<uint32_t>(8, 0x101);
    hda.put<uint16_t>(0x14, 0x800);
    hda.put<uint32_t>(0x800, 0x00030900);
    hda.put<uint32_t>(PPCTL, ace::kPpGlobalEnable);
    hda.put<uint32_t>(0x900, 0x00040000);
    const bool defaults = fault == Fault::Defaults;
    hda.put<uint8_t>(SD + 2, defaults ? 0xc : 8);
    if (defaults) {
        hda.put<uint32_t>(SPIB, 0x80);
        hda.put<uint32_t>(SD + 8, 0x100);
        hda.put<uint16_t>(SD + 0xc, 3);
        hda.put<uint16_t>(SD + 0x12, 0x11);
    }
    if (fault == Fault::BoundStreams) {
        for (uint32_t i = 0; i < ace::kOutputs; ++i)
            hda.put<uint32_t>(SD + ace::kHdaStreamStride * i + 0x18, 0x4000);
    }
    if (fault == Fault::MissingSpib)
        hda.put<uint32_t>(0x800, 0x00030000);
    if (fault == Fault::HdaBusy)
        hda.put<uint8_t>(0x80, ace::kHdaStreamRun);
    if (fault == Fault::HdaSyncPending)
        hda.put<uint32_t>(0x34, 1);
    if (fault == Fault::DspActive)
        dsp.put<uint32_t>(ace::kSubsystem, ace::kSubsystemPowered);
    Delay delay;
    ace::Report report;
    assert(ace::run(hda, dsp, delay, 0x200000, 1155072, 18, report) == outcome);
    assert(dsp.sentD3 == (report.ready && report.dmaSafe));
    if (fault == Fault::None || defaults)
        assert(report.entered && report.ready && report.romAck == ace::kBusy && report.dmaPosition == 1155072);
    const bool skipped = fault == Fault::HdaBusy || fault == Fault::HdaSyncPending ||
        fault == Fault::DspActive || fault == Fault::BoundStreams || fault == Fault::MissingSpib;
    assert(report.hardwareTouched == !skipped);
    if (skipped)
        assert(hda.writes == 0 && dsp.writes == 0);
    if (fault == Fault::BoundStreams || fault == Fault::MissingSpib) {
        assert(!report.romInitialized);
        assert(!report.ready && report.streamTag == 0 && report.candidateMask == 0);
        assert(report.rejection[0] == (fault == Fault::BoundStreams ? 2U : 0U));
    }
    if (fault == Fault::DmaStuck || fault == Fault::DescriptorStuck)
        assert(!report.dmaSafe && !report.streamRestored);
    else {
        assert(report.dmaSafe && report.streamRestored);
        assert(hda.read32(SD + 0x18) == (fault == Fault::BoundStreams ? 0x4000U : 0U));
        assert(hda.read32(PPCTL) == ace::kPpGlobalEnable);
        assert(hda.read8(SD + 2) == (defaults ? 0xc : 8));
        assert(hda.read32(SPIB) == (defaults ? 0x80U : 0U));
        assert(hda.read32(SD + 8) == (defaults ? 0x100U : 0U));
        assert(hda.read16(SD + 0xc) == (defaults ? 3U : 0U));
        assert(hda.read16(SD + 0x12) == (defaults ? 0x11U : 0U));
    }
    if (fault == Fault::CoreStuck)
        assert(!report.dspRestored && (dsp.read32(ace::kSubsystem) & ace::kSubsystemPowered));
    else
        assert(report.dspRestored && dsp.read32(ace::kSubsystem) ==
               (fault == Fault::DspActive ? ace::kSubsystemPowered : 0));
    if (fault == Fault::D3Rejected)
        assert(report.d3Ack && std::string_view(report.d3Result) == "firmware-rejected");
    if (fault == Fault::D3NoAck)
        assert(!report.d3Ack && std::string_view(report.d3Result) == "accepted-reply-only");
    if (fault == Fault::D3TerminalBusy)
        assert(report.d3Accepted && !report.d3Ack && !report.d3RequestIdle &&
               std::string_view(report.d3Result) == "accepted-terminal-reply");
    if (fault == Fault::D3NoReply)
        assert(report.d3Ack && std::string_view(report.d3Result) == "reply-timeout");
    if (fault == Fault::HostStatusFollowsSubsystem) {
        assert(report.hostStatusBeforeSubsystemOff == 1 && report.subsystemAfterShutdown == 0);
        assert(std::string_view(report.d3Result) == "accepted");
    }
}

void testCapture(Fault fault) {
    Bar hda(0x4000), dsp(0x200000);
    hda.other = &dsp;
    hda.fault = dsp.fault = fault;
    hda.put<uint16_t>(0, 0x9a01);
    hda.put<uint32_t>(8, 0x101);
    hda.put<uint16_t>(0x14, 0x800);
    hda.put<uint32_t>(0x800, 0x00030900);
    hda.put<uint32_t>(PPCTL, ace::kPpGlobalEnable);
    hda.put<uint32_t>(0x900, 0x00040000);
    uint8_t dmic[ace::kDmicPayloadBytes] = {};
    uint8_t host[ace::kHostPayloadBytes] = {};
    host[ace::kHostNodeOffset + 1] = 1;
    int32_t samples[ace::kCaptureBytes / sizeof(int32_t)] = {};
    ace::CaptureBuffer capture;
    capture.bdl = 0x300000;
    capture.samples = reinterpret_cast<uint8_t *>(samples);
    capture.dmic = dmic;
    capture.host = host;
    capture.reservedIndex = 0;
    capture.reservedTag = 1;
    capture.readBarrier = [](uint8_t *raw, uint32_t) {
        auto pcm = reinterpret_cast<int32_t *>(raw);
        pcm[0] = INT32_MIN;
        pcm[1] = 255;
    };
    Delay delay;
    ace::Report boot;
    constexpr uint32_t playbackSD = SD + ace::kHdaStreamStride;
    const bool overlap = fault == Fault::CaptureDuringPlayback;
    boot.requirePlayback = overlap;
    std::string_view result;
    if (overlap) {
        hda.put<uint32_t>(playbackSD, 0x200000 | ace::kHdaStreamRun);
        hda.put<uint32_t>(playbackSD + 0x18, 0x400000);
        ace::DspPowerState power;
        assert(std::string_view(ace::run<Bar, Delay>(hda, dsp, delay, 0x200000, 1155072,
            18, boot, nullptr, nullptr, &power)) == "hda-stream-active");
        assert(hda.writes == 0 && dsp.writes == 0);
        hda.put<uint32_t>(playbackSD, 0x200000);
        boot = {};
        boot.requirePlayback = true;
        assert(std::string_view(ace::run<Bar, Delay>(hda, dsp, delay, 0x200000, 1155072,
            18, boot, nullptr, nullptr, &power)) == "firmware-resident");
        assert(power.coreTouched && !boot.dspRestored && !dsp.sentD3);

        hda.put<uint32_t>(playbackSD, 0x200000 | ace::kHdaStreamRun);
        ace::CaptureSession session;
        assert(ace::beginCapture(hda, dsp, delay, boot, capture, session));
        for (unsigned poll = 0; poll < 20; ++poll) {
            hda.put<uint32_t>(playbackSD + 4, poll * 1536);
            assert(ace::pollCapture(hda, boot, capture, session));
        }
        // Another stream's PPCTL bit may change while the gate is open.
        hda.put<uint32_t>(PPCTL, hda.read32(PPCTL) | (1U << 11));
        ace::endCapture(hda, dsp, delay, boot, capture, session);
        assert(hda.read32(PPCTL) == (ace::kPpGlobalEnable | (1U << 11)));
        hda.put<uint32_t>(PPCTL, ace::kPpGlobalEnable);
        const auto writes = dsp.writes;
        assert(!ace::shutdownResidentIfIdle(hda, dsp, delay, boot, power));
        assert(!dsp.sentD3 && dsp.writes == writes);
        hda.put<uint32_t>(playbackSD, 0x200000);
        hda.put<uint32_t>(0x34, 1U << 11);
        assert(!ace::shutdownResidentIfIdle(hda, dsp, delay, boot, power));
        assert(dsp.writes == writes);
        hda.put<uint32_t>(0x34, 0);
        assert(ace::shutdownResidentIfIdle(hda, dsp, delay, boot, power));
        result = boot.dspRestored && boot.d3Accepted ? "firmware-ready" : "cleanup-incomplete";
    } else {
        result = ace::run(hda, dsp, delay, 0x200000, 1155072, 18, boot,
                          ace::runCapture<Bar, Delay>, &capture);
    }
    const auto &report = capture.report;
    assert(boot.dspRestored && boot.streamRestored && boot.dmaSafe);
    assert(report.streamIndex == 0 && report.streamTag == 1);
    if (fault == Fault::CaptureDmaStuck) {
        assert(!report.dmaSafe && !report.streamRestored && !dsp.sentD3);
        assert(std::string_view(report.result) == "capture-stream-restore-unconfirmed");
        return;
    }
    assert(report.dmaSafe && report.streamRestored && !hda.read32(0x98));
    assert(hda.read32(PPCTL) == ace::kPpGlobalEnable);
    if (fault == Fault::CaptureRunTimeout) {
        assert(dsp.captureStep == 6 && !dsp.sentD3);
        assert(std::string_view(report.result) == "start-pipeline-failed");
        assert(std::string_view(report.ipcError) == "reply-timeout");
        return;
    }
    assert(dsp.captureStep == 9 && dsp.sentD3 && std::string_view(result) == "firmware-ready");
    assert(std::string_view(report.cleanup) == "pipeline-deleted");
    assert(std::string_view(report.result) == "capture-signal-observed");
    assert(report.peak[0] == 0x80000000 && report.peak[1] == 255);
    assert(report.playbackPolls == (overlap ? 20 : 0));
    assert(report.playbackPositionChanges == (overlap ? 19 : 0));
    if (overlap) {
        assert(hda.read32(playbackSD) == 0x200000);
        assert(hda.read32(playbackSD + 0x18) == 0x400000);
    }
}

void testCoexistence(Fault fault) {
    Bar hda(0x4000), dsp(0x200000); hda.other = &dsp;
    hda.put<uint16_t>(0, 0x9a01); hda.put<uint32_t>(8, 0x101);
    hda.put<uint16_t>(0x14, 0x800); hda.put<uint32_t>(0x800, 0x00030900);
    hda.put<uint32_t>(PPCTL, ace::kPpGlobalEnable); hda.put<uint32_t>(0x900, 0x00040000);
    Delay clock; ace::Report boot; ace::DspPowerState power;
    assert(std::string_view(ace::run<Bar, Delay>(hda,dsp,clock,0x200000,1155072,18,boot,nullptr,nullptr,&power)) == "firmware-resident");
    assert(boot.ready && boot.dmaSafe && boot.streamRestored && power.coreTouched && !dsp.sentD3);
    dsp.fault = fault;
    ace::IpcResult result {"not-run"};
    const bool accepted = ace::holdDmiL0(dsp,clock,boot,result);
    assert(accepted == (fault == Fault::None));
    if (accepted) assert(ace::preventD0ix(dsp,clock,boot,result));
    else assert(result.reusable == (fault == Fault::PolicyRejected));
    if (fault == Fault::PolicyTimeout) assert(clock.nowMs() >= 500 && !boot.ipcSafeForD3);
    // D3 must be deferred for an AppleHDA output. Neither the stream nor the
    // resident DSP may be stopped behind the controller's back.
    hda.put<uint32_t>(SD + 0x20, 0x200002); hda.put<uint32_t>(SD + 0x38, 0x400000);
    auto beforeHda = hda.regs, beforeDsp = dsp.regs;
    assert(!ace::shutdownResidentIfIdle(hda,dsp,clock,boot,power));
    assert(hda.regs == beforeHda && dsp.regs == beforeDsp);
    hda.put<uint32_t>(SD + 0x20, 0x200000);
    assert(ace::shutdownResidentIfIdle(hda,dsp,clock,boot,power));
    assert(boot.dspRestored && (fault == Fault::PolicyTimeout || boot.d3Accepted));
}
void testAnalogPlayback() {
    using Playback = ace::AnalogPlayback;
    Bar hda(0x4000), dsp(0x200000);
    constexpr uint32_t ml = 0x640, link = 0x800 + 0x10 + (ace::kStreams + Playback::index) * 0x10;
    hda.put<uint16_t>(0, 0x9a01);
    hda.put<uint16_t>(0x14, 0x600);
    hda.put<uint32_t>(0x600, 0x00020800);
    hda.put<uint32_t>(PPCTL, ace::kPpGlobalEnable);
    hda.put<uint32_t>(ml + 4, Playback::spa | Playback::cpa);
    hda.put<uint16_t>(ml + 8, 0x0040); // unrelated link tag
    hda.put<uint16_t>(ml + 0xc, 1);
    hda.put<uint32_t>(Playback::sd, Playback::tagBits | ace::kHdaStreamRun);
    hda.put<uint32_t>(Playback::sd + 8, 8192);
    hda.put<uint16_t>(Playback::sd + 0xc, 5);
    hda.put<uint16_t>(Playback::sd + 0x12, 0x0031);
    hda.put<uint32_t>(Playback::sd + 0x18, 0x400000);
    hda.put<uint32_t>(Playback::sd + 0x1c, 1);
    hda.modelSpeakerReset = true;
    Delay clock; ace::Report boot; boot.ppOffset = 0x800;
    Playback playback;
    const auto &profile = kAnalogProfiles[1];
    assert(profile.hdaFormat == 0x0031);
    assert(!playback.prepare(hda, dsp, clock, boot, profile));
    assert(!playback.touched && hda.writes == 0 && dsp.writes == 0);
    hda.put<uint32_t>(Playback::sd, Playback::tagBits);
    const auto baseline = hda.regs;
    assert(playback.prepare(hda, dsp, clock, boot, profile));
    assert(playback.ready && playback.hostReinitialized && !playback.started);
    assert(hda.read32(Playback::sd + 0x18) == 0x400000 && hda.read32(Playback::sd + 0x1c) == 1);
    assert(hda.read16(Playback::sd + 0xc) == 5 && hda.read32(Playback::sd + 8) == 8192);
    assert(hda.read32(PPCTL) == (ace::kPpGlobalEnable | Playback::ppBit));
    assert(hda.read16(ml + 8) == 0x42 && hda.read16(link + 4) == 0x0031);
    assert(playback.observe(hda, dsp, clock, boot) && !playback.started);
    // Resource notifications between requests must be drained without mistaking
    // a notification for the enable reply.
    dsp.put<uint32_t>(ace::kTargetRequest, ace::kBusy | ace::kResourceNotification);
    hda.put<uint32_t>(Playback::sd, Playback::tagBits | ace::kHdaStreamRun);
    hda.put<uint32_t>(0x34, Playback::ppBit);
    assert(playback.observe(hda, dsp, clock, boot) && !playback.started);
    hda.put<uint32_t>(0x34, 0);
    assert(playback.observe(hda, dsp, clock, boot) && playback.started);
    assert(hda.read32(link) & ace::kHdaStreamRun);
    hda.put<uint32_t>(Playback::sd + 4, 320);
    assert(playback.observe(hda, dsp, clock, boot) && playback.positionChanges == 1);
    assert(playback.hostProgress == 1 && playback.linkProgress == 0 && playback.latest.lpib == 320);
    clock.sleepMs(110);
    assert(playback.observe(hda, dsp, clock, boot));
    assert(std::string_view(playback.status) == "speaker-host-dma-stalled");
    const auto hdaActive = hda.regs, dspActive = dsp.regs;
    assert(!playback.restore(hda, dsp, clock, boot));
    assert(hda.regs == hdaActive && dsp.regs == dspActive);
    hda.put<uint32_t>(Playback::sd, Playback::tagBits); hda.put<uint32_t>(Playback::sd + 4, 0);
    assert(playback.observe(hda, dsp, clock, boot) && !playback.started);
    assert(!playback.linkPrepared && !playback.pipelineNeedsReset && hda.read16(ml + 8) == 0x40);
    assert(!(hda.read32(link) & ace::kHdaStreamRun));
    assert(dsp.playbackRequests.back() == (Playback::state | ace::kPipelineReset));
    // A new AppleHDA RUN reprograms only the owned link. The host descriptor
    // and the BDL must still be exactly as AppleHDA left them.
    hda.put<uint32_t>(Playback::sd, Playback::tagBits | ace::kHdaStreamRun);
    const unsigned hostWrites = hda.writes;
    assert(playback.observe(hda, dsp, clock, boot) && playback.starts == 2);
    assert(hda.writes > hostWrites && hda.read32(Playback::sd) == (Playback::tagBits | ace::kHdaStreamRun));
    assert(hda.read32(Playback::sd + 0x18) == 0x400000 && hda.read16(ml + 8) == 0x42);
    hda.put<uint32_t>(Playback::sd, Playback::tagBits);
    assert(playback.restore(hda, dsp, clock, boot) && hda.regs == baseline);
    const std::vector<uint32_t> expected {
        Playback::create, Playback::hostInstance, Playback::linkInstance, Playback::bind,
        Playback::state | ace::kPipelinePaused, Playback::state | ace::kPipelineRunning,
        Playback::state | ace::kPipelinePaused, Playback::state | ace::kPipelineReset,
        Playback::state | ace::kPipelineRunning, Playback::state | ace::kPipelinePaused,
        Playback::state | ace::kPipelineReset, Playback::remove
    };
    assert(dsp.playbackRequests == expected);

    hda.fault = Fault::SpeakerResetStuck;
    const unsigned beforeFailedReset = dsp.writes;
    assert(!playback.prepare(hda, dsp, clock, boot, profile) && playback.hostResetUnconfirmed);
    assert(!playback.restore(hda, dsp, clock, boot) && dsp.writes == beforeFailedReset);
    hda.regs = baseline;
    hda.fault = Fault::None;
    playback = {};

    dsp.rejectPlayback = Playback::linkInstance;
    assert(!playback.prepare(hda, dsp, clock, boot, profile) && playback.pipelineCreated && !playback.ready);
    assert(playback.restore(hda, dsp, clock, boot) && hda.regs == baseline);
    assert(dsp.playbackRequests.back() == Playback::remove && playback.failedRequest == Playback::linkInstance);
    dsp.rejectPlayback = 0;
    dsp.fault = Fault::PolicyTimeout;
    assert(!playback.prepare(hda, dsp, clock, boot, profile) && playback.touched && !boot.ipcSafeForD3);
    assert(!playback.restore(hda, dsp, clock, boot));
    assert(hda.read32(Playback::sd + 0x18) == 0x400000);
    std::cout << "Analog playback pipeline setup, progress, idle release and failure cleanup passed\n";
}

void testTimeline() {
    ace::CaptureTimeline t; t.reset(1000000000);
    auto u = t.observe(1920,1005000000); assert(u.valid && !u.gap && !u.wrapped && !u.stalled);
    assert(t.lapNs == 1000000000 && t.laps == 0);
    // A jittered same-lap poll must not move the HAL's zero timestamp.
    u = t.observe(3840,1010300000); assert(t.lapNs == 1000000000);
    u = t.observe(24000,1062500000); assert(!u.gap && t.laps == 0);
    u = t.observe(1344,1067500000); assert(u.wrapped && t.laps == 1 && t.lapNs == 1064000000);
    auto before = t;
    assert(!t.observe(24576,1069000000).valid && t.position == before.position);
    assert(!t.observe(1345,1069000000).valid && t.position == before.position);
    assert(!t.observe(0,1060000000).valid && t.tickNs == before.tickNs);
    u = t.observe(3264,1197500000); assert(u.gap && t.laps == 4 && !u.stalled);
    const auto boundary = t.lapNs;
    u = t.observe(5184,1202500000); assert(!u.gap && t.lapNs == boundary);
    u = t.observe(5184,1410000000); assert(u.stalled);
    t.reset(2000000000); assert(t.laps == 0 && !t.aligned && t.position == 0);
    std::cout << "timeline wrap, jitter, invalid positions, missed laps, stall and restart cases passed\n";
}
int main() {
    test(Fault::None, "firmware-ready");
    test(Fault::Defaults, "firmware-ready");
    test(Fault::HdaBusy, "hda-stream-active");
    test(Fault::HdaSyncPending, "hda-stream-sync-pending");
    test(Fault::DspActive, "dsp-not-fully-off");
    test(Fault::BoundStreams, "no-idle-unbound-playback-stream");
    test(Fault::MissingSpib, "spib-cap-unavailable");
    test(Fault::Power, "subsystem-power-timeout");
    test(Fault::RomAck, "rom-purge-ack-timeout");
    test(Fault::NoReady, "firmware-ready-timeout");
    test(Fault::DmaStuck, "dma-stop-unconfirmed-buffer-retained");
    test(Fault::DescriptorStuck, "dma-stop-unconfirmed-buffer-retained");
    test(Fault::CoreStuck, "cleanup-incomplete");
    test(Fault::D3Rejected, "firmware-ready-d3-unconfirmed");
    test(Fault::D3NoAck, "firmware-ready");
    test(Fault::D3TerminalBusy, "firmware-ready");
    test(Fault::D3NoReply, "firmware-ready-d3-unconfirmed");
    test(Fault::HostStatusFollowsSubsystem, "firmware-ready");
    testCapture(Fault::Capture);
    testCapture(Fault::CaptureRunTimeout);
    testCapture(Fault::CaptureDmaStuck);
    testCapture(Fault::CaptureDuringPlayback);
    testCoexistence(Fault::None);
    testCoexistence(Fault::PolicyRejected);
    testCoexistence(Fault::PolicyTimeout);
    testTimeline();
    testAnalogPlayback();
    std::cout << "25 firmware, capture, coexistence and cleanup cases passed\n";
}
