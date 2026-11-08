#include "VoodooI2C Satellites/VoodooI2CHID/VoodooI2CHID/GoodixReportGate.hpp"
#include <assert.h>
#include <stdio.h>
unsigned hexNibble(char c) { return c<='9' ? c-'0' : c-'a'+10; }
int main(int argc,char**argv) {
    GoodixReportGate gate;
    uint8_t p[40]={4}; p[1]=3; p[38]=1;
    assert(gate.accept(p,40,1000000000));
    assert(!gate.accept(p,40,1001400000));
    p[2]=1; assert(gate.accept(p,40,1002000000)); // one coordinate unit must survive
    p[36]=1; assert(gate.accept(p,40,1003000000)); // identical coordinates, new scan must survive
    p[39]=1; assert(gate.accept(p,40,1004000000)); // button transition must survive
    p[6]=1; assert(gate.accept(p,40,1005000000)); // pressure transition must survive
    assert(!gate.accept(p,40,1006000000));
    assert(gate.accept(p,40,1026000001)); // same packet after a long gap is allowed
    gate.reset();assert(gate.accept(p,40,1027000000));
    assert(gate.accept(p,39,1028000000)); // unrelated report formats pass through
    p[0]=14;assert(gate.accept(p,40,1029000000));
    if(argc>1) {
        FILE*f=fopen(argv[1],"r");assert(f);
        unsigned long long ns; char bytes[81]; uint8_t current[40];
        gate=GoodixReportGate{}; unsigned total=0,accepted=0,changed=0,changedLost=0;
        uint8_t previous[40]={}; bool havePrevious=false;
        while(fscanf(f,"%llu %80s",&ns,bytes)==2) {
            bool different=!havePrevious;
            for(unsigned i=0;i<40;++i) {
                current[i]=(hexNibble(bytes[2*i])<<4)|hexNibble(bytes[2*i+1]);
                different|=current[i]!=previous[i];
            }
            bool keep=gate.accept(current,40,ns);++total;accepted+=keep;
            if(different){++changed;changedLost+=!keep;}
            for(unsigned i=0;i<40;++i)previous[i]=current[i];havePrevious=true;
        }
        fclose(f);
        printf("Replay: total=%u accepted=%u dropped=%u changed=%u changed_lost=%u\n",total,accepted,gate.dropped,changed,changedLost);
        assert(total==4204 && changedLost==0 && gate.dropped>=2000 && accepted>=changed);
    }
    puts("Report gate tests passed");
}
