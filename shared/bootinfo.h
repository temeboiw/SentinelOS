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

#define GFX_BACKEND_NONE 0u
#define GFX_BACKEND_FRAMEBUFFER 1u
#define GFX_BACKEND_VIRTIO_GPU 2u

#define GFX_CAP_FRAMEBUFFER (1u << 0)
#define GFX_CAP_PCI_SCAN    (1u << 1)
#define GFX_CAP_VIRTIO_GPU  (1u << 2)
#define GFX_CAP_ACCEL_2D    (1u << 3)
#define GFX_CAP_ACCEL_3D    (1u << 4)

typedef struct {
    uint32_t boot_mode;      /* BOOT_MODE_UEFI or BOOT_MODE_BIOS */
    uint32_t reserved0;      /* keep fb_base 8-byte aligned */
    uint64_t fb_base;
    uint32_t width, height;
    uint32_t pitch;
    uint32_t reserved1;
    /* Graphics device information — merged from GfxBootInfo so user-mode
     * can select the correct rendering backend (Vulkan, DXVK, soft3d, etc.). */
    uint32_t gfx_backend;
    uint32_t gfx_capabilities;
    uint32_t gpu_vendor_id;
    uint32_t gpu_device_id;
    uint8_t  gfx_pci_bus;
    uint8_t  gfx_pci_device;
    uint8_t  gfx_pci_function;
    uint8_t  gfx_class;
    uint8_t  gfx_subclass;
    uint8_t  gfx_prog_if;
    uint8_t  gfx_initialized;
    uint64_t gfx_surface_va;
    /* UEFI: EFI memory map; BIOS: E820 entries (reused storage) */
    uint64_t mmap;
    uint64_t mmap_size;
    uint64_t desc_size;
    /* Module list from bootloader */
    BootModule modules[BOOT_MAX_MODULES];
    uint32_t module_count, pad;
    /* UEFI: SIPI page below 1 MiB; BIOS: 0 */
    uint64_t ap_trampoline_phys;
    /* BIOS: physical address of E820 entries (same storage as mmap) */
    uint64_t e820_map;
    uint32_t e820_count;
    uint32_t e820_entry_size;
} BootInfo;
