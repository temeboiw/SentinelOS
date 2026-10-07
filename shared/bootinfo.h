#pragma once
#include <stdint.h>

#define BOOT_MAX_MODULES 16

// one image loaded by loadufi (like an entry of PsLoadedModuleList)
typedef struct {
    char     name[32];
    uint64_t base, size, entry;
} BootModule;

// Handed from the bootloader to the kernel (pointer arrives in RCX).
typedef struct {
    uint64_t fb_base;
    uint32_t width, height;
    uint32_t pitch;                 // bytes per scanline
    uint64_t mmap;                  // EFI_MEMORY_DESCRIPTOR array
    uint64_t mmap_size;
    uint64_t desc_size;
    uint32_t module_count, pad;
    BootModule modules[BOOT_MAX_MODULES];
} BootInfo;
