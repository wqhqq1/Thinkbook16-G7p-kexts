#include "LightCodec.hpp"
#include <assert.h>
#include <stdio.h>
int main(){assert(ishLightFresh(true,true,91696,1000,2000));assert(!ishLightFresh(true,true,91696,1000,4000));assert(!ishLightFresh(false,true,1,1000,1001));assert(!ishLightFresh(true,false,1,1000,1001));assert(!ishLightFresh(true,true,0xffffffffu,1000,1001));assert(!ishLightFresh(true,true,1,1001,1000));assert(ishLightFP(91696)==1502347);assert(ishLightChannel(91696)==91);assert(ishLightFP(0)==0);assert(ishLightFP(4000000000u)==0xffffffffu);assert(ishLightChannel(4000000000u)==65535);puts("PASS: 91.696 lux to native FP18.14, zero lux, saturation, stale/invalid/missing/future input");}
