#pragma once
#include <stdint.h>
static inline bool ishLightFresh(bool stream,bool valid,uint64_t raw,uint64_t stamp,uint64_t now){return stream && valid && raw<0xffffffffu && stamp>0 && now>=stamp && now-stamp<3000;}
static inline uint32_t ishLightFP(uint32_t milli){uint64_t n=uint64_t(milli)*16384/1000;return n>0xffffffffu?0xffffffffu:uint32_t(n);}
static inline uint16_t ishLightChannel(uint32_t milli){uint32_t n=milli/1000;return n>65535?65535:uint16_t(n);}
