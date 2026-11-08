//
//  ISHLightSensor.cpp
//  ISHLightSensor
//
//  Copyright © 2018 usrsse2. All rights reserved.
//

#include "ISHLightSensor.hpp"
#include <Headers/kern_version.hpp>
#include <kern/clock.h>
#include "LightCodec.hpp"

OSDefineMetaClassAndStructors(ISHLightSensor, IOService)

bool ADDPR(debugEnabled) = true;
uint32_t ADDPR(debugPrintDelay) = 0;

bool ISHLightSensor::init(OSDictionary *dict) {
	if (!IOService::init(dict)) {
		SYSLOG("alsd", "failed to init the parent");
		return false;
	}

	atomic_init(&currentLux, 0xFFFFFFFFu);
	return true;
}

IOService *ISHLightSensor::probe(IOService *provider, SInt32 *score) {
	DBGLOG("alsd", "probe");

	if (!IOService::probe(provider, score))	{
		SYSLOG("alsd", "failed to probe the parent");
		return nullptr;
	}

	// The sensor is discovered asynchronously; never wait on the IOKit matching thread.

	ALSSensor sensor {ALSSensor::Type::Unknown7, true, 6, false};
	ALSSensor noSensor {ALSSensor::Type::NoSensor, false, 0, false};
	ISHSMCAmbientLightValue::Value emptyValue;
	lkb lkb;
	lks lks;

	VirtualSMCAPI::addKey(KeyAL, vsmcPlugin.data, VirtualSMCAPI::valueWithUint16(0, &forceBits, SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE));
	VirtualSMCAPI::addKey(KeyALI0, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
		reinterpret_cast<const SMC_DATA *>(&sensor), sizeof(sensor), SmcKeyTypeAli, nullptr, SMC_KEY_ATTRIBUTE_CONST | SMC_KEY_ATTRIBUTE_READ));
	VirtualSMCAPI::addKey(KeyALI1, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
		reinterpret_cast<const SMC_DATA *>(&noSensor), sizeof(noSensor), SmcKeyTypeAli, nullptr, SMC_KEY_ATTRIBUTE_CONST | SMC_KEY_ATTRIBUTE_READ));
	VirtualSMCAPI::addKey(KeyALRV, vsmcPlugin.data, VirtualSMCAPI::valueWithUint16(1, nullptr, SMC_KEY_ATTRIBUTE_CONST | SMC_KEY_ATTRIBUTE_READ));
	VirtualSMCAPI::addKey(KeyALV0, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
		reinterpret_cast<const SMC_DATA *>(&emptyValue), sizeof(emptyValue), SmcKeyTypeAlv, new ISHSMCAmbientLightValue(&currentLux, &forceBits),
		SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE));
	VirtualSMCAPI::addKey(KeyALV1, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
		reinterpret_cast<const SMC_DATA *>(&emptyValue), sizeof(emptyValue), SmcKeyTypeAlv, nullptr,
		SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE));
	VirtualSMCAPI::addKey(KeyLKSB, vsmcPlugin.data, VirtualSMCAPI::valueWithData(reinterpret_cast<const SMC_DATA *>(&lkb), sizeof(lkb),
		SmcKeyTypeLkb, nullptr, SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE));
	VirtualSMCAPI::addKey(KeyLKSS, vsmcPlugin.data, VirtualSMCAPI::valueWithData(reinterpret_cast<const SMC_DATA *>(&lks), sizeof(lks),
		SmcKeyTypeLks, nullptr, SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE));
	VirtualSMCAPI::addKey(KeyMSLD, vsmcPlugin.data, VirtualSMCAPI::valueWithUint8(0));

	return this;
}

bool ISHLightSensor::start(IOService *provider) {
	DBGLOG("alsd", "start");
	
	if (!IOService::start(provider)) {
		SYSLOG("asld", "failed to start the parent");
		return false;
	}

	setProperty("VersionInfo", "1.0.0");
    setProperty("ALSBackend","Intel ISH live readings");

	vsmcNotifier = VirtualSMCAPI::registerHandler(vsmcNotificationHandler, this);
	return vsmcNotifier != nullptr;
}

bool ISHLightSensor::vsmcNotificationHandler(void *sensors, void *refCon, IOService *vsmc, IONotifier *notifier) {
	if (sensors && vsmc) {
		DBGLOG("asld", "got vsmc notification");
		auto self = static_cast<ISHLightSensor *>(sensors);
		auto ret = vsmc->callPlatformFunction(VirtualSMCAPI::SubmitPlugin, true, sensors, &self->vsmcPlugin, nullptr, nullptr);
		if (ret == kIOReturnSuccess) {
			DBGLOG("asld", "submitted plugin");

			self->retain(); // Submitted SMC keys reference this object for the plugin lifetime.
            self->setProperty("NativeSMCPluginSubmitted",true);
            self->workloop = IOWorkLoop::workLoop();
			self->poller = IOTimerEventSource::timerEventSource(self, [](OSObject *object, IOTimerEventSource *sender) {
				auto ls = OSDynamicCast(ISHLightSensor, object);
				if (ls) ls->refreshSensor(true);
			});

			if (!self->poller || !self->workloop) {
				SYSLOG("asld", "failed to create poller or workloop");
				return false;
			}

			if (self->workloop->addEventSource(self->poller) != kIOReturnSuccess) {
				SYSLOG("asld", "failed to add timer event source to workloop");
				return false;
			}

			if (self->poller->setTimeoutMS(SensorUpdateTimeoutMS) != kIOReturnSuccess) {
				SYSLOG("asld", "failed to set timeout");
				return false;
			}

			return true;
		} else if (ret != kIOReturnUnsupported) {
			SYSLOG("asld", "plugin submission failure %X", ret);
		} else {
			DBGLOG("asld", "plugin submission to non vsmc");
		}
	} else {
		SYSLOG("asld", "got null vsmc notification");
	}

	return false;
}

bool ISHLightSensor::refreshSensor(bool post) {
    uint32_t milli=0xffffffffu;uint64_t stamp=0,raw=0;bool stream=false,valid=false;
    auto dict=IOService::serviceMatching("ThinkBookISHProbe");
    auto iterator=dict ? IOService::getMatchingServices(dict) : nullptr;if(dict)dict->release();
    if(iterator){auto service=OSDynamicCast(IOService,iterator->getNextObject());
        if(service){
            // Copy properties individually under IORegistry ownership; do not hold raw borrowed pointers.
            auto v=service->copyProperty("ALSStreaming");stream=v==kOSBooleanTrue;OSSafeReleaseNULL(v);
            v=service->copyProperty("ALSSampleValid");valid=v==kOSBooleanTrue;OSSafeReleaseNULL(v);
            v=service->copyProperty("ALSRawMilliLux");auto n=OSDynamicCast(OSNumber,v);if(n)raw=n->unsigned64BitValue();OSSafeReleaseNULL(v);
            v=service->copyProperty("ALSReadingTimestampMS");n=OSDynamicCast(OSNumber,v);if(n)stamp=n->unsigned64BitValue();OSSafeReleaseNULL(v);
        }iterator->release();
    }
    uint64_t ticks,ns;clock_get_uptime(&ticks);absolutetime_to_nanoseconds(ticks,&ns);
    if(ishLightFresh(stream,valid,raw,stamp,ns/1000000))milli=uint32_t(raw);
    uint32_t previous=atomic_exchange_explicit(&currentLux,milli,memory_order_acq_rel);
    setProperty("ALSReadingValid",milli!=0xffffffffu);setProperty("ALSLuxMilli",milli,64);setProperty("SensorTimestampMS",stamp,64);
    if(post){if(previous!=milli)setProperty("ALSInterruptPostSucceeded",VirtualSMCAPI::postInterrupt(SmcEventALSChange));poller->setTimeoutMS(SensorUpdateTimeoutMS);}
    return milli!=0xffffffffu;
}

void ISHLightSensor::stop(IOService *provider) {
    atomic_store_explicit(&currentLux,0xffffffffu,memory_order_release);
    if(poller){poller->cancelTimeout();if(workloop)workloop->removeEventSource(poller);}
    OSSafeReleaseNULL(poller);OSSafeReleaseNULL(workloop);
    IOService::stop(provider);
}

EXPORT extern "C" kern_return_t ADDPR(kern_start)(kmod_info_t *, void *) {
	// Report success but actually do not start and let I/O Kit unload us.
	// This works better and increases boot speed in some cases.
	lilu_get_boot_args("liludelay", &ADDPR(debugPrintDelay), sizeof(ADDPR(debugPrintDelay)));
	ADDPR(debugEnabled) = checkKernelArgument("-vsmcdbg") || checkKernelArgument("-alsddbg") || checkKernelArgument("-liludbgall");
	return KERN_SUCCESS;
}

EXPORT extern "C" kern_return_t ADDPR(kern_stop)(kmod_info_t *, void *) {
	// It is not safe to unload VirtualSMC plugins!
	return KERN_FAILURE;
}
