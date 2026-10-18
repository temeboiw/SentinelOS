#pragma once
#include "serial.h"

#define VI_EV_KEY 1
#define VI_EV_ABS 3
#define VI_ABS_X  0
#define VI_ABS_Y  1
#define VI_BTN_LEFT 0x110

struct vi_event { uint16_t type, code; int32_t value; };
struct vi_desc { uint64_t addr; uint32_t len; uint16_t flags, next; };
struct vi_avail { uint16_t flags, idx; uint16_t ring[64]; };
struct vi_used_elem { uint32_t id, len; };
struct vi_used { uint16_t flags, idx; struct vi_used_elem ring[64]; };

static int vi_ok, vi_x = 640, vi_y = 400, vi_btn, vi_moved, vi_clicked, vi_evt_count;
static volatile uint8_t *vi_common, *vi_notify;
static uint32_t vi_notify_mult;
static uint16_t vi_qsize, vi_notify_off, vi_last_used;
static struct vi_desc *vi_desc;
static struct vi_avail *vi_avail;
static struct vi_used *vi_used;
static struct vi_event *vi_ev;

static inline void vi_outl(uint16_t p, uint32_t v) {
    __asm__ volatile("outl %0, %1" :: "a"(v), "Nd"(p));
}
static inline uint32_t vi_inl(uint16_t p) {
    uint32_t v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v;
}
static uint32_t vi_cfg(uint8_t slot, uint8_t off) {
    vi_outl(0xCF8, (1u << 31) | ((uint32_t)slot << 11) | (off & 0xFC));
    return vi_inl(0xCFC);
}
static void vi_cfgw(uint8_t slot, uint8_t off, uint32_t val) {
    vi_outl(0xCF8, (1u << 31) | ((uint32_t)slot << 11) | (off & 0xFC));
    vi_outl(0xCFC, val);
}
static uint8_t vi_cfg8(uint8_t slot, uint8_t off) {
    return (uint8_t)(vi_cfg(slot, off & 0xFC) >> ((off & 3) * 8));
}
static uint64_t vi_bar(uint8_t slot, uint8_t bar) {
    uint32_t lo = vi_cfg(slot, 0x10 + bar * 4);
    if (lo & 1) return 0;
    uint64_t a = lo & ~0xFull;
    if ((lo & 4) == 4) a |= (uint64_t)vi_cfg(slot, 0x10 + bar * 4 + 4) << 32;
    return a;
}
static void vi_map(uint64_t pa) {
    map_4k(pa & ~0xFFFULL, pa & ~0xFFFULL, PTE_RW);
}
static void vi_kick(void) {
    if (!vi_notify) return;
    *(volatile uint16_t *)(vi_notify + (uint32_t)vi_notify_off * vi_notify_mult) = 0;
}

static int virtio_input_init(void) {
    for (uint8_t slot = 0; slot < 32; slot++) {
        uint32_t id = vi_cfg(slot, 0);
        if ((id & 0xFFFF) != 0x1AF4) continue;
        uint16_t did = (uint16_t)(id >> 16);
        if (did != 0x1052 && did != 0x1005) continue;

        vi_cfgw(slot, 0x04, vi_cfg(slot, 0x04) | 0x06);

        volatile uint8_t *common = 0, *notify = 0;
        uint32_t nmult = 4;
        uint8_t cap = vi_cfg8(slot, 0x34);
        while (cap && cap != 0xFF) {
            uint8_t next = vi_cfg8(slot, cap + 1);
            if (vi_cfg8(slot, cap) == 0x09) {
                uint8_t typ = vi_cfg8(slot, cap + 3);
                uint8_t bar = vi_cfg8(slot, cap + 4);
                uint32_t off =
                    (uint32_t)vi_cfg8(slot, cap + 8) |
                    ((uint32_t)vi_cfg8(slot, cap + 9) << 8) |
                    ((uint32_t)vi_cfg8(slot, cap + 10) << 16) |
                    ((uint32_t)vi_cfg8(slot, cap + 11) << 24);
                uint64_t base = vi_bar(slot, bar);
                if (base) {
                    vi_map(base + off);
                    volatile uint8_t *p = (volatile uint8_t *)(HHDM_BASE + base + off);
                    if (typ == 1) common = p;
                    if (typ == 2) {
                        notify = p;
                        nmult =
                            (uint32_t)vi_cfg8(slot, cap + 16) |
                            ((uint32_t)vi_cfg8(slot, cap + 17) << 8) |
                            ((uint32_t)vi_cfg8(slot, cap + 18) << 16) |
                            ((uint32_t)vi_cfg8(slot, cap + 19) << 24);
                        if (!nmult) nmult = 4;
                    }
                }
            }
            cap = next;
        }
        if (!common) continue;

        vi_common = common;
        vi_notify = notify;
        vi_notify_mult = nmult;

        *(volatile uint8_t *)(common + 20) = 0;
        for (volatile int t = 0; t < 100000; t++) {}
        *(volatile uint8_t *)(common + 20) = 1;
        *(volatile uint8_t *)(common + 20) = 3;
        *(volatile uint32_t *)(common + 4) = 0;
        (void)*(volatile uint32_t *)(common + 0);
        *(volatile uint32_t *)(common + 8) = 0;
        *(volatile uint8_t *)(common + 20) = 11;
        if (!(*(volatile uint8_t *)(common + 20) & 8)) continue;

        *(volatile uint16_t *)(common + 22) = 0;
        uint16_t qsz = *(volatile uint16_t *)(common + 24);
        if (qsz > 64) qsz = 64;
        if (qsz < 4) continue;
        vi_qsize = qsz;
        vi_notify_off = *(volatile uint16_t *)(common + 30);

        uint64_t dpa = pmm_alloc_zeroed();
        uint64_t apa = pmm_alloc_zeroed();
        uint64_t upa = pmm_alloc_zeroed();
        uint64_t epa = pmm_alloc_zeroed();
        if (!dpa || !apa || !upa || !epa) continue;

        vi_desc = (struct vi_desc *)(HHDM_BASE + dpa);
        vi_avail = (struct vi_avail *)(HHDM_BASE + apa);
        vi_used = (struct vi_used *)(HHDM_BASE + upa);
        vi_ev = (struct vi_event *)(HHDM_BASE + epa);

        for (uint16_t i = 0; i < qsz; i++) {
            vi_desc[i].addr = epa + (uint64_t)i * sizeof(struct vi_event);
            vi_desc[i].len = (uint32_t)sizeof(struct vi_event);
            vi_desc[i].flags = 2;
            vi_desc[i].next = 0;
            vi_avail->ring[i] = i;
        }
        __asm__ volatile("mfence" ::: "memory");
        vi_avail->idx = qsz;
        vi_last_used = 0;

        *(volatile uint16_t *)(common + 22) = 0;
        *(volatile uint16_t *)(common + 26) = qsz;
        *(volatile uint64_t *)(common + 32) = dpa;
        *(volatile uint64_t *)(common + 40) = apa;
        *(volatile uint64_t *)(common + 48) = upa;
        __asm__ volatile("mfence" ::: "memory");
        *(volatile uint16_t *)(common + 28) = 1;
        *(volatile uint8_t *)(common + 20) = 15;
        vi_kick();

        vi_ok = 1;
        DbgPrint("virtio-tablet: ready slot=%u qsz=%u notify_off=%u\n",
                 slot, qsz, vi_notify_off);
        return 1;
    }
    DbgPrint("virtio-tablet: not found\n");
    return 0;
}

static void virtio_input_poll(void) {
    if (!vi_ok || !vi_used) return;
    __asm__ volatile("mfence" ::: "memory");
    uint16_t idx = vi_used->idx;
    while (vi_last_used != idx) {
        uint16_t s = vi_last_used % vi_qsize;
        uint32_t id = vi_used->ring[s].id;
        if (id < vi_qsize) {
            struct vi_event ev = vi_ev[id];
            if (ev.type == VI_EV_ABS) {
                int v = ev.value;
                if (v < 0) v = 0;
                if (v > 32767) v = 32767;
                if (ev.code == VI_ABS_X) { vi_x = (v * 1279) / 32767; vi_moved = 1; }
                if (ev.code == VI_ABS_Y) { vi_y = (v * 799) / 32767; vi_moved = 1; }
            } else if (ev.type == VI_EV_KEY && ev.code == VI_BTN_LEFT) {
                if (ev.value) { vi_btn |= 1; vi_clicked = 1; }
                else vi_btn &= ~1;
                vi_moved = 1;
            }
            uint16_t ai = vi_avail->idx;
            vi_avail->ring[ai % vi_qsize] = (uint16_t)id;
            __asm__ volatile("mfence" ::: "memory");
            vi_avail->idx = (uint16_t)(ai + 1);
            vi_evt_count++;
            if (vi_evt_count == 1)
                DbgPrint("virtio-tablet: first event t=%u c=%u v=%d\n",
                         ev.type, ev.code, (int)ev.value);
        }
        vi_last_used++;
    }
    vi_kick();
}
