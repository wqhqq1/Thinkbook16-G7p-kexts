// Test the OhMyDSP IOKit transport without storing microphone audio.
#include <IOKit/IOKitLib.h>
#include <mach/mach_time.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "include/OhMyDSPShared.h"

struct cursor { uint32_t state, epoch, position; uint64_t laps, faults; };
static bool snapshot(const volatile struct OhMyDSPStatus *s, struct cursor *out) {
    for (unsigned i = 0; i < 8; ++i) {
        uint32_t seq = __atomic_load_n(&s->seq, __ATOMIC_ACQUIRE);
        if (seq & 1) continue;
        *out = (struct cursor){s->state, s->epoch, s->lpib, s->laps, s->faults};
        if (seq == __atomic_load_n(&s->seq, __ATOMIC_ACQUIRE)) return true;
    }
    return false;
}
static double seconds(uint64_t ticks, mach_timebase_info_data_t tb) {
    return (double)ticks * tb.numer / tb.denom * 1e-9;
}
int main(int argc, char **argv) {
    bool probe = argc == 2 && !strcmp(argv[1], "--probe");
    double duration = argc == 2 && !probe ? strtod(argv[1], NULL) : 5;
    if (argc > 2 || !isfinite(duration) || duration < 1 || duration > 10) {
        fprintf(stderr, "Usage: ace-test [--probe|seconds 1..10]\n"); return 2;
    }
    io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching(OHMYDSP_SERVICE_CLASS));
    if (!svc) { fprintf(stderr, "OhMyDSP ACE service is not present\n"); return 1; }
    io_connect_t c = 0;
    kern_return_t kr = IOServiceOpen(svc, mach_task_self(), 0, &c); IOObjectRelease(svc);
    if (kr) { fprintf(stderr, "IOServiceOpen: 0x%x (root and exclusive access required)\n", kr); return 1; }
    mach_vm_address_t ra = 0, sa = 0; mach_vm_size_t rs = 0, ss = 0;
    int result = 1;
    if (IOConnectMapMemory64(c, OHMYDSP_MEM_RING, mach_task_self(), &ra, &rs, kIOMapAnywhere | kIOMapReadOnly) ||
        IOConnectMapMemory64(c, OHMYDSP_MEM_STATUS, mach_task_self(), &sa, &ss, kIOMapAnywhere | kIOMapReadOnly)) {
        fprintf(stderr, "Read-only mapping failed\n"); goto done;
    }
    const volatile struct OhMyDSPStatus *st = (const volatile struct OhMyDSPStatus *)sa;
    const volatile int32_t *ring = (const volatile int32_t *)ra;
    if (ss < sizeof(*st) || st->magic != OHMYDSP_STATUS_MAGIC || st->version != OHMYDSP_STATUS_VERSION ||
        st->frameBytes != 8 || st->channels != 2 || st->rate != 48000 || st->ringBytes != 24576 || rs < st->ringBytes) {
        fprintf(stderr, "Unexpected ACE status or ring geometry\n"); goto done;
    }
    printf("ABI %u, %u Hz, %u ch, %u ring bytes; state %u\n", st->version, st->rate, st->channels, st->ringBytes, st->state);
    if (probe) { result = 0; goto done; }
    uint64_t oldFaults = st->faults;
    kr = IOConnectCallScalarMethod(c, OHMYDSP_SEL_START, NULL, 0, NULL, NULL);
    printf("start 0x%x\n", kr); fflush(stdout);
    if (kr) goto done;
    mach_timebase_info_data_t tb; mach_timebase_info(&tb);
    uint64_t t0 = mach_absolute_time(), frame = 0, n = 0, skipped = 0;
    const uint32_t rf = st->ringBytes / st->frameBytes;
    uint32_t epoch = st->epoch, peak[2] = {};
    double squares[2] = {}, q1[3] = {}, q2[3] = {}, coef[3];
    for (unsigned i = 0; i < 3; ++i) coef[i] = 2 * cos(2 * M_PI * (640 + 20*i) / 48000.0);
    while (seconds(mach_absolute_time() - t0, tb) < duration) {
        usleep(4000);
        struct cursor p;
        if (!snapshot(st, &p)) continue;
        if (p.state != OHMYDSP_STATE_RUNNING || p.position >= st->ringBytes || p.position % st->frameBytes) break;
        uint64_t end = p.laps * rf + p.position / st->frameBytes;
        if (p.epoch != epoch || end < frame || end - frame >= rf) { ++skipped; epoch = p.epoch; frame = end; continue; }
        for (; frame < end; ++frame, ++n) {
            uint32_t ix = (uint32_t)(frame % rf) * 2;
            double mix = 0;
            for (unsigned ch = 0; ch < 2; ++ch) {
                int64_t raw = ring[ix + ch]; uint32_t magnitude = (uint32_t)(raw < 0 ? -raw : raw);
                if (magnitude > peak[ch]) peak[ch] = magnitude;
                double sample = raw / 2147483648.0;
                squares[ch] += sample * sample; mix += sample * 0.5;
            }
            for (unsigned i = 0; i < 3; ++i) { double q0 = mix + coef[i] * q1[i] - q2[i]; q2[i] = q1[i]; q1[i] = q0; }
        }
    }
    const uint32_t state = st->state;
    const uint64_t faults = st->faults - oldFaults;
    kr = IOConnectCallScalarMethod(c, OHMYDSP_SEL_STOP, NULL, 0, NULL, NULL);
    printf("stop 0x%x; %llu frames, %llu gaps, %llu new faults\n", kr, n, skipped, faults);
    printf("peak L/R %u/%u, RMS L/R %.2f/%.2f dBFS\n", peak[0], peak[1],
           10*log10(squares[0]/(n ? n : 1) + 1e-24), 10*log10(squares[1]/(n ? n : 1) + 1e-24));
    double bands[3];
    for (unsigned i = 0; i < 3; ++i) bands[i] = fmax(0, q1[i]*q1[i] + q2[i]*q2[i] - coef[i]*q1[i]*q2[i]);
    printf("660 Hz relative to 640/680 Hz: %.1f dB\n", 10*log10((bands[1]+1e-24)/(fmax(bands[0],bands[2])+1e-24)));
    result = !kr && state == OHMYDSP_STATE_RUNNING && !faults && n >= duration * 48000 * 0.85 && (peak[0] || peak[1]) ? 0 : 1;
done:
    if (sa) IOConnectUnmapMemory64(c, OHMYDSP_MEM_STATUS, mach_task_self(), sa);
    if (ra) IOConnectUnmapMemory64(c, OHMYDSP_MEM_RING, mach_task_self(), ra);
    IOServiceClose(c);
    return result;
}
