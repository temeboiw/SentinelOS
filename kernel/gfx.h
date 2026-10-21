#pragma once
/*
 * SentinelOS graphics bootstrap.
 *
 * This stage owns PCI discovery and the graphics-backend state reported to
 * bootvid.exe. It deliberately does NOT mark a GPU as initialized merely
 * because a PCI function exists: the VirtIO-GPU command/virtqueue driver is a
 * separate stage. The firmware framebuffer remains the active safe backend.
 */
#include <stdint.h>

#define GFX_BOOTINFO_VERSION 1u
#define GFX_BACKEND_NONE       0u
#define GFX_BACKEND_FRAMEBUFFER 1u
#define GFX_BACKEND_VIRTIO_GPU_DETECTED 2u

#define GFX_CAP_FRAMEBUFFER (1u << 0)
#define GFX_CAP_PCI_SCAN    (1u << 1)
#define GFX_CAP_VIRTIO_GPU  (1u << 2)
#define GFX_CAP_ACCEL_2D    (1u << 3)
#define GFX_CAP_ACCEL_3D    (1u << 4)

typedef struct {
    uint32_t size;
    uint32_t version;
    uint32_t backend;
    uint32_t capabilities;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t pci_bus;
    uint32_t pci_device;
    uint32_t pci_function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t pci_class;
    uint8_t pci_subclass;
    uint8_t pci_prog_if;
    uint8_t initialized;
} GfxBootInfo;

static uint32_t gfx_pci_address(uint8_t bus, uint8_t dev, uint8_t fn,
                                uint8_t reg) {
    return 0x80000000u | ((uint32_t)bus << 16) |
           ((uint32_t)dev << 11) | ((uint32_t)fn << 8) |
           ((uint32_t)reg & 0xFCu);
}

static uint32_t gfx_pci_read32(uint8_t bus, uint8_t dev, uint8_t fn,
                               uint8_t reg) {
    uint32_t address = gfx_pci_address(bus, dev, fn, reg);
    uint32_t value;
    __asm__ volatile("outl %0, $0xCF8" : : "a"(address));
    __asm__ volatile("inl $0xCFC, %0" : "=a"(value));
    return value;
}

static int gfx_pci_read_identity(uint8_t bus, uint8_t dev, uint8_t fn,
                                 uint16_t *vendor, uint16_t *device) {
    uint32_t id = gfx_pci_read32(bus, dev, fn, 0);
    *vendor = (uint16_t)id;
    *device = (uint16_t)(id >> 16);
    return *vendor != 0xFFFFu && *vendor != 0;
}

static int gfx_find_virtio_gpu(GfxBootInfo *out) {
    /* Scan all PCI buses; multifunction functions are checked explicitly. */
    for (uint32_t bus = 0; bus < 256; ++bus) {
        for (uint32_t dev = 0; dev < 32; ++dev) {
            uint16_t vendor, device;
            if (!gfx_pci_read_identity((uint8_t)bus, (uint8_t)dev, 0,
                                       &vendor, &device))
                continue;

            uint32_t header = gfx_pci_read32((uint8_t)bus, (uint8_t)dev, 0, 0x0C);
            uint32_t functions = ((header >> 16) & 0x80u) ? 8u : 1u;
            for (uint32_t fn = 0; fn < functions; ++fn) {
                if (fn && !gfx_pci_read_identity((uint8_t)bus, (uint8_t)dev,
                                                  (uint8_t)fn, &vendor, &device))
                    continue;

                /* VirtIO vendor 1AF4, GPU device 1050 (modern) or 1005
                   (legacy/transitional device ID used by older implementations). */
                if (vendor == 0x1AF4u &&
                    (device == 0x1050u || device == 0x1005u)) {
                    uint32_t classreg = gfx_pci_read32((uint8_t)bus,
                                                       (uint8_t)dev,
                                                       (uint8_t)fn, 0x08);
                    out->pci_bus = bus;
                    out->pci_device = dev;
                    out->pci_function = fn;
                    out->vendor_id = vendor;
                    out->device_id = device;
                    out->pci_class = (uint8_t)(classreg >> 24);
                    out->pci_subclass = (uint8_t)(classreg >> 16);
                    out->pci_prog_if = (uint8_t)(classreg >> 8);
                    return 1;
                }
            }
        }
    }
    return 0;
}

static void gfx_boot_initialize(GfxBootInfo *out,
                                uint32_t fb_width, uint32_t fb_height,
                                uint32_t fb_pitch) {
    if (!out) return;
    uint32_t requested_size = out->size;
    (void)requested_size;
    *out = (GfxBootInfo){0};
    out->size = sizeof(*out);
    out->version = GFX_BOOTINFO_VERSION;
    out->width = fb_width;
    out->height = fb_height;
    out->pitch = fb_pitch;

    if (fb_width >= 320u && fb_height >= 200u &&
        fb_pitch >= fb_width * 4u) {
        out->capabilities |= GFX_CAP_FRAMEBUFFER;
        out->backend = GFX_BACKEND_FRAMEBUFFER;
        out->initialized = 1;
    }

    out->capabilities |= GFX_CAP_PCI_SCAN;
    if (gfx_find_virtio_gpu(out)) {
        out->capabilities |= GFX_CAP_VIRTIO_GPU;
        /* Detection is not driver initialization; do not switch backends. */
        out->backend = GFX_BACKEND_VIRTIO_GPU_DETECTED;
    }

    DbgPrint("gfx: bootstrap backend=%s mode=%ux%u pitch=%u\n",
        out->backend == GFX_BACKEND_VIRTIO_GPU_DETECTED
            ? "virtio-gpu-detected/framebuffer-active"
            : (out->backend == GFX_BACKEND_FRAMEBUFFER
                ? "firmware-framebuffer" : "none"),
        out->width, out->height, out->pitch);
    if (out->capabilities & GFX_CAP_VIRTIO_GPU) {
        DbgPrint("gfx: found VirtIO-GPU PCI %02x:%02x.%u vendor=%04x device=%04x; command driver not initialized yet\n",
            out->pci_bus, out->pci_device, out->pci_function,
            out->vendor_id, out->device_id);
    } else {
        DbgPrint("gfx: no supported VirtIO-GPU PCI function; framebuffer fallback retained\n");
    }
}
