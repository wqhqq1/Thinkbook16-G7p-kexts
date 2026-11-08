// SPDX-License-Identifier: GPL-2.0-only
// Wire definitions verified against Linux intel-ish-hid. No hardware access.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace ish {
constexpr size_t MaxIPC = 128;
constexpr size_t MaxFragment = 124; // IPC payload includes the 4-byte ISHTP header.
inline uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
}
inline void put32(uint8_t* p, uint32_t n) {
    for (unsigned i=0;i<4;i++) p[i]=uint8_t(n>>(8*i));
}
struct Firmware {
    uint8_t state;
    bool linkUp, transportReady, dmaActive;
    static Firmware decode(uint32_t n) {
        return {uint8_t((n>>12)&15), bool(n&1), bool(n&2), bool(n&0xf0000)};
    }
};
struct Doorbell {
    uint16_t length;
    uint8_t protocol, command;
    bool busy;
    static bool decode(uint32_t n, Doorbell& d) {
        d={uint16_t(n&1023), uint8_t((n>>10)&15),uint8_t((n>>16)&15),bool(n&0x80000000u)};
        return d.busy && d.length<=MaxIPC && (d.protocol==1 || d.protocol==3);
    }
};
struct Fragment {
    uint8_t firmware, host;
    const uint8_t* bytes;
    size_t length;
    bool complete;
};
enum class Channel {HBM, HID, Foreign};
inline Channel classify(const Fragment& f,uint8_t hid,bool connected){
    if(!f.firmware && !f.host)return Channel::HBM;
    if(hid && connected && f.firmware==hid && f.host==1)return Channel::HID;
    return Channel::Foreign;
}
inline bool decodeFragment(const uint8_t* p, size_t n, Fragment& out) {
    if (!p || n<4 || n>MaxIPC) return false;
    uint32_t hdr=le32(p);
    size_t length=(hdr>>16)&511;
    if ((hdr&0x7e000000u) || length>MaxFragment || length!=n-4) return false;
    out={uint8_t(hdr),uint8_t(hdr>>8),p+4,length,bool(hdr&0x80000000u)};
    return true;
}
inline size_t encodeHBM(const uint8_t* payload, size_t n, uint8_t* out, size_t cap) {
    if (!payload || !out || !n || n>MaxFragment || cap<n+4) return 0;
    put32(out,0x80000000u | uint32_t(n<<16)); // HBM is firmware=host=0.
    for(size_t i=0;i<n;i++) out[i+4]=payload[i];
    return n+4;
}
class Reassembly {
    uint8_t buffer[16384] {};
    size_t used=0;
    uint8_t fw=0, host=0;
    bool active=false;
public:
    void reset() {used=0;active=false;}
    // 0 means awaiting fragments, 1 complete, -1 rejected and discarded.
    int append(const Fragment& f) {
        if (!f.bytes || f.length>MaxFragment || !f.length ||
            (active && (fw!=f.firmware || host!=f.host)) || f.length>sizeof(buffer)-used) {
            reset(); return -1;
        }
        if (!active) {fw=f.firmware;host=f.host;}
        for(size_t i=0;i<f.length;i++) buffer[used+i]=f.bytes[i];
        used+=f.length;
        active=!f.complete;
        return f.complete ? 1 : 0;
    }
    const uint8_t* data() const {return buffer;}
    size_t size() const {return used;}
};
// Caller must reset the assembler after consuming a complete message, and on
// channel reset or fragment timeout. Never interpret DMA_XFER as an address.
constexpr uint8_t HIDGuid[16]={0x58,0xcd,0xae,0x33,0x79,0xb6,0x54,0x4e,
    0x9b,0xd9,0xa0,0x4d,0x34,0xf0,0xc2,0x26};
}
