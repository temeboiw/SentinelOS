#pragma once
#include <stdint.h>

#define BOOT_MAX_MODULES 16

typedef struct {
    char     name[32];
    uint64_t base;    /* virtual (higher-half preferred) */
    uint64_t phys;    /* physical load address */
    uint64_t size, entry;
} BootModule;

typedef struct {
    uint64_t fb_base;
    uint32_t width, height;
    uint32_t pitch;
    uint64_t mmap;
    uint64_t mmap_size;
    uint64_t desc_size;
    uint32_t module_count, pad;
    BootModule modules[BOOT_MAX_MODULES];
    uint64_t ap_trampoline_phys; /* UEFI-reserved SIPI page below 1 MiB, zero if unavailable */
} BootInfo;
