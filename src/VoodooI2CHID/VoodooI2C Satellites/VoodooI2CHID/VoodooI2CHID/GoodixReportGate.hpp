// SPDX-License-Identifier: GPL-2.0
#ifndef GOODIX_REPORT_GATE_HPP
#define GOODIX_REPORT_GATE_HPP
#include <stdint.h>
#include <stddef.h>
// Only identical complete Goodix touch reports within 20ms are stale.
// The scan-time bytes are included; no coordinate delta threshold is used.
struct GoodixReportGate {
    uint8_t last[40] = {};
    uint64_t deliveredAt = 0;
    bool valid = false;
    uint32_t dropped = 0;
    void reset() { valid = false; }
    bool accept(const uint8_t* bytes, size_t length, uint64_t now) {
        if (!bytes || length != sizeof(last) || bytes[0] != 4) return true;
        bool identical = valid;
        for (size_t i = 0; i < sizeof(last) && identical; ++i)
            if (bytes[i] != last[i]) identical = false;
        if (identical && now >= deliveredAt && now-deliveredAt <= 20000000) {
            ++dropped;
            return false;
        }
        for (size_t i = 0; i < sizeof(last); ++i) last[i] = bytes[i];
        deliveredAt = now;
        valid = true;
        return true;
    }
};
#endif
