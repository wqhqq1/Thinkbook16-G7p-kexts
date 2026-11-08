#include "GoodixPressureState.hpp"
#include <assert.h>
#include <stdio.h>
int main(int argc, char** argv) {
    GoodixPressureState s;
    s.update(1000000000, 200, true, 1, 0, true); assert(!s.heavy);
    s.update(1010000000, 450, true, 1, 0, true); assert(!s.heavy);
    s.update(1029000000, 450, true, 1, 0, true); assert(!s.heavy);
    s.update(1030000000, 450, true, 1, 0, true); assert(s.heavy);
    s.update(1040000000, 380, true, 1, 0, true); assert(s.heavy);
    s.update(1050000000, 340, true, 1, 0, true); assert(!s.heavy);
    s.update(1060000000, 900, true, 1, 0, true);
    s.update(1090000000, 900, true, 1, 0, true); assert(s.heavy);
    s.update(1100000000, 900, true, 2, 0, true); assert(!s.heavy);
    s.update(1110000000, 900, true, 1, 0, true);
    s.update(1140000000, 900, true, 1, 1, true); assert(!s.heavy);
    s.update(1170000000, 900, true, 1, 1, true); assert(s.heavy);
    s.update(1180000000, 900, true, 1, 1, false); assert(!s.heavy);
    s.update(1190000000, 900, false, 1, 1, true); assert(!s.heavy);
    s.update(1200000000, 65535, true, 1, 1, true); assert(!s.heavy);
    if (argc > 1) {
        FILE* f = fopen(argv[1], "r"); assert(f);
        unsigned long long ns; unsigned raw, button, contacts, phase;
        unsigned normal = 0, normalHeavy = 0, hard = 0, hardHeavy = 0;
        s = GoodixPressureState{};
        while (fscanf(f, "%llu,%u,%u,%u,%u", &ns, &raw, &button, &contacts, &phase)==5) {
            s.update(ns,raw,button,contacts,0,true);
            if (phase==1 && button) {++normal;normalHeavy+=s.heavy;}
            if (phase==2 && button && raw>=s.down) {++hard;hardHeavy+=s.heavy;}
        }
        fclose(f);
        printf("Replay: normal=%u heavy_misfires=%u; above-threshold heavy=%u detected=%u\n",normal,normalHeavy,hard,hardHeavy);
        assert(normal > 100 && normalHeavy==0 && hard>100 && hardHeavy*100/hard>=95);
    }
    puts("Pressure state tests passed");
}
