//
//  AmbientLightValue.cpp
//  ISHLightSensor
//
//  Copyright © 2018 usrsse2. All rights reserved.
//

#include "AmbientLightValue.hpp"
#include "LightCodec.hpp"

SMC_RESULT ISHSMCAmbientLightValue::readAccess() {
	auto value = reinterpret_cast<Value *>(data);
	uint32_t lux = atomic_load_explicit(currentLux, memory_order_acquire);
	uint8_t bits = forceBits->bits();

	if (lux == 0xFFFFFFFF) {
		value->valid = false;
	} else {
		value->valid = true;
		if (!(bits & ALSForceBits::kALSForceHighGain))
			value->highGain = true;
		if (!(bits & ALSForceBits::kALSForceChan))
			value->chan0 = OSSwapHostToBigInt16(ishLightChannel(lux));
		if (!(bits & ALSForceBits::kALSForceLux))
			value->roomLux = OSSwapHostToBigInt32(ishLightFP(lux));
	}

	return SmcSuccess;
}
