// SPDX-License-Identifier: GPL-2.0-only
// Bounded IPC/HBM enumeration state machine; no hardware access.
#pragma once
#include "ISHWire.hpp"
#include "ALSLayout.hpp"
namespace ish {
enum class Phase {Idle, Reset, Ready, Start, Enumerate, Properties, Connect, HIDEnum, HIDDescriptor, ReportDescriptor, Feature, InputSamples, Disconnect, Done, Failed, RetryDelay};
struct Tx {uint8_t protocol=0, command=0; uint16_t length=0; uint8_t bytes[128] {};};
struct Client {uint8_t address=0, version=0, connections=0, fixed=0, single=0, dmaHeader=0; uint32_t maxMessage=0; uint8_t guid[16] {};};
class Session {
    Tx queue[8] {}; unsigned head=0, count=0;
    uint64_t deadline=0, totalDeadline=0;
    uint8_t bitmap[32] {}; unsigned address=0;
    void transition(Phase p,uint64_t now,uint64_t timeout) {if(phase!=Phase::Failed){phase=p;deadline=now+timeout;}}
    void queueTx(uint8_t proto,uint8_t cmd,const uint8_t* data,size_t n) {
        if(count==8 || n>128) {fail("Transmit queue overflow");return;}
        Tx& t=queue[(head+count)%8];t.protocol=proto;t.command=cmd;t.length=uint16_t(n);
        for(size_t i=0;i<n;i++)t.bytes[i]=data[i];++count;
    }
    void sendHBM(const uint8_t* p,size_t n) {
        uint8_t b[128] {};size_t size=encodeHBM(p,n,b,sizeof b);
        if(!size){fail("Invalid outbound HBM");return;}queueTx(1,0,b,size);
    }
    uint64_t nextInput=0;
    uint8_t credits=0,requestCommand=0;bool requestWaiting=false,requestSent=false;
    bool requestDispatched=false,recovering=false,disconnectDispatched=false;
    unsigned consecutiveRecoveries=0;
    unsigned overdueDrains=0;
    void recoverInput(uint64_t now,const char* reason){
        if(consecutiveRecoveries>=3){fail("ALS recovery limit reached");return;}
        ++consecutiveRecoveries;++recoveryCount;lastRecoveryError=reason;latestValid=false;
        recovering=true;requestWaiting=requestSent=requestDispatched=disconnectDispatched=false;credits=0;head=count=0;overdueDrains=0;++transportEpoch;
        // Only restart the HID client after a matching successful disconnect acknowledgement.
        const uint8_t p[]={7,hidAddress,1,0};sendHBM(p,4);transition(Phase::Disconnect,now,5000);
    }
    void startHID(uint64_t now){
        if(!hidAddress){fail("HID client not found");return;}
        const uint8_t p[]={6,hidAddress,1,0};sendHBM(p,4);transition(Phase::Connect,now,5000);
    }
    void request(uint8_t cmd,uint8_t id,Phase next,uint64_t now){
        requestCommand=cmd;requestDevice=id;requestWaiting=true;requestSent=requestDispatched=false;overdueDrains=0;
        const uint8_t grant[]={8,hidAddress,1,0,0,0,0,0};sendHBM(grant,8);transition(next,now,10000);
    }
    void nextProperty(uint64_t now) {
        do {++address;} while(address<256 && !(bitmap[address/8]&(1u<<(address%8))));
        if(address==256){startHID(now);return;}
        const uint8_t p[]={5,uint8_t(address),0,0};sendHBM(p,4);transition(Phase::Properties,now,2000);
    }
public:
    struct Device {uint32_t id=0;uint8_t cls=0;uint16_t pid=0,vid=0;};
    Device devices[32] {};unsigned deviceCount=0;
    uint8_t hidAddress=0,requestDevice=0,alsDevice=0;
    uint8_t hidDescriptor[256] {},reportDescriptor[16384] {};
    size_t hidDescriptorLength=0,reportDescriptorLength=0;
    uint8_t featureReport[305] {},inputReports[10][31] {};
    bool featureReceived=false;
    unsigned inputCount=0,validInputCount=0;uint32_t rawMilliLux=0;
    uint64_t sampleSequence=0;bool latestValid=false;
    Phase phase=Phase::Idle;const char* error="";
    unsigned recoveryCount=0,recoverySuccesses=0,lateInputReplies=0;
    unsigned transportEpoch=0;
    uint64_t lastInputDispatchMS=0,lastInputReplyMS=0;
    const char* lastRecoveryError="";
    Client clients[255] {};unsigned clientCount=0;uint8_t startFlags=0;
    void reset(){head=count=0;deadline=totalDeadline=nextInput=0;lastInputDispatchMS=lastInputReplyMS=0;address=0;credits=requestCommand=0;requestWaiting=requestSent=requestDispatched=recovering=disconnectDispatched=false;overdueDrains=transportEpoch=0;consecutiveRecoveries=recoveryCount=recoverySuccesses=lateInputReplies=0;lastRecoveryError="";deviceCount=0;hidAddress=requestDevice=alsDevice=0;hidDescriptorLength=reportDescriptorLength=0;featureReceived=false;inputCount=validInputCount=0;rawMilliLux=0;sampleSequence=0;latestValid=false;phase=Phase::Idle;error="";clientCount=0;startFlags=0;}
    bool recoveringInput()const{return recovering;}
    unsigned txCredits() const {return credits;}
    bool waitingForCredit() const {return requestWaiting;}
    bool inputRequestSent() const {return requestSent;}
    bool inputRequestDispatched() const {return requestDispatched && requestSent;}
    unsigned queueDepth() const {return count;}
    bool finished() const {return phase==Phase::Done || phase==Phase::Failed;}
    void fail(const char* reason){phase=Phase::Failed;error=reason;count=0;}
    void begin(uint64_t now){
        if(phase!=Phase::Idle){fail("Session already started");return;}
        totalDeadline=now+30000;transition(Phase::Reset,now,10000);
        const uint8_t p[]={1,0,0,0};queueTx(3,3,p,4);
    }
    const Tx* pending() const {return count ? &queue[head] : nullptr;}
    void sent(uint64_t now){if(count){
        const Tx& t=queue[head];
        if(t.protocol==1 && t.length>=10 && t.bytes[0]==hidAddress && t.bytes[1]==1 && t.bytes[4]==requestCommand && requestSent){requestDispatched=true;deadline=now+10000;if(requestCommand==4)lastInputDispatchMS=now;}
        if(t.protocol==1 && t.length==8 && t.bytes[0]==0 && t.bytes[1]==0 && t.bytes[4]==7 && phase==Phase::Disconnect)disconnectDispatched=true;
        head=(head+1)%8;--count;
    }}
    void tick(uint64_t now,uint32_t firmware,bool inboundPending=false){
        if(phase==Phase::Idle || finished())return;
        if(firmware==0xffffffffu){fail("PCI/MMIO unavailable");return;}
        if(phase==Phase::RetryDelay){if(now>=deadline)startHID(now);return;}
        // A response deadline applies only while a request/credit is outstanding.
        // A late host timer during the inter-sample idle period is not a firmware timeout.
        bool awaitingResponse=phase!=Phase::InputSamples || requestWaiting || requestSent;
        if((!validInputCount && now>=totalDeadline) || (awaitingResponse && now>=deadline)){
            // Drain an already pending mailbox before declaring its response missing.
            if(inboundPending && overdueDrains++<8)return;
            const char* reason=phase==Phase::InputSamples && requestWaiting && !credits ? "HID transmit credit timeout" : phase==Phase::InputSamples && requestSent && requestDispatched ? "HID input response timeout" : "Firmware response timeout";
            if(phase==Phase::InputSamples && validInputCount)recoverInput(now,reason);else fail(reason);
            return;
        }
        if(phase==Phase::InputSamples && !requestWaiting && !requestSent && now>=nextInput)request(4,alsDevice,Phase::InputSamples,now);
        if(requestWaiting && credits){
            uint8_t p[11] {};bool report=requestCommand==2 || requestCommand==4;put32(p,(report?0x80070000u:0x80060000u) | uint32_t(1)<<8 | hidAddress);
            p[4]=requestCommand;p[5]=requestDevice;p[10]=1;queueTx(1,0,p,report?11:10);
            --credits;requestWaiting=false;requestSent=true;
        }
        if(phase==Phase::Ready && (firmware&3)==3){
            const uint8_t p[]={1,0,0,1};sendHBM(p,4);transition(Phase::Start,now,7000);
        }
    }
    void management(uint8_t cmd,const uint8_t* p,size_t n,uint64_t now){
        if(finished())return;
        if(cmd==2)return; // RX completion; actual transmit availability is checked by adapter.
        if(cmd!=3 && cmd!=4){fail("Unexpected IPC management command");return;}
        if(phase!=Phase::Reset || !p || n!=4){fail("Unexpected or malformed firmware reset");return;}
        if(cmd==4 && (p[0]!=1 || p[1]!=0)){fail("Reset ACK id mismatch");return;}
        if(cmd==3)queueTx(3,4,p,4);
        transition(Phase::Ready,now,2000);
    }
    void hbm(const uint8_t* p,size_t n,uint64_t now){
        if(finished())return;
        if(!p || !n){fail("Empty HBM response");return;}
        if(p[0]==8){
            if(n!=8 || p[1]!=hidAddress || p[2]!=1 || phase<Phase::Connect){fail("Invalid flow-control grant");return;}
            if(recovering && (phase==Phase::Disconnect || phase==Phase::RetryDelay))return;
            if(credits>=8){fail("Invalid flow-control grant");return;}
            ++credits;return;
        }
        if(phase==Phase::Connect && p[0]==0x86){
            if(n!=4 || p[1]!=hidAddress || p[2]!=1 || p[3]){fail("HID connect rejected");return;}
            request(33,0,Phase::HIDEnum,now);return;
        }
        if(phase==Phase::Disconnect && p[0]==0x87){
            if(n!=4 || p[1]!=hidAddress || p[2]!=1 || p[3] || !disconnectDispatched){fail("HID disconnect rejected");return;}
            credits=0;++transportEpoch;
            if(recovering)transition(Phase::RetryDelay,now,1000u<< (consecutiveRecoveries-1));
            else phase=Phase::Done;
            return;
        }
        if(phase==Phase::Start && p[0]==0x81){
            if(n!=4 || !p[1]){fail("HBM version rejected or malformed");return;}
            // Upper bits can advertise firmware-loader capability; retain for analysis.
            startFlags=p[1];
            const uint8_t msg[]={4,0,0,0};sendHBM(msg,4);transition(Phase::Enumerate,now,2000);return;
        }
        if(phase==Phase::Enumerate && p[0]==0x84){
            if(n!=36 || (p[4]&1)){fail("Invalid client bitmap");return;}
            for(unsigned i=0;i<32;i++)bitmap[i]=p[4+i];address=0;nextProperty(now);return;
        }
        if(phase==Phase::Properties && p[0]==0x85){
            if(n!=32 || p[1]!=address || p[2] || clientCount>=255){fail("Client properties invalid");return;}
            Client& c=clients[clientCount++];c.address=p[1];
            for(unsigned i=0;i<16;i++)c.guid[i]=p[4+i];
            c.version=p[20];c.connections=p[21];c.fixed=p[22];c.single=p[23];
            c.maxMessage=le32(p+24);c.dmaHeader=p[28];
            bool isHID=true;for(unsigned i=0;i<16;i++)if(c.guid[i]!=HIDGuid[i])isHID=false;
            if(isHID){if(hidAddress){fail("Ambiguous HID clients");return;}hidAddress=c.address;}
            if(!c.maxMessage || c.maxMessage>16*1024*1024){fail("Invalid client message bound");return;}
            nextProperty(now);return;
        }
        fail("Unexpected HBM command or ordering");
    }
    void hid(const uint8_t* p,size_t n,uint64_t now){
        if(finished())return;
        if(recovering && (phase==Phase::Disconnect || phase==Phase::RetryDelay || phase==Phase::Connect) &&
            p && n==37 && p[0]==0x84 && p[1]==alsDevice && !p[2] && p[4]==31 && !p[5]){++lateInputReplies;return;}
        if(!p || n<6 || !requestSent || !requestDispatched || !(p[0]&0x80) || (p[0]&0x7f)!=requestCommand || (phase!=Phase::HIDEnum && p[1]!=requestDevice) || p[2] || (size_t(p[4])|(size_t(p[5])<<8))!=n-6){fail("Invalid HOSTIF response");return;}
        const uint8_t* data=p+6;size_t len=n-6;requestSent=false;
        if(phase==Phase::HIDEnum){
            if(!len || data[0]>32 || len!=1+size_t(data[0])*9){fail("Invalid HID device list");return;}
            deviceCount=data[0];unsigned found=0;
            for(unsigned i=0;i<deviceCount;i++){
                const uint8_t* d=data+1+9*i;auto& dev=devices[i];dev.id=le32(d);dev.cls=d[4];dev.pid=uint16_t(unsigned(d[5])|(unsigned(d[6])<<8));dev.vid=uint16_t(unsigned(d[7])|(unsigned(d[8])<<8));
                if(dev.vid==0x8087 && dev.pid==0x0ac2){++found;alsDevice=uint8_t(dev.id);}
            }
            if(found!=1){fail("Expected Windows ALS device 8087:0AC2 not uniquely found");return;}
            request(0,alsDevice,Phase::HIDDescriptor,now);return;
        }
        if(phase==Phase::HIDDescriptor){
            if(!len || len>sizeof hidDescriptor){fail("HID descriptor exceeds bound");return;}
            for(size_t i=0;i<len;i++)hidDescriptor[i]=data[i];hidDescriptorLength=len;
            request(1,alsDevice,Phase::ReportDescriptor,now);return;
        }
        if(phase==Phase::ReportDescriptor){
            if(!len || len>sizeof reportDescriptor){fail("Report descriptor exceeds bound");return;}
            for(size_t i=0;i<len;i++)reportDescriptor[i]=data[i];reportDescriptorLength=len;
            if(len!=sizeof ExpectedALSDescriptor){fail("ALS descriptor layout changed");return;}
            for(size_t i=0;i<len;i++)if(data[i]!=ExpectedALSDescriptor[i]){fail("ALS descriptor layout changed");return;}
            request(2,alsDevice,Phase::Feature,now);return;
        }
        if(phase==Phase::Feature){
            if(len!=305 || data[0]!=1){fail("Feature report length or id mismatch");return;}
            for(size_t i=0;i<len;i++)featureReport[i]=data[i];featureReceived=true;
            request(4,alsDevice,Phase::InputSamples,now);return;
        }
        if(phase==Phase::InputSamples){
            if(len!=31 || data[0]!=1){fail("Input report length or id mismatch");return;}
            lastInputReplyMS=now;
            for(size_t i=0;i<len;i++)inputReports[sampleSequence%10][i]=data[i];++sampleSequence;if(inputCount<10)++inputCount;
            uint32_t v=le32(data+27);latestValid=data[1]==2 && v!=0xffffffffu;if(latestValid){if(validInputCount<0xffffffffu)++validInputCount;rawMilliLux=v;if(recovering){++recoverySuccesses;recovering=false;}consecutiveRecoveries=0;}
            if(!validInputCount && sampleSequence>=10){fail("No ready sensor samples");return;}
            nextInput=now+500;deadline=nextInput+10000;return;
        }
        fail("Unexpected HOSTIF phase");
    }

};
}
