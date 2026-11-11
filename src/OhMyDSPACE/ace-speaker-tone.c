#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { rate = 44100, blockFrames = 4410 };
struct tone { unsigned total, queued, pending; int failed; };

static CFStringRef internalOutput(AudioDeviceID *deviceID) {
    AudioObjectPropertyAddress a = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, NULL, &size) || !size) return NULL;
    AudioDeviceID *ids = malloc(size);
    if (!ids) return NULL;
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &size, ids)) { free(ids); return NULL; }
    CFStringRef result = NULL;
    for (unsigned i = 0; i < size / sizeof(*ids); i++) {
        UInt32 transport = 0, len = sizeof transport;
        a.mSelector = kAudioDevicePropertyTransportType; a.mScope = kAudioObjectPropertyScopeGlobal;
        if (AudioObjectGetPropertyData(ids[i], &a, 0, NULL, &len, &transport) || transport != kAudioDeviceTransportTypeBuiltIn) continue;
        a.mSelector = kAudioDevicePropertyStreams; a.mScope = kAudioObjectPropertyScopeOutput;
        if (AudioObjectGetPropertyDataSize(ids[i], &a, 0, NULL, &len) || !len) continue;
        a.mSelector = kAudioDevicePropertyDeviceUID; a.mScope = kAudioObjectPropertyScopeGlobal; len = sizeof result;
        if (!AudioObjectGetPropertyData(ids[i], &a, 0, NULL, &len, &result)) {
            if (deviceID) *deviceID = ids[i];
            break;
        }
    }
    free(ids);
    return result;
}

static int nominalRate(const char *value) {
    AudioDeviceID deviceID = kAudioObjectUnknown;
    CFStringRef device = internalOutput(&deviceID);
    if (!device) return 1;
    CFRelease(device);
    AudioObjectPropertyAddress property = {
        kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
    };
    if (value && !strcmp(value, "--available")) {
        property.mSelector = kAudioDevicePropertyAvailableNominalSampleRates;
        UInt32 length = 0;
        if (AudioObjectGetPropertyDataSize(deviceID, &property, 0, NULL, &length) || !length) return 1;
        AudioValueRange *rates = malloc(length);
        if (!rates) return 1;
        OSStatus status = AudioObjectGetPropertyData(deviceID, &property, 0, NULL, &length, rates);
        if (!status)
            for (UInt32 i = 0; i < length / sizeof(*rates); ++i)
                printf("%.0f %.0f\n", rates[i].mMinimum, rates[i].mMaximum);
        free(rates);
        return status ? 1 : 0;
    }
    Float64 original;
    UInt32 size = sizeof original;
    OSStatus status = AudioObjectGetPropertyData(deviceID, &property, 0, NULL, &size, &original);
    if (status) return 1;
    if (!value) {
        printf("%.0f\n", original);
        return 0;
    }
    char *end = NULL;
    const Float64 rate = strtod(value, &end);
    if (!end || *end || !isfinite(rate) || rate < 8000 || rate > 384000) return 2;
    if (rate == original) return 0;
    AudioObjectPropertyAddress activeProperty = {
        kAudioDevicePropertyDeviceIsRunningSomewhere, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
    };
    UInt32 running = 1;
    size = sizeof running;
    status = AudioObjectGetPropertyData(deviceID, &activeProperty, 0, NULL, &size, &running);
    if (status || running) {
        fputs("Wait for the built-in output to become idle before changing its sample rate\n", stderr);
        return 3;
    }
    status = AudioObjectSetPropertyData(deviceID, &property, 0, NULL, sizeof rate, &rate);
    if (status) fprintf(stderr, "Could not set built-in output sample rate (%d)\n", (int)status);
    return status ? 1 : 0;
}

static void enqueue(struct tone *t, AudioQueueRef q, AudioQueueBufferRef b) {
    unsigned n = t->total - t->queued;
    if (n > blockFrames) n = blockFrames;
    if (!n) return;
    int16_t *dst = b->mAudioData;
    for (unsigned i = 0; i < n; i++) {
        unsigned f = t->queued + i;
        double envelope = fmin(1, fmin(f, t->total - f) / 600.0);
        int16_t sample = (int16_t)lrint(8192 * envelope * sin(2 * M_PI * 660 * f / rate));
        dst[2*i] = dst[2*i+1] = sample;
    }
    b->mAudioDataByteSize = n * 4;
    if (AudioQueueEnqueueBuffer(q, b, 0, NULL)) { t->failed = 1; return; }
    t->queued += n;
    t->pending++;
}

static void played(void *ctx, AudioQueueRef q, AudioQueueBufferRef b) {
    struct tone *t = ctx;
    t->pending--;
    enqueue(t, q, b);
}

int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "--nominal-rate")) {
        if (argc > 3) return 2;
        return nominalRate(argc == 3 ? argv[2] : NULL);
    }
    double seconds = argc == 2 ? strtod(argv[1], NULL) : 3;
    if (argc > 2 || !isfinite(seconds) || seconds < 0.2 || seconds > 4) return 2;
    CFStringRef device = internalOutput(NULL);
    if (!device) { fputs("Built-in output is unavailable\n", stderr); return 1; }
    struct tone t = {.total = (unsigned)(seconds * rate)};
    AudioStreamBasicDescription f = {rate, kAudioFormatLinearPCM, kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked, 4, 1, 4, 2, 16, 0};
    AudioQueueRef q = NULL;
    OSStatus s = AudioQueueNewOutput(&f, played, &t, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode, 0, &q);
    if (!s) s = AudioQueueSetProperty(q, kAudioQueueProperty_CurrentDevice, &device, sizeof device);
    CFStringRef selected = NULL;
    UInt32 size = sizeof selected;
    if (!s) s = AudioQueueGetProperty(q, kAudioQueueProperty_CurrentDevice, &selected, &size);
    if (!s && (!selected || !CFEqual(device, selected))) s = -1;
    if (selected) CFRelease(selected);
    CFRelease(device);
    for (unsigned i = 0; !s && i < 3; i++) {
        AudioQueueBufferRef b;
        s = AudioQueueAllocateBuffer(q, blockFrames * 4, &b);
        if (!s) enqueue(&t, q, b);
    }
    if (!s && !t.failed) s = AudioQueueStart(q, NULL);
    if (!s && !t.failed) {
        double end = CFAbsoluteTimeGetCurrent() + seconds + 2;
        while (t.pending && !t.failed && CFAbsoluteTimeGetCurrent() < end) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true);
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.2, false);
    }
    if (q) AudioQueueDispose(q, true);
    if (s || t.failed || t.pending) { fprintf(stderr, "Built-in output playback failed (%d, pending %u)\n", (int)s, t.pending); return 1; }
    puts("660 Hz reference sent to built-in output");
    return 0;
}
