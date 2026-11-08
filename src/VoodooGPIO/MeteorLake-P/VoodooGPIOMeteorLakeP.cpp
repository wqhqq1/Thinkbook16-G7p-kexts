#include "VoodooGPIOMeteorLakeP.hpp"
OSDefineMetaClassAndStructors(VoodooGPIOMeteorLakeP, VoodooGPIOIntel);
bool VoodooGPIOMeteorLakeP::start(IOService *provider) {
    this->pins = mtlp_pins;
    this->npins = ARRAY_SIZE(mtlp_pins);
    this->groups = nullptr;
    this->ngroups = 0;
    this->functions = nullptr;
    this->nfunctions = 0;
    this->communities = mtlp_communities;
    this->ncommunities = ARRAY_SIZE(mtlp_communities);
    setProperty("GPIOPlatform", "Meteor Lake-P / INTC105E");
    setProperty("GPIOInputStatusOffset", 0x200, 32);
    IOLog("%s::Loading GPIO Data for Meteor Lake-P / INTC105E\n", getName());
    return VoodooGPIOIntel::start(provider);
}
