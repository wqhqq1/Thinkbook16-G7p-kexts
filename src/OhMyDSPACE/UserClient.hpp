// Adapted from OhMyDSP (c) 2026 Vints, BSD-3-Clause.
#pragma once
#include <IOKit/IOUserClient.h>
class OhMyDSPACE;
class OhMyDSPACEUserClient : public IOUserClient {
    OSDeclareDefaultStructors(OhMyDSPACEUserClient)
public:
    bool initWithTask(task_t, void *, UInt32, OSDictionary *) override;
    bool start(IOService *) override;
    void stop(IOService *) override;
    IOReturn clientClose() override;
    IOReturn clientMemoryForType(UInt32, IOOptionBits *, IOMemoryDescriptor **) override;
    IOReturn externalMethod(uint32_t, IOExternalMethodArguments *, IOExternalMethodDispatch *, OSObject *, void *) override;
private:
    OhMyDSPACE *owner = nullptr;
    bool opened = false;
    void closeOwner();
};
