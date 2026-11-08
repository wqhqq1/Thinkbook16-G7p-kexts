from pathlib import Path
import subprocess,tempfile
w=Path(__file__).resolve().parent;s=(w/'ThinkBookISHProbe.cpp').read_text()
a=s.index('    bool resumeAfterWake(){');b=s.index('    void poll(){',a);resume=s[a:b]
a=s.index('    static IOReturn quiesce(');b=s.index('    void releaseResources()',a);actions=s[a:b]
pre=r'''
#include "ISHSession.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
using IOReturn=int;using UInt32=uint32_t;using vm_size_t=unsigned long;
constexpr int kIOReturnSuccess=0,kIOMessageSystemWillSleep=1,kIOMessageSystemWillPowerOff=2,kIOMessageSystemWillRestart=3,kIOMessageSystemHasPoweredOn=4;
struct OSObject{};struct IOService{};
struct Timer{unsigned interval=0,arms=0,cancels=0;void setTimeoutMS(unsigned n){interval=n;++arms;}void cancelTimeout(){++cancels;}};
struct Loop{void runAction(int(*fn)(OSObject*,void*,void*,void*,void*),OSObject*o){assert(!fn(o,nullptr,nullptr,nullptr,nullptr));}};
struct ThinkBookISHProbe:OSObject{
 ish::Session session;ish::Reassembly assembly,hidAssembly;ish::Phase published=ish::Phase::InputSamples;
 bool stopping=false,wakePending=false,wakeAwaitingSample=false,parked=false,changed=true,cleaning=false,resuming=false;
 unsigned wakeChecks=0,wakeResumes=0,assemblyEpoch=0,rxCount=1,txCount=2,foreignCount=3,traceCount=4;uint64_t publishedSample=77;
 uint32_t oldHost=0,oldRemap=0;int dummy=1;void *base=&dummy;Timer timerObject;Timer* timer=&timerObject;Loop loopObject;Loop*loop=&loopObject;
 std::map<unsigned,uint32_t> regs{{0x34,0x10f83b},{0x38,0x80},{0x368,1}};std::vector<unsigned> writes;
 std::map<std::string,unsigned long long> numbers;std::map<std::string,std::string> strings;
 uint32_t read(unsigned n){return regs[n];}void write(unsigned n,uint32_t v){writes.push_back(n);regs[n]=v;}
 void setProperty(const char*k,bool v){numbers[k]=v;}void setProperty(const char*k,unsigned long long v,unsigned){numbers[k]=v;}void setProperty(const char*k,const char*v){strings[k]=v;}
'''
main=r'''
};
int main(){
 for(unsigned mode=0;mode<3;++mode){ThinkBookISHProbe p;auto empty=p.timer->arms;p.parked=false;p.changed=false;
  ThinkBookISHProbe::wakeAction(&p,nullptr,nullptr,nullptr,nullptr);assert(p.timer->arms==empty&&!p.wakePending);
  p.changed=true;p.regs[0x34]=mode==0?0x14000:mode==1?0x104000:0xffffffffu;
  // Sleep always invalidates values immediately; host control is restored only when DMA is quiet.
  ThinkBookISHProbe::power(&p,nullptr,kIOMessageSystemWillSleep,nullptr,nullptr,0);
  assert(p.stopping&&p.parked&&!p.numbers["ALSStreaming"]&&!p.numbers["ALSSampleValid"]&&p.timer->cancels==1);
  if(mode==2)assert(p.writes.empty());else if(mode==0)assert(p.writes.size()==1&&p.writes[0]==0x368);else assert(p.writes.size()==2&&p.writes[1]==0x38);
 }
 {ThinkBookISHProbe p;p.regs[0x34]=0x14000;ThinkBookISHProbe::parkAction(&p,nullptr,nullptr,nullptr,nullptr);
  ThinkBookISHProbe::wakeAction(&p,nullptr,nullptr,nullptr,nullptr);auto arms=p.timer->arms;
  ThinkBookISHProbe::wakeAction(&p,nullptr,nullptr,nullptr,nullptr);assert(p.timer->arms==arms&&p.wakePending&&!p.stopping&&p.timer->interval==5000);
  const auto writes=p.writes.size();for(unsigned i=0;i<4;++i)assert(!p.resumeAfterWake()&&p.writes.size()==writes&&p.timer->interval==500);
  p.regs[0x34]=0x104000;assert(p.resumeAfterWake()&&p.resuming&&!p.parked&&!p.wakePending&&p.wakeAwaitingSample);
  assert(p.session.phase==ish::Phase::Idle&&p.publishedSample==0&&p.wakeResumes==1&&p.assemblyEpoch==0&&p.rxCount==0);
  assert(!p.numbers["ALSSampleValid"]&&p.numbers["ALSWakeResumeAttempts"]==1);
 }
 {ThinkBookISHProbe p;p.parked=true;p.wakePending=true;p.regs[0x34]=0x14000;auto n=p.writes.size();
  for(unsigned i=0;i<10;++i)assert(!p.resumeAfterWake());
  assert(p.stopping&&!p.wakePending&&p.writes.size()==n&&p.wakeResumes==0);}
 {ThinkBookISHProbe p;p.parked=true;p.wakePending=true;p.regs[0x34]=0xffffffff;assert(!p.resumeAfterWake()&&p.stopping&&p.writes.empty());}
 {ThinkBookISHProbe p;p.parked=true;p.wakePending=true;ThinkBookISHProbe::quiesce(&p,nullptr,nullptr,nullptr,nullptr);assert(!p.wakePending&&p.stopping);}
 puts("PASS: actual sleep/wake methods, invalidation, no unpaired/duplicate resume, delayed DMA quiescence, bounded failure, absent MMIO without writes and teardown cancellation");
}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'wake.cpp').write_text(pre+resume+actions+main)
 subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I',str(w),str(p/'wake.cpp'),'-o',str(p/'wake')],check=True)
 subprocess.run([str(p/'wake')],check=True)
assert 'if(assemblyEpoch!=session.transportEpoch){assembly.reset();hidAssembly.reset();assemblyEpoch=session.transportEpoch;}' in s
assert 'session.sent(ms)' in s
assert 'wakeAwaitingSample && session.latestValid && !session.recoveringInput()' in s
