#pragma once
/* Minimal modern virtio-blk-pci – read sectors. QEMU: -device virtio-blk-pci */
#include "serial.h"

static inline void pci_outl(uint16_t p, uint32_t v) {
    __asm__ volatile("outl %0, %1" :: "a"(v), "Nd"(p));
}
static inline uint32_t pci_inl(uint16_t p) {
    uint32_t v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v;
}
static uint32_t pci_cfg_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    pci_outl(0xCF8, (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
             ((uint32_t)func << 8) | (off & 0xFC));
    return pci_inl(0xCFC);
}
static void pci_cfg_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t val) {
    pci_outl(0xCF8, (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
             ((uint32_t)func << 8) | (off & 0xFC));
    pci_outl(0xCFC, val);
}
static uint8_t pci_cfg_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    uint32_t v = pci_cfg_read(bus, slot, func, off & 0xFC);
    return (uint8_t)(v >> ((off & 3) * 8));
}

#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_DEVICE_CFG 4
#define VIRTQ_DESC_F_NEXT  1
#define VIRTQ_DESC_F_WRITE 2
#define VIRTIO_BLK_T_IN    0

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};
struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[16];
};
struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
};
struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[16];
};
struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

static int vb_ok;
static volatile uint8_t *vb_common;
static volatile uint8_t *vb_notify;
static uint32_t vb_notify_mult;
static uint16_t vb_qsize;

static uint64_t vb_desc_pa, vb_avail_pa, vb_used_pa, vb_req_pa, vb_data_pa, vb_status_pa;
static struct virtq_desc *vb_desc;
static struct virtq_avail *vb_avail;
static struct virtq_used *vb_used;
static struct virtio_blk_req *vb_req;
static uint8_t *vb_data;
static volatile uint8_t *vb_status;
static uint16_t vb_avail_idx;
static uint16_t vb_last_used;

static void *vb_map_bar(uint64_t pa, uint64_t len) {
    for (uint64_t o = 0; o < len; o += 0x1000) {
        uint64_t phys = pa + o;
        /* Reuse an existing identity-correct HHDM mapping. Device BARs may
           share a 2 MiB page with another device, so map_4k would panic when
           that large page is already present. */
        if (vmm_translate(HHDM_BASE + phys) != phys)
            map_4k(HHDM_BASE + phys, phys, PTE_RW | PTE_PCD | PTE_PWT);
    }
    return (void *)(HHDM_BASE + pa);
}

static uint64_t vb_bar_addr(uint8_t bus, uint8_t slot, uint8_t bar) {
    uint32_t lo = pci_cfg_read(bus, slot, 0, 0x10 + bar * 4);
    if (lo & 1) return 0;
    uint64_t pa = lo & ~0xFull;
    if ((lo & 0x6) == 0x4) {
        uint32_t hi = pci_cfg_read(bus, slot, 0, 0x10 + bar * 4 + 4);
        pa |= (uint64_t)hi << 32;
    }
    return pa;
}

static int virtio_blk_init(void) {
    for (uint8_t slot = 0; slot < 32; slot++) {
        uint32_t id = pci_cfg_read(0, slot, 0, 0);
        uint16_t vend = (uint16_t)(id & 0xFFFF);
        uint16_t did = (uint16_t)(id >> 16);
        if (vend != 0x1AF4) continue;
        if (did != 0x1042 && did != 0x1001) continue;

        uint32_t cmd = pci_cfg_read(0, slot, 0, 0x04);
        pci_cfg_write(0, slot, 0, 0x04, cmd | 0x06);

        uint8_t cap = pci_cfg_read8(0, slot, 0, 0x34);
        volatile uint8_t *common = 0, *notify = 0;
        uint32_t notify_mult = 0;
        while (cap && cap != 0xFF) {
            uint8_t cid = pci_cfg_read8(0, slot, 0, cap);
            uint8_t next = pci_cfg_read8(0, slot, 0, cap + 1);
            if (cid == 0x09) {
                uint8_t typ = pci_cfg_read8(0, slot, 0, cap + 3);
                uint8_t bar = pci_cfg_read8(0, slot, 0, cap + 4);
                uint32_t off =
                    (uint32_t)pci_cfg_read8(0, slot, 0, cap + 8) |
                    ((uint32_t)pci_cfg_read8(0, slot, 0, cap + 9) << 8) |
                    ((uint32_t)pci_cfg_read8(0, slot, 0, cap + 10) << 16) |
                    ((uint32_t)pci_cfg_read8(0, slot, 0, cap + 11) << 24);
                uint64_t bar_pa = vb_bar_addr(0, slot, bar);
                if (bar_pa) {
                    void *base = vb_map_bar(bar_pa, 0x10000);
                    volatile uint8_t *p = (volatile uint8_t *)base + off;
                    if (typ == VIRTIO_PCI_CAP_COMMON_CFG) common = p;
                    else if (typ == VIRTIO_PCI_CAP_NOTIFY_CFG) {
                        notify = p;
                        notify_mult =
                            (uint32_t)pci_cfg_read8(0, slot, 0, cap + 16) |
                            ((uint32_t)pci_cfg_read8(0, slot, 0, cap + 17) << 8) |
                            ((uint32_t)pci_cfg_read8(0, slot, 0, cap + 18) << 16) |
                            ((uint32_t)pci_cfg_read8(0, slot, 0, cap + 19) << 24);
                    }
                }
            }
            cap = next;
        }

        if (!common) {
            DbgPrint("virtio-blk: no common cfg (slot %u)\n", slot);
            continue;
        }

        common[20] = 0;
        common[20] = 1;
        common[20] = 3;
        *(volatile uint32_t *)(common + 8) = 0;
        *(volatile uint32_t *)(common + 12) = 0;
        common[20] = 11;
        if (!(common[20] & 8)) {
            DbgPrint("virtio-blk: FEATURES_OK rejected\n");
            continue;
        }

        *(volatile uint16_t *)(common + 22) = 0;
        uint16_t qsz = *(volatile uint16_t *)(common + 24);
        if (qsz > 16) qsz = 16;
        if (qsz < 4) {
            DbgPrint("virtio-blk: bad qsz %u\n", qsz);
            continue;
        }
        *(volatile uint16_t *)(common + 24) = qsz;
        vb_qsize = qsz;

        vb_desc_pa = pmm_alloc_zeroed();
        vb_avail_pa = pmm_alloc_zeroed();
        vb_used_pa = pmm_alloc_zeroed();
        vb_req_pa = pmm_alloc_zeroed();
        if (!vb_desc_pa || !vb_avail_pa || !vb_used_pa || !vb_req_pa) {
            DbgPrint("virtio-blk: OOM\n");
            continue;
        }
        vb_desc = (struct virtq_desc *)(HHDM_BASE + vb_desc_pa);
        vb_avail = (struct virtq_avail *)(HHDM_BASE + vb_avail_pa);
        vb_used = (struct virtq_used *)(HHDM_BASE + vb_used_pa);
        vb_req = (struct virtio_blk_req *)(HHDM_BASE + vb_req_pa);
        vb_data = (uint8_t *)(HHDM_BASE + vb_req_pa + 64);
        vb_status = (volatile uint8_t *)(HHDM_BASE + vb_req_pa + 64 + 512);
        vb_data_pa = vb_req_pa + 64;
        vb_status_pa = vb_req_pa + 64 + 512;

        *(volatile uint64_t *)(common + 32) = vb_desc_pa;
        *(volatile uint64_t *)(common + 40) = vb_avail_pa;
        *(volatile uint64_t *)(common + 48) = vb_used_pa;
        *(volatile uint16_t *)(common + 28) = 1;

        common[20] = 15;

        vb_common = common;
        vb_notify = notify;
        vb_notify_mult = notify_mult ? notify_mult : 1;
        vb_avail_idx = 0;
        vb_last_used = vb_used->idx;
        vb_ok = 1;
        DbgPrint("virtio-blk: ready slot=%u qsz=%u\n", slot, qsz);
        return 0;
    }
    DbgPrint("virtio-blk: no device\n");
    return -1;
}

static int virtio_blk_read_sector(uint64_t sector, void *buf512) {
    if (!vb_ok) return -1;
    vb_req->type = VIRTIO_BLK_T_IN;
    vb_req->reserved = 0;
    vb_req->sector = sector;
    *vb_status = 0xFF;

    vb_desc[0].addr = vb_req_pa;
    vb_desc[0].len = 16;
    vb_desc[0].flags = VIRTQ_DESC_F_NEXT;
    vb_desc[0].next = 1;
    vb_desc[1].addr = vb_data_pa;
    vb_desc[1].len = 512;
    vb_desc[1].flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;
    vb_desc[1].next = 2;
    vb_desc[2].addr = vb_status_pa;
    vb_desc[2].len = 1;
    vb_desc[2].flags = VIRTQ_DESC_F_WRITE;
    vb_desc[2].next = 0;

    uint16_t a = vb_avail_idx;
    vb_avail->ring[a % vb_qsize] = 0;
    __asm__ volatile("sfence" ::: "memory");
    vb_avail->idx = ++vb_avail_idx;
    __asm__ volatile("sfence" ::: "memory");

    if (vb_notify && vb_common) {
        uint16_t noff = *(volatile uint16_t *)(vb_common + 30);
        *(volatile uint16_t *)(vb_notify + (uint32_t)noff * vb_notify_mult) = 0;
    }

    for (int i = 0; i < 10000000; i++) {
        if (vb_used->idx != vb_last_used) {
            vb_last_used = vb_used->idx;
            break;
        }
        __asm__ volatile("pause");
    }
    if (*vb_status != 0) return -1;
    uint8_t *out = (uint8_t *)buf512;
    for (int i = 0; i < 512; i++) out[i] = vb_data[i];
    return 0;
}

static int blk_read(uint64_t off, void *dst, uint64_t len) {
    uint8_t sec[512];
    uint8_t *out = (uint8_t *)dst;
    while (len) {
        uint64_t lba = off / 512;
        uint32_t skip = (uint32_t)(off % 512);
        uint32_t n = 512 - skip;
        if (n > len) n = (uint32_t)len;
        if (virtio_blk_read_sector(lba, sec) < 0) return -1;
        for (uint32_t i = 0; i < n; i++) out[i] = sec[skip + i];
        out += n; off += n; len -= n;
    }
    return 0;
}

static int blk_read_lba(uint32_t lba, uint8_t *buf) {
    return virtio_blk_read_sector(lba, buf);
}
