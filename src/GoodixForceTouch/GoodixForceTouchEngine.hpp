//
//  GoodixForceTouchEngine.hpp
//  VoodooI2C
//
//  Created by Alexandre on 10/02/2018.
//  Copyright © 2018 Alexandre Daoud and Kishor Prins. All rights reserved.
//

#ifndef GoodixForceTouchEngine_hpp
#define GoodixForceTouchEngine_hpp

#include <IOKit/IOLib.h>
#include "GoodixPressureState.hpp"
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOService.h>

#include "VoodooI2CMultitouchInterface.hpp"
#include "VoodooI2CMultitouchEngine.hpp"


#include "VoodooInputMultitouch/VoodooInputTransducer.h"
#include "VoodooInputMultitouch/VoodooInputMessages.h"

class EXPORT GoodixForceTouchEngine : public VoodooI2CMultitouchEngine {
    OSDeclareDefaultStructors(GoodixForceTouchEngine);
    
    VoodooInputEvent message;
    VoodooI2CMultitouchInterface* parentProvider;
    IOService* voodooInputInstance;

    bool lastIsForceClickEnabled = false;
    AbsoluteTime lastForceClickPropertyUpdateTime = 0;
    GoodixPressureState pressureState;

    bool isForceClickEnabled();
 public:
    UInt8 getScore() override { return 100; }
    bool start(IOService* provider) override;
    void stop(IOService* provider) override;
    
    bool handleOpen(IOService *forClient, IOOptionBits options, void *arg) override;
    bool handleIsOpen(const IOService *forClient) const override;
    void handleClose(IOService *forClient, IOOptionBits options) override;
    void onPropertyChange() override;
    bool willTerminate(IOService* provider, IOOptionBits options) override;
    
    MultitouchReturn handleInterruptReport(VoodooI2CMultitouchEvent event, AbsoluteTime timestamp) override;
 private:
    int stylus_check = 0;
};


#endif /* GoodixForceTouchEngine_hpp */
