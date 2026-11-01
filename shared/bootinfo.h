#pragma once
#include <stdint.h>

#define BOOT_MAX_MODULES 16

#define BOOT_MODE_UEFI 0
#define BOOT_MODE_BIOS  1

typedef struct {
    char     name[32];
    uint64_t base;    /* virtual (higher-half preferred) */
    uint64_t phys;    /* physical load address */
    uint64_t size, entry;
} BootModule;

typedef struct {
    uint32_t boot_mode;      /* BOOT_MODE_UEFI or BOOT_MODE_BIOS */
    uint32_t reserved0;      /* keep fb_base 8-byte aligned */
    uint64_t fb_base;
    uint32_t width, height;
    uint32_t pitch;
    uint32_t reserved1;
    uint64_t mmap;           /* UEFI: EFI memory map; BIOS: E820 entries (reused storage) */
    uint64_t mmap_size;      /* bytes */
    uint64_t desc_size;      /* bytes per descriptor */
    uint32_t module_count, pad;
    BootModule modules[BOOT_MAX_MODULES];
    uint64_t ap_trampoline_phys; /* UEFI: SIPI page below 1 MiB; BIOS: 0 */
    uint64_t e820_map;       /* BIOS: physical address of E820 entries (same storage as mmap) */
    uint32_t e820_count;     /* BIOS: number of E820 entries */
    uint32_t e820_entry_size;/* BIOS: bytes per E820 entry (typically 24) */
} BootInfo;
