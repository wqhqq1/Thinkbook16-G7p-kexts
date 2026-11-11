#pragma once
#include <stdint.h>
namespace ace {
struct CaptureTimeline {
    static constexpr uint32_t bytes = 24576, frameBytes = 8, rate = 48000;
    static constexpr uint64_t ringNs = 64000000;
    uint32_t position = 0;
    uint64_t laps = 0, lapNs = 0, tickNs = 0, progressNs = 0;
    bool aligned = false;
    struct Update { bool valid, gap, wrapped, stalled; };
    void reset(uint64_t now) { position = 0; laps = 0; lapNs = tickNs = progressNs = now; aligned = false; }
    Update observe(uint32_t next, uint64_t now) {
        if (next >= bytes || next % frameBytes || now < tickNs) return {false,false,false,false};
        const bool gap = now - tickNs >= ringNs;
        const bool wrapped = !gap && next < position;
        const bool changed = next != position;
        const bool stalled = !changed && now - progressNs >= 200000000;
        if (gap) laps += (now - tickNs) / ringNs + 1; // discard uncertain laps
        else if (wrapped) ++laps;
        if ((!aligned && changed) || wrapped || gap) {
            lapNs = now - uint64_t(next / frameBytes) * 1000000000ULL / rate;
            aligned = true;
        }
        if (changed) progressNs = now;
        position = next; tickNs = now;
        return {true,gap,wrapped,stalled};
    }
};
}
