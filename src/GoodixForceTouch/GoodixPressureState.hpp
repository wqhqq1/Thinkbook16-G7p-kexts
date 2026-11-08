// SPDX-License-Identifier: GPL-2.0
#ifndef GOODIX_PRESSURE_STATE_HPP
#define GOODIX_PRESSURE_STATE_HPP
#include <stdint.h>
struct GoodixPressureState {
    bool heavy = false;
    uint64_t candidate = 0;
    uint16_t contact = 0xffff;
    uint32_t down = 420;
    uint32_t up = 340;
    void update(uint64_t now, uint32_t pressure, bool pressed,
                unsigned contacts, uint16_t id, bool enabled) {
        if (!enabled || !pressed || contacts != 1 || pressure > 2000) {
            heavy = false; candidate = 0; contact = 0xffff; return;
        }
        if (contact != id) { heavy = false; candidate = 0; contact = id; }
        if (heavy) {
            if (pressure <= up) { heavy = false; candidate = 0; }
        } else if (pressure >= down) {
            if (!candidate) candidate = now;
            if (now - candidate >= 20000000) heavy = true;
        } else { candidate = 0; }
    }
};
#endif
