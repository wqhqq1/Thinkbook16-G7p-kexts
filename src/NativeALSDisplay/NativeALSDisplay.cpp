// Add the existing normalized usable range to this specific NVRM internal panel.
// Does not send brightness requests, access hardware, or modify GPU driver code.
#include <IOKit/IOService.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSBoolean.h>
#include <libkern/libkern.h>

class NativeALSDisplay : public IOService {
    OSDeclareDefaultStructors(NativeALSDisplay)
    OSDictionary *inserted {nullptr};
    IOService *screen {nullptr};
    IONotifier *notifier {nullptr};
    bool target(IOService *display) {
        auto v=display->copyProperty("DisplayVendorID");auto vn=OSDynamicCast(OSNumber,v);
        bool ok=vn && vn->unsigned32BitValue()==0x06af;OSSafeReleaseNULL(v);if(!ok)return false;
        v=display->copyProperty("DisplayProductID");vn=OSDynamicCast(OSNumber,v);
        ok=vn && vn->unsigned32BitValue()==0xc4b5;OSSafeReleaseNULL(v);if(!ok)return false;
        IORegistryEntry *entry=display;entry->retain();
        for(unsigned n=0;entry && n<12;n++) {
            if(!strcmp(entry->getName(),"NVRMFramebuffer")){entry->release();return true;}
            auto next=entry->getParentEntry(gIOServicePlane);if(next)next->retain();entry->release();entry=next;
        }
        OSSafeReleaseNULL(entry);return false;
    }
    bool apply(IOService *provider) {
        if(!target(provider))return true;
        if(screen)return true;
        auto raw=provider->copyProperty("IODisplayParameters");auto params=OSDynamicCast(OSDictionary,raw);
        if(!params){OSSafeReleaseNULL(raw);setProperty("RangeStatus","Target display has no parameters");return true;}
        if(params->getObject("usable-linear-brightness")) {
            setProperty("RangeStatus","Existing range left intact");raw->release();return true;
        }
        auto linear=OSDynamicCast(OSDictionary,params->getObject("linear-brightness"));
        auto bright=OSDynamicCast(OSDictionary,params->getObject("brightness"));
        auto lo=linear?OSDynamicCast(OSNumber,linear->getObject("min")):nullptr;
        auto hi=linear?OSDynamicCast(OSNumber,linear->getObject("max")):nullptr;
        auto blo=bright?OSDynamicCast(OSNumber,bright->getObject("min")):nullptr;
        auto bhi=bright?OSDynamicCast(OSNumber,bright->getObject("max")):nullptr;
        // Accept only the known 0..65535 normalized NVRM interface, never infer hardware bounds.
        bool ok=lo && hi && blo && bhi && lo->unsigned64BitValue()==0 && hi->unsigned64BitValue()==65535
            && blo->unsigned64BitValue()==0 && bhi->unsigned64BitValue()==65535;
        OSDictionary *updated=nullptr,*range=nullptr;
        if(ok){updated=OSDictionary::withDictionary(params);range=OSDictionary::withCapacity(2);
            ok=updated && range && range->setObject("min",lo) && range->setObject("max",hi)
                && updated->setObject("usable-linear-brightness",range);}
        // Outer copy keeps all existing parameter dictionaries shared with NVRMBacklightHandler.
        // No existing parameter, value, probe, or handler reference is replaced.
        if(ok)ok=provider->setProperty("IODisplayParameters",updated);
        if(ok){screen=provider;screen->retain();inserted=range;range=nullptr;setProperty("RangeStatus","Added normalized usable-linear-brightness");
            setProperty("UsableMinimum",0ULL,32);setProperty("UsableMaximum",65535ULL,32);}
        OSSafeReleaseNULL(range);OSSafeReleaseNULL(updated);raw->release();
        if(!ok)setProperty("RangeStatus","Display parameters not compatible; left intact");
        return true;
    }
    static bool matched(void *target,void *,IOService *service,IONotifier *) {
        return static_cast<NativeALSDisplay *>(target)->apply(service);
    }
public:
    bool start(IOService *provider) override {
        if(!IOService::start(provider))return false;
        setProperty("VersionInfo","1.0.1");setProperty("RangeStatus","Waiting for target NVRM display");
        auto dict=serviceMatching("AppleBacklightDisplay");
        if(!dict){IOService::stop(provider);return false;}
        // Load at IOResources during boot. Observe the later display instead of
        // requiring sandboxed WindowServer to request a lazy kernel-extension load.
        notifier=addMatchingNotification(gIOFirstPublishNotification,dict,matched,this,nullptr,1000);
        dict->release();
        if(!notifier){IOService::stop(provider);return false;}
        return true;
    }
    void stop(IOService *provider) override {
        if(notifier){notifier->remove();notifier=nullptr;}
        auto display=screen;
        // Remove only our own entry, retaining any later updates and existing brightness values.
        if(inserted && display){auto raw=display->copyProperty("IODisplayParameters");auto params=OSDynamicCast(OSDictionary,raw);
            if(params && params->getObject("usable-linear-brightness")==inserted){auto copy=OSDictionary::withDictionary(params);
                if(copy){copy->removeObject("usable-linear-brightness");display->setProperty("IODisplayParameters",copy);copy->release();}}
            OSSafeReleaseNULL(raw);}
        OSSafeReleaseNULL(inserted);OSSafeReleaseNULL(screen);
        IOService::stop(provider);
    }
};
OSDefineMetaClassAndStructors(NativeALSDisplay,IOService)
