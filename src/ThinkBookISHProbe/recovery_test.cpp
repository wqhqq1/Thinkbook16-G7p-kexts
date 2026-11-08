#include "ISHSession.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using ish::Phase;
static uint64_t t;
static void drain(ish::Session&s){unsigned n=0;while(s.pending()){assert(++n<9);s.sent(t);}}
static void dispatch(ish::Session&s){uint8_t c[]={8,35,1,0,0,0,0,0};s.hbm(c,8,++t);s.tick(++t,3);assert(s.pending());s.sent(t);}
static void configure(ish::Session&s){
 assert(s.phase==Phase::Connect);drain(s);uint8_t c[]={0x86,35,1,0};s.hbm(c,4,++t);drain(s);dispatch(s);
 uint8_t e[]={0xa1,15,0,0,10,0,1,7,0,0,0,0,0xc2,0x0a,0x87,0x80};s.hid(e,sizeof e,++t);drain(s);dispatch(s);
 uint8_t h[]={0x80,7,0,0,3,0,1,2,3};s.hid(h,sizeof h,++t);drain(s);dispatch(s);
 uint8_t r[6+sizeof ish::ExpectedALSDescriptor]={0x81,7};r[4]=uint8_t(sizeof ish::ExpectedALSDescriptor);r[5]=uint8_t(sizeof ish::ExpectedALSDescriptor>>8);memcpy(r+6,ish::ExpectedALSDescriptor,sizeof ish::ExpectedALSDescriptor);s.hid(r,sizeof r,++t);drain(s);dispatch(s);
 uint8_t f[311]={0x82,7,0,0,0x31,1,1};s.hid(f,sizeof f,++t);drain(s);assert(s.phase==Phase::InputSamples);
}
static void init(ish::Session&s){t=0;s.begin(t);drain(s);uint8_t a[]={1,0,0,0};s.management(4,a,4,++t);s.tick(++t,3);drain(s);
 uint8_t v[]={0x81,13,0,1};s.hbm(v,4,++t);drain(s);uint8_t e[36]={0x84};e[8]=8;s.hbm(e,36,++t);drain(s);
 uint8_t p[32]={0x85,35};memcpy(p+4,ish::HIDGuid,16);p[21]=1;ish::put32(p+24,16384);s.hbm(p,32,++t);configure(s);}
static void sample(ish::Session&s,bool valid=true){dispatch(s);uint8_t p[37]={0x84,7,0,0,31,0,1,2};p[7]=valid?2:1;ish::put32(p+33,123456);s.hid(p,sizeof p,++t);assert(!s.finished());}
static void next(ish::Session&s){t+=501;s.tick(t,3);drain(s);}
static void timeout(ish::Session&s){t+=10001;s.tick(t,3);}
static void ackDisconnect(ish::Session&s){assert(s.phase==Phase::Disconnect && s.pending() && s.pending()->bytes[4]==7);drain(s);uint8_t d[]={0x87,35,1,0};s.hbm(d,4,++t);assert(s.phase==Phase::RetryDelay);t+=5000;s.tick(t,3);configure(s);}
int main(){
 {ish::Session s;init(s);sample(s);for(unsigned i=0;i<2000;++i){next(s);sample(s);}assert(s.sampleSequence==2001&&s.validInputCount==2001&&s.inputCount==10&&!s.recoveryCount);}
 {ish::Session s;init(s);sample(s);auto old=s.sampleSequence;
  // Host timer arrives 13 seconds late while idle: queue a fresh request, not a timeout.
  t+=13000;s.tick(t,3);assert(s.phase==Phase::InputSamples&&s.waitingForCredit());drain(s);sample(s);assert(s.sampleSequence==old+1&&!s.recoveryCount);
  // The request sits queued for 9 seconds: response timeout starts at physical send.
  next(s);uint8_t c[]={8,35,1};s.hbm(c,8,++t);s.tick(++t,3);assert(s.pending()&&!s.inputRequestDispatched());t+=9000;s.sent(t);assert(s.inputRequestDispatched());t+=9999;s.tick(t,3);assert(s.phase==Phase::InputSamples);
  ++t;s.tick(t,3);assert(s.phase==Phase::Disconnect&&s.recoveryCount==1&&!s.latestValid&&s.recoveringInput());
  uint8_t late[37]={0x84,7,0,0,31,0,1,2};s.hid(late,37,++t);assert(s.lateInputReplies==1&&s.sampleSequence==old+1);
  ackDisconnect(s);sample(s);assert(s.recoverySuccesses==1&&!s.recoveringInput()&&s.sampleSequence==old+2&&s.latestValid);
 }
 {ish::Session s;init(s);sample(s);next(s);dispatch(s);t+=10001;s.tick(t,3,true);assert(s.phase==Phase::InputSamples&&!s.recoveryCount);
  uint8_t p[37]={0x84,7,0,0,31,0,1,2};s.hid(p,37,++t);assert(!s.finished()&&s.sampleSequence==2);}
 {ish::Session s;init(s);sample(s);next(s);dispatch(s);t+=10001;for(unsigned i=0;i<9;++i)s.tick(++t,3,true);assert(s.phase==Phase::Disconnect&&s.recoveryCount==1);}
 {ish::Session s;init(s);sample(s);
  // Three consecutive reconnects without a valid new read, then stop rather than loop forever.
  for(unsigned i=0;i<3;++i){if(i==0)next(s);dispatch(s);timeout(s);assert(s.recoveryCount==i+1);ackDisconnect(s);}
  dispatch(s);timeout(s);assert(s.finished()&&!strcmp(s.error,"ALS recovery limit reached"));}
 {ish::Session s;init(s);sample(s);next(s);dispatch(s);timeout(s);drain(s);t+=5001;s.tick(t,3);assert(s.finished());}
 {ish::Session s;init(s);sample(s);next(s);dispatch(s);timeout(s);drain(s);uint8_t d[]={0x87,35,1,1};s.hbm(d,4,++t);assert(s.finished()&&!strcmp(s.error,"HID disconnect rejected"));}
 {ish::Session s;init(s);dispatch(s);timeout(s);assert(s.finished()&&!s.recoveryCount);}
 {ish::Session s;init(s);sample(s);next(s);timeout(s);assert(s.phase==Phase::Disconnect&&!strcmp(s.lastRecoveryError,"HID transmit credit timeout"));}
 {ish::Session s;init(s);sample(s);s.tick(++t,0xffffffff);assert(s.finished()&&!s.recoveryCount);}
 {ish::Session s;init(s);sample(s);next(s);dispatch(s);timeout(s);s.reset();assert(s.phase==Phase::Idle&&!s.recoveryCount&&!s.lastInputDispatchMS&&!s.pending());}
 puts("PASS: 2001 real samples, idle host gap, dispatch-based deadline, pending mailbox grace and bound, real timeout/disconnect/reconnect/revalidated sample, late reply, credit timeout, 3-attempt cap, rejected/missing ACK, MMIO failure and reset");
}
