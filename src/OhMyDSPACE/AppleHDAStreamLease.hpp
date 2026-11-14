#pragma once

#include <IOKit/IOService.h>
#include <libkern/version.h>
#include <mach/vm_types.h>

extern "C" vm_size_t ml_nofault_copy(vm_offset_t, vm_offset_t, vm_size_t);

// AppleHDAController 600.2, Mach-O UUID 127B008B-0862-3232-9A2B-05A8B1C75BEE.
// Sequoia 24G824 ships it, and SimpleHDA 1.0.1 embeds the same image on
// Tahoe 25G342 (linked at runtime by its own kxld). Vtable slots 0x948 and
// 0x950 are allocateLinkStream and deallocateLinkStream, which manage the
// descriptor and tag free masks. The image header, LC_UUID and the method
// prologues identify the binary, so neither the OS build nor the load
// address alignment is compared.
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

    // Diagnostics published by OhMyDSPACE; no kernel addresses are kept.
    const char *stage = "not-run";
    uint64_t allocPrologue = 0, releasePrologue = 0;
    uint32_t methodDelta = 0, imagePageOffset = 0, headerMagic = 0, headerType = 0;
    uint32_t headerCommands = 0, uuidOffset = 0;
    uint64_t metaOffset = 0;
    IOReturn acquireResult = kIOReturnSuccess;
    IOReturn releaseResult = kIOReturnSuccess;

    template <typename T>
    static bool read(uintptr_t address, T &value) {
        return ml_nofault_copy(address, reinterpret_cast<uintptr_t>(&value), sizeof(value)) == sizeof(value);
    }

    bool resolve(IOService *controller) {
        if (allocate && release) { stage = "resolved"; return true; }
        stage = "os-major";
        if (version_major != 24 && version_major != 25) return false;
        stage = "controller-class";
        if (strcmp(controller->getMetaClass()->getClassName(), "AppleHDAController")) return false;
        uintptr_t vtable = 0;
        uintptr_t methods[2] = {};
        stage = "vtable-read";
        if (!read(reinterpret_cast<uintptr_t>(controller), vtable) || !vtable ||
            !read(vtable + 0x948, methods) || !methods[0] || !methods[1]) return false;
        methodDelta = static_cast<uint32_t>(methods[1] - methods[0]);
        stage = "method-delta";
        if (methods[1] - methods[0] != 0x13e) return false;
        const auto image = methods[0] - 0xb98a;
        imagePageOffset = static_cast<uint32_t>(image & 0xfff);
        uint32_t header[8] = {};
        stage = "image-header";
        if (!read(image, header)) return false;
        headerMagic = header[0]; headerType = header[3]; headerCommands = header[4];
        if (header[0] != 0xfeedfacf || header[1] != 0x01000007 || header[3] != 11) return false;
        constexpr uint8_t uuidCommand[] = {
            0x1b, 0, 0, 0, 0x18, 0, 0, 0,
            0x12, 0x7b, 0x00, 0x8b, 0x08, 0x62, 0x32, 0x32,
            0x9a, 0x2b, 0x05, 0xa8, 0xb1, 0xc7, 0x5b, 0xee
        };
        constexpr uint8_t allocPrefix[] = {0xb8, 0xc2, 0x02, 0x00, 0xe0, 0x83, 0xfe, 0x01};
        constexpr uint8_t releasePrefix[] = {0x4c, 0x63, 0xc9, 0x46, 0x8b, 0x94, 0x8f, 0x64};
        // A runtime linker (SimpleHDA's kxld) rewrites the load commands, so the
        // UUID is located by walking them instead of at its on-disk offset 0x568.
        stage = "image-commands";
        const uint32_t commands = header[4], commandBytes = header[5];
        if (!commands || commands > 64 || commandBytes < 8 || commandBytes > 0x4000) return false;
        uint32_t offset = 32;
        bool found = false;
        for (uint32_t i = 0; i < commands && offset + 8 <= 32 + commandBytes; ++i) {
            uint32_t command[2] = {};
            if (!read(image + offset, command) || command[1] < 8 || (command[1] & 7) ||
                offset + command[1] > 32 + commandBytes) return false;
            if (command[0] == 0x1b) {
                uint8_t uuid[sizeof(uuidCommand)] = {};
                uuidOffset = offset;
                stage = "image-uuid";
                if (command[1] != sizeof(uuidCommand) || !read(image + offset, uuid) ||
                    memcmp(uuid, uuidCommand, sizeof(uuid))) return false;
                found = true;
                break;
            }
            offset += command[1];
        }
        // AppleHDAController::gMetaClass sits at 0x15d88 in this image. When the
        // linker emitted no LC_UUID, the controller's metaclass address must
        // land exactly there; a present but different UUID was rejected above.
        metaOffset = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(controller->getMetaClass()) - image);
        stage = "image-uuid-missing";
        if (!found && metaOffset != 0x15d88) return false;
        stage = "method-read";
        if (!read(methods[0], allocPrologue) || !read(methods[1], releasePrologue)) return false;
        stage = "alloc-prologue";
        if (memcmp(&allocPrologue, allocPrefix, sizeof(allocPrefix))) return false;
        stage = "release-prologue";
        if (memcmp(&releasePrologue, releasePrefix, sizeof(releasePrefix))) return false;
        allocate = reinterpret_cast<Allocate>(methods[0]);
        release = reinterpret_cast<Release>(methods[1]);
        stage = "resolved";
        return true;
    }

    bool acquire(IOService *controller) {
        acquireResult = allocate(controller, kInput, &tag, &kind, &index, false);
        stage = acquireResult == kIOReturnSuccess ? "acquired" : "allocate-failed";
        return acquireResult == kIOReturnSuccess;
    }

    bool returnToController(IOService *controller) {
        if (tag < 1 || tag > 15 || kind < 0 || kind > 2 || index < 0 || index >= 30) {
            stage = "release-invalid-lease";
            return false;
        }
        releaseResult = release(controller, kInput, tag, kind, index);
        stage = releaseResult == kIOReturnSuccess ? "released" : "release-failed";
        return releaseResult == kIOReturnSuccess;
    }
};
