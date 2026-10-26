/*
 * SentinelOS legacy ATA PIO block driver.
 * This is a self-contained kernel module: it uses port I/O directly and
 * publishes its byte-range block callbacks through the append-only ABI.
 */
#include <stdint.h>
#include <stddef.h>
#include "../../kernel/driver.h"
#include "../../kernel/block.h"

#define ATA_DATA   0x1F0
#define ATA_ERROR  0x1F1
#define ATA_COUNT  0x1F2
#define ATA_LBA0   0x1F3
#define ATA_LBA1   0x1F4
#define ATA_LBA2   0x1F5
#define ATA_DEVICE 0x1F6
#define ATA_STATUS 0x1F7
#define ATA_COMMAND 0x1F7
#define ATA_CONTROL 0x3F6
#define ATA_SECTOR_BYTES 512u
#define ATA_WAIT_LIMIT 500000u
#define ATA_MAX_LBA28 0x10000000ull

typedef struct {
    const SENTINEL_DRIVER_SERVICES *services;
    uint64_t sector_count;
    uint8_t drive_select;
    int online;
} ATA_CONTEXT;

static ATA_CONTEXT g_ata;

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

static int ata_wait_not_busy(uint8_t *status_out) {
    for (uint32_t i = 0; i < ATA_WAIT_LIMIT; ++i) {
        uint8_t status = ata_inb(ATA_STATUS);
        if (status == 0 || status == 0xFF) return -1;
        if (!(status & 0x80)) {
            if (status_out) *status_out = status;
            return 0;
        }
    }
    return -1;
}

static int ata_wait_data(void) {
    for (uint32_t i = 0; i < ATA_WAIT_LIMIT; ++i) {
        uint8_t status = ata_inb(ATA_STATUS);
        if (status == 0 || status == 0xFF || (status & 0x01)) return -1;
        if (!(status & 0x80) && (status & 0x08)) return 0;
    }
    return -1;
}

static int ata_identify(uint8_t drive_select, uint64_t *sectors) {
    uint8_t status;
    if (ata_wait_not_busy(&status) != 0) return -1;
    if (status == 0) return -1;

    ata_outb(ATA_CONTROL, 0);
    ata_outb(ATA_DEVICE, drive_select);
    for (volatile uint32_t i = 0; i < 1000; ++i) { }
    ata_outb(ATA_COUNT, 0);
    ata_outb(ATA_LBA0, 0);
    ata_outb(ATA_LBA1, 0);
    ata_outb(ATA_LBA2, 0);
    ata_outb(ATA_COMMAND, 0xEC);

    status = ata_inb(ATA_STATUS);
    if (status == 0 || status == 0xFF) return -1;
    for (uint32_t i = 0; i < ATA_WAIT_LIMIT; ++i) {
        status = ata_inb(ATA_STATUS);
        if (status & 0x01) return -1;
        if (!(status & 0x80)) break;
    }
    if (status & 0x80) return -1;

    uint8_t sig_lba1 = ata_inb(ATA_LBA1);
    uint8_t sig_lba2 = ata_inb(ATA_LBA2);
    if (sig_lba1 != 0 || sig_lba2 != 0) return -1;
    if (ata_wait_data() != 0) return -1;

    uint16_t identify[256];
    ata_insw(ATA_DATA, identify, 256);
    uint64_t count = (uint64_t)identify[60] | ((uint64_t)identify[61] << 16);
    if (!count) return -1;
    if (count > ATA_MAX_LBA28) count = ATA_MAX_LBA28;
    *sectors = count;
    return 0;
}

static int ata_read_sector(uint32_t lba, uint8_t *buffer) {
    if (!g_ata.online || lba >= ATA_MAX_LBA28) return -1;
    if (ata_wait_not_busy(0) != 0) return -1;
    ata_outb(ATA_DEVICE, (uint8_t)(g_ata.drive_select | ((lba >> 24) & 0x0F)));
    ata_outb(ATA_COUNT, 1);
    ata_outb(ATA_LBA0, (uint8_t)lba);
    ata_outb(ATA_LBA1, (uint8_t)(lba >> 8));
    ata_outb(ATA_LBA2, (uint8_t)(lba >> 16));
    ata_outb(ATA_COMMAND, 0x20);
    if (ata_wait_data() != 0) return -1;
    ata_insw(ATA_DATA, buffer, 256);
    return 0;
}

static int ata_write_sector(uint32_t lba, const uint8_t *buffer) {
    if (!g_ata.online || lba >= ATA_MAX_LBA28) return -1;
    if (ata_wait_not_busy(0) != 0) return -1;
    ata_outb(ATA_DEVICE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_outb(ATA_COUNT, 1);
    ata_outb(ATA_LBA0, (uint8_t)lba);
    ata_outb(ATA_LBA1, (uint8_t)(lba >> 8));
    ata_outb(ATA_LBA2, (uint8_t)(lba >> 16));
    ata_outb(ATA_COMMAND, 0x30);
    if (ata_wait_data() != 0) return -1;
    ata_outsw(ATA_DATA, buffer, 256);
    ata_outb(ATA_COMMAND, 0xE7);
    return ata_wait_not_busy(0);
}

static int ata_block_read(void *context, uint64_t offset,
                          void *destination, uint64_t length) {
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
        if (ata_read_sector(lba, sector) != 0) return -1;
        for (uint32_t i = 0; i < chunk; ++i) out[i] = sector[skip + i];
        out += chunk;
        offset += chunk;
        length -= chunk;
    }
    return 0;
}

static int ata_block_write(void *context, uint64_t offset,
                           const void *source, uint64_t length) {
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
            if (ata_read_sector(lba, sector) != 0) return -1;
        }
        for (uint32_t i = 0; i < chunk; ++i) sector[skip + i] = in[i];
        if (ata_write_sector(lba, sector) != 0) return -1;
        in += chunk;
        offset += chunk;
        length -= chunk;
    }
    return 0;
}

static void ata_unload(SENTINEL_DRIVER_OBJECT *driver) {
    (void)driver;
    g_ata.online = 0;
    g_ata.sector_count = 0;
    g_ata.services = 0;
}

static void (*volatile g_relocation_anchor)(SENTINEL_DRIVER_OBJECT *) = ata_unload;

int32_t DriverEntry(SENTINEL_DRIVER_OBJECT *driver,
                    const SENTINEL_DRIVER_SERVICES *services) {
    if (!driver || !services ||
        services->abi_version != SENTINEL_DRIVER_ABI_VERSION ||
        services->size < offsetof(SENTINEL_DRIVER_SERVICES, register_block_device) +
                         sizeof(services->register_block_device) ||
        !services->register_block_device)
        return -1;

    /* A Windows-style driver may load successfully even when Plug and
       Play finds no matching device. Device discovery is separate from
       image loading; absence of legacy ATA hardware is not a loader error. */

    /* Keep a relocatable function pointer in the image so the loader can
       rebase the module away from its preferred link address. */
    if (g_relocation_anchor != ata_unload) return -4;

    g_ata.services = services;
    g_ata.sector_count = 0;
    g_ata.online = 0;
    driver->unload = ata_unload;

    uint64_t sectors = 0;
    uint8_t selected_drive = 0xA0;
    if (ata_identify(selected_drive, &sectors) != 0) {
        selected_drive = 0xB0;
        if (ata_identify(selected_drive, &sectors) != 0) {
            if (services->debug_print)
                services->debug_print("ata.sys: loaded; no primary-channel ATA LBA28 device\n");
            return 0;
        }
    }

    g_ata.drive_select = selected_drive;
    g_ata.sector_count = sectors;
    g_ata.online = 1;

    SENTINEL_BLOCK_DEVICE block = {0};
    block.size = sizeof(block);
    block.abi_version = SENTINEL_BLOCK_ABI_VERSION;
    block.name[0] = 'a'; block.name[1] = 't'; block.name[2] = 'a';
    block.name[3] = selected_drive == 0xA0 ? '0' : '1';
    block.name[4] = 0;
    block.sector_size = ATA_SECTOR_BYTES;
    block.sector_count = sectors;
    block.context = &g_ata;
    block.read = ata_block_read;
    block.write = ata_block_write;

    if (services->register_block_device(&block) != 0) {
        g_ata.online = 0;
        return -3;
    }

    if (services->debug_print) {
        services->debug_print(selected_drive == 0xA0
            ? "ata.sys: primary-master LBA28 PIO disk registered as ata0\n"
            : "ata.sys: primary-slave LBA28 PIO disk registered as ata1\n");
    }
    return 0;
}
