#pragma once
/*
 * SentinelOS Intel E1000 Ethernet driver.
 *
 * Target: QEMU's 82540EM-compatible E1000 (PCI 8086:100E) and a small set
 * of closely related Intel IDs. Polling I/O only: no IRQ handler is assumed.
 * This is an Ethernet device driver, not an IP/TCP/DNS implementation.
 */
#include <stdint.h>
#include "serial.h"
#include "pmm.h"
#include "vmm.h"
#include "../shared/netabi.h"

#define NET_RX_COUNT 32u
#define NET_TX_COUNT 8u
#define NET_BUF_SIZE 2048u
#define E1000_MMIO_SIZE 0x20000u
#define E1000_PCI_VENDOR 0x8086u

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

_Static_assert(sizeof(E1000RxDesc) == 16, "E1000 RX descriptor must be 16 bytes");
_Static_assert(sizeof(E1000TxDesc) == 16, "E1000 TX descriptor must be 16 bytes");

#define E1000_REG_CTRL   0x0000
#define E1000_REG_STATUS 0x0008
#define E1000_REG_EERD   0x0014
#define E1000_REG_ICR    0x00C0
#define E1000_REG_IMS    0x00D0
#define E1000_REG_IMC    0x00D8
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

#define E1000_CTRL_RST  (1u << 26)
#define E1000_CTRL_SLU  (1u << 6)
#define E1000_STATUS_LU (1u << 1)
#define E1000_RCTL_EN   (1u << 1)
#define E1000_RCTL_UPE  (1u << 3)
#define E1000_RCTL_MPE  (1u << 4)
#define E1000_RCTL_BAM  (1u << 15)
#define E1000_RCTL_SECRC (1u << 26)
#define E1000_TCTL_EN   (1u << 1)
#define E1000_TCTL_PSP  (1u << 3)
#define E1000_TXD_STAT_DD 0x01u
#define E1000_TXD_CMD_EOP 0x01u
#define E1000_TXD_CMD_IFCS 0x02u
#define E1000_TXD_CMD_RS 0x08u
#define E1000_RXD_STAT_DD 0x01u
#define E1000_RXD_STAT_EOP 0x02u

static volatile uint32_t *net_regs;
static int net_ready;
static uint8_t net_mac[6];
static uint16_t net_pci_device;
static uint64_t net_mmio_pa;
static uint64_t net_rx_ring_pa, net_tx_ring_pa;
static E1000RxDesc *net_rx_ring;
static E1000TxDesc *net_tx_ring;
static uint64_t net_rx_buf_pa[NET_RX_COUNT], net_tx_buf_pa[NET_TX_COUNT];
static uint8_t *net_rx_buf[NET_RX_COUNT], *net_tx_buf[NET_TX_COUNT];
static uint32_t net_rx_next, net_tx_next;
static uint64_t net_rx_packets, net_tx_packets, net_rx_dropped, net_tx_errors;

static inline uint32_t net_rd(uint32_t reg) {
    uint32_t v = net_regs[reg / 4];
    __asm__ volatile("" ::: "memory");
    return v;
}
static inline void net_wr(uint32_t reg, uint32_t val) {
    __asm__ volatile("" ::: "memory");
    net_regs[reg / 4] = val;
    (void)net_regs[reg / 4]; /* flush posted MMIO write */
    __asm__ volatile("" ::: "memory");
}
static inline void net_outl(uint16_t p, uint32_t v) {
    __asm__ volatile("outl %0, %1" :: "a"(v), "Nd"(p));
}
static inline uint32_t net_inl(uint16_t p) {
    uint32_t v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p));
    return v;
}

/* PCI configuration mechanism #1; QEMU's Q35 E1000 is on bus 0. */
static uint32_t net_pci_read(uint8_t slot, uint8_t function, uint8_t off) {
    uint32_t address = (1u << 31) | ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) | (off & 0xFC);
    net_outl(0xCF8, address);
    return net_inl(0xCFC);
}
static void net_pci_write(uint8_t slot, uint8_t function, uint8_t off, uint32_t val) {
    uint32_t address = (1u << 31) | ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) | (off & 0xFC);
    net_outl(0xCF8, address);
    net_outl(0xCFC, val);
}
static int net_is_supported_device(uint16_t device) {
    return device == 0x100E || device == 0x100F ||
           device == 0x10D3 || device == 0x153A;
}
static int net_find_pci_device(uint8_t *found_slot, uint8_t *found_function,
                               uint16_t *found_device, uint32_t *found_bar) {
    for (uint8_t slot = 0; slot < 32; slot++) {
        uint32_t id0 = net_pci_read(slot, 0, 0);
        if ((uint16_t)id0 == 0xFFFFu) continue;
        uint8_t functions = (net_pci_read(slot, 0, 0x0C) & (1u << 23)) ? 8 : 1;
        for (uint8_t fn = 0; fn < functions; fn++) {
            uint32_t id = fn ? net_pci_read(slot, fn, 0) : id0;
            if ((uint16_t)id != E1000_PCI_VENDOR ||
                !net_is_supported_device((uint16_t)(id >> 16))) continue;
            uint32_t bar = net_pci_read(slot, fn, 0x10);
            /* This implementation intentionally supports a 32-bit memory BAR. */
            if ((bar & 1u) || ((bar >> 1) & 3u) == 2u || !(bar & ~0xFu)) continue;
            *found_slot = slot;
            *found_function = fn;
            *found_device = (uint16_t)(id >> 16);
            *found_bar = bar;
            return 0;
        }
    }
    return -1;
}

static void net_zero_bytes(void *ptr, uint64_t count) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    for (uint64_t i = 0; i < count; i++) p[i] = 0;
}

/* Releases all DMA allocations. Call only while the device engines are stopped. */
static void net_release_dma(void) {
    for (uint32_t i = 0; i < NET_RX_COUNT; i++) {
        if (net_rx_buf_pa[i]) pmm_free(net_rx_buf_pa[i]);
        net_rx_buf_pa[i] = 0; net_rx_buf[i] = 0;
    }
    for (uint32_t i = 0; i < NET_TX_COUNT; i++) {
        if (net_tx_buf_pa[i]) pmm_free(net_tx_buf_pa[i]);
        net_tx_buf_pa[i] = 0; net_tx_buf[i] = 0;
    }
    if (net_rx_ring_pa) pmm_free(net_rx_ring_pa);
    if (net_tx_ring_pa) pmm_free(net_tx_ring_pa);
    net_rx_ring_pa = net_tx_ring_pa = 0;
    net_rx_ring = 0; net_tx_ring = 0;
}
static int net_alloc_dma(void) {
    net_rx_ring_pa = pmm_alloc_n(1);
    net_tx_ring_pa = pmm_alloc_n(1);
    if (!net_rx_ring_pa || !net_tx_ring_pa) goto fail;
    net_rx_ring = (E1000RxDesc *)(HHDM_BASE + net_rx_ring_pa);
    net_tx_ring = (E1000TxDesc *)(HHDM_BASE + net_tx_ring_pa);
    net_zero_bytes(net_rx_ring, 4096);
    net_zero_bytes(net_tx_ring, 4096);

    for (uint32_t i = 0; i < NET_RX_COUNT; i++) {
        net_rx_buf_pa[i] = pmm_alloc_n(1);
        if (!net_rx_buf_pa[i]) goto fail;
        net_rx_buf[i] = (uint8_t *)(HHDM_BASE + net_rx_buf_pa[i]);
        net_zero_bytes(net_rx_buf[i], NET_BUF_SIZE);
        net_rx_ring[i].addr = net_rx_buf_pa[i];
        net_rx_ring[i].status = 0;
    }
    for (uint32_t i = 0; i < NET_TX_COUNT; i++) {
        net_tx_buf_pa[i] = pmm_alloc_n(1);
        if (!net_tx_buf_pa[i]) goto fail;
        net_tx_buf[i] = (uint8_t *)(HHDM_BASE + net_tx_buf_pa[i]);
        net_zero_bytes(net_tx_buf[i], NET_BUF_SIZE);
        net_tx_ring[i].addr = net_tx_buf_pa[i];
        net_tx_ring[i].status = E1000_TXD_STAT_DD;
    }
    return 0;
fail:
    net_release_dma();
    return -1;
}

/* Initialize hardware and its polling RX/TX descriptor rings. */
static int net_e1000_init(void) {
    net_ready = 0;
    net_regs = 0;
    net_rx_next = net_tx_next = 0;
    net_rx_packets = net_tx_packets = net_rx_dropped = net_tx_errors = 0;
    net_release_dma();

    uint8_t slot = 0, function = 0;
    uint16_t device = 0;
    uint32_t bar = 0;
    if (net_find_pci_device(&slot, &function, &device, &bar)) {
        DbgPrint("net: supported Intel E1000 PCI device not found\n");
        return -1;
    }

    uint64_t mmio = (uint64_t)(bar & ~0xFu);
    /* The kernel's HHDM maps EFI MMIO/reserved ranges uncached. Avoid remapping
       a 2 MiB large-page mapping with map_4k(), which would corrupt the page tables. */
    if (vmm_translate(HHDM_BASE + mmio) != mmio) {
        DbgPrint("net: E1000 MMIO BAR %p is not mapped in HHDM\n", (void *)mmio);
        return -1;
    }

    uint32_t command_status = net_pci_read(slot, function, 0x04);
    /* Only write the low command word; upper status bits can be write-one-to-clear. */
    net_pci_write(slot, function, 0x04, (command_status & 0xFFFFu) | 0x0006u);
    net_mmio_pa = mmio;
    net_regs = (volatile uint32_t *)(HHDM_BASE + mmio);

    /* Disable all device interrupts: this driver deliberately polls. */
    net_wr(E1000_REG_IMC, 0xFFFFFFFFu);
    (void)net_rd(E1000_REG_ICR);

    net_wr(E1000_REG_CTRL, net_rd(E1000_REG_CTRL) | E1000_CTRL_RST);
    uint32_t spin;
    for (spin = 0; spin < 1000000u; spin++)
        if (!(net_rd(E1000_REG_CTRL) & E1000_CTRL_RST)) break;
    if (spin == 1000000u) {
        DbgPrint("net: E1000 reset timed out\n");
        net_regs = 0;
        return -1;
    }
    net_wr(E1000_REG_IMC, 0xFFFFFFFFu);
    (void)net_rd(E1000_REG_ICR);

    uint32_t ral = net_rd(E1000_REG_RAL), rah = net_rd(E1000_REG_RAH);
    net_mac[0] = (uint8_t)ral; net_mac[1] = (uint8_t)(ral >> 8);
    net_mac[2] = (uint8_t)(ral >> 16); net_mac[3] = (uint8_t)(ral >> 24);
    net_mac[4] = (uint8_t)rah; net_mac[5] = (uint8_t)(rah >> 8);
    if (!(rah & (1u << 31)) ||
        !(net_mac[0] | net_mac[1] | net_mac[2] | net_mac[3] | net_mac[4] | net_mac[5]) ||
        (net_mac[0] & 1u)) {
        DbgPrint("net: E1000 has no valid unicast MAC in RAL/RAH\n");
        net_regs = 0;
        return -1;
    }

    if (net_alloc_dma()) {
        DbgPrint("net: DMA ring/buffer allocation failed\n");
        net_regs = 0;
        return -1;
    }

    /* Ring lengths must be 128-byte aligned; 32/8 descriptors satisfy that. */
    net_wr(E1000_REG_RCTL, 0);
    net_wr(E1000_REG_TCTL, 0);
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

    /* Accept broadcast + our unicast; strip Ethernet CRC. */
    net_wr(E1000_REG_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC);
    net_wr(E1000_REG_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP |
           (0x10u << 4) | (0x40u << 12));
    net_wr(E1000_REG_TIPG, 10u | (8u << 10) | (6u << 20));
    net_wr(E1000_REG_CTRL, net_rd(E1000_REG_CTRL) | E1000_CTRL_SLU);

    net_pci_device = device;
    net_ready = 1;
    DbgPrint("net: e1000 %04x:%04x ready; MAC %02x:%02x:%02x:%02x:%02x:%02x link=%s\n",
        E1000_PCI_VENDOR, net_pci_device,
        net_mac[0], net_mac[1], net_mac[2], net_mac[3], net_mac[4], net_mac[5],
        (net_rd(E1000_REG_STATUS) & E1000_STATUS_LU) ? "up" : "down");
    return 0;
}

/* Transmit one complete Ethernet frame (including destination/source/type).
 * Returns 0 when the NIC reports descriptor completion; otherwise -1.
 */
static int net_send_frame(const void *data, uint16_t len) {
    if (!net_ready || !data || len < 14 || len > NET_BUF_SIZE) return -1;
    uint32_t i = net_tx_next;
    volatile E1000TxDesc *d = &net_tx_ring[i];
    if (!(d->status & E1000_TXD_STAT_DD)) return -1;

    const uint8_t *src = (const uint8_t *)data;
    for (uint16_t n = 0; n < len; n++) net_tx_buf[i][n] = src[n];
    d->length = len; d->cso = 0; d->css = 0; d->special = 0;
    d->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    __asm__ volatile("" ::: "memory");
    d->status = 0;
    net_tx_next = (i + 1) % NET_TX_COUNT;
    net_wr(E1000_REG_TDT, net_tx_next);

    for (volatile uint32_t spin = 0; spin < 5000000u; spin++) {
        if (d->status & E1000_TXD_STAT_DD) {
            net_tx_packets++;
            return 0;
        }
    }
    net_tx_errors++;
    return -1;
}

/* Poll for one received frame. Returns its length, 0 if none is ready, or -1
 * on invalid arguments/driver state. Frames larger than capacity are dropped.
 */
static int net_receive_frame(void *out, uint16_t capacity) {
    if (!net_ready || !out || !capacity) return -1;
    uint32_t i = net_rx_next;
    volatile E1000RxDesc *d = &net_rx_ring[i];
    if (!(d->status & E1000_RXD_STAT_DD)) return 0;

    uint16_t len = d->length;
    uint8_t errors = d->errors;
    uint8_t status = d->status;
    int valid = (status & E1000_RXD_STAT_EOP) && !errors &&
                len >= 14 && len <= NET_BUF_SIZE && len <= capacity;
    if (valid) {
        for (uint16_t n = 0; n < len; n++) ((uint8_t *)out)[n] = net_rx_buf[i][n];
        net_rx_packets++;
    } else {
        net_rx_dropped++;
    }

    /* Re-arm the descriptor only after we have consumed its data. */
    d->length = 0; d->checksum = 0; d->errors = 0; d->special = 0;
    __asm__ volatile("" ::: "memory");
    d->status = 0;
    net_wr(E1000_REG_RDT, i);
    net_rx_next = (i + 1) % NET_RX_COUNT;
    return valid ? (int)len : -1;
}

/* Current physical/link counters and MAC for the user-mode ipconfig command. */
static void net_query_config(SENTINELOS_NET_CONFIG *out) {
    if (!out) return;
    net_zero_bytes(out, sizeof(*out));
    out->Version = SENTINELOS_NET_CONFIG_VERSION;
    if (!net_ready) return;
    out->Flags |= SENTINELOS_NET_DRIVER_READY;
    if (net_rd(E1000_REG_STATUS) & E1000_STATUS_LU)
        out->Flags |= SENTINELOS_NET_LINK_UP;
    for (int i = 0; i < 6; i++) out->MacAddress[i] = net_mac[i];
    out->RxPackets = net_rx_packets;
    out->TxPackets = net_tx_packets;
}

/* Driver statistics for kernel diagnostics; no protocol stack is implied. */
static void net_get_stats(uint64_t *rx, uint64_t *tx,
                          uint64_t *rx_dropped, uint64_t *tx_errors) {
    if (rx) *rx = net_rx_packets;
    if (tx) *tx = net_tx_packets;
    if (rx_dropped) *rx_dropped = net_rx_dropped;
    if (tx_errors) *tx_errors = net_tx_errors;
}
