#pragma once

#include <IOKit/IOService.h>
#include <libkern/version.h>
#include <mach/vm_types.h>

extern "C" vm_size_t ml_nofault_copy(vm_offset_t, vm_offset_t, vm_size_t);

// AppleHDAController 600.2 on 24G824. Its activation and deactivation
// methods use these slots to manage the descriptor and tag free masks.
// Call only with the controller's work-loop gate held.
struct AppleHDAStreamLease {
    using Allocate = IOReturn (*)(IOService *, unsigned, int *, int *, int *, bool);
    using Release = IOReturn (*)(IOService *, unsigned, int, int, int);

    Allocate allocate = nullptr;
    Release release = nullptr;
    int tag = 0;
    int kind = -1;
    int index = -1;
    static constexpr unsigned kInput = 0;

    template <typename T>
    static bool read(uintptr_t address, T &value) {
        return ml_nofault_copy(address, reinterpret_cast<uintptr_t>(&value), sizeof(value)) == sizeof(value);
    }

    bool resolve(IOService *controller) {
        if (strcmp(osversion, "24G824") ||
            strcmp(controller->getMetaClass()->getClassName(), "AppleHDAController"))
            return false;
        uintptr_t vtable = 0;
        uintptr_t methods[2] = {};
        if (!read(reinterpret_cast<uintptr_t>(controller), vtable) || !vtable ||
            !read(vtable + 0x948, methods) || !methods[0] || methods[1] - methods[0] != 0x13e)
            return false;
        const auto image = methods[0] - 0xb98a;
        if (image & 0xfff)
            return false;
        constexpr uint8_t uuidCommand[] = {
            0x1b, 0, 0, 0, 0x18, 0, 0, 0,
            0x12, 0x7b, 0x00, 0x8b, 0x08, 0x62, 0x32, 0x32,
            0x9a, 0x2b, 0x05, 0xa8, 0xb1, 0xc7, 0x5b, 0xee
        };
        constexpr uint8_t allocPrefix[] = {0xb8, 0xc2, 0x02, 0x00, 0xe0, 0x83, 0xfe, 0x01};
        constexpr uint8_t releasePrefix[] = {0x4c, 0x63, 0xc9, 0x46, 0x8b, 0x94, 0x8f, 0x64};
        uint8_t uuid[sizeof(uuidCommand)] = {};
        uint8_t allocCode[sizeof(allocPrefix)] = {};
        uint8_t releaseCode[sizeof(releasePrefix)] = {};
        if (!read(image + 0x568, uuid) || memcmp(uuid, uuidCommand, sizeof(uuid)) ||
            !read(methods[0], allocCode) || memcmp(allocCode, allocPrefix, sizeof(allocCode)) ||
            !read(methods[1], releaseCode) || memcmp(releaseCode, releasePrefix, sizeof(releaseCode)))
            return false;
        allocate = reinterpret_cast<Allocate>(methods[0]);
        release = reinterpret_cast<Release>(methods[1]);
        return true;
    }

    bool acquire(IOService *controller) {
        return allocate(controller, kInput, &tag, &kind, &index, false) == kIOReturnSuccess;
    }

    bool returnToController(IOService *controller) {
        if (tag < 1 || tag > 15 || kind < 0 || kind > 2 || index < 0 || index >= 30)
            return false;
        return release(controller, kInput, tag, kind, index) == kIOReturnSuccess;
    }
};
