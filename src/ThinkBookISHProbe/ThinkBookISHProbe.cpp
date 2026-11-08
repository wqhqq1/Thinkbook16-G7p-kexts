// SPDX-License-Identifier: GPL-2.0-only
// Continuous ARL-H ALS polling with bounded client recovery. No host DMA buffer advertised.
#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <libkern/OSByteOrder.h>
#include <pexpert/pexpert.h>
#include <kern/clock.h>
#include <IOKit/IOMessage.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include "ISHSession.hpp"

class ThinkBookISHProbe : public IOService {
    OSDeclareDefaultStructors(ThinkBookISHProbe)
    IOPCIDevice* pci=nullptr; IOMemoryMap* mapping=nullptr;
    IOWorkLoop* loop=nullptr; IOTimerEventSource* timer=nullptr;
    volatile uint8_t* base=nullptr;
    ish::Session session; ish::Reassembly assembly, hidAssembly;
    ish::Phase published=ish::Phase::Idle;
    bool opened=false, changed=false, cleaning=false, stopping=false, timerAdded=false;
    unsigned cleanupTicks=0, rxCount=0, txCount=0, foreignCount=0, traceCount=0;
    uint64_t publishedSample=0;
    IONotifier* powerNotifier=nullptr;bool parked=false,resuming=false;
    bool wakePending=false,wakeAwaitingSample=false;unsigned wakeChecks=0,wakeResumes=0,wakeSuccesses=0,assemblyEpoch=0;
    unsigned publishedRecoveryCount=~0u,publishedRecoverySuccesses=~0u;
    uint64_t lastPollMS=0,maxCallbackGapMS=0;
    uint8_t trace[64*8] {};
    uint32_t oldHost=0, oldRemap=0;
    uint32_t read(unsigned o){return OSSwapLittleToHostInt32(*reinterpret_cast<volatile uint32_t*>(base+o));}
    void write(unsigned o,uint32_t v){*reinterpret_cast<volatile uint32_t*>(base+o)=OSSwapHostToLittleInt32(v);(void)read(0x34);}
    static uint64_t now(){uint64_t t,n;clock_get_uptime(&t);absolutetime_to_nanoseconds(t,&n);return n/1000000;}
    static void fired(OSObject* owner,IOTimerEventSource*){static_cast<ThinkBookISHProbe*>(owner)->poll();}
    void snapshot(){setProperty("FirmwareStatus",read(0x34),32);setProperty("HostCommunication",read(0x38),32);setProperty("Remap2",read(0x368),32);setProperty("HostToISH",read(0x48),32);setProperty("ISHToHost",read(0x54),32);}
    void publish(){
        if(session.recoveryCount!=publishedRecoveryCount || session.recoverySuccesses!=publishedRecoverySuccesses){
            publishedRecoveryCount=session.recoveryCount;publishedRecoverySuccesses=session.recoverySuccesses;
            setProperty("ALSRecoveryAttempts",session.recoveryCount,32);setProperty("ALSRecoverySuccesses",session.recoverySuccesses,32);
            setProperty("ALSLastRecoveryError",session.lastRecoveryError);
            setProperty("ALSLastInputDispatchMS",session.lastInputDispatchMS,64);setProperty("ALSLastInputReplyMS",session.lastInputReplyMS,64);
        }
        if(session.sampleSequence!=publishedSample){
            publishedSample=session.sampleSequence;
            if(wakeAwaitingSample && session.latestValid && !session.recoveringInput()){wakeAwaitingSample=false;setProperty("ALSWakeRecoverySuccesses",++wakeSuccesses,32);}
            setProperty("ALSStreaming",!session.recoveringInput());setProperty("ALSSampleValid",session.latestValid && !session.recoveringInput());
            setProperty("ALSSampleSequence",session.sampleSequence,64);setProperty("ALSReadingTimestampMS",now(),64);
            setProperty("ALSRawMilliLux",session.rawMilliLux,64);setProperty("ALSValidInputCount",session.validInputCount,32);
            setProperty("RXCount",rxCount,32);setProperty("TXCount",txCount,32);
            auto data=OSData::withBytes(session.inputReports[(session.sampleSequence-1)%10],31);if(data){setProperty("ALSLatestInput",data);data->release();}
        }
        if(session.finished()){setProperty("ALSStreaming",false);setProperty("ALSSampleValid",false);}

        static const char* names[]={"Idle","Waiting for reset ACK","Waiting for firmware ready","Waiting for HBM start","Enumerating clients","Reading client properties","Connecting HID client","Enumerating HID devices","Reading ALS HID descriptor","Reading ALS report descriptor","Reading sensor feature report","Sampling sensor input","Disconnecting HID client","Sensor sample session complete","Session failed","Waiting before HID reconnect"};
        if(session.phase==published)return;published=session.phase;
        if(session.phase!=ish::Phase::InputSamples || session.recoveringInput()){setProperty("ALSStreaming",false);setProperty("ALSSampleValid",false);}
        setProperty("ProbeStatus",names[unsigned(session.phase)]);setProperty("LastError",session.error);
        setProperty("RXCount",rxCount,32);setProperty("TXCount",txCount,32);snapshot();
        if(!session.finished())return;
        setProperty("SessionFirmwareStatus",read(0x34),32);
        setProperty("FailureTimestampMS",now(),64);setProperty("HBMTxCredits",session.txCredits(),32);
        setProperty("HIDWaitingForCredit",session.waitingForCredit());setProperty("HIDRequestSent",session.inputRequestSent());
        setProperty("HIDRequestDispatched",session.inputRequestDispatched());
        setProperty("PendingTXCount",session.queueDepth(),32);
        setProperty("ForeignChannelCount",foreignCount,32);
        auto log=OSData::withBytes(trace,traceCount*8);if(log){setProperty("IPCChannelTrace",log);log->release();}
        setProperty("HBMStartFlags",session.startFlags,32);
        auto array=OSArray::withCapacity(session.clientCount ? session.clientCount : 1);
        unsigned hid=0;
        if(array){
            for(unsigned i=0;i<session.clientCount;i++){
                const auto& c=session.clients[i];auto d=OSDictionary::withCapacity(8);
                if(!d)continue;
                auto number=OSNumber::withNumber(c.address,32);if(number){d->setObject("Address",number);number->release();}
                auto guid=OSData::withBytes(c.guid,16);if(guid){d->setObject("GUID",guid);guid->release();}
                number=OSNumber::withNumber(c.maxMessage,32);if(number){d->setObject("MaxMessage",number);number->release();}
                number=OSNumber::withNumber(c.dmaHeader,32);if(number){d->setObject("DMAHeader",number);number->release();}
                bool isHID=true;for(unsigned j=0;j<16;j++)if(c.guid[j]!=ish::HIDGuid[j])isHID=false;
                if(isHID){++hid;setProperty("HIDClientAddress",c.address,32);}
                d->setObject("IsHID",isHID?kOSBooleanTrue:kOSBooleanFalse);array->setObject(d);d->release();
            }
            setProperty("ISHTPClients",array);array->release();
        }
        uint8_t deviceBytes[32*9] {};
        for(unsigned i=0;i<session.deviceCount;i++){const auto& d=session.devices[i];auto p=deviceBytes+i*9;ish::put32(p,d.id);p[4]=d.cls;p[5]=uint8_t(d.pid);p[6]=uint8_t(d.pid>>8);p[7]=uint8_t(d.vid);p[8]=uint8_t(d.vid>>8);}
        auto devs=OSData::withBytes(deviceBytes,session.deviceCount*9);if(devs){setProperty("HIDDevicesPacked",devs);devs->release();}
        setProperty("HIDDeviceCount",session.deviceCount,32);setProperty("ALSDeviceID",session.alsDevice,32);
        if(session.hidDescriptorLength){auto data=OSData::withBytes(session.hidDescriptor,unsigned(session.hidDescriptorLength));if(data){setProperty("ALSHIDDescriptor",data);data->release();}}
        if(session.reportDescriptorLength){auto data=OSData::withBytes(session.reportDescriptor,unsigned(session.reportDescriptorLength));if(data){setProperty("ALSReportDescriptor",data);data->release();}}
        setProperty("ALSFeatureValid",session.featureReceived);
        auto feature=OSData::withBytes(session.featureReport,session.featureReceived?305:0);if(feature){setProperty("ALSFeatureReport",feature);feature->release();}
        auto inputs=OSData::withBytes(session.inputReports,session.inputCount*31);if(inputs){setProperty("ALSInputSamples",inputs);inputs->release();}
        setProperty("ALSInputCount",session.inputCount,32);setProperty("ALSValidInputCount",session.validInputCount,32);
        setProperty("ALSRawMilliLux",session.rawMilliLux,64);setProperty("ALSReadingsValid",session.validInputCount!=0);
        setProperty("ALSFeaturePower",session.featureReport[4],32);setProperty("ALSFeatureReporting",session.featureReport[1],32);setProperty("ALSFeatureInterval",ish::le32(session.featureReport+9),32);
        setProperty("ClientCount",session.clientCount,32);setProperty("HIDClientCount",hid,32);
    }
    void cleanup(){
        if(!changed)return;
        if(read(0x34)==0xffffffffu){setProperty("CleanupStatus","PCI unavailable; no further writes");changed=false;return;}
        if(!cleaning){write(0x368,oldRemap);cleaning=true;cleanupTicks=0;}
        if(!(read(0x34)&0xf0000)){
            write(0x38,oldHost);changed=false;setProperty("CleanupStatus","DMA inactive; original host/DMA gates restored");snapshot();
        }else if(++cleanupTicks>=10){setProperty("CleanupStatus","DMA remains active after bounded wait; reboot required");return;}
        else timer->setTimeoutMS(10);
    }
    bool resumeAfterWake(){
            // Sleep quiescence can complete after its notification. Recheck asynchronously;
            // never reset the sensor while DMA is active or MMIO is unavailable.
            uint32_t fw=read(0x34);
            if(fw==0xffffffffu || (fw&0xf0000)){
                if(fw!=0xffffffffu && ++wakeChecks<10){timer->setTimeoutMS(500);return false;}
                wakePending=false;stopping=true;setProperty("ProbeStatus","Wake stopped: DMA not quiesced or MMIO unavailable");return false;
            }
            if(changed){write(0x38,oldHost);changed=false;}
            session.reset();assembly.reset();hidAssembly.reset();published=ish::Phase::Idle;publishedSample=0;
            rxCount=txCount=foreignCount=traceCount=0;cleaning=parked=false;resuming=true;wakePending=false;
            setProperty("ALSWakeResumeAttempts",++wakeResumes,32);wakeAwaitingSample=true;assemblyEpoch=0;
        return true;
    }
    void poll(){
        if(stopping)return;
        if(wakePending && !resumeAfterWake())return;
        if(session.finished()){cleanup();return;}
        const uint64_t ms=now();
        if(lastPollMS && ms>=lastPollMS && ms-lastPollMS>maxCallbackGapMS){maxCallbackGapMS=ms-lastPollMS;setProperty("ALSMaximumCallbackGapMS",maxCallbackGapMS,64);}lastPollMS=ms;
        if(session.phase==ish::Phase::Idle){
            uint32_t fw=read(0x34);oldHost=read(0x38);oldRemap=read(0x368);
            if(fw==0xffffffffu || (!resuming && (((fw>>12)&15)!=4 || (fw&3))) || (resuming && ((fw>>12)&15)!=4 && ((fw>>12)&15)!=7 && ((fw>>12)&15)!=15) || (fw&0xf0000) || oldHost || oldRemap || read(0x08) || read(0x48) || read(0x54)){
                session.fail("Initial hardware state differs from verified WAIT_FOR_HOST; no writes");publish();return;
            }
            // Poll with hardware interrupts kept masked. Do not enable PIMR without an IRQ handler.
            resuming=false;changed=true;write(0x38,oldHost|0x80);write(0x368,1);session.begin(ms);
        }
        if(read(0x34)==0xffffffffu){session.fail("PCI/MMIO unavailable");publish();cleanup();return;}
        uint32_t bell=read(0x54);
        if(bell&0x80000000u){
            setProperty("LastInboundDoorbell",bell,32);
            ish::Doorbell d {};
            if(!ish::Doorbell::decode(bell,d)){session.fail("Invalid inbound IPC doorbell");}
            else{
                uint8_t bytes[128] {};
                for(unsigned i=0;i<d.length;i+=4){uint32_t v=read(0x60+i);for(unsigned j=0;j<4 && i+j<d.length;j++)bytes[i+j]=uint8_t(v>>(8*j));}
                ++rxCount;
                if(d.protocol==3)session.management(d.command,bytes,d.length,ms);
                else{
                    ish::Fragment f {};
                    if(!ish::decodeFragment(bytes,d.length,f)){session.fail("Invalid IPC fragment");}
                    else {
                    setProperty("LastProtocolPhase",unsigned(session.phase),32);
                    if(traceCount<64){auto p=trace+traceCount++*8;p[0]=f.firmware;p[1]=f.host;p[2]=uint8_t(session.phase);p[3]=f.complete?1:0;ish::put32(p+4,uint32_t(f.length));}
                    auto channel=ish::classify(f,session.hidAddress,session.phase>=ish::Phase::HIDEnum);
                    if(channel==ish::Channel::HBM){
                        int result=assembly.append(f);if(result<0)session.fail("HBM reassembly invalid");else if(result==1){setProperty("LastHBMCommand",assembly.data()[0],32);session.hbm(assembly.data(),assembly.size(),ms);assembly.reset();}
                    }else if(channel==ish::Channel::HID){
                        int result=hidAssembly.append(f);if(result<0)session.fail("HID reassembly invalid");else if(result==1){setProperty("LastHOSTIFCommand",hidAssembly.data()[0],32);
                        const char* replyKey=hidAssembly.data()[0]==0xa1?"RawHIDEnumerationReply":hidAssembly.data()[0]==0x80?"RawHIDDescriptorReply":hidAssembly.data()[0]==0x81?"RawReportDescriptorReply":hidAssembly.data()[0]==0x82?"RawFeatureReply":hidAssembly.data()[0]==0x84?"RawInputReply":nullptr;
                        if(replyKey){auto raw=OSData::withBytes(hidAssembly.data(),unsigned(hidAssembly.size()));if(raw){setProperty(replyKey,raw);raw->release();}}
                        auto hdr=OSData::withBytes(hidAssembly.data(),unsigned(hidAssembly.size()<6?hidAssembly.size():6));if(hdr){setProperty("LastHOSTIFHeader",hdr);hdr->release();}session.hid(hidAssembly.data(),hidAssembly.size(),ms);hidAssembly.reset();}
                    }else {
                        ++foreignCount;
                        if(foreignCount==1){auto data=OSData::withBytes(bytes,d.length<16?d.length:16);if(data){setProperty("FirstForeignIPCHeader",data);data->release();}}
                    }
                    }
                }
            }
            write(0x54,0); // Consume at most one bounded mailbox message per callback.
        }
        session.tick(ms,read(0x34),bool(read(0x54)&0x80000000u));
        if(assemblyEpoch!=session.transportEpoch){assembly.reset();hidAssembly.reset();assemblyEpoch=session.transportEpoch;}
        if(!session.finished() && !(read(0x48)&0x80000000u)){
            if(const auto* t=session.pending()){
                for(unsigned i=0;i<t->length;i+=4){uint32_t v=0;for(unsigned j=0;j<4 && i+j<t->length;j++)v|=uint32_t(t->bytes[i+j])<<(8*j);write(0xe0+i,v);}
                uint32_t header=0x80000000u | uint32_t(t->protocol)<<10 | t->length;
                if(t->protocol==3)header|=uint32_t(t->command)<<16;
                write(0x48,header);session.sent(ms);++txCount;
            }
        }
        publish();
        if(session.finished())cleanup();else timer->setTimeoutMS(10);
    }
public:
    bool start(IOService* provider) override {
        int off=0;if(PE_parse_boot_argn("-tbishoff",&off,sizeof off))return false;
        pci=OSDynamicCast(IOPCIDevice,provider);
        if(!pci || pci->configRead16(0)!=0x8086 || pci->configRead16(2)!=0x7745 || (pci->configRead16(4)&6)!=6)return false;
        if(!IOService::start(provider))return false;
        if(!pci->open(this))return false;opened=true;
        mapping=pci->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0,kIOMapInhibitCache);
        if(!mapping || mapping->getLength()<0x400 || !mapping->getVirtualAddress()){releaseResources();return false;}
        base=reinterpret_cast<volatile uint8_t*>(mapping->getVirtualAddress());
        loop=IOWorkLoop::workLoop();timer=IOTimerEventSource::timerEventSource(this,fired);
        if(!loop || !timer || loop->addEventSource(timer)!=kIOReturnSuccess){releaseResources();return false;}
        timerAdded=true;
        powerNotifier=registerPrioritySleepWakeInterest(power,this);if(!powerNotifier){releaseResources();return false;}
        setProperty("ProbeVersion","0.5.3");setProperty("ProbeStatus","Scheduled asynchronous IPC/HBM enumeration");
        setProperty("ProbeMode","Continuous ALS GET_INPUT polling; DMA gate remains enabled while connected; no host DMA buffers or sensor configuration");
        setProperty("BAR0Length",mapping->getLength(),64);snapshot();
        if(timer->setTimeoutMS(1000)!=kIOReturnSuccess){releaseResources();return false;}
        registerService();return true;
    }
    static IOReturn quiesce(OSObject* object,void*,void*,void*,void*){
        auto self=static_cast<ThinkBookISHProbe*>(object);self->stopping=true;self->wakePending=false;
        if(self->timer)self->timer->cancelTimeout();
        if(self->base && self->changed && self->read(0x34)!=0xffffffffu){self->write(0x368,self->oldRemap);if(!(self->read(0x34)&0xf0000))self->write(0x38,self->oldHost);}
        return kIOReturnSuccess;
    }
    static IOReturn parkAction(OSObject* object,void*,void*,void*,void*){
        auto self=static_cast<ThinkBookISHProbe*>(object);quiesce(object,nullptr,nullptr,nullptr,nullptr);
        self->wakeAwaitingSample=false;self->parked=true;
        self->setProperty("ALSStreaming",false);self->setProperty("ALSSampleValid",false);self->setProperty("ProbeStatus","Paused for sleep/shutdown");return kIOReturnSuccess;
    }
    static IOReturn wakeAction(OSObject* object,void*,void*,void*,void*){
        auto self=static_cast<ThinkBookISHProbe*>(object);
        if(!self->parked || self->wakePending)return kIOReturnSuccess;
        self->wakePending=true;self->wakeChecks=0;self->stopping=false;
        self->setProperty("ProbeStatus","Wake quiescence check scheduled");self->timer->setTimeoutMS(5000);return kIOReturnSuccess;
    }
    static IOReturn power(void* target,void*,UInt32 type,IOService*,void*,vm_size_t){
        auto self=static_cast<ThinkBookISHProbe*>(target);
        if(type==kIOMessageSystemWillSleep || type==kIOMessageSystemWillPowerOff || type==kIOMessageSystemWillRestart)self->loop->runAction(parkAction,self);
        else if(type==kIOMessageSystemHasPoweredOn)self->loop->runAction(wakeAction,self);
        return kIOReturnSuccess; // Synchronous completion; no deferred power acknowledgement requested.
    }
    void releaseResources(){
        if(powerNotifier){powerNotifier->remove();powerNotifier=nullptr;}
        if(loop)loop->runAction(quiesce,this);else quiesce(this,nullptr,nullptr,nullptr,nullptr);
        if(loop && timer && timerAdded)loop->removeEventSource(timer);timerAdded=false;
        OSSafeReleaseNULL(timer);OSSafeReleaseNULL(loop);base=nullptr;OSSafeReleaseNULL(mapping);
        if(pci && opened)pci->close(this);opened=false;
    }
    void stop(IOService* provider) override {releaseResources();IOService::stop(provider);}
};
OSDefineMetaClassAndStructors(ThinkBookISHProbe,IOService)
