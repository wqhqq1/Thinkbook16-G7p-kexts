//
//  GoodixForceTouchEngine.cpp
//  VoodooI2C
//
//  Created by Alexandre on 10/02/2018.
//  Copyright © 2018 Alexandre Daoud and Kishor Prins. All rights reserved.
//

#include "GoodixForceTouchEngine.hpp"

#define super VoodooI2CMultitouchEngine
OSDefineMetaClassAndStructors(GoodixForceTouchEngine, VoodooI2CMultitouchEngine);

MultitouchReturn GoodixForceTouchEngine::handleInterruptReport(VoodooI2CMultitouchEvent event, AbsoluteTime timestamp) {
    if (!voodooInputInstance) {
        return MultitouchReturnContinue;
    }

    if (!event.transducers || event.contact_count > VOODOO_INPUT_MAX_TRANSDUCERS)
        return MultitouchReturnContinue;
    message.timestamp = timestamp;
    message.contact_count = event.contact_count;
    memset(message.transducers, 0, sizeof(message.transducers));
    auto* firstTransducer = OSDynamicCast(VoodooI2CDigitiserTransducer, event.transducers->getObject(0));
    if (!firstTransducer) return MultitouchReturnContinue;
    if (firstTransducer->type == kDigitiserTransducerStylus)
        return MultitouchReturnContinue;
    const bool forceEnabled = isForceClickEnabled();
    unsigned activeCount = 0;
    VoodooI2CDigitiserTransducer* activeFinger = nullptr;
    for (int i = 0; i < event.contact_count; ++i) {
        auto* t = OSDynamicCast(VoodooI2CDigitiserTransducer, event.transducers->getObject(i));
        if (t && t->is_valid && t->tip_switch.value() && t->confidence.value()) {
            ++activeCount;
            activeFinger = t;
        }
    }
    const bool button = !firstTransducer->has_secondary_button && firstTransducer->physical_button.value();
    uint64_t nowNs = 0;
    absolutetime_to_nanoseconds(timestamp, &nowNs);
    pressureState.update(nowNs, activeFinger ? activeFinger->tip_pressure.value() : 0,
                         button, activeCount, activeFinger ? activeFinger->secondary_id : 0xffff,
                         forceEnabled);

    int valid_touch_count = 0;

    for (int i = 0; i < event.contact_count; i++) {
        VoodooI2CDigitiserTransducer* transducer = (VoodooI2CDigitiserTransducer*) event.transducers->getObject(i+stylus_check);
        VoodooInputTransducer* inputTransducer = &message.transducers[i];
        
        if (!transducer) {
            continue;
        }
        
        inputTransducer->fingerType = (MT2FingerType) (kMT2FingerTypeIndexFinger + (i % 4));
        inputTransducer->secondaryId = transducer->secondary_id;
        
        inputTransducer->type = (transducer->type == DigitiserTransducerType::kDigitiserTransducerFinger) ? VoodooInputTransducerType::FINGER : VoodooInputTransducerType::STYLUS;
        
        inputTransducer->isValid = transducer->is_valid;
        if (inputTransducer->isValid) {
            valid_touch_count++;
        }
        
        if (!transducer->confidence.value()) {
            inputTransducer->fingerType = kMT2FingerTypePalm;
        }
        
        inputTransducer->isTransducerActive = transducer->tip_switch.value();
        inputTransducer->isPhysicalButtonDown = !transducer->has_secondary_button && transducer->physical_button.value(); // if it has secondary button, then it will be passed as buttons on the "trackpoint" device
        
        inputTransducer->currentCoordinates.x = transducer->coordinates.x.value();
        inputTransducer->previousCoordinates.x = transducer->coordinates.x.last.value;
        
        inputTransducer->currentCoordinates.y = transducer->coordinates.y.value();
        inputTransducer->previousCoordinates.y = transducer->coordinates.y.last.value;
        inputTransducer->supportsPressure = false;
        inputTransducer->timestamp = timestamp;

        // Keep contact geometry stable. Raw Goodix pressure is 16-bit (0..2000),
        // while the Magic Trackpad pressure field is only 8-bit.
        inputTransducer->currentCoordinates.width = 10;
        inputTransducer->previousCoordinates.width = 10;
        const UInt32 rawPressure = transducer->tip_pressure.value();
        const UInt32 lightPressure = 20 + ((rawPressure < 10 ? rawPressure : 10));
        inputTransducer->currentCoordinates.pressure = transducer->tip_switch.value() ? lightPressure : 0;
        inputTransducer->previousCoordinates.pressure = 5;
        if (forceEnabled && !firstTransducer->has_secondary_button) {
            inputTransducer->supportsPressure = true;
            if (button && transducer->tip_switch.value()) {
                inputTransducer->currentCoordinates.pressure =
                    (pressureState.heavy && transducer == activeFinger) ? 255 : 120;
            }
        }

    }
    
    // set the thumb to improve 4F pinch and spread gesture and cross-screen dragging
    if (valid_touch_count >= 4 || firstTransducer->physical_button.value()) {
        // simple thumb detection: to find the lowest finger touch in the vertical direction.
        UInt32 y_max = 0;
        int thumb_index = 0;
        for (int i = 0; i < event.contact_count; i++) {
            VoodooInputTransducer* inputTransducer = &message.transducers[i];
            if (inputTransducer->isValid && inputTransducer->currentCoordinates.y >= y_max) {
                y_max = inputTransducer->currentCoordinates.y;
                thumb_index = i;
            }
        }
        message.transducers[thumb_index].fingerType = kMT2FingerTypeThumb;
    }
        
    super::messageClient(kIOMessageVoodooInputMessage, voodooInputInstance, &message, sizeof(VoodooInputEvent));
    
    if (firstTransducer->has_secondary_button) {
        RelativePointerEvent event;
        event.dx = 0;
        event.dy = 0;
        event.buttons = firstTransducer->physical_button.value();
        event.timestamp = timestamp;
        super::messageClient(kIOMessageVoodooTrackpointRelativePointer, voodooInputInstance, &event, sizeof(event));
    }
    return MultitouchReturnBreak;
}

bool GoodixForceTouchEngine::start(IOService* provider) {
    OSNumber* vendor = OSDynamicCast(OSNumber, provider->getProperty("VendorID"));
    OSNumber* product = OSDynamicCast(OSNumber, provider->getProperty("ProductID"));
    if (!vendor || !product || vendor->unsigned32BitValue() != 0x27c6 || product->unsigned32BitValue() != 0x01e9)
        return false;
    if (OSNumber* value = OSDynamicCast(OSNumber, getProperty("ForceDownPressure"))) pressureState.down = value->unsigned32BitValue();
    if (OSNumber* value = OSDynamicCast(OSNumber, getProperty("ForceUpPressure"))) pressureState.up = value->unsigned32BitValue();
    if (pressureState.down > 2000 || pressureState.up >= pressureState.down) return false;
    setProperty("PressureMapping", "Goodix calibrated two-stage 120/255");
    if (!super::start(provider))
        return false;

    parentProvider = OSDynamicCast(VoodooI2CMultitouchInterface, provider);
    
    if (!parentProvider) {
        return false;
    }
    
    voodooInputInstance = nullptr;
    
    setProperty(VOODOO_INPUT_LOGICAL_MAX_X_KEY, parentProvider->logical_max_x, 32);
    setProperty(VOODOO_INPUT_LOGICAL_MAX_Y_KEY, parentProvider->logical_max_y, 32);
    
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_X_KEY, parentProvider->physical_max_x, 32);
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_Y_KEY, parentProvider->physical_max_y, 32);

    setProperty("VoodooInputSupported", kOSBooleanTrue);
    
    stylus_check = 0;

    return true;
}

bool GoodixForceTouchEngine::isForceClickEnabled() {
    AbsoluteTime now_abs;
    uint64_t diff_ns;
    clock_get_uptime(&now_abs);
    absolutetime_to_nanoseconds(now_abs - lastForceClickPropertyUpdateTime, &diff_ns);

    if (diff_ns < 1e9) {
        return lastIsForceClickEnabled;
    }
    
    // Force-click availability is independent of tap-to-click.
    OSDictionary* dict = OSDynamicCast(OSDictionary, this->getProperty("MultitouchPreferences", gIOServicePlane, kIORegistryIterateRecursively));
    if (dict) {
        if (OSBoolean* value = OSDynamicCast(OSBoolean, dict->getObject("ForceSuppressed")))
            lastIsForceClickEnabled = !value->getValue();
        else if (OSNumber* value = OSDynamicCast(OSNumber, dict->getObject("ForceSuppressed")))
            lastIsForceClickEnabled = value->unsigned32BitValue() == 0;
    }

    lastForceClickPropertyUpdateTime = now_abs;
    return lastIsForceClickEnabled;
}

void GoodixForceTouchEngine::onPropertyChange() {
    lastForceClickPropertyUpdateTime = 0;
    if (voodooInputInstance) {
        super::messageClient(kIOMessageVoodooInputUpdatePropertiesNotification, voodooInputInstance);
    }
}

void GoodixForceTouchEngine::stop(IOService* provider) {
    super::stop(provider);
}

bool GoodixForceTouchEngine::handleOpen(IOService *forClient, IOOptionBits options, void *arg) {
    if (forClient && forClient->getProperty(VOODOO_INPUT_IDENTIFIER)) {
        voodooInputInstance = forClient;
        voodooInputInstance->retain();
        
        return true;
    }
    return false;
}

bool GoodixForceTouchEngine::handleIsOpen(const IOService *forClient) const {
    return voodooInputInstance != nullptr && forClient == voodooInputInstance;
}

void GoodixForceTouchEngine::handleClose(IOService *forClient, IOOptionBits options) {
    if (voodooInputInstance && forClient == voodooInputInstance) {
        OSSafeReleaseNULL(voodooInputInstance);
    }
}

bool GoodixForceTouchEngine::willTerminate(IOService *provider, IOOptionBits options) {
    return super::willTerminate(provider, options);
}
