#pragma once
/* Minimal polling Intel E1000 driver for QEMU's emulated e1000 NIC.
 * DMA descriptors and packet buffers are physical-memory backed.
 * This is the hardware/link foundation; IP protocols are layered above it.
 */
#include <stdint.h>
#include "serial.h"
#include "pmm.h"
#include "vmm.h"

#define NET_RX_COUNT 32
#define NET_TX_COUNT 8
#define NET_BUF_SIZE 2048

typedef struct __attribute__((packed, aligned(16))) {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} E1000RxDesc;

typedef struct __attribute__((packed, aligned(16))) {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} E1000TxDesc;

static volatile uint32_t *net_regs;
static int net_ready;
static uint8_t net_mac[6];
static uint64_t net_rx_ring_pa, net_tx_ring_pa;
static E1000RxDesc *net_rx_ring;
static E1000TxDesc *net_tx_ring;
static uint64_t net_rx_buf_pa[NET_RX_COUNT], net_tx_buf_pa[NET_TX_COUNT];
static uint8_t *net_rx_buf[NET_RX_COUNT], *net_tx_buf[NET_TX_COUNT];
static uint32_t net_rx_next, net_tx_next;
static uint64_t net_rx_packets, net_tx_packets;

#define E1000_REG_CTRL   0x0000
#define E1000_REG_STATUS 0x0008
#define E1000_REG_EERD   0x0014
#define E1000_REG_ICR    0x00C0
#define E1000_REG_IMS    0x00D0
#define E1000_REG_RCTL   0x0100
#define E1000_REG_TCTL   0x0400
#define E1000_REG_TIPG   0x0410
#define E1000_REG_RDBAL  0x2800
#define E1000_REG_RDBAH  0x2804
#define E1000_REG_RDLEN  0x2808
#define E1000_REG_RDH    0x2810
#define E1000_REG_RDT    0x2818
#define E1000_REG_TDBAL  0x3800
#define E1000_REG_TDBAH  0x3804
#define E1000_REG_TDLEN  0x3808
#define E1000_REG_TDH    0x3810
#define E1000_REG_TDT    0x3818
#define E1000_REG_RAL    0x5400
#define E1000_REG_RAH    0x5404

static inline uint32_t net_rd(uint32_t reg) { return net_regs[reg / 4]; }
static inline void net_wr(uint32_t reg, uint32_t val) { net_regs[reg / 4] = val; }
static inline void net_outl(uint16_t p, uint32_t v) {
    __asm__ volatile("outl %0, %1" :: "a"(v), "Nd"(p));
}
static inline uint32_t net_inl(uint16_t p) {
    uint32_t v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v;
}
static uint32_t net_pci_read(uint8_t slot, uint8_t off) {
    net_outl(0xCF8, (1u<<31) | ((uint32_t)slot<<11) | (off & 0xFC));
    return net_inl(0xCFC);
}
static void net_pci_write(uint8_t slot, uint8_t off, uint32_t val) {
    net_outl(0xCF8, (1u<<31) | ((uint32_t)slot<<11) | (off & 0xFC));
    net_outl(0xCFC, val);
}

static int net_e1000_init(void) {
    net_ready = 0;
    uint8_t slot = 0xFF;
    uint32_t bar = 0;
    for (uint8_t s = 0; s < 32; s++) {
        uint32_t id = net_pci_read(s, 0);
        uint16_t vendor = (uint16_t)id;
        uint16_t device = (uint16_t)(id >> 16);
        if (vendor == 0x8086 &&
            (device == 0x100E || device == 0x100F ||
             device == 0x10D3 || device == 0x153A)) {
            slot = s;
            bar = net_pci_read(s, 0x10);
            break;
        }
    }
    if (slot == 0xFF || (bar & 1) || !(bar & ~0xFULL)) {
        DbgPrint("net: e1000 PCI device not found\n");
        return -1;
    }

    uint64_t mmio = (uint64_t)(bar & ~0xFULL);
    uint32_t cmd = net_pci_read(slot, 0x04);
    net_pci_write(slot, 0x04, cmd | 0x06); /* memory space + bus mastering */
    for (uint64_t off = 0; off < 0x20000; off += 0x1000)
        map_4k(HHDM_BASE + mmio + off, mmio + off, PTE_RW | PTE_PCD | PTE_PWT);
    net_regs = (volatile uint32_t *)(HHDM_BASE + mmio);

    net_wr(E1000_REG_CTRL, net_rd(E1000_REG_CTRL) | (1u << 26));
    for (volatile uint32_t wait = 0; wait < 100000; wait++) { }
    net_wr(E1000_REG_CTRL, 0);
    net_wr(E1000_REG_IMS, 0xFFFFFFFF);
    (void)net_rd(E1000_REG_ICR);

    uint32_t ral = net_rd(E1000_REG_RAL), rah = net_rd(E1000_REG_RAH);
    for (int i = 0; i < 4; i++) net_mac[i] = (uint8_t)(ral >> (i * 8));
    net_mac[4] = (uint8_t)rah;
    net_mac[5] = (uint8_t)(rah >> 8);
    if (!(rah & (1u << 31))) {
        /* QEMU usually exposes the MAC in RAL/RAH. No EEPROM fallback yet. */
        DbgPrint("net: E1000 reports no valid MAC address\n");
        return -1;
    }

    net_rx_ring_pa = pmm_alloc_n(1);
    net_tx_ring_pa = pmm_alloc_n(1);
    if (!net_rx_ring_pa || !net_tx_ring_pa) {
        DbgPrint("net: cannot allocate descriptor rings\n");
        return -1;
    }
    net_rx_ring = (E1000RxDesc *)(HHDM_BASE + net_rx_ring_pa);
    net_tx_ring = (E1000TxDesc *)(HHDM_BASE + net_tx_ring_pa);
    for (int i = 0; i < NET_RX_COUNT; i++) {
        net_rx_buf_pa[i] = pmm_alloc_n(1);
        if (!net_rx_buf_pa[i]) return -1;
        net_rx_buf[i] = (uint8_t *)(HHDM_BASE + net_rx_buf_pa[i]);
        net_rx_ring[i].addr = net_rx_buf_pa[i];
        net_rx_ring[i].status = 0;
    }
    for (int i = 0; i < NET_TX_COUNT; i++) {
        net_tx_buf_pa[i] = pmm_alloc_n(1);
        if (!net_tx_buf_pa[i]) return -1;
        net_tx_buf[i] = (uint8_t *)(HHDM_BASE + net_tx_buf_pa[i]);
        net_tx_ring[i].addr = net_tx_buf_pa[i];
        net_tx_ring[i].status = 1;
    }

    net_wr(E1000_REG_RDBAL, (uint32_t)net_rx_ring_pa);
    net_wr(E1000_REG_RDBAH, (uint32_t)(net_rx_ring_pa >> 32));
    net_wr(E1000_REG_RDLEN, sizeof(E1000RxDesc) * NET_RX_COUNT);
    net_wr(E1000_REG_RDH, 0);
    net_wr(E1000_REG_RDT, NET_RX_COUNT - 1);
    net_wr(E1000_REG_TDBAL, (uint32_t)net_tx_ring_pa);
    net_wr(E1000_REG_TDBAH, (uint32_t)(net_tx_ring_pa >> 32));
    net_wr(E1000_REG_TDLEN, sizeof(E1000TxDesc) * NET_TX_COUNT);
    net_wr(E1000_REG_TDH, 0);
    net_wr(E1000_REG_TDT, 0);

    net_wr(E1000_REG_RCTL, (1u<<1) | (1u<<2) | (1u<<15) | (1u<<26) | 0x02);
    net_wr(E1000_REG_TCTL, (1u<<1) | (1u<<3) | (0x10u<<4) | (0x40u<<12));
    net_wr(E1000_REG_TIPG, 10 | (8u<<10) | (6u<<20));
    net_wr(E1000_REG_CTRL, net_rd(E1000_REG_CTRL) | (1u<<6)); /* set link up */
    net_ready = 1;
    DbgPrint("net: e1000 ready MAC %02x:%02x:%02x:%02x:%02x:%02x link=%s\n",
        net_mac[0],net_mac[1],net_mac[2],net_mac[3],net_mac[4],net_mac[5],
        (net_rd(E1000_REG_STATUS) & 2) ? "up" : "down");
    return 0;
}

/* Send one Ethernet frame. Returns 0 on success, -1 if unavailable/busy. */
static int net_send_frame(const void *data, uint16_t len) {
    if (!net_ready || !data || len < 14 || len > NET_BUF_SIZE) return -1;
    uint32_t i = net_tx_next;
    E1000TxDesc *d = &net_tx_ring[i];
    if (!(d->status & 1)) return -1;
    const uint8_t *src = (const uint8_t *)data;
    for (uint16_t n = 0; n < len; n++) net_tx_buf[i][n] = src[n];
    d->length = len; d->cso = 0; d->css = 0; d->special = 0;
    d->cmd = 0x0B; /* EOP | IFCS | RS */
    d->status = 0;
    __asm__ volatile("" ::: "memory");
    net_tx_next = (i + 1) % NET_TX_COUNT;
    net_wr(E1000_REG_TDT, net_tx_next);
    for (volatile uint32_t spin = 0; spin < 1000000; spin++)
        if (d->status & 1) { net_tx_packets++; return 0; }
    return -1;
}

/* Poll a single received Ethernet frame into caller memory. */
static int net_receive_frame(void *out, uint16_t capacity) {
    if (!net_ready || !out || !capacity) return -1;
    uint32_t i = net_rx_next;
    E1000RxDesc *d = &net_rx_ring[i];
    if (!(d->status & 1)) return 0;
    uint16_t len = d->length;
    if (len > capacity) len = capacity;
    for (uint16_t n = 0; n < len; n++) ((uint8_t *)out)[n] = net_rx_buf[i][n];
    d->status = 0; d->errors = 0; d->length = 0;
    __asm__ volatile("" ::: "memory");
    net_wr(E1000_REG_RDT, i);
    net_rx_next = (i + 1) % NET_RX_COUNT;
    net_rx_packets++;
    return len;
}
