#pragma once
#include <stdint.h>

/*
 * Minimal legacy PCI configuration-space scanner.
 * This is the discovery layer for the SentinelOS E1000 driver. It uses PCI
 * Configuration Mechanism #1 (CF8/CFC), available in QEMU's x86 PC machine.
 * It only reads config space and never changes BARs or command bits.
 */
static inline void PciCfgOut32(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" :: "a"(value), "Nd"(port));
}
static inline uint32_t PciCfgIn32(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline uint32_t PciRead32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16) |
        ((uint32_t)device << 11) | ((uint32_t)function << 8) |
        ((uint32_t)offset & 0xFCu);
    PciCfgOut32(0xCF8, address);
    return PciCfgIn32(0xCFC);
}
static inline uint16_t PciRead16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t value = PciRead32(bus, device, function, offset);
    return (uint16_t)(value >> ((offset & 2u) * 8u));
}

typedef struct {
    uint8_t bus, device, function, header_type;
    uint16_t vendor_id, device_id;
    uint8_t class_code, subclass, prog_if, revision;
    uint32_t bar[6];
    uint16_t command, status;
} PCI_DEVICE_INFO;

static int PciIsNetworkClass(const PCI_DEVICE_INFO *d) {
    return d->class_code == 0x02;
}
static int PciIsIntelE1000(const PCI_DEVICE_INFO *d) {
    if (d->vendor_id != 0x8086) return 0;
    switch (d->device_id) {
        case 0x100E: /* 82540EM, QEMU default e1000 */
        case 0x100F: /* 82545EM */
        case 0x1010: /* 82546EB */
        case 0x1011:
        case 0x1012:
        case 0x101D:
        case 0x105E:
        case 0x107C:
        case 0x108B:
        case 0x10B9:
        case 0x10D3:
            return 1;
        default: return 0;
    }
}
static int PciReadDevice(uint8_t bus, uint8_t device, uint8_t function, PCI_DEVICE_INFO *out) {
    uint32_t id = PciRead32(bus, device, function, 0);
    uint16_t vendor = (uint16_t)id;
    if (vendor == 0xFFFF || vendor == 0) return 0;
    uint32_t classreg = PciRead32(bus, device, function, 8);
    uint32_t hdr = PciRead32(bus, device, function, 0x0C);
    out->bus=bus; out->device=device; out->function=function;
    out->vendor_id=vendor; out->device_id=(uint16_t)(id>>16);
    out->revision=(uint8_t)classreg;
    out->prog_if=(uint8_t)(classreg>>8);
    out->subclass=(uint8_t)(classreg>>16);
    out->class_code=(uint8_t)(classreg>>24);
    out->header_type=(uint8_t)(hdr>>16);
    uint32_t command_status=PciRead32(bus,device,function,4);
    out->command=(uint16_t)command_status; out->status=(uint16_t)(command_status>>16);
    for (unsigned i=0;i<6;i++) out->bar[i]=PciRead32(bus,device,function,(uint8_t)(0x10+i*4));
    return 1;
}

/* Log only NICs and E1000 candidates; scanning every function is quiet. */
static unsigned int PciScanNetworkDevices(void) {
    unsigned int network_count=0, e1000_count=0;
    for (unsigned int bus=0;bus<256;bus++) {
        for (unsigned int dev=0;dev<32;dev++) {
            PCI_DEVICE_INFO info;
            if (!PciReadDevice((uint8_t)bus,(uint8_t)dev,0,&info)) continue;
            unsigned int functions=(info.header_type&0x80u)?8u:1u;
            for (unsigned int fn=0;fn<functions;fn++) {
                if (fn && !PciReadDevice((uint8_t)bus,(uint8_t)dev,(uint8_t)fn,&info)) continue;
                if (!PciIsNetworkClass(&info)) continue;
                network_count++;
                if (PciIsIntelE1000(&info)) {
                    e1000_count++;
                    DbgPrint("pci: E1000 candidate %04x:%04x at %02x:%02x.%u cmd=%04x BAR0=%08x BAR1=%08x\n",
                        info.vendor_id,info.device_id,info.bus,info.device,info.function,
                        info.command,info.bar[0],info.bar[1]);
                } else {
                    DbgPrint("pci: network device %04x:%04x class=%02x:%02x at %02x:%02x.%u\n",
                        info.vendor_id,info.device_id,info.class_code,info.subclass,
                        info.bus,info.device,info.function);
                }
            }
        }
    }
    DbgPrint("pci: network scan complete: %u network device(s), %u E1000 candidate(s)\n",
        network_count,e1000_count);
    return e1000_count;
}
