#pragma once
#include "AceBootTrial.hpp"
namespace ace {
// SOF v2.15 IPC4 base_fw.h/module.h and base_fw_intel.c. These are firmware
// commands, not accesses to the DSP-local gateway registers inaccessible to BAR4.
constexpr uint32_t kLargeConfigSetBase = 0x44000000;
constexpr uint32_t kSetFwConfig12Bytes = (1U << 29) | (1U << 28) | (7U << 20) | 12U;
constexpr uint32_t kSetD0ix = 0x48000000;
template<class Bar, class Clock>
bool holdDmiL0(Bar &dsp, Clock &clock, Report &boot, IpcResult &result) {
    const uint8_t tlv[] = {28,0,0,0, 4,0,0,0, 1,0,0,0};
    result = sendIpc(dsp, clock, kLargeConfigSetBase, kSetFwConfig12Bytes, tlv, sizeof(tlv));
    boot.ipcSafeForD3 = result.reusable;
    return result.accepted();
}
template<class Bar, class Clock>
bool preventD0ix(Bar &dsp, Clock &clock, Report &boot, IpcResult &result) {
    result = sendIpc(dsp, clock, kSetD0ix, 1U << 2);
    boot.ipcSafeForD3 = result.reusable;
    return result.accepted();
}
}
