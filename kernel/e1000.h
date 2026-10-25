#pragma once
#include <stdint.h>
#include "pci_network.h"
#include "pmm.h"
#include "vmm.h"

/* Intel 82540EM/E1000 polling-mode Ethernet driver. DMA rings and buffers are
 * allocated from contiguous physical RAM. No IRQ dependency: RX/TX are polled.
 * The first milestone is link/MAC plus frame TX/RX; IP/ARP/TCP sits above it. */
#define E1000_REG_CTRL   0x0000
#define E1000_REG_STATUS 0x0008
#define E1000_REG_EERD   0x0014
#define E1000_REG_ICR    0x00C0
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
#define E1000_REG_RAL0   0x5400
#define E1000_REG_RAH0   0x5404
#define E1000_RCTL_EN    (1u<<1)
#define E1000_RCTL_BAM   (1u<<15)
#define E1000_RCTL_SECRC (1u<<26)
#define E1000_TCTL_EN    (1u<<1)
#define E1000_TCTL_PSP   (1u<<3)
#define E1000_RING_COUNT 32
#define E1000_BUFFER_SIZE 2048

typedef struct __attribute__((packed, aligned(16))) {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} E1000_RX_DESC;
typedef struct __attribute__((packed, aligned(16))) {
    uint64_t address;
    uint16_t length;
    uint8_t cso;
    uint8_t command;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} E1000_TX_DESC;

static volatile uint32_t *e1000_regs;
static E1000_RX_DESC *e1000_rx;
static E1000_TX_DESC *e1000_tx;
static uint64_t e1000_rx_phys, e1000_tx_phys;
static uint64_t e1000_rxbuf_phys, e1000_txbuf_phys;
static unsigned e1000_tx_index, e1000_rx_index;
static uint8_t e1000_mac[6];
static int e1000_ready;

static uint32_t E1000Read(uint32_t reg) { return e1000_regs[reg/4]; }
static void E1000Write(uint32_t reg,uint32_t value) { e1000_regs[reg/4]=value; }
static void E1000Pause(void) { for (volatile unsigned i=0;i<10000;i++) __asm__ volatile("pause"); }

static int E1000Init(void) {
    PCI_DEVICE_INFO d;
    int found=0;
    for (unsigned bus=0;bus<256&&!found;bus++)
      for (unsigned dev=0;dev<32&&!found;dev++) {
        PCI_DEVICE_INFO t;
        if (!PciReadDevice((uint8_t)bus,(uint8_t)dev,0,&t)) continue;
        unsigned fns=(t.header_type&0x80u)?8u:1u;
        for (unsigned fn=0;fn<fns;fn++) {
          if (fn && !PciReadDevice((uint8_t)bus,(uint8_t)dev,(uint8_t)fn,&t)) continue;
          if (PciIsIntelE1000(&t)) { d=t; found=1; break; }
        }
      }
    if (!found) { DbgPrint("e1000: no supported Intel adapter found\n"); return 0; }
    uint32_t bar=d.bar[0];
    if ((bar&1u) || bar==0 || bar==0xFFFFFFFFu) {
        DbgPrint("e1000: unsupported BAR0=%08x (MMIO required)\n",bar); return 0;
    }
    uint64_t mmio=(uint64_t)(bar&0xFFFFFFF0u);
    if (mmio == 0 || mmio >= 0x100000000ULL || mmio + 0x6000ULL > 0x100000000ULL) {
        DbgPrint("e1000: unsupported BAR0 address %p\n",(void*)mmio); return 0;
    }
    /* Firmware may omit this PCI BAR from its memory map. Explicitly map
       every page containing the registers, with cache-disable flags, before
       touching the device. */
    if (!vmm_map_mmio(mmio, 0x6000ULL)) {
        DbgPrint("e1000: failed to map BAR0=%p into HHDM\n",(void*)mmio); return 0;
    }
    e1000_regs=(volatile uint32_t *)(HHDM_BASE + mmio);
    uint32_t cmd=PciRead32(d.bus,d.device,d.function,4);
    PciCfgOut32(0xCF8,0x80000000u|((uint32_t)d.bus<<16)|((uint32_t)d.device<<11)|((uint32_t)d.function<<8)|4u);
    PciCfgOut32(0xCFC,(cmd&0xFFFFu)|0x0006u); /* memory space + bus master */
    E1000Write(E1000_REG_IMC,0xFFFFFFFFu);
    (void)E1000Read(E1000_REG_ICR);
    uint32_t ctrl=E1000Read(E1000_REG_CTRL);
    E1000Write(E1000_REG_CTRL,ctrl|(1u<<26)); /* reset */
    for (unsigned i=0;i<1000000;i++) { if (!(E1000Read(E1000_REG_CTRL)&(1u<<26))) break; __asm__ volatile("pause"); }
    E1000Pause();
    E1000Write(E1000_REG_IMC,0xFFFFFFFFu);
    /* Read the factory MAC from receive-address registers. */
    uint32_t ral=E1000Read(E1000_REG_RAL0), rah=E1000Read(E1000_REG_RAH0);
    e1000_mac[0]=(uint8_t)ral; e1000_mac[1]=(uint8_t)(ral>>8);
    e1000_mac[2]=(uint8_t)(ral>>16); e1000_mac[3]=(uint8_t)(ral>>24);
    e1000_mac[4]=(uint8_t)rah; e1000_mac[5]=(uint8_t)(rah>>8);
    if (!(rah&(1u<<31))) {
        DbgPrint("e1000: receive address invalid (RAH=%08x)\n",rah); return 0;
    }
    e1000_rx_phys=pmm_alloc_n(1);
    e1000_tx_phys=pmm_alloc_n(1);
    e1000_rxbuf_phys=pmm_alloc_n(16); /* 32 * 2048 bytes */
    e1000_txbuf_phys=pmm_alloc_n(16);
    if (!e1000_rx_phys||!e1000_tx_phys||!e1000_rxbuf_phys||!e1000_txbuf_phys) {
        DbgPrint("e1000: DMA memory allocation failed\n"); return 0;
    }
    e1000_rx=(E1000_RX_DESC *)e1000_rx_phys;
    e1000_tx=(E1000_TX_DESC *)e1000_tx_phys;
    for (unsigned i=0;i<E1000_RING_COUNT;i++) {
        uint64_t rb=e1000_rxbuf_phys+(uint64_t)i*E1000_BUFFER_SIZE;
        uint64_t tb=e1000_txbuf_phys+(uint64_t)i*E1000_BUFFER_SIZE;
        e1000_rx[i]=(E1000_RX_DESC){.address=rb,.status=0};
        e1000_tx[i]=(E1000_TX_DESC){.address=tb,.status=1};
    }
    E1000Write(E1000_REG_RDBAL,(uint32_t)e1000_rx_phys);
    E1000Write(E1000_REG_RDBAH,(uint32_t)(e1000_rx_phys>>32));
    E1000Write(E1000_REG_RDLEN,sizeof(E1000_RX_DESC)*E1000_RING_COUNT);
    E1000Write(E1000_REG_RDH,0); E1000Write(E1000_REG_RDT,E1000_RING_COUNT-1);
    E1000Write(E1000_REG_TDBAL,(uint32_t)e1000_tx_phys);
    E1000Write(E1000_REG_TDBAH,(uint32_t)(e1000_tx_phys>>32));
    E1000Write(E1000_REG_TDLEN,sizeof(E1000_TX_DESC)*E1000_RING_COUNT);
    E1000Write(E1000_REG_TDH,0); E1000Write(E1000_REG_TDT,0);
    E1000Write(E1000_REG_TIPG,0x0060200Au);
    E1000Write(E1000_REG_RCTL,E1000_RCTL_EN|E1000_RCTL_BAM|E1000_RCTL_SECRC); /* BSIZE=00 selects 2048-byte buffers */
    E1000Write(E1000_REG_TCTL,E1000_TCTL_EN|E1000_TCTL_PSP|(0x10u<<4)|(0x40u<<12));
    e1000_tx_index=e1000_rx_index=0;
    e1000_ready=1;
    DbgPrint("e1000: initialized %04x:%04x BAR0=%p MAC=%02x:%02x:%02x:%02x:%02x:%02x; polling DMA rings ready\n",
        d.vendor_id,d.device_id,(void*)mmio,e1000_mac[0],e1000_mac[1],e1000_mac[2],e1000_mac[3],e1000_mac[4],e1000_mac[5]);
    return 1;
}

/* Queue one Ethernet frame. Returns 1 on submission, 0 if unavailable/full. */
static int E1000SendFrame(const void *frame,unsigned length) {
    if (!e1000_ready||!frame||length<14||length> E1000_BUFFER_SIZE) return 0;
    E1000_TX_DESC *d=&e1000_tx[e1000_tx_index];
    if (!(d->status&1u)) return 0;
    uint8_t *dst=(uint8_t *)(e1000_txbuf_phys+(uint64_t)e1000_tx_index*E1000_BUFFER_SIZE);
    const uint8_t *src=(const uint8_t *)frame;
    for (unsigned i=0;i<length;i++) dst[i]=src[i];
    d->length=(uint16_t)length; d->command=0x0B; d->status=0;
    e1000_tx_index=(e1000_tx_index+1)%E1000_RING_COUNT;
    E1000Write(E1000_REG_TDT,e1000_tx_index);
    return 1;
}

/* Returns frame length; zero means no completed packet. Caller owns no buffer:
 * frame bytes are copied to dst before the descriptor is returned to hardware. */
static unsigned E1000ReceiveFrame(void *dst,unsigned capacity) {
    if (!e1000_ready||!dst||!capacity) return 0;
    E1000_RX_DESC *d=&e1000_rx[e1000_rx_index];
    if (!(d->status&1u)) return 0;
    unsigned n=d->length;
    if (n>capacity) n=capacity;
    const uint8_t *src=(const uint8_t *)(e1000_rxbuf_phys+(uint64_t)e1000_rx_index*E1000_BUFFER_SIZE);
    for (unsigned i=0;i<n;i++) ((uint8_t *)dst)[i]=src[i];
    d->status=0; d->errors=0; d->length=0;
    E1000Write(E1000_REG_RDT,e1000_rx_index);
    e1000_rx_index=(e1000_rx_index+1)%E1000_RING_COUNT;
    return n;
}
