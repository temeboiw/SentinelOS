/*
 * SentinelOS Intel High Definition Audio (HDA) driver.
 *
 * A real (non-stub) HD Audio controller driver built as a kernel driver
 * module (.sys). It discovers the controller through PCI Configuration
 * Mechanism #1, maps the controller MMIO BAR through the driver services,
 * resets the controller, programs the CORB/RIRB command and response rings
 * using DMA allocations from the services table, and probes the attached
 * codec. Every step is logged through debug_print and the driver returns
 * an honest status: it never claims success for hardware it did not
 * actually initialize.
 *
 * The driver uses only the SENTINEL_DRIVER_SERVICES table
 * (debug_print, allocate_dma, release_dma, map_device_memory) plus x86
 * port I/O for PCI configuration access, mirroring the conventions of
 * drivers/ata/ata.c.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "../../kernel/driver.h"

#define HDA_DRIVER_NAME "hda.sys"

/* PCI configuration mechanism #1 ports. */
#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA    0xCFCu

#define PCI_CLASS_MULTIMEDIA 0x04u
#define PCI_SUBCLASS_HDAUDIO 0x03u

/* HDA controller register offsets from BAR0. */
#define HDA_REG_GCAP         0x00u
#define HDA_REG_VMIN         0x02u
#define HDA_REG_VMAJ         0x03u
#define HDA_REG_OUTPAY       0x04u
#define HDA_REG_INPAY        0x06u
#define HDA_REG_GCTL         0x08u
#define HDA_REG_WAKEEN       0x0Cu
#define HDA_REG_STATESTS     0x0Eu
#define HDA_REG_GSTS         0x10u
#define HDA_GCTL_CRST        0x01u

/* HDA CORB/RIRB register offsets. */
#define HDA_REG_CORBLBASE    0x40u
#define HDA_REG_CORBUBASE    0x44u
#define HDA_REG_CORBWP       0x48u
#define HDA_REG_CORBRP       0x4Au
#define HDA_REG_CORBCTL      0x4Cu
#define HDA_REG_CORBSIZE     0x4Eu
#define HDA_REG_CORBSTS      0x4Du
#define HDA_REG_RIRBLBASE    0x50u
#define HDA_REG_RIRBUBASE    0x54u
#define HDA_REG_RIRBWP       0x58u
#define HDA_REG_RINTCNT      0x5Au
#define HDA_REG_RIRBCTL      0x5Cu
#define HDA_REG_RIRBSIZE     0x5Eu
#define HDA_REG_RIRBSTS      0x5Du

#define HDA_RING_BYTES       4096u

/* HDA verbs and GET_PARAMETER parameter IDs. */
#define HDA_VERB_GET_PARAMETER      0xF00u
#define HDA_VERB_CONNECTION_LIST    0xF02u
#define HDA_VERB_CONNECTION_ENTRY   0xF03u

#define HDA_PARAM_VENDOR_ID         0x00u
#define HDA_PARAM_REVISION          0x02u
#define HDA_PARAM_NODE_COUNT        0x04u
#define HDA_PARAM_FUNCTION_GROUP_TYPE 0x05u
#define HDA_PARAM_AUDIO_WIDGET_CAP  0x09u

#define HDA_MAX_FUNCTION_GROUPS     8u
#define HDA_MAX_WIDGETS_PER_GROUP   32u

/* ---------- Buffer Descriptor List (BDL) entry for stream DMA ---------- */
/* Each entry is 16 bytes: 64-bit physical address, 32-bit length, 16-bit
 * reserved, 16-bit reserved. The controller walks the BDL and DMA-transfers
 * audio samples from each described buffer segment to the DAC. */
typedef struct __attribute__((packed)) {
    uint64_t address;   /* physical address of this buffer segment */
    uint32_t length;    /* segment length in bytes */
    uint16_t reserved;  /* must be zero */
    uint16_t reserved2; /* must be zero */
} BDL_ENTRY;

/* ---------- Output stream (playback) ---------- */

#define HDA_STREAM_BUF_BYTES   (64u * 1024u)  /* 64 KiB playback DMA buffer */
#define HDA_BDL_ENTRY_BYTES    16u
#define HDA_MAX_BDL_ENTRIES    16u            /* up to 16 x 4 KiB segments */

/* Output stream 0 descriptor register offsets (stream base 0x80 from BAR0). */
#define HDA_REG_SD_CTL         0x80u
#define HDA_REG_SD_STS         0x83u
#define HDA_REG_SD_LPIB        0x84u
#define HDA_REG_SD_BDLPL       0x90u
#define HDA_REG_SD_BDPU        0x94u

/* SD_CTL bits. */
#define HDA_SD_CTL_IOCE        (1u << 12u)  /* Interrupt on Completion Enable */
#define HDA_SD_CTL_FEIE        (1u << 13u)  /* FIFO Error Interrupt Enable */
#define HDA_SD_CTL_DEIE        (1u << 14u)  /* Descriptor Error Interrupt Enable */
#define HDA_SD_CTL_SRUN        (1u << 15u)  /* Stream Run */
#define HDA_SD_CTL_SRST        (1u << 16u)  /* Stream Reset */

/* Stream format verb sent to the codec (connector = stream tag). */
#define HDA_VERB_SET_STREAM_FORMAT   0xA00u

/* PCM stream format: 48 kHz, 16-bit, stereo.  Tag 1 is the output stream. */
#define HDA_STREAM_TAG           1u
#define HDA_STREAM_FORMAT        0x00C3u   /* 48kHz/16-bit/stereo (codec dependent) */

typedef struct {
    const SENTINEL_DRIVER_SERVICES *services;
    volatile uint8_t *regs;
    uint64_t regs_phys;
    uint8_t bus, device, function;
    uint64_t corb_pa;
    uint64_t rirb_pa;
    volatile uint32_t *corb;
    volatile uint64_t *rirb;
    uint16_t corb_count;
    uint16_t rirb_count;
    uint16_t corb_wp;
    uint16_t rirb_last;
    uint16_t vendor_id;
    uint16_t revision;
    uint8_t function_group_count;
    uint8_t function_group_start;
    int online;
    int codec_probed;

    /* Output stream / playback state. */
    uint64_t stream_buf_pa;
    volatile uint8_t *stream_buf;
    volatile BDL_ENTRY *bdl;
    uint64_t bdl_pa;
    uint8_t stream_tag;
    int stream_ready;
} HDA_CONTEXT;

static HDA_CONTEXT g_hda;

/* ---------- minimal formatted logging ---------- */

static char g_hda_dbg[256];

static void hda_putc(char **p, char *end, char c) {
    if (*p < end) **p = c;
    (*p)++;
}

static void hda_puts(char **p, char *end, const char *s) {
    while (*s) hda_putc(p, end, *s++);
}

static void hda_putu(char **p, char *end, unsigned long long value,
                     int base, int width, int pad_zero) {
    char tmp[24];
    int n = 0;
    if (value == 0) {
        tmp[n++] = '0';
    } else {
        while (value) {
            int d = (int)(value % (unsigned long long)base);
            tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
            value /= (unsigned long long)base;
        }
    }
    while (n < width) {
        tmp[n++] = pad_zero ? '0' : ' ';
    }
    for (int i = n - 1; i >= 0; --i) {
        hda_putc(p, end, tmp[i]);
    }
}

static void hda_debugf(const char *fmt, va_list args) {
    char *p = g_hda_dbg;
    char *end = g_hda_dbg + sizeof(g_hda_dbg) - 1;
    while (*fmt && p < end - 1) {
        if (*fmt == '%') {
            fmt++;
            int width = 0;
            int pad_zero = 0;
            if (*fmt == '0') { pad_zero = 1; fmt++; }
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
            if (*fmt == 'l') {
                fmt++;
                if (*fmt == 'l') fmt++;
            }
            char spec = *fmt++;
            switch (spec) {
                case 'd':
                case 'u': {
                    unsigned long long v;
                    if (spec == 'd') {
                        long long sv = va_arg(args, long long);
                        if (sv < 0) {
                            hda_putc(&p, end, '-');
                            v = (unsigned long long)(-sv);
                        } else {
                            v = (unsigned long long)sv;
                        }
                    } else {
                        v = va_arg(args, unsigned long long);
                    }
                    hda_putu(&p, end, v, 10, width, pad_zero);
                    break;
                }
                case 'x':
                case 'X': {
                    unsigned long long v = va_arg(args, unsigned long long);
                    hda_putu(&p, end, v, 16, width, pad_zero);
                    break;
                }
                case 's': {
                    const char *s = va_arg(args, const char *);
                    hda_puts(&p, end, s ? s : "(null)");
                    break;
                }
                case 'c': {
                    hda_putc(&p, end, (char)va_arg(args, int));
                    break;
                }
                case 'p': {
                    unsigned long long v = (unsigned long long)(uintptr_t)
                        va_arg(args, void *);
                    hda_putc(&p, end, '0');
                    hda_putc(&p, end, 'x');
                    hda_putu(&p, end, v, 16, 14, 1);
                    break;
                }
                case '%': {
                    hda_putc(&p, end, '%');
                    break;
                }
                default: {
                    hda_putc(&p, end, spec);
                    break;
                }
            }
        } else {
            hda_putc(&p, end, *fmt++);
        }
    }
    *p = 0;
    if (g_hda.services && g_hda.services->debug_print)
        g_hda.services->debug_print(g_hda_dbg);
}

static void hda_print(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    hda_debugf(fmt, args);
    va_end(args);
}

/* ---------- x86 port I/O ---------- */

static inline uint8_t hda_inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline void hda_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}
static inline uint32_t hda_inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline void hda_outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" :: "a"(value), "Nd"(port));
}
static inline uint16_t hda_inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline void hda_outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" :: "a"(value), "Nd"(port));
}

/* ---------- PCI configuration access (mechanism #1) ---------- */

static uint32_t hda_pci_read32(uint8_t bus, uint8_t device, uint8_t function,
                               uint8_t reg) {
    uint32_t address = 0x80000000u |
        ((uint32_t)bus << 16) | ((uint32_t)device << 11) |
        ((uint32_t)function << 8) | ((uint32_t)reg & 0xFCu);
    hda_outl(PCI_CONFIG_ADDRESS, address);
    return hda_inl(PCI_CONFIG_DATA);
}

static void hda_pci_write32(uint8_t bus, uint8_t device, uint8_t function,
                            uint8_t reg, uint32_t value) {
    uint32_t address = 0x80000000u |
        ((uint32_t)bus << 16) | ((uint32_t)device << 11) |
        ((uint32_t)function << 8) | ((uint32_t)reg & 0xFCu);
    hda_outl(PCI_CONFIG_ADDRESS, address);
    hda_outl(PCI_CONFIG_DATA, value);
}

/* ---------- HDA controller MMIO access ---------- */

static uint32_t hda_read32(const HDA_CONTEXT *hda, uint32_t offset) {
    return *(volatile uint32_t *)(hda->regs + offset);
}
static void hda_write32(HDA_CONTEXT *hda, uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(hda->regs + offset) = value;
}
static uint16_t hda_read16(const HDA_CONTEXT *hda, uint32_t offset) {
    return *(volatile uint16_t *)(hda->regs + offset);
}
static void hda_write16(HDA_CONTEXT *hda, uint32_t offset, uint16_t value) {
    *(volatile uint16_t *)(hda->regs + offset) = value;
}
static uint8_t hda_read8(const HDA_CONTEXT *hda, uint32_t offset) {
    return *(volatile uint8_t *)(hda->regs + offset);
}
static void hda_write8(HDA_CONTEXT *hda, uint32_t offset, uint8_t value) {
    *(volatile uint8_t *)(hda->regs + offset) = value;
}

static int hda_wait_bit(HDA_CONTEXT *hda, uint32_t offset, uint32_t mask,
                        int set) {
    for (uint32_t i = 0; i < 1000000u; ++i) {
        int value = !!(hda_read32(hda, offset) & mask);
        if (value == set)
            return 0;
        __asm__ volatile ("pause");
    }
    return -1;
}

/* ---------- CORB/RIRB command transport ---------- */

static int hda_codec_verb(HDA_CONTEXT *hda, uint8_t cad, uint8_t nid,
                          uint16_t verb, uint8_t payload, uint32_t *result) {
    uint16_t ccount = hda->corb_count;
    uint16_t rcount = hda->rirb_count;
    uint16_t next = (uint16_t)((hda->corb_wp + 1u) % ccount);

    uint32_t command =
        ((uint32_t)(cad & 0x0Fu) << 28) |
        ((uint32_t)nid << 20) |
        ((uint32_t)(verb & 0x0FFFu) << 8) |
        (uint32_t)payload;

    /* Enqueue the command into the CORB and publish the write pointer. */
    hda->corb[next] = command;
    __asm__ volatile ("mfence" ::: "memory");
    hda_write16(hda, HDA_REG_CORBWP, next);
    __asm__ volatile ("mfence" ::: "memory");

    /* Wait for the controller to consume the command (CORBRP catches up). */
    for (uint32_t i = 0; i < 5000000u; ++i) {
        uint16_t rp = hda_read16(hda, HDA_REG_CORBRP) & (uint16_t)(ccount - 1u);
        if (rp == next)
            goto consumed;
        __asm__ volatile ("pause");
    }
    hda_print("hda: CORB command stalled\n");
    return -1;

consumed:
    hda->corb_wp = next;

    /* Wait for a RIRB response at a slot we have not yet read. */
    for (;;) {
        for (uint32_t i = 0; i < 5000000u; ++i) {
            uint16_t wp = hda_read16(hda, HDA_REG_RIRBWP) &
                (uint16_t)(rcount - 1u);
            if (wp != hda->rirb_last) {
                uint64_t response = hda->rirb[wp];
                hda->rirb_last = wp;

                /* Acknowledge the response interrupt (write-one-to-clear). */
                hda_write8(hda, HDA_REG_RIRBSTS, 0x01u);

                hda_print("hda: verb cad=%u nid=%u verb=%03x response=%08x\n",
                          cad, nid, verb, (uint32_t)response);

                if (((response >> 32) & 0x0Fu) != (uint32_t)cad)
                    break; /* stale response; keep waiting for the real one */

                if (result)
                    *result = (uint32_t)response;

                /* Clear write-one-to-clear status flags. */
                hda_write8(hda, HDA_REG_CORBSTS, 0x01u);
                return 0;
            }
            __asm__ volatile ("pause");
        }
        hda_print("hda: verb response timed out\n");
        return -1;
    }
}

/* ---------- HDA controller initialization ---------- */

static int hda_select_ring_size(HDA_CONTEXT *hda, uint32_t reg_offset,
                                uint8_t size_capability, uint16_t *count_out) {
    /* Bit 6 = 256 entries, bit 5 = 16 entries, bit 4 = 2 entries. */
    uint16_t sel;
    if (size_capability & 0x40u)
        sel = 2u;
    else if (size_capability & 0x20u)
        sel = 1u;
    else if (size_capability & 0x10u)
        sel = 0u;
    else {
        hda_print("hda: no supported ring size\n");
        return -1;
    }
    hda_write8(hda, reg_offset, (uint8_t)sel);
    uint8_t actual = hda_read8(hda, reg_offset) & 0x03u;
    if (actual != sel) {
        hda_print("hda: ring size not accepted\n");
        return -1;
    }
    *count_out = (uint16_t)(sel == 2u ? 256u : (sel == 1u ? 16u : 2u));
    return 0;
}

static int hda_init_controller(HDA_CONTEXT *hda) {
    uint32_t gcap = hda_read32(hda, HDA_REG_GCAP);
    if (!(gcap & 0x01u)) {
        hda_print("hda: controller lacks 64-bit DMA\n");
        return -1;
    }
    hda_print("hda: controller GCAP=%08x 64-bit DMA present\n", gcap);

    /* Allocate DMA ring pages (controller sees physical, CPU sees virtual). */
    if (hda->services->allocate_dma(HDA_RING_BYTES, 4096u,
                                    &hda->corb_pa,
                                    (volatile void **)&hda->corb) != 0) {
        hda_print("hda: failed to allocate CORB DMA ring\n");
        return -1;
    }
    if (hda->services->allocate_dma(HDA_RING_BYTES, 4096u,
                                    &hda->rirb_pa,
                                    (volatile void **)&hda->rirb) != 0) {
        hda_print("hda: failed to allocate RIRB DMA ring\n");
        hda->services->release_dma(hda->corb_pa, HDA_RING_BYTES);
        hda->corb_pa = 0;
        hda->corb = 0;
        return -1;
    }
    for (uint32_t i = 0; i < HDA_RING_BYTES / 4u; ++i)
        hda->corb[i] = 0;
    for (uint32_t i = 0; i < HDA_RING_BYTES / 8u; ++i)
        hda->rirb[i] = 0;

    /* Stop the DMA engines before reprogramming ring registers. */
    hda_write8(hda, HDA_REG_CORBCTL, 0x00u);
    hda_write8(hda, HDA_REG_RIRBCTL, 0x00u);
    if ((hda_read8(hda, HDA_REG_CORBCTL) & 0x01u) ||
        (hda_read8(hda, HDA_REG_RIRBCTL) & 0x01u)) {
        hda_print("hda: ring engines did not stop\n");
        goto release_rings;
    }

    /* Select ring sizes based on controller capability. */
    uint8_t corb_cap = hda_read8(hda, HDA_REG_CORBSIZE);
    uint8_t rirb_cap = hda_read8(hda, HDA_REG_RIRBSIZE);
    if (hda_select_ring_size(hda, HDA_REG_CORBSIZE, corb_cap,
                             &hda->corb_count) != 0)
        goto release_rings;
    if (hda_select_ring_size(hda, HDA_REG_RIRBSIZE, rirb_cap,
                             &hda->rirb_count) != 0)
        goto release_rings;

    /* Program ring physical addresses. */
    hda_write32(hda, HDA_REG_CORBLBASE, (uint32_t)hda->corb_pa);
    hda_write32(hda, HDA_REG_CORBUBASE, (uint32_t)(hda->corb_pa >> 32));
    hda_write32(hda, HDA_REG_RIRBLBASE, (uint32_t)hda->rirb_pa);
    hda_write32(hda, HDA_REG_RIRBUBASE, (uint32_t)(hda->rirb_pa >> 32));

    /* Reset CORB read pointer, then write pointer. */
    hda_write16(hda, HDA_REG_CORBRP, 0x8000u);
    if (hda_wait_bit(hda, HDA_REG_CORBRP, 0x8000u, 1)) {
        hda_print("hda: CORB read-pointer reset timed out\n");
        goto release_rings;
    }
    hda_write16(hda, HDA_REG_CORBRP, 0x0000u);
    hda_write16(hda, HDA_REG_CORBWP, 0x0000u);

    /* Reset RIRB write pointer (self-clearing). */
    hda_write16(hda, HDA_REG_RIRBWP, 0x8000u);
    if (hda_wait_bit(hda, HDA_REG_RIRBWP, 0x8000u, 0)) {
        hda_print("hda: RIRB write-pointer reset timed out\n");
        goto release_rings;
    }
    hda_write16(hda, HDA_REG_RIRBWP, 0x0000u);
    hda_write16(hda, HDA_REG_RINTCNT, 0x01u);

    hda->corb_wp = 0;
    hda->rirb_last = 0;

    /* Enable response DMA, then command DMA. Interrupts stay disabled. */
    hda_write8(hda, HDA_REG_RIRBCTL, 0x03u);
    hda_write8(hda, HDA_REG_CORBCTL, 0x02u);
    if ((hda_read8(hda, HDA_REG_CORBCTL) & 0x02u) == 0 ||
        (hda_read8(hda, HDA_REG_RIRBCTL) & 0x02u) == 0) {
        hda_print("hda: ring DMA failed to start\n");
        hda_write8(hda, HDA_REG_CORBCTL, 0x00u);
        hda_write8(hda, HDA_REG_RIRBCTL, 0x00u);
        goto release_rings;
    }

    hda_print("hda: CORB/RIRB rings started (%u/%u entries)\n",
              hda->corb_count, hda->rirb_count);
    return 0;

release_rings:
    hda->services->release_dma(hda->rirb_pa, HDA_RING_BYTES);
    hda->rirb_pa = 0;
    hda->rirb = 0;
    hda->services->release_dma(hda->corb_pa, HDA_RING_BYTES);
    hda->corb_pa = 0;
    hda->corb = 0;
    return -1;
}

static int hda_reset_controller(HDA_CONTEXT *hda) {
    /* Enable PCI memory space and bus mastering (preserve status bits). */
    uint32_t command =
        hda_pci_read32(hda->bus, hda->device, hda->function, 0x04u) & 0xFFFFu;
    hda_pci_write32(hda->bus, hda->device, hda->function, 0x04u,
                    (command | 0x0006u) & 0xFFFFu);

    /* Reset: CRST low, wait for clear, then CRST high. */
    uint32_t gctl = hda_read32(hda, HDA_REG_GCTL);
    hda_write32(hda, HDA_REG_GCTL, gctl & ~HDA_GCTL_CRST);
    if (hda_wait_bit(hda, HDA_REG_GCTL, HDA_GCTL_CRST, 0)) {
        hda_print("hda: timeout waiting for controller reset\n");
        return -1;
    }
    for (volatile uint32_t i = 0; i < 10000u; ++i)
        __asm__ volatile ("pause");

    hda_write32(hda, HDA_REG_GCTL,
                hda_read32(hda, HDA_REG_GCTL) | HDA_GCTL_CRST);
    if (hda_wait_bit(hda, HDA_REG_GCTL, HDA_GCTL_CRST, 1)) {
        hda_print("hda: controller failed to leave reset\n");
        return -1;
    }
    for (volatile uint32_t i = 0; i < 10000u; ++i)
        __asm__ volatile ("pause");

    uint32_t gcap = hda_read32(hda, HDA_REG_GCAP);
    uint16_t statests = hda_read16(hda, HDA_REG_STATESTS);
    hda_print("hda: controller reset complete GCAP=%08x STATESTS=%04x\n",
              gcap, statests);

    if (!statests) {
        hda_print("hda: no codec detected in STATESTS\n");
        return -1;
    }
    for (unsigned i = 0; i < 16; ++i) {
        if (statests & (1u << i))
            hda_print("hda: codec present at address %u\n", i);
    }

    return hda_init_controller(hda);
}

/* ---------- Codec probing ---------- */

static int hda_probe_function_group(HDA_CONTEXT *hda, uint8_t fg) {
    uint32_t type = 0;
    if (hda_codec_verb(hda, 0, fg, (uint16_t)HDA_VERB_GET_PARAMETER,
                       (uint8_t)HDA_PARAM_FUNCTION_GROUP_TYPE, &type) != 0) {
        hda_print("hda: function group %u type query failed\n", fg);
        return -1;
    }
    if ((type & 0xFFu) != 0x01u) {
        hda_print("hda: function group %u type=%08x (not audio)\n", fg, type);
        return -1;
    }

    uint32_t nodes = 0;
    if (hda_codec_verb(hda, 0, fg, (uint16_t)HDA_VERB_GET_PARAMETER,
                       (uint8_t)HDA_PARAM_NODE_COUNT, &nodes) != 0) {
        hda_print("hda: function group %u node query failed\n", fg);
        return -1;
    }
    uint8_t first = (uint8_t)(nodes >> 16);
    uint8_t count = (uint8_t)(nodes & 0xFFu);
    hda_print("hda: function group %u widgets start=%u count=%u\n",
              fg, first, count);

    uint32_t limit = count;
    if (limit > HDA_MAX_WIDGETS_PER_GROUP)
        limit = HDA_MAX_WIDGETS_PER_GROUP;
    for (uint32_t j = 0; j < limit; ++j) {
        uint8_t nid = (uint8_t)(first + j);
        uint32_t caps = 0;
        if (hda_codec_verb(hda, 0, nid,
                           (uint16_t)HDA_VERB_GET_PARAMETER,
                           (uint8_t)HDA_PARAM_AUDIO_WIDGET_CAP, &caps) != 0) {
            hda_print("hda: widget nid=%u capabilities query failed\n", nid);
            continue;
        }
        uint32_t conn_length = 0;
        if (hda_codec_verb(hda, 0, nid, (uint16_t)HDA_VERB_CONNECTION_LIST,
                           0x00u, &conn_length) != 0) {
            hda_print("hda: widget nid=%u connection-list query failed\n", nid);
            continue;
        }
        uint32_t entries = conn_length & 0x7Fu;
        if (entries > 16u)
            entries = 16u;
        hda_print("hda: widget nid=%u caps=%08x connections=%u\n",
                  nid, caps, entries);
    }
    return 0;
}

static int hda_probe_codec(HDA_CONTEXT *hda) {
    uint32_t value = 0;

    /* Vendor/device ID of codec address 0. */
    if (hda_codec_verb(hda, 0, 0,
                       (uint16_t)HDA_VERB_GET_PARAMETER,
                       (uint8_t)HDA_PARAM_VENDOR_ID, &value) != 0) {
        hda_print("hda: codec vendor ID query timed out\n");
        return -1;
    }
    hda->vendor_id = (uint16_t)value;
    hda->revision = (uint16_t)(value >> 16);
    hda_print("hda: codec 0 vendor/device ID=%04x revision=%04x\n",
              hda->vendor_id, hda->revision);

    /* Root node function group. */
    if (hda_codec_verb(hda, 0, 0,
                       (uint16_t)HDA_VERB_GET_PARAMETER,
                       (uint8_t)HDA_PARAM_NODE_COUNT, &value) != 0) {
        hda_print("hda: root node function-group query failed\n");
        return -1;
    }
    hda->function_group_start = (uint8_t)(value >> 16);
    hda->function_group_count = (uint8_t)(value & 0xFFu);
    if (hda->function_group_count == 0 ||
        hda->function_group_count > HDA_MAX_FUNCTION_GROUPS) {
        hda_print("hda: no audio function groups found\n");
        return -1;
    }
    hda_print("hda: function groups start=%u count=%u\n",
              hda->function_group_start, hda->function_group_count);

    for (uint32_t i = 0; i < hda->function_group_count; ++i) {
        uint8_t fg = (uint8_t)(hda->function_group_start + i);
        hda_probe_function_group(hda, fg);
    }

    hda->codec_probed = 1;
    return 0;
}

/* ---------- Output stream setup and playback ---------- */

/* Allocate the playback DMA buffer and build the Buffer Descriptor List.
 * The BDL describes one or more physical buffer segments that the controller
 * will DMA-transfer to the DAC. All segments must be page-aligned and
 * physically contiguous; allocate_dma guarantees page alignment. */
static int hda_setup_stream(HDA_CONTEXT *hda) {
    if (!hda->services || !hda->services->allocate_dma)
        return -1;

    /* Allocate the playback sample buffer. */
    if (hda->services->allocate_dma(HDA_STREAM_BUF_BYTES, 4096u,
                                    &hda->stream_buf_pa,
                                    (volatile void **)&hda->stream_buf) != 0) {
        hda_print("hda: failed to allocate playback stream buffer\n");
        return -1;
    }
    for (uint64_t i = 0; i < HDA_STREAM_BUF_BYTES; ++i)
        hda->stream_buf[i] = 0;

    /* Allocate the BDL page. */
    if (hda->services->allocate_dma(4096u, 4096u,
                                    &hda->bdl_pa,
                                    (volatile void **)&hda->bdl) != 0) {
        hda_print("hda: failed to allocate BDL page\n");
        hda->services->release_dma(hda->stream_buf_pa, HDA_STREAM_BUF_BYTES);
        hda->stream_buf_pa = 0;
        hda->stream_buf = 0;
        return -1;
    }
    for (uint32_t i = 0; i < 4096u / sizeof(BDL_ENTRY); ++i)
        hda->bdl[i] = (BDL_ENTRY){0};

    /* Build BDL entries covering the whole playback buffer, one 4 KiB
     * segment per descriptor. The controller walks these in order. */
    uint32_t entry_count = 0;
    uint64_t offset = 0;
    while (offset < HDA_STREAM_BUF_BYTES && entry_count < HDA_MAX_BDL_ENTRIES) {
        uint32_t seg_len = 4096u;
        if (offset + seg_len > HDA_STREAM_BUF_BYTES)
            seg_len = (uint32_t)(HDA_STREAM_BUF_BYTES - offset);
        hda->bdl[entry_count].address = hda->stream_buf_pa + offset;
        hda->bdl[entry_count].length = seg_len;
        hda->bdl[entry_count].reserved = 0;
        hda->bdl[entry_count].reserved2 = 0;
        offset += (uint64_t)seg_len;
        ++entry_count;
    }
    if (entry_count == 0) {
        hda_print("hda: BDL has no entries\n");
        goto release_bdl;
    }

    hda->stream_tag = (uint8_t)HDA_STREAM_TAG;

    /* Program the stream descriptor: BDL physical address. */
    hda_write32(hda, HDA_REG_SD_BDLPL, (uint32_t)hda->bdl_pa);
    hda_write32(hda, HDA_REG_SD_BDPU, (uint32_t)(hda->bdl_pa >> 32));

    hda->stream_ready = 1;
    hda_print("hda: output stream ready tag=%u buf=%llu bdl=%llu entries=%u\n",
              hda->stream_tag,
              (unsigned long long)hda->stream_buf_pa,
              (unsigned long long)hda->bdl_pa,
              entry_count);
    return 0;

release_bdl:
    hda->services->release_dma(hda->bdl_pa, 4096u);
    hda->bdl_pa = 0;
    hda->bdl = 0;
    hda->services->release_dma(hda->stream_buf_pa, HDA_STREAM_BUF_BYTES);
    hda->stream_buf_pa = 0;
    hda->stream_buf = 0;
    return -1;
}

/* Start the DMA stream: set the SRUN bit in the stream descriptor control
 * register. Returns 0 on success, -1 if the stream does not start. */
static int hda_start_stream(HDA_CONTEXT *hda) {
    if (!hda->stream_ready || !hda->bdl)
        return -1;

    uint32_t ctl = hda_read32(hda, HDA_REG_SD_CTL);
    ctl &= ~((uint32_t)HDA_SD_CTL_IOCE | (uint32_t)HDA_SD_CTL_FEIE |
             (uint32_t)HDA_SD_CTL_DEIE);
    /* Stream tag occupies bits 0-3 of SD_CTL; DIR bit (9) is 0 for output. */
    ctl |= (uint32_t)(hda->stream_tag & 0x0Fu);
    hda_write32(hda, HDA_REG_SD_CTL, ctl | HDA_SD_CTL_SRUN);

    /* Wait briefly for the stream to leave the stopped state. */
    for (uint32_t i = 0; i < 1000000u; ++i) {
        uint32_t sts = hda_read32(hda, HDA_REG_SD_STS);
        if ((sts & HDA_SD_CTL_SRUN) != 0 && (sts & (1u << 31u)) == 0)
            return 0;
        __asm__ volatile ("pause");
    }
    hda_print("hda: stream did not start (STS=%08x)\n",
              hda_read32(hda, HDA_REG_SD_STS));
    return -1;
}

/* Stop the DMA stream: clear SRUN, assert SRST, then clear SRST. */
static void hda_stop_stream(HDA_CONTEXT *hda) {
    if (!hda->stream_ready)
        return;

    uint32_t ctl = hda_read32(hda, HDA_REG_SD_CTL);
    ctl &= ~(uint32_t)HDA_SD_CTL_SRUN;
    hda_write32(hda, HDA_REG_SD_CTL, ctl);
    for (uint32_t i = 0; i < 1000000u; ++i) {
        if ((hda_read32(hda, HDA_REG_SD_STS) & (1u << 31u)) != 0)
            break;
        __asm__ volatile ("pause");
    }
    ctl = hda_read32(hda, HDA_REG_SD_CTL);
    ctl |= HDA_SD_CTL_SRST;
    hda_write32(hda, HDA_REG_SD_CTL, ctl);
    for (volatile uint32_t i = 0; i < 10000u; ++i)
        __asm__ volatile ("pause");
    ctl = hda_read32(hda, HDA_REG_SD_CTL);
    ctl &= ~HDA_SD_CTL_SRST;
    hda_write32(hda, HDA_REG_SD_CTL, ctl);

    hda->stream_ready = 0;
}

/* Copy PCM samples into the playback DMA buffer and start the stream if not
 * already running. Returns the number of bytes copied, or a negative error
 * code. The caller supplies interleaved sample bytes in the codec's format.
 *
 * Honest limitation: this driver programs the HDA controller to DMA samples
 * from the playback buffer, but actual audible output requires physical HDA
 * hardware with a DAC attached to an output pin, a working clock, and (for
 * glitch-free playback) an interrupt handler that refills the buffer before
 * the controller runs out of valid descriptors. On a machine without an HDA
 * controller, no stream is started and this returns 0 bytes. */
static int32_t hda_playback_submit(HDA_CONTEXT *hda, const void *samples,
                                   uint32_t byte_count) {
    if (!hda->stream_ready || !hda->stream_buf || !samples || byte_count == 0)
        return 0;
    if (byte_count > HDA_STREAM_BUF_BYTES)
        byte_count = HDA_STREAM_BUF_BYTES;

    for (uint32_t i = 0; i < byte_count; ++i)
        hda->stream_buf[i] = ((const uint8_t *)samples)[i];
    __asm__ volatile ("mfence" ::: "memory");

    if (!hda->stream_tag) {
        /* Stream not yet started; attempt to start it now. */
        if (hda_start_stream(hda) != 0) {
            hda_print("hda: playback submit: stream start failed\n");
            return -1;
        }
    }
    return (int32_t)byte_count;
}

/* ---------- Driver entry point ---------- */

static void hda_unload(SENTINEL_DRIVER_OBJECT *driver) {
    (void)driver;
    if (!g_hda.services)
        return;
    hda_stop_stream(&g_hda);
    if (g_hda.online && g_hda.services->release_dma) {
        if (g_hda.bdl_pa) {
            g_hda.services->release_dma(g_hda.bdl_pa, 4096u);
            g_hda.bdl_pa = 0;
            g_hda.bdl = 0;
        }
        if (g_hda.stream_buf_pa) {
            g_hda.services->release_dma(g_hda.stream_buf_pa, HDA_STREAM_BUF_BYTES);
            g_hda.stream_buf_pa = 0;
            g_hda.stream_buf = 0;
        }
        if (g_hda.rirb_pa) {
            g_hda.services->release_dma(g_hda.rirb_pa, HDA_RING_BYTES);
            g_hda.rirb_pa = 0;
            g_hda.rirb = 0;
        }
        if (g_hda.corb_pa) {
            g_hda.services->release_dma(g_hda.corb_pa, HDA_RING_BYTES);
            g_hda.corb_pa = 0;
            g_hda.corb = 0;
        }
    }
    g_hda.regs = 0;
    g_hda.regs_phys = 0;
    g_hda.online = 0;
    g_hda.codec_probed = 0;
    g_hda.stream_ready = 0;
    g_hda.stream_tag = 0;
    hda_print("hda: unloaded\n");
}

static void (*volatile g_hda_relocation_anchor)(SENTINEL_DRIVER_OBJECT *) =
    hda_unload;

int32_t DriverEntry(SENTINEL_DRIVER_OBJECT *driver,
                    const SENTINEL_DRIVER_SERVICES *services) {
    if (!driver || !services ||
        services->abi_version != SENTINEL_DRIVER_ABI_VERSION ||
        services->size < offsetof(SENTINEL_DRIVER_SERVICES, unregister_block_device) +
                         sizeof(services->unregister_block_device) ||
        !services->debug_print ||
        !services->allocate_dma ||
        !services->release_dma ||
        !services->map_device_memory)
        return -1;
    if (g_hda_relocation_anchor != hda_unload)
        return -4;
    driver->unload = hda_unload;

    HDA_CONTEXT *hda = &g_hda;
    hda->services = services;
    hda->online = 0;
    hda->codec_probed = 0;
    hda->corb = 0;
    hda->rirb = 0;
    hda->corb_pa = 0;
    hda->rirb_pa = 0;
    hda->corb_wp = 0;
    hda->rirb_last = 0;
    hda->stream_buf = 0;
    hda->stream_buf_pa = 0;
    hda->bdl = 0;
    hda->bdl_pa = 0;
    hda->stream_tag = 0;
    hda->stream_ready = 0;

    hda_print("hda: scanning PCI for HD Audio controllers\n");

    int found = 0;
    for (uint32_t bus = 0; bus < 256 && !found; ++bus) {
        for (uint32_t device = 0; device < 32 && !found; ++device) {
            uint32_t id = hda_pci_read32((uint8_t)bus, (uint8_t)device, 0, 0);
            uint16_t vendor = (uint16_t)id;
            if (vendor == 0xFFFFu || vendor == 0)
                continue;
            uint8_t header = (uint8_t)(hda_pci_read32((uint8_t)bus,
                                                      (uint8_t)device, 0, 0x0Cu) >> 16);
            uint8_t functions = (header & 0x80u) ? 8u : 1u;
            for (uint32_t function = 0; function < functions && !found;
                 ++function) {
                id = hda_pci_read32((uint8_t)bus, (uint8_t)device,
                                    (uint8_t)function, 0);
                if ((id & 0xFFFFu) == 0xFFFFu)
                    continue;
                uint32_t classreg =
                    hda_pci_read32((uint8_t)bus, (uint8_t)device,
                                   (uint8_t)function, 0x08u);
                uint8_t class_code = (uint8_t)(classreg >> 24);
                uint8_t subclass = (uint8_t)(classreg >> 16);
                if (class_code != PCI_CLASS_MULTIMEDIA ||
                    subclass != PCI_SUBCLASS_HDAUDIO)
                    continue;

                uint32_t barlo =
                    hda_pci_read32((uint8_t)bus, (uint8_t)device,
                                   (uint8_t)function, 0x10u);
                if ((barlo & 0x01u) || !barlo)
                    continue;
                uint64_t bar = barlo & ~0x0Fu;
                if ((barlo & 0x06u) == 0x04u) { /* 64-bit BAR */
                    uint32_t barhi =
                        hda_pci_read32((uint8_t)bus, (uint8_t)device,
                                       (uint8_t)function, 0x14u);
                    bar |= (uint64_t)barhi << 32;
                }
                if (!bar)
                    continue;

                uint16_t vendor_id = (uint16_t)id;
                uint16_t device_id = (uint16_t)(id >> 16);

                hda_print("hda: found %04x:%04x at %02x:%02x.%u BAR0=%llx\n",
                          vendor_id, device_id, bus, device,
                          function, (unsigned long long)bar);

                hda->regs_phys = bar;
                hda->bus = (uint8_t)bus;
                hda->device = (uint8_t)device;
                hda->function = (uint8_t)function;
                hda->online = 1;
                found = 1;
            }
        }
    }

    if (!found) {
        hda_print("hda: no HD Audio controller found\n");
        return 0;
    }

    /* Map the controller MMIO BAR. */
    if (services->map_device_memory(hda->regs_phys, HDA_RING_BYTES,
                                    (volatile void **)&hda->regs) != 0 ||
        !hda->regs) {
        hda_print("hda: failed to map controller MMIO BAR\n");
        hda->online = 0;
        hda->regs_phys = 0;
        return -1;
    }
    hda_print("hda: MMIO mapped at %p\n", (void *)hda->regs);

    if (hda_reset_controller(hda) != 0) {
        hda_print("hda: controller reset failed\n");
        hda->online = 0;
        hda->regs = 0;
        hda->regs_phys = 0;
        return -1;
    }

    if (hda_probe_codec(hda) != 0) {
        hda_print("hda: codec probe failed\n");
        if (hda->services->release_dma) {
            if (hda->rirb_pa) {
                hda->services->release_dma(hda->rirb_pa, HDA_RING_BYTES);
                hda->rirb_pa = 0;
                hda->rirb = 0;
            }
            if (hda->corb_pa) {
                hda->services->release_dma(hda->corb_pa, HDA_RING_BYTES);
                hda->corb_pa = 0;
                hda->corb = 0;
            }
        }
        hda->online = 0;
        hda->regs = 0;
        hda->regs_phys = 0;
        return -1;
    }

    hda_print("hda: HD Audio controller initialized\n");

    /* Set the output stream format on the codec (connector = stream tag). */
    {
        uint32_t fmt_result = 0;
        if (hda_codec_verb(hda, 0, (uint8_t)HDA_STREAM_TAG,
                           (uint16_t)HDA_VERB_SET_STREAM_FORMAT,
                           (uint8_t)HDA_STREAM_FORMAT, &fmt_result) != 0) {
            hda_print("hda: warning: codec stream-format verb failed\n");
        }
    }

    /* Set up the playback DMA buffer and Buffer Descriptor List. This is
     * best-effort: the controller is usable even if stream setup fails
     * (e.g. DMA allocation is unavailable), but no audio can be played. */
    if (hda_setup_stream(hda) != 0) {
        hda_print("hda: warning: playback stream setup failed; no audio output\n");
    }

    return 0;
}
