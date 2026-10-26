/*
 * SentinelOS legacy ATA PIO block driver.
 * Probes both IDE channels and registers each detected ATA LBA28 disk.
 */
#include <stdint.h>
#include <stddef.h>
#include "../../kernel/driver.h"
#include "../../kernel/block.h"

#define ATA_SECTOR_BYTES 512u
#define ATA_WAIT_LIMIT 500000u
#define ATA_MAX_LBA28 0x10000000ull
#define ATA_MAX_DRIVES 4u

typedef struct {
    const SENTINEL_DRIVER_SERVICES *services;
    uint16_t io_base;
    uint16_t control_port;
    uint64_t sector_count;
    uint8_t drive_select;
    uint8_t index;
    int online;
} ATA_CONTEXT;

static ATA_CONTEXT g_ata[ATA_MAX_DRIVES];

static inline uint8_t ata_inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline void ata_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}
static inline void ata_insw(uint16_t port, void *buffer, uint32_t words) {
    __asm__ volatile ("rep insw" : "+D"(buffer), "+c"(words) : "d"(port) : "memory");
}
static inline void ata_outsw(uint16_t port, const void *buffer, uint32_t words) {
    __asm__ volatile ("rep outsw" : "+S"(buffer), "+c"(words) : "d"(port) : "memory");
}

static int ata_wait_not_busy(ATA_CONTEXT *ata, uint8_t *status_out) {
    for (uint32_t i = 0; i < ATA_WAIT_LIMIT; ++i) {
        uint8_t status = ata_inb((uint16_t)(ata->io_base + 7));
        if (status == 0 || status == 0xFF) return -1;
        if (!(status & 0x80)) {
            if (status_out) *status_out = status;
            return 0;
        }
    }
    return -1;
}
static int ata_wait_data(ATA_CONTEXT *ata) {
    for (uint32_t i = 0; i < ATA_WAIT_LIMIT; ++i) {
        uint8_t status = ata_inb((uint16_t)(ata->io_base + 7));
        if (status == 0 || status == 0xFF || (status & 0x01)) return -1;
        if (!(status & 0x80) && (status & 0x08)) return 0;
    }
    return -1;
}
static int ata_identify(ATA_CONTEXT *ata, uint8_t drive_select, uint64_t *sectors) {
    uint8_t status;
    /* Select first: an empty master slot can report 0xFF before the
       slave is selected, so pre-selection polling would hide that slave. */
    ata_outb(ata->control_port, 0);
    ata_outb((uint16_t)(ata->io_base + 6), drive_select);
    for (volatile uint32_t i = 0; i < 1000; ++i) { }
    status = ata_inb((uint16_t)(ata->io_base + 7));
    if (status == 0 || status == 0xFF) return -1;
    if (ata_wait_not_busy(ata, &status) != 0) return -1;
    ata_outb((uint16_t)(ata->io_base + 2), 0);
    ata_outb((uint16_t)(ata->io_base + 3), 0);
    ata_outb((uint16_t)(ata->io_base + 4), 0);
    ata_outb((uint16_t)(ata->io_base + 5), 0);
    ata_outb((uint16_t)(ata->io_base + 7), 0xEC);
    status = ata_inb((uint16_t)(ata->io_base + 7));
    if (status == 0 || status == 0xFF) return -1;
    uint32_t i;
    for (i = 0; i < ATA_WAIT_LIMIT; ++i) {
        status = ata_inb((uint16_t)(ata->io_base + 7));
        if (status & 0x01) return -1;
        if (!(status & 0x80)) break;
    }
    if (i == ATA_WAIT_LIMIT || (status & 0x80)) return -1;
    if (ata_inb((uint16_t)(ata->io_base + 4)) != 0 ||
        ata_inb((uint16_t)(ata->io_base + 5)) != 0) return -1;
    if (ata_wait_data(ata) != 0) return -1;
    uint16_t identify[256];
    ata_insw(ata->io_base, identify, 256);
    uint64_t count = (uint64_t)identify[60] | ((uint64_t)identify[61] << 16);
    if (!count) return -1;
    if (count > ATA_MAX_LBA28) count = ATA_MAX_LBA28;
    *sectors = count;
    return 0;
}
static int ata_read_sector(ATA_CONTEXT *ata, uint32_t lba, uint8_t *buffer) {
    if (!ata || !ata->online || lba >= ATA_MAX_LBA28 ||
        (uint64_t)lba >= ata->sector_count) return -1;
    if (ata_wait_not_busy(ata, 0) != 0) return -1;
    ata_outb((uint16_t)(ata->io_base + 6),
             (uint8_t)(ata->drive_select | ((lba >> 24) & 0x0F)));
    ata_outb((uint16_t)(ata->io_base + 2), 1);
    ata_outb((uint16_t)(ata->io_base + 3), (uint8_t)lba);
    ata_outb((uint16_t)(ata->io_base + 4), (uint8_t)(lba >> 8));
    ata_outb((uint16_t)(ata->io_base + 5), (uint8_t)(lba >> 16));
    ata_outb((uint16_t)(ata->io_base + 7), 0x20);
    if (ata_wait_data(ata) != 0) return -1;
    ata_insw(ata->io_base, buffer, 256);
    return 0;
}
static int ata_write_sector(ATA_CONTEXT *ata, uint32_t lba, const uint8_t *buffer) {
    if (!ata || !ata->online || lba >= ATA_MAX_LBA28 ||
        (uint64_t)lba >= ata->sector_count) return -1;
    if (ata_wait_not_busy(ata, 0) != 0) return -1;
    ata_outb((uint16_t)(ata->io_base + 6),
             (uint8_t)(ata->drive_select | 0x40 | ((lba >> 24) & 0x0F)));
    ata_outb((uint16_t)(ata->io_base + 2), 1);
    ata_outb((uint16_t)(ata->io_base + 3), (uint8_t)lba);
    ata_outb((uint16_t)(ata->io_base + 4), (uint8_t)(lba >> 8));
    ata_outb((uint16_t)(ata->io_base + 5), (uint8_t)(lba >> 16));
    ata_outb((uint16_t)(ata->io_base + 7), 0x30);
    if (ata_wait_data(ata) != 0) return -1;
    ata_outsw(ata->io_base, buffer, 256);
    ata_outb((uint16_t)(ata->io_base + 7), 0xE7);
    return ata_wait_not_busy(ata, 0);
}
static int ata_block_read(void *context, uint64_t offset, void *destination, uint64_t length) {
    ATA_CONTEXT *ata = (ATA_CONTEXT *)context;
    if (!ata || !ata->online || (!destination && length)) return -1;
    uint64_t capacity = ata->sector_count * ATA_SECTOR_BYTES;
    if (offset > capacity || length > capacity - offset) return -1;
    uint8_t sector[ATA_SECTOR_BYTES];
    uint8_t *out = (uint8_t *)destination;
    while (length) {
        uint32_t lba = (uint32_t)(offset / ATA_SECTOR_BYTES);
        uint32_t skip = (uint32_t)(offset % ATA_SECTOR_BYTES);
        uint32_t chunk = ATA_SECTOR_BYTES - skip;
        if ((uint64_t)chunk > length) chunk = (uint32_t)length;
        if (ata_read_sector(ata, lba, sector) != 0) return -1;
        for (uint32_t i = 0; i < chunk; ++i) out[i] = sector[skip + i];
        out += chunk; offset += chunk; length -= chunk;
    }
    return 0;
}
static int ata_block_write(void *context, uint64_t offset, const void *source, uint64_t length) {
    ATA_CONTEXT *ata = (ATA_CONTEXT *)context;
    if (!ata || !ata->online || (!source && length)) return -1;
    uint64_t capacity = ata->sector_count * ATA_SECTOR_BYTES;
    if (offset > capacity || length > capacity - offset) return -1;
    uint8_t sector[ATA_SECTOR_BYTES];
    const uint8_t *in = (const uint8_t *)source;
    while (length) {
        uint32_t lba = (uint32_t)(offset / ATA_SECTOR_BYTES);
        uint32_t skip = (uint32_t)(offset % ATA_SECTOR_BYTES);
        uint32_t chunk = ATA_SECTOR_BYTES - skip;
        if ((uint64_t)chunk > length) chunk = (uint32_t)length;
        if (skip || chunk != ATA_SECTOR_BYTES) {
            if (ata_read_sector(ata, lba, sector) != 0) return -1;
        }
        for (uint32_t i = 0; i < chunk; ++i) sector[skip + i] = in[i];
        if (ata_write_sector(ata, lba, sector) != 0) return -1;
        in += chunk; offset += chunk; length -= chunk;
    }
    return 0;
}
static void ata_unload(SENTINEL_DRIVER_OBJECT *driver) {
    (void)driver;
    for (uint32_t i = 0; i < ATA_MAX_DRIVES; ++i) {
        g_ata[i].online = 0;
        g_ata[i].sector_count = 0;
        g_ata[i].services = 0;
    }
}
static void (*volatile g_relocation_anchor)(SENTINEL_DRIVER_OBJECT *) = ata_unload;

int32_t DriverEntry(SENTINEL_DRIVER_OBJECT *driver,
                    const SENTINEL_DRIVER_SERVICES *services) {
    if (!driver || !services ||
        services->abi_version != SENTINEL_DRIVER_ABI_VERSION ||
        services->size < offsetof(SENTINEL_DRIVER_SERVICES, register_block_device) +
                         sizeof(services->register_block_device) ||
        !services->register_block_device) return -1;
    if (g_relocation_anchor != ata_unload) return -4;
    driver->unload = ata_unload;

    static const uint16_t io_bases[2] = { 0x1F0, 0x170 };
    static const uint16_t control_ports[2] = { 0x3F6, 0x376 };
    static const uint8_t drive_selects[2] = { 0xA0, 0xB0 };
    uint32_t found = 0;
    for (uint32_t channel = 0; channel < 2; ++channel) {
        for (uint32_t device = 0; device < 2; ++device) {
            uint32_t index = channel * 2 + device;
            ATA_CONTEXT *ata = &g_ata[index];
            ata->services = services;
            ata->io_base = io_bases[channel];
            ata->control_port = control_ports[channel];
            ata->drive_select = drive_selects[device];
            ata->index = (uint8_t)index;
            ata->online = 0;
            ata->sector_count = 0;
            uint64_t sectors = 0;
            if (ata_identify(ata, ata->drive_select, &sectors) != 0) continue;
            ata->sector_count = sectors;
            ata->online = 1;
            SENTINEL_BLOCK_DEVICE block = {0};
            block.size = sizeof(block);
            block.abi_version = SENTINEL_BLOCK_ABI_VERSION;
            block.name[0] = 'a'; block.name[1] = 't'; block.name[2] = 'a';
            block.name[3] = (char)('0' + index); block.name[4] = 0;
            block.sector_size = ATA_SECTOR_BYTES;
            block.sector_count = sectors;
            block.context = ata;
            block.read = ata_block_read;
            block.write = ata_block_write;
            if (services->register_block_device(&block) != 0) {
                ata->online = 0;
                if (services->debug_print)
                    services->debug_print("ata.sys: block-device registration failed\n");
                continue;
            }
            ++found;
            if (services->debug_print) {
                static const char *messages[4] = {
                    "ata.sys: primary-master registered as ata0\n",
                    "ata.sys: primary-slave registered as ata1\n",
                    "ata.sys: secondary-master registered as ata2\n",
                    "ata.sys: secondary-slave registered as ata3\n"
                };
                services->debug_print(messages[index]);
            }
        }
    }
    if (!found && services->debug_print)
        services->debug_print("ata.sys: loaded; no compatible IDE ATA LBA28 disks found\n");
    return 0;
}
