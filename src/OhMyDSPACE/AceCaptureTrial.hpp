#pragma once

#include "AceBootTrial.hpp"

namespace ace {

constexpr uint32_t kCapturePeriodBytes = 6144; // 16 ms of stereo S32 at 48 kHz
constexpr uint32_t kCapturePeriods = 4;
constexpr uint32_t kCaptureBytes = kCapturePeriodBytes * kCapturePeriods;
constexpr uint16_t kCaptureFormat = 0x0041;
constexpr uint32_t kDmicPayloadBytes = 2192;
constexpr uint32_t kHostPayloadBytes = 84;
constexpr uint32_t kHostNodeOffset = 68;
constexpr uint32_t kHostNode = 0x100;
constexpr uint32_t kDmicCopier = 0x40000005; // module 5, instance 0
constexpr uint32_t kHostCopier = 0x40010005; // module 5, instance 1
constexpr uint32_t kBindCopiers = 0x45000005;
constexpr uint32_t kCreatePipeline = 0x11000004; // pipeline 0, priority 0, four pages
constexpr uint32_t kDeletePipeline = 0x12000000;
constexpr uint32_t kSetPipelineState = 0x13000000;
constexpr uint32_t kPipelineReset = 2;
constexpr uint32_t kPipelinePaused = 3;
constexpr uint32_t kPipelineRunning = 4;

struct CaptureReport {
    const char *result = "not-run";
    const char *ipcError = "none";
    const char *cleanup = "not-needed";
    uint32_t failedRequest = 0;
    uint32_t failedReply = 0;
    uint32_t streamIndex = UINT32_MAX;
    uint32_t streamTag = 0;
    uint32_t lastPosition = 0;
    uint32_t positionChanges = 0;
    uint32_t playbackPolls = 0;
    uint32_t playbackPositionChanges = 0;
    uint32_t status = 0;
    uint32_t peak[2] = {};
    bool dmaSafe = true;
    bool streamRestored = true;
    const char *leaseResult = "not-run";
};

struct CaptureBuffer {
    uint64_t bdl = 0;
    uint8_t *samples = nullptr;
    const uint8_t *dmic = nullptr;
    const uint8_t *host = nullptr;
    uint32_t reservedIndex = UINT32_MAX;
    uint32_t reservedTag = 0;
    void (*readBarrier)(uint8_t *, uint32_t) = nullptr;
    CaptureReport report;
};

template <class Bar>
bool findCaptureStream(Bar &hda, const Report &boot, const CaptureBuffer &buffer, Stream &stream) {
    // The loader restored PPCTL and the controller's work loop is still held.
    const auto ppctl = boot.ppOffset + 4;
    const auto pp = hda.read32(ppctl);
    const auto ints = hda.read32(0x20);
    const auto sync = hda.read32(0x34);
    if (pp != boot.ppControl || ints == UINT32_MAX || sync == UINT32_MAX ||
        (sync & kStreamMask))
        return false;
    const auto index = buffer.reservedIndex;
    const auto tag = buffer.reservedTag;
    if (index >= kInputs || !tag || tag > 15)
        return false;
    uint32_t usedTags = 0;
    uint32_t reservedControl = 0;
    for (uint32_t i = 0; i < kStreams; ++i) {
        const auto sd = kHdaStreamBase + kHdaStreamStride * i;
        const auto ctl = hda.read32(sd);
        if (ctl == UINT32_MAX || ((i < kInputs || !boot.requirePlayback) && (ctl & kHdaStreamRun)))
            return false;
        if (i < kInputs)
            usedTags |= 1U << ((ctl >> 20) & 0xf);
        if (i == index)
            reservedControl = ctl;
    }
    const auto sd = kHdaStreamBase + kHdaStreamStride * index;
    if ((usedTags & (1U << tag)) || (reservedControl & 0x1cf0001fU) ||
        (ints & (1U << index)) || hda.read32(sd + 0x18) || hda.read32(sd + 0x1c))
        return false;
    const auto cbl = hda.read32(sd + 8);
    const auto lvi = hda.read16(sd + 0xc);
    const auto fmt = hda.read16(sd + 0x12);
    if (cbl == UINT32_MAX || lvi == UINT16_MAX || fmt == UINT16_MAX)
        return false;
    stream.index = index;
    stream.tag = tag;
    stream.offset = sd;
    stream.ppctl = ppctl;
    stream.savedPpctl = pp;
    stream.savedCbl = cbl;
    stream.savedLvi = lvi;
    stream.savedFmt = fmt;
    stream.savedCtl2 = static_cast<uint8_t>(reservedControl >> 16);
    return true;
}

template <class Bar, class Clock>
bool resetCaptureStream(Bar &hda, Clock &clock, uint32_t sd) {
    hda.write8(sd, kHdaStreamReset);
    const bool asserted = waitStream(hda, clock, sd, kHdaStreamReset, kHdaStreamReset);
    hda.write8(sd, 0);
    return waitStream(hda, clock, sd, kHdaStreamReset | kHdaStreamRun, 0) && asserted;
}

struct CaptureSession {
    Stream stream;
    uint32_t position = 0;
    uint32_t outputPositions[kOutputs] = {};
    uint32_t observedOutputMask = 0;
    bool configured = false;
    bool descriptorInstalled = false;
    bool pipelineCreated = false;
    bool pipelineRunning = false;
};

template <class Bar, class Clock>
bool captureIpc(Bar &dsp, Clock &clock, Report &boot, CaptureReport &report, const char *stage,
                uint32_t primary, uint32_t extension = 0,
                const uint8_t *payload = nullptr, uint32_t bytes = 0) {
    const auto result = sendIpc(dsp, clock, primary, extension, payload, bytes,
                                boot.requirePlayback ? 60U : 500U);
    boot.ipcSafeForD3 = result.reusable;
    if (!result.accepted() && !report.failedRequest) {
        report.ipcError = result.status;
        report.failedRequest = primary;
        report.failedReply = result.reply;
        report.result = stage;
    }
    return result.accepted();
}

template <class Bar, class Clock>
bool beginCapture(Bar &hda, Bar &dsp, Clock &clock, Report &boot,
                  CaptureBuffer &buffer, CaptureSession &session) {
    auto &report = buffer.report;
    auto &stream = session.stream;
    if (!findCaptureStream(hda, boot, buffer, stream)) {
        report.result = "reserved-input-stream-not-idle";
        return false;
    }
    report.streamIndex = stream.index;
    report.streamTag = stream.tag;
    const auto sd = stream.offset;
    const auto bit = 1U << stream.index;
    uint8_t hostPayload[kHostPayloadBytes];
    for (uint32_t i = 0; i < kHostPayloadBytes; ++i)
        hostPayload[i] = buffer.host[i];
    hostPayload[kHostNodeOffset] = static_cast<uint8_t>(stream.tag - 1);

    session.configured = true;
    hda.write32(stream.ppctl, stream.savedPpctl | bit);
    do {
        report.result = "input-stream-reset-failed";
        if (!resetCaptureStream(hda, clock, sd))
            break;
        hda.write8(sd + 3, kHdaStreamStatus);
        hda.write8(sd + 2, stream.savedCtl2 | static_cast<uint8_t>(stream.tag << 4));
        hda.write32(sd + 8, kCaptureBytes);
        hda.write16(sd + 0xc, kCapturePeriods - 1);
        hda.write16(sd + 0x12, kCaptureFormat);
        hda.write32(sd + 0x18, static_cast<uint32_t>(buffer.bdl));
        hda.write32(sd + 0x1c, static_cast<uint32_t>(buffer.bdl >> 32));
        session.descriptorInstalled = true;
        report.result = "input-stream-program-failed";
        if (hda.read32(stream.ppctl) != (stream.savedPpctl | bit) ||
            hda.read8(sd + 2) >> 4 != stream.tag || hda.read32(sd + 8) != kCaptureBytes ||
            hda.read16(sd + 0xc) != kCapturePeriods - 1 ||
            hda.read16(sd + 0x12) != kCaptureFormat ||
            hda.read32(sd + 0x18) != static_cast<uint32_t>(buffer.bdl) ||
            hda.read32(sd + 0x1c) != static_cast<uint32_t>(buffer.bdl >> 32))
            break;
        if (!captureIpc(dsp, clock, boot, report, "create-pipeline-failed", kCreatePipeline))
            break;
        session.pipelineCreated = true;
        if (!captureIpc(dsp, clock, boot, report, "init-dmic-failed", kDmicCopier, kDmicPayloadBytes / 4, buffer.dmic, kDmicPayloadBytes) ||
            !captureIpc(dsp, clock, boot, report, "init-host-failed", kHostCopier, kHostPayloadBytes / 4, hostPayload, kHostPayloadBytes) ||
            !captureIpc(dsp, clock, boot, report, "bind-dmic-to-host-failed", kBindCopiers, 0x00010005) ||
            !captureIpc(dsp, clock, boot, report, "pause-pipeline-failed", kSetPipelineState | kPipelinePaused))
            break;
        hda.write8(sd, kHdaStreamRun);
        report.result = "input-dma-start-failed";
        if (!waitStream(hda, clock, sd, kHdaStreamRun, kHdaStreamRun))
            break;
        if (!captureIpc(dsp, clock, boot, report, "start-pipeline-failed", kSetPipelineState | kPipelineRunning))
            break;
        session.pipelineRunning = true;
        report.result = "capture-dma-no-progress";
        session.position = hda.read32(sd + 4);
    } while (false);
    return session.pipelineRunning;
}

template <class Bar>
bool pollCapture(Bar &hda, const Report &boot, CaptureBuffer &buffer, CaptureSession &session) {
    auto &report = buffer.report;
    if (boot.requirePlayback) {
        uint32_t runningMask = 0;
        bool advanced = false;
        for (uint32_t i = 0; i < kOutputs; ++i) {
            const auto sd = kHdaStreamBase + kHdaStreamStride * (i + kInputs);
            const auto control = hda.read8(sd);
            if (control == UINT8_MAX || !(control & kHdaStreamRun))
                continue;
            const auto pos = hda.read32(sd + 4);
            if (pos == UINT32_MAX)
                continue;
            const auto bit = 1U << i;
            runningMask |= bit;
            if ((session.observedOutputMask & bit) && session.outputPositions[i] != pos)
                advanced = true;
            session.outputPositions[i] = pos;
        }
        session.observedOutputMask = runningMask;
        if (runningMask)
            ++report.playbackPolls;
        if (advanced)
            ++report.playbackPositionChanges;
    }
    const auto sd = session.stream.offset;
    report.status = hda.read8(sd + 3);
    if (report.status & 0x18) {
        report.result = "capture-dma-error";
        return false;
    }
    const auto position = hda.read32(sd + 4);
    if (position < kCaptureBytes && position != session.position) {
        ++report.positionChanges;
        report.lastPosition = position;
        session.position = position;
    }
    return true;
}

template <class Bar, class Clock>
void endCapture(Bar &hda, Bar &dsp, Clock &clock, Report &boot,
                CaptureBuffer &buffer, CaptureSession &session) {
    if (!session.configured)
        return;
    auto &report = buffer.report;
    const auto &stream = session.stream;
    const auto sd = stream.offset;
    if (session.pipelineRunning && boot.ipcSafeForD3)
        captureIpc(dsp, clock, boot, report, "stop-pipeline-failed", kSetPipelineState | kPipelinePaused);
    hda.write8(sd, 0);
    const bool stopped = waitStream(hda, clock, sd, kHdaStreamRun | kHdaStreamReset, 0);
    if (session.pipelineCreated && boot.ipcSafeForD3) {
        captureIpc(dsp, clock, boot, report, "reset-pipeline-failed", kSetPipelineState | kPipelineReset);
        if (boot.ipcSafeForD3 && captureIpc(dsp, clock, boot, report, "delete-pipeline-failed", kDeletePipeline))
            report.cleanup = "pipeline-deleted";
        else
            report.cleanup = "pipeline-cleanup-unconfirmed";
    } else if (session.pipelineCreated || !boot.ipcSafeForD3) {
        report.cleanup = "pipeline-cleanup-unconfirmed";
    }
    if (stopped) {
        const bool reset = resetCaptureStream(hda, clock, sd);
        if (reset) {
            hda.write8(sd + 2, stream.savedCtl2);
            hda.write32(sd + 0x18, 0);
            hda.write32(sd + 0x1c, 0);
            hda.write32(sd + 8, stream.savedCbl);
            hda.write16(sd + 0xc, stream.savedLvi);
            hda.write16(sd + 0x12, stream.savedFmt);
            hda.write8(sd + 3, kHdaStreamStatus);
        }
        const auto pp = hda.read32(stream.ppctl);
        const auto expectedPp = pp & ~(1U << stream.index);
        if (pp != UINT32_MAX)
            hda.write32(stream.ppctl, expectedPp);
        report.dmaSafe = !session.descriptorInstalled ||
            (reset && !hda.read32(sd + 0x18) && !hda.read32(sd + 0x1c));
        report.streamRestored = reset && report.dmaSafe &&
            pp != UINT32_MAX && hda.read32(stream.ppctl) == expectedPp &&
            hda.read32(sd + 8) == stream.savedCbl && hda.read16(sd + 0xc) == stream.savedLvi &&
            hda.read16(sd + 0x12) == stream.savedFmt && hda.read8(sd + 2) == stream.savedCtl2;
    } else {
        report.dmaSafe = !session.descriptorInstalled;
        report.streamRestored = false;
    }
    if (!report.streamRestored) {
        boot.ipcSafeForD3 = boot.ipcSafeForD3 && report.dmaSafe;
        report.result = "capture-stream-restore-unconfirmed";
        return;
    }
    if (!session.pipelineRunning || report.failedRequest || (report.status & 0x18))
        return;

    // Inspect only the DMA buffer after the engine is stopped. Publish peaks,
    // never sample data or an audio file.
    buffer.readBarrier(buffer.samples, kCaptureBytes);
    const auto samples = reinterpret_cast<const int32_t *>(buffer.samples);
    for (uint32_t i = 0; i < kCaptureBytes / sizeof(*samples); ++i) {
        const auto sample = samples[i];
        const auto magnitude = sample < 0 ? static_cast<uint32_t>(-static_cast<int64_t>(sample))
                                          : static_cast<uint32_t>(sample);
        if (magnitude > report.peak[i & 1])
            report.peak[i & 1] = magnitude;
    }
    if (report.peak[0] || report.peak[1])
        report.result = "capture-signal-observed";
    else if (report.positionChanges)
        report.result = "capture-dma-progress-no-signal";
}

template <class Bar, class Clock>
void runCapture(Bar &hda, Bar &dsp, Clock &clock, Report &boot, void *context) {
    auto &buffer = *static_cast<CaptureBuffer *>(context);
    CaptureSession session;
    if (beginCapture(hda, dsp, clock, boot, buffer, session)) {
        for (unsigned i = 0; i < 20; ++i) {
            clock.sleepMs(8);
            if (!pollCapture(hda, boot, buffer, session))
                break;
        }
    }
    endCapture(hda, dsp, clock, boot, buffer, session);
}

} // namespace ace
