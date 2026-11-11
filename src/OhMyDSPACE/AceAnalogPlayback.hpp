#pragma once
#include "AceCaptureTrial.hpp"

namespace ace {

// The analog AppleHDA output on this board uses descriptor 10, tag 1 and
// codec address 0 on link 0. A SOF playback pipeline connects host-out 0 to link-out 0.
// AppleHDA owns the PCM buffers, codec, RUN and interrupt handling. Re-latch
// its idle descriptor after decoupling, before allowing any stream to start.
struct PlaybackProfile {
    uint16_t hdaFormat;
    uint8_t host[84];
    uint8_t link[84];
};

struct AnalogPlayback {
    static constexpr uint32_t index = kInputs;
    static constexpr uint32_t sd = kHdaStreamBase + index * kHdaStreamStride;
    static constexpr uint32_t ppBit = 1U << index;
    static constexpr uint32_t tagBits = 1U << 20;
    static constexpr uint16_t sidBit = 1U << 1;
    static constexpr uint32_t tagMask = 0xfU << 20;
    static constexpr uint32_t spa = 1U << 16, cpa = 1U << 23;
    static constexpr uint32_t pipeline = 1U << 16;
    static constexpr uint32_t create = kCreatePipeline | pipeline;
    static constexpr uint32_t remove = kDeletePipeline | pipeline;
    static constexpr uint32_t state = kSetPipelineState | pipeline;
    static constexpr uint32_t hostInstance = 0x40020005;
    static constexpr uint32_t linkInstance = 0x40030005;
    static constexpr uint32_t bind = kBindCopiers | (2U << 16);
    static constexpr uint32_t linkBinding = (3U << 16) | 5;

    const char *status = "not-run";
    IpcResult ipc {"not-run"};
    uint32_t pp = 0, link = 0, ml = 0, linkControl = 0, savedLinkControl = 0;
    uint32_t hostControl = 0, hostBytes = 0, codecMask = 0, ppBefore = 0;
    uint16_t format = 0, savedFormat = 0, savedSid = 0;
    uint32_t lastRequest = 0;
    uint32_t failedRequest = 0, failedReply = 0;
    uint32_t bdlLow = 0, bdlHigh = 0;
    uint16_t hostLvi = 0;
    uint32_t lastHost = 0, lastAlias = 0, lastLink = 0;
    uint32_t runningPolls = 0, positionChanges = 0;
    uint32_t hostProgress = 0, linkProgress = 0, starts = 0;
    uint64_t hostProgressMs = 0, linkProgressMs = 0;
    struct Trace {
        uint32_t sdCtl, lpib, dpib, pplcCtl, linkPos, mlCtl, mlSid, spbCtl, spib;
    } first {}, latest {};
    static_assert(sizeof(Trace) == 9 * sizeof(uint32_t), "playback trace wire format");
    bool touched = false, pipelineCreated = false, ready = false, started = false, sawPosition = false;
    bool linkPrepared = false, pipelineNeedsReset = false;
    bool hostResetUnconfirmed = false, hostReinitialized = false;

    template<class Bar>
    bool hostBufferPresent(Bar &hda) const {
        const auto bytes = hda.read32(sd + 8);
        const auto low = hda.read32(sd + 0x18);
        const auto high = hda.read32(sd + 0x1c);
        return bytes && bytes != UINT32_MAX && low != UINT32_MAX && high != UINT32_MAX && (low || high);
    }

    template<class Bar>
    bool identify(Bar &hda, const Report &boot) {
        hostControl = hda.read32(sd);
        hostBytes = hda.read32(sd + 8);
        format = hda.read16(sd + 0x12);
        hostLvi = hda.read16(sd + 0xc);
        bdlLow = hda.read32(sd + 0x18);
        bdlHigh = hda.read32(sd + 0x1c);
        uint32_t running, sync;
        status = "speaker-controller-busy";
        if (!readHdaActivity(hda, running, sync) || running || (sync & kStreamMask)) return false;
        status = "speaker-descriptor-not-idle-or-bound";
        if (hostControl == UINT32_MAX || (hostControl & (kHdaStreamRun | kHdaStreamReset)) ||
            (hostControl & tagMask) != tagBits || !hostBytes || hostBytes == UINT32_MAX ||
            !hostLvi || hostLvi > 255 || bdlLow == UINT32_MAX || bdlHigh == UINT32_MAX ||
            !(bdlLow || bdlHigh) || (bdlLow & 0x7f)) return false;
        status = "speaker-link-capability-unavailable";
        uint32_t cap = hda.read16(0x14);
        for (unsigned i = 0; i < 10 && cap >= 0x80 && cap + 0x50 <= hda.length() && !(cap & 3); ++i) {
            const auto header = hda.read32(cap);
            if (header == UINT32_MAX) break;
            if (((header >> 16) & 0xfff) == 2) { ml = cap + 0x40; break; }
            cap = header & 0xffff;
        }
        if (!ml || !boot.ppOffset) return false;
        pp = boot.ppOffset + 4;
        link = boot.ppOffset + 0x10 + 0x10 * (kStreams + index);
        if (link + 16 > hda.length()) return false;
        codecMask = hda.read16(ml + 0xc);
        ppBefore = hda.read32(pp);
        status = "speaker-link-not-available";
        if (codecMask == UINT16_MAX || !(codecMask & 1) || ppBefore == UINT32_MAX ||
            !(ppBefore & kPpGlobalEnable) || (ppBefore & ppBit)) return false;
        linkControl = hda.read32(ml + 4);
        savedSid = hda.read16(ml + 8);
        savedLinkControl = hda.read32(link);
        savedFormat = hda.read16(link + 4);
        status = "speaker-link-not-idle";
        return linkControl != UINT32_MAX && savedSid != UINT16_MAX && savedLinkControl != UINT32_MAX &&
            savedFormat != UINT16_MAX && !(savedLinkControl & (kHdaStreamRun | kHdaStreamReset));
    }

    template<class Bar, class Clock>
    bool reinitializeHost(Bar &hda, Clock &clock) {
        const uint8_t control = hostControl & 0xff;
        const uint8_t control1 = (hostControl >> 8) & 0xff;
        const uint8_t control2 = (hostControl >> 16) & 0xff;
        const auto live = hda.read32(sd);
        if (live == UINT32_MAX || (live & 0x00ffffff) != (hostControl & 0x00ffffff) ||
            hda.read32(sd + 0x18) != bdlLow ||
            hda.read32(sd + 0x1c) != bdlHigh || hda.read16(sd + 0xc) != hostLvi ||
            hda.read32(sd + 8) != hostBytes || hda.read16(sd + 0x12) != format) return false;
        hostResetUnconfirmed = true;
        hda.write8(sd, kHdaStreamReset);
        const bool asserted = waitStream(hda, clock, sd, kHdaStreamReset | kHdaStreamRun, kHdaStreamReset);
        hda.write8(sd, 0);
        // Give the controller time to leave SRST before writing the format or BDL.
        clock.delayUs(3);
        if (!waitStream(hda, clock, sd, kHdaStreamReset | kHdaStreamRun, 0)) return false;
        hda.write8(sd + 1, control1);
        hda.write8(sd + 2, control2);
        hda.write32(sd + 8, hostBytes);
        hda.write16(sd + 0x12, format);
        hda.write16(sd + 0xc, hostLvi);
        hda.write32(sd + 0x18, bdlLow);
        hda.write32(sd + 0x1c, bdlHigh);
        hda.write8(sd, control);
        const auto restored = hda.read32(sd);
        hostResetUnconfirmed = restored == UINT32_MAX || (restored & 0x00ffffff) != (hostControl & 0x00ffffff) ||
            hda.read32(sd + 8) != hostBytes || hda.read16(sd + 0x12) != format ||
            hda.read16(sd + 0xc) != hostLvi || hda.read32(sd + 0x18) != bdlLow || hda.read32(sd + 0x1c) != bdlHigh;
        return hostReinitialized = asserted && !hostResetUnconfirmed;
    }

    template<class Bar, class Clock>
    bool request(Bar &dsp, Clock &clock, Report &boot, const char *stage,
                 uint32_t primary, uint32_t extension = 0, const uint8_t *data = nullptr, uint32_t size = 0) {
        status = stage;
        lastRequest = primary;
        ipc = sendIpc(dsp, clock, primary, extension, data, size, 100);
        boot.ipcSafeForD3 = ipc.reusable;
        if (!ipc.accepted() && !failedRequest) {
            failedRequest = primary;
            failedReply = ipc.reply;
        }
        return ipc.accepted();
    }

    template<class Bar, class Clock>
    bool prepareLink(Bar &hda, Clock &clock) {
        status = "speaker-link-reset-failed";
        if (!update32(hda, link, kHdaStreamRun | kHdaStreamReset, kHdaStreamReset) ||
            !wait32(hda, clock, link, kHdaStreamReset | kHdaStreamRun, kHdaStreamReset, 5) ||
            !update32(hda, link, kHdaStreamReset, 0) ||
            !wait32(hda, clock, link, kHdaStreamReset | kHdaStreamRun, 0, 5)) return false;
        status = "speaker-link-program-failed";
        if (!update32(hda, link, tagMask, tagBits)) return false;
        const auto sid = hda.read16(ml + 8);
        if (sid == UINT16_MAX) return false;
        hda.write16(link + 4, format);
        hda.write16(ml + 8, sid | sidBit);
        const auto activeSid = hda.read16(ml + 8);
        linkPrepared = (hda.read32(link) & tagMask) == tagBits && hda.read16(link + 4) == format &&
            activeSid != UINT16_MAX && (activeSid & sidBit);
        return linkPrepared;
    }

    template<class Bar, class Clock>
    bool stopPlayback(Bar &hda, Bar &dsp, Clock &clock, Report &boot) {
        // AppleHDA stopped its host descriptor. Complete an IPC4 STOP cycle
        // so that the next RUN initializes fresh DMA state instead of releasing PAUSE.
        if (started) {
            if (!request(dsp, clock, boot, "speaker-pause-pipeline-failed", state | kPipelinePaused)) return false;
            started = false;
        }
        status = "speaker-link-stop-timeout";
        if (!update32(hda, link, kHdaStreamRun, 0) || !wait32(hda, clock, link, kHdaStreamRun, 0, 5))
            return false;
        linkPrepared = false;
        if (pipelineNeedsReset) {
            if (!request(dsp, clock, boot, "speaker-reset-pipeline-failed", state | kPipelineReset)) return false;
            pipelineNeedsReset = false;
        }
        const auto sid = hda.read16(ml + 8);
        status = "speaker-link-id-clear-failed";
        if (sid == UINT16_MAX) return false;
        hda.write16(ml + 8, sid & ~sidBit);
        const auto cleared = hda.read16(ml + 8);
        if (cleared == UINT16_MAX || (cleared & sidBit)) return false;
        status = "speaker-pipeline-stopped";
        return true;
    }

    template<class Bar, class Clock>
    bool prepare(Bar &hda, Bar &dsp, Clock &clock, Report &boot, const PlaybackProfile &profile) {
        if (touched) return ready && format == profile.hdaFormat && boot.ipcSafeForD3;
        *this = {};
        if (!identify(hda, boot)) return false;
        status = "speaker-format-changed";
        if (format != profile.hdaFormat) return false;
        touched = true;
        status = "speaker-decouple-failed";
        if (!update32(hda, pp, ppBit, ppBit) || !wait32(hda, clock, pp, ppBit, ppBit, 5)) return false;
        status = "speaker-host-reinitialize-failed";
        if (!reinitializeHost(hda, clock)) return false;
        status = "speaker-link-power-timeout";
        if (!update32(hda, ml + 4, spa, spa) || !wait32(hda, clock, ml + 4, cpa, cpa, 15)) return false;
        if (!prepareLink(hda, clock)) return false;

        if (!request(dsp, clock, boot, "speaker-create-pipeline-failed", create)) return false;
        pipelineCreated = true;
        const uint32_t init = pipeline | (sizeof(profile.host) / 4);
        if (!request(dsp, clock, boot, "speaker-init-host-failed", hostInstance, init, profile.host, sizeof(profile.host)) ||
            !request(dsp, clock, boot, "speaker-init-link-failed", linkInstance, init, profile.link, sizeof(profile.link)) ||
            !request(dsp, clock, boot, "speaker-bind-copiers-failed", bind, linkBinding) ||
            !request(dsp, clock, boot, "speaker-arm-pipeline-failed", state | kPipelinePaused)) return false;
        ready = true;
        status = "speaker-pipeline-armed";
        return true;
    }

    template<class Bar>
    Trace trace(Bar &hda, const Report &boot) const {
        return {hda.read32(sd), hda.read32(sd + 4), hda.read32(sd + 0x2004), hda.read32(link), hda.read32(link + 8),
                hda.read32(ml + 4), hda.read16(ml + 8),
                boot.spibOffset ? hda.read32(boot.spibOffset + 4) : UINT32_MAX,
                boot.spibOffset ? hda.read32(boot.spibOffset + 8 + 8 * index) : UINT32_MAX};
    }

    template<class Bar, class Clock>
    bool observe(Bar &hda, Bar &dsp, Clock &clock, Report &boot) {
        if (!ready) return !touched;
        auto host = hda.read32(sd);
        if (host == UINT32_MAX) { status = "speaker-host-descriptor-unavailable"; return false; }
        const auto sync = hda.read32(0x34);
        if (sync == UINT32_MAX) { status = "speaker-sync-unavailable"; return false; }
        if (sync & ppBit) return true; // wait for AppleHDA's synchronized start/stop
        if (!(host & kHdaStreamRun)) {
            sawPosition = false;
            return !pipelineNeedsReset || stopPlayback(hda, dsp, clock, boot);
        }
        const auto ppState = hda.read32(pp);
        if (ppState == UINT32_MAX || !(ppState & ppBit) || (host & tagMask) != tagBits ||
            (host & kHdaStreamReset) || hda.read16(sd + 0x12) != format ||
            !hostBufferPresent(hda)) {
            status = "speaker-host-descriptor-changed";
            return false;
        }
        if (!started) {
            if (!linkPrepared && !prepareLink(hda, clock)) return false;
            status = "speaker-link-start-timeout";
            if (!update32(hda, link, kHdaStreamRun, kHdaStreamRun) ||
                !wait32(hda, clock, link, kHdaStreamRun, kHdaStreamRun, 5)) return false;
            pipelineNeedsReset = true;
            if (!request(dsp, clock, boot, "speaker-start-pipeline-failed", state | kPipelineRunning)) return false;
            started = true;
            ++starts;
            hostProgressMs = linkProgressMs = clock.nowMs();
        }
        ++runningPolls;
        auto hostPosition = hda.read32(sd + 4);
        auto alias = hda.read32(sd + 0x2004);
        auto linkPosition = hda.read32(link + 8);
        const auto advanced = [](uint32_t before, uint32_t after) {
            return before != UINT32_MAX && after != UINT32_MAX && before != after;
        };
        const bool hostAdvanced = sawPosition && (advanced(lastHost, hostPosition) || advanced(lastAlias, alias));
        const bool linkAdvanced = sawPosition && advanced(lastLink, linkPosition);
        const auto now = clock.nowMs();
        if (hostAdvanced) { ++hostProgress; hostProgressMs = now; }
        if (linkAdvanced) { ++linkProgress; linkProgressMs = now; }
        status = now - hostProgressMs >= 100 ? "speaker-host-dma-stalled" :
            now - linkProgressMs >= 100 ? "speaker-link-dma-stalled" : "speaker-pipeline-running";
        if (hostAdvanced || linkAdvanced) ++positionChanges;
        if (runningPolls == 1) first = latest = trace(hda, boot);
        else if (runningPolls == 2 || runningPolls % 40 == 0) latest = trace(hda, boot);
        lastHost = hostPosition; lastAlias = alias; lastLink = linkPosition; sawPosition = true;
        return true;
    }

    template<class Bar, class Clock>
    bool restore(Bar &hda, Bar &dsp, Clock &clock, Report &boot) {
        if (!touched) return true;
        const auto host = hda.read32(sd);
        if (hostResetUnconfirmed || host == UINT32_MAX || (host & (kHdaStreamRun | kHdaStreamReset))) return false;
        if (!boot.ipcSafeForD3) return false;
        if (!stopPlayback(hda, dsp, clock, boot) ||
            !update32(hda, link, kHdaStreamReset, 0) ||
            !wait32(hda, clock, link, kHdaStreamRun | kHdaStreamReset, 0, 5)) return false;
        if (pipelineCreated) {
            // Firmware deletes a newly created pipeline even when a copier INIT was rejected.
            if (!request(dsp, clock, boot, "speaker-delete-pipeline-failed", remove)) return false;
            pipelineCreated = false;
            ready = false;
        }
        const auto sid = hda.read16(ml + 8);
        if (sid == UINT16_MAX || !update32(hda, link, tagMask, savedLinkControl)) return false;
        hda.write16(link + 4, savedFormat);
        hda.write16(ml + 8, (sid & ~sidBit) | (savedSid & sidBit));
        if (hda.read16(link + 4) != savedFormat || (hda.read32(link) & tagMask) != (savedLinkControl & tagMask) ||
            (hda.read16(ml + 8) & sidBit) != (savedSid & sidBit)) return false;
        if (!update32(hda, pp, ppBit, 0) || !wait32(hda, clock, pp, ppBit, 0, 5) ||
            !update32(hda, ml + 4, spa, linkControl) || !wait32(hda, clock, ml + 4, spa, linkControl & spa, 5)) return false;
        touched = false;
        status = "speaker-pipeline-released";
        return true;
    }
};

} // namespace ace
