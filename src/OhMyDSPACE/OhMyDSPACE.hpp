// BSD-3-Clause. ACE/IPC4 backend for the OhMyDSP userspace protocol.
#pragma once
#include <IOKit/IOService.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include "include/OhMyDSPShared.h"
#include "CaptureTimeline.hpp"

struct AceBackend;
class OhMyDSPACE : public IOService {
    OSDeclareDefaultStructors(OhMyDSPACE)
public:
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    void free() override;
    IOWorkLoop *getWorkLoop() const override { return loop; }
    IOReturn newUserClient(task_t task, void *token, UInt32 type,
                          OSDictionary *properties, IOUserClient **handler) override;
    IOReturn startCapture();
    IOReturn stopCapture();
    IOReturn setPowerState(unsigned long state, IOService *device) override;
    IOMemoryDescriptor *sharedMemory(UInt32 type) const;
private:
    static IOReturn gateStart(OSObject *, void *, void *, void *, void *);
    static IOReturn gateStop(OSObject *, void *, void *, void *, void *);
    static IOReturn gatePower(OSObject *, void *, void *, void *, void *);
    static void tick(OSObject *, IOTimerEventSource *);
    IOReturn begin();
    IOReturn end();
    void poll();
    void publish(UInt32 state, UInt32 position, UInt64 laps, UInt64 hostTime);
    void publishDiagnostics();
    AceBackend *backend = nullptr;
    IOWorkLoop *loop = nullptr;
    IOCommandGate *gate = nullptr;
    IOTimerEventSource *timer = nullptr;
    IOBufferMemoryDescriptor *ring = nullptr, *status = nullptr;
    OhMyDSPStatus *st = nullptr;
    bool active = false, suspended = false, pmReady = false;
    ace::CaptureTimeline timeline;
    UInt32 epoch = 0;
    UInt64 faults = 0;
};
