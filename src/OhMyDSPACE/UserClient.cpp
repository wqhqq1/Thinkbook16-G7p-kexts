// Adapted from OhMyDSP (c) 2026 Vints, BSD-3-Clause.
#include "UserClient.hpp"
#include "OhMyDSPACE.hpp"
#include <IOKit/IOLib.h>
OSDefineMetaClassAndStructors(OhMyDSPACEUserClient, IOUserClient)

bool OhMyDSPACEUserClient::initWithTask(task_t task, void *token, UInt32 type, OSDictionary *props) {
    if (type || clientHasPrivilege(task, "root") != kIOReturnSuccess) return false;
    return IOUserClient::initWithTask(task, token, type, props);
}
bool OhMyDSPACEUserClient::start(IOService *provider) {
    owner = OSDynamicCast(OhMyDSPACE, provider);
    if (!owner || !IOUserClient::start(provider) || !owner->open(this)) return false;
    opened = true;
    return true;
}
void OhMyDSPACEUserClient::closeOwner() {
    if (opened && owner) {
        owner->stopCapture();
        owner->close(this);
        opened = false;
    }
}
void OhMyDSPACEUserClient::stop(IOService *provider) {
    closeOwner(); IOUserClient::stop(provider);
}
IOReturn OhMyDSPACEUserClient::clientClose() {
    closeOwner(); terminate(); return kIOReturnSuccess;
}
IOReturn OhMyDSPACEUserClient::clientMemoryForType(UInt32 type, IOOptionBits *options, IOMemoryDescriptor **memory) {
    if (!opened || !owner || !options || !memory) return kIOReturnNotAttached;
    auto m = owner->sharedMemory(type);
    if (!m) return kIOReturnBadArgument;
    m->retain(); *memory = m; *options = kIOMapReadOnly;
    return kIOReturnSuccess;
}
IOReturn OhMyDSPACEUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *a,
                                            IOExternalMethodDispatch *, OSObject *, void *) {
    if (!opened || !owner || !a) return kIOReturnNotAttached;
    if (a->scalarInputCount || a->scalarOutputCount || a->structureInputSize || a->structureOutputSize ||
        a->structureInputDescriptor || a->structureOutputDescriptor) return kIOReturnBadArgument;
    switch (selector) {
    case OHMYDSP_SEL_START: return owner->startCapture();
    case OHMYDSP_SEL_STOP: return owner->stopCapture();
    default: return kIOReturnUnsupported;
    }
}
