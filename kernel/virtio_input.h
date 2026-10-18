#pragma once
/* Minimal virtio-input tablet (absolute pointer) – like Windows USB HID tablet.
 * QEMU: -device virtio-tablet-pci
 */
#include "serial.h"

#define VI_EV_SYN 0
#define VI_EV_KEY 1
#define VI_EV_ABS 3
#define VI_ABS_X  0
#define VI_ABS_Y  1
#define VI_BTN_LEFT 0x110

struct vi_event {
    uint16_t type;
    uint16_t code;
    int32_t  value;
};

struct vi_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};
struct vi_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[64];
};
struct vi_used_elem {
    uint32_t id;
    uint32_t len;
};
struct vi_used {
    uint16_t flags;
    uint16_t idx;
    struct vi_used_elem ring[64];
};

static int vi_ok;
static int vi_x = 640, vi_y = 400, vi_btn;
static int vi_have; /* 1 = move/sync pending, 3 = click */

static volatile uint8_t *vi_common;
static volatile uint8_t *vi_notify;
static uint32_t vi_notify_mult;
static uint16_t vi_qsize;

static uint64_t vi_desc_pa, vi_avail_pa, vi_used_pa, vi_ev_pa;
static struct vi_desc *vi_desc;
static struct vi_avail *vi_avail;
static struct vi_used *vi_used;
static struct vi_event *vi_ev;
static uint16_t vi_last_used;

static inline void vi_pci_outl(uint16_t p, uint32_t v) {
    __asm__ volatile("outl %0, %1" :: "a"(v), "Nd"(p));
}
static inline uint32_t vi_pci_inl(uint16_t p) {
    uint32_t v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v;
}
static uint32_t vi_cfg_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    vi_pci_outl(0xCF8, (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                ((uint32_t)func << 8) | (off & 0xFC));
    return vi_pci_inl(0xCFC);
}
static void vi_cfg_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t val) {
    vi_pci_outl(0xCF8, (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                ((uint32_t)func << 8) | (off & 0xFC));
    vi_pci_outl(0xCFC, val);
}
static uint8_t vi_cfg_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    uint32_t v = vi_cfg_read(bus, slot, func, off & 0xFC);
    return (uint8_t)(v >> ((off & 3) * 8));
}

static uint64_t vi_bar_addr(uint8_t bus, uint8_t slot, uint8_t bar) {
    uint32_t lo = vi_cfg_read(bus, slot, 0, 0x10 + bar * 4);
    if ((lo & 1) != 0) return 0;
    uint64_t a = lo & ~0xFull;
    if ((lo & 4) == 4) {
        uint32_t hi = vi_cfg_read(bus, slot, 0, 0x10 + bar * 4 + 4);
        a |= (uint64_t)hi << 32;
    }
    return a;
}

static int virtio_input_init(void) {
    for (uint8_t slot = 0; slot < 32; slot++) {
        uint32_t id = vi_cfg_read(0, slot, 0, 0);
        if ((id & 0xFFFF) != 0x1AF4) continue;
        uint16_t did = (uint16_t)(id >> 16);
        /* virtio-input: modern 0x1052, transitional varies – accept 0x1005/0x1052 */
        if (did != 0x1052 && did != 0x1005 && did != 0x1040) continue;

        uint32_t cmd = vi_cfg_read(0, slot, 0, 0x04);
        vi_cfg_write(0, slot, 0, 0x04, cmd | 0x06);

        volatile uint8_t *common = 0, *notify = 0;
        uint32_t notify_mult = 0;
        uint8_t cap = vi_cfg_read8(0, slot, 0, 0x34);
        while (cap && cap != 0xFF) {
            uint8_t cid = vi_cfg_read8(0, slot, 0, cap);
            uint8_t next = vi_cfg_read8(0, slot, 0, cap + 1);
            if (cid == 0x09) {
                uint8_t typ = vi_cfg_read8(0, slot, 0, cap + 3);
                uint8_t bar = vi_cfg_read8(0, slot, 0, cap + 4);
                uint32_t off =
                    (uint32_t)vi_cfg_read8(0, slot, 0, cap + 8) |
                    ((uint32_t)vi_cfg_read8(0, slot, 0, cap + 9) << 8) |
                    ((uint32_t)vi_cfg_read8(0, slot, 0, cap + 10) << 16) |
                    ((uint32_t)vi_cfg_read8(0, slot, 0, cap + 11) << 24);
                uint64_t base = vi_bar_addr(0, slot, bar);
                if (base) {
                    map_4k((base + off) & ~0xFFFULL, (base + off) & ~0xFFFULL, PTE_RW);
                    volatile uint8_t *p = (volatile uint8_t *)(HHDM_BASE + base + off);
                    if (typ == 1) common = p;
                    if (typ == 2) {
                        notify = p;
                        notify_mult =
                            (uint32_t)vi_cfg_read8(0, slot, 0, cap + 16) |
                            ((uint32_t)vi_cfg_read8(0, slot, 0, cap + 17) << 8) |
                            ((uint32_t)vi_cfg_read8(0, slot, 0, cap + 18) << 16) |
                            ((uint32_t)vi_cfg_read8(0, slot, 0, cap + 19) << 24);
                    }
                }
            }
            cap = next;
        }
        if (!common) continue;

        vi_common = common;
        vi_notify = notify;
        vi_notify_mult = notify_mult ? notify_mult : 4;

        /* reset + ack + driver */
        *(volatile uint8_t *)(common + 20) = 0;
        for (volatile int i = 0; i < 10000; i++) {}
        *(volatile uint8_t *)(common + 20) = 1; /* ACK */
        *(volatile uint8_t *)(common + 20) = 3; /* DRIVER */

        /* features */
        *(volatile uint32_t *)(common + 4) = 0;
        uint32_t f0 = *(volatile uint32_t *)(common + 0);
        (void)f0;
        *(volatile uint32_t *)(common + 8) = 0;
        *(volatile uint8_t *)(common + 20) = 3 | 8; /* FEATURES_OK */
        if (!(*(volatile uint8_t *)(common + 20) & 8)) {
            DbgPrint("virtio-tablet: FEATURES_OK rejected\n");
            continue;
        }

        /* queue 0 – eventq */
        *(volatile uint16_t *)(common + 22) = 0;
        uint16_t qsz = *(volatile uint16_t *)(common + 24);
        if (qsz < 4 || qsz > 64) qsz = 16;
        vi_qsize = qsz;

        uint64_t desc_pa = pmm_alloc_zeroed();
        uint64_t avail_pa = pmm_alloc_zeroed();
        uint64_t used_pa = pmm_alloc_zeroed();
        uint64_t ev_pa = pmm_alloc_zeroed();
        if (!desc_pa || !avail_pa || !used_pa || !ev_pa) continue;

        vi_desc_pa = desc_pa; vi_avail_pa = avail_pa; vi_used_pa = used_pa; vi_ev_pa = ev_pa;
        vi_desc = (struct vi_desc *)(HHDM_BASE + desc_pa);
        vi_avail = (struct vi_avail *)(HHDM_BASE + avail_pa);
        vi_used = (struct vi_used *)(HHDM_BASE + used_pa);
        vi_ev = (struct vi_event *)(HHDM_BASE + ev_pa);

        for (uint16_t i = 0; i < qsz; i++) {
            vi_desc[i].addr = ev_pa + i * sizeof(struct vi_event);
            vi_desc[i].len = sizeof(struct vi_event);
            vi_desc[i].flags = 2; /* WRITE */
            vi_desc[i].next = 0;
            vi_avail->ring[i] = i;
        }
        vi_avail->idx = qsz;
        vi_last_used = 0;

        *(volatile uint16_t *)(common + 22) = 0;
        *(volatile uint16_t *)(common + 26) = qsz;
        *(volatile uint64_t *)(common + 32) = desc_pa;
        *(volatile uint64_t *)(common + 40) = avail_pa;
        *(volatile uint64_t *)(common + 48) = used_pa;
        *(volatile uint16_t *)(common + 28) = 1; /* enable */

        *(volatile uint8_t *)(common + 20) = 3 | 8 | 4; /* DRIVER_OK */

        if (vi_notify)
            *(volatile uint16_t *)(vi_notify + 0 * vi_notify_mult) = 0;

        vi_ok = 1;
        DbgPrint("virtio-tablet: ready slot=%u qsz=%u\n", slot, qsz);
        return 1;
    }
    DbgPrint("virtio-tablet: not found (ok – PS/2 only)\n");
    return 0;
}

static void virtio_input_poll(void) {
    if (!vi_ok || !vi_used) return;
    uint16_t idx = vi_used->idx;
    while (vi_last_used != idx) {
        uint16_t i = vi_last_used % vi_qsize;
        uint32_t id = vi_used->ring[i].id;
        if (id < vi_qsize) {
            struct vi_event *ev = &vi_ev[id];
            if (ev->type == VI_EV_ABS) {
                if (ev->code == VI_ABS_X) {
                    /* QEMU tablet 0..32767 */
                    vi_x = (int)((ev->value * 1280) / 32768);
                    if (vi_x < 0) vi_x = 0;
                    if (vi_x > 1279) vi_x = 1279;
                    vi_have = 1;
                } else if (ev->code == VI_ABS_Y) {
                    vi_y = (int)((ev->value * 800) / 32768);
                    if (vi_y < 0) vi_y = 0;
                    if (vi_y > 799) vi_y = 799;
                    vi_have = 1;
                }
            } else if (ev->type == VI_EV_KEY && ev->code == VI_BTN_LEFT) {
                if (ev->value) {
                    vi_btn |= 1;
                    vi_have = 3;
                } else {
                    vi_btn &= ~1;
                    vi_have = 1;
                }
            }
            /* re-post descriptor */
            uint16_t ai = vi_avail->idx;
            vi_avail->ring[ai % vi_qsize] = (uint16_t)id;
            vi_avail->idx = (uint16_t)(ai + 1);
        }
        vi_last_used++;
    }
    if (vi_notify)
        *(volatile uint16_t *)(vi_notify + 0 * vi_notify_mult) = 0;
}
