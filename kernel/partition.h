#pragma once
#include <stdint.h>
#include <stddef.h>

/* Read-only MBR/GPT partition discovery.  Reads are byte addressed so this
 * can use any registered block device; sectors below 4 KiB are supported. */
#define SENTINEL_PARTITION_MAX 128u
#define SENTINEL_PARTITION_MBR 1u
#define SENTINEL_PARTITION_GPT 2u
typedef int (*SENTINEL_PARTITION_READ)(void *context, uint64_t offset,
                                       void *dst, uint64_t length);
typedef struct {
    uint64_t first_lba;
    uint64_t sector_count;
    uint8_t type_guid[16];
    uint8_t unique_guid[16];
    uint8_t mbr_type;
    uint8_t scheme;
} SENTINEL_PARTITION;

static uint32_t sentinel_part_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t sentinel_part_u64(const uint8_t *p) {
    return (uint64_t)sentinel_part_u32(p) |
           ((uint64_t)sentinel_part_u32(p + 4) << 32);
}
static uint32_t sentinel_part_crc32(const uint8_t *p, uint32_t length) {
    uint32_t crc = 0xffffffffu;
    for (uint32_t i = 0; i < length; ++i) {
        crc ^= p[i];
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1));
    }
    return ~crc;
}
static int sentinel_part_zero(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; ++i) if (p[i]) return 0;
    return 1;
}
static int sentinel_part_overlap(const SENTINEL_PARTITION *parts, uint32_t count,
                                 uint64_t first, uint64_t sectors) {
    uint64_t end = first + sectors;
    for (uint32_t i = 0; i < count; ++i) {
        uint64_t old_end = parts[i].first_lba + parts[i].sector_count;
        if (first < old_end && parts[i].first_lba < end) return 1;
    }
    return 0;
}

/* On success returns number of usable partitions (zero is a valid empty MBR).
 * A protective MBR requires a valid GPT; it never falls back to interpreting
 * the protective entry as a regular partition. */
static int SentinelPartitionScan(SENTINEL_PARTITION_READ read, void *context,
                                 uint64_t disk_sectors, uint32_t sector_size,
                                 SENTINEL_PARTITION *out, uint32_t out_capacity) {
    if (!read || !out || !disk_sectors || !sector_size || sector_size < 512 ||
        sector_size > 4096 || out_capacity > SENTINEL_PARTITION_MAX) return -1;
    uint8_t sector[4096];
    uint64_t disk_bytes;
    if (disk_sectors > UINT64_MAX / sector_size) return -1;
    disk_bytes = disk_sectors * (uint64_t)sector_size;
    if (read(context, 0, sector, sector_size) < 0) return -2;
    if (sector[510] != 0x55 || sector[511] != 0xaa) return -1;
    int protective = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const uint8_t *e = sector + 446 + i * 16;
        uint8_t type = e[4];
        uint32_t first = sentinel_part_u32(e + 8), count = sentinel_part_u32(e + 12);
        if (!type && !first && !count) continue;
        if (type == 0xee) {
            if (first != 1 || !count || (uint64_t)count > disk_sectors - first) return -1;
            protective = 1;
            continue;
        }
        if (!type || !count || first >= disk_sectors ||
            (uint64_t)count > disk_sectors - first ||
            sentinel_part_overlap(out, 0, first, count)) return -1;
    }
    if (!protective) {
        /* Compact entries and reject overlaps among real primary partitions. */
        SENTINEL_PARTITION compact[4]; uint32_t n = 0;
        for (unsigned i = 0; i < 4; ++i) {
            const uint8_t *e = sector + 446 + i * 16;
            uint8_t type = e[4]; uint64_t first = sentinel_part_u32(e + 8);
            uint64_t len = sentinel_part_u32(e + 12);
            if (!type && !first && !len) continue;
            if (type == 0xee) continue;
            if (sentinel_part_overlap(compact, n, first, len)) return -1;
            compact[n].first_lba = first; compact[n].sector_count = len;
            compact[n].mbr_type = type; compact[n].scheme = SENTINEL_PARTITION_MBR;
            ++n;
        }
        if (n > out_capacity) return -3;
        for (uint32_t i = 0; i < n; ++i) out[i] = compact[i];
        (void)disk_bytes;
        return (int)n;
    }

    if (disk_sectors < 3 || read(context, sector_size, sector, sector_size) < 0) return -2;
    if (sentinel_part_u64(sector) != 0x5452415020494645ULL) return -1; /* EFI PART */
    uint32_t header_size = sentinel_part_u32(sector + 12);
    uint32_t header_crc = sentinel_part_u32(sector + 16);
    uint64_t my_lba = sentinel_part_u64(sector + 24);
    uint64_t backup_lba = sentinel_part_u64(sector + 32);
    uint64_t first_usable = sentinel_part_u64(sector + 40);
    uint64_t last_usable = sentinel_part_u64(sector + 48);
    uint64_t entries_lba = sentinel_part_u64(sector + 72);
    uint32_t entries_count = sentinel_part_u32(sector + 80);
    uint32_t entry_size = sentinel_part_u32(sector + 84);
    uint32_t entries_crc = sentinel_part_u32(sector + 88);
    if (header_size < 92 || header_size > sector_size || my_lba != 1 ||
        backup_lba >= disk_sectors || backup_lba == my_lba ||
        first_usable < 2 || last_usable < first_usable || last_usable >= disk_sectors ||
        !entries_count || entries_count > SENTINEL_PARTITION_MAX ||
        entry_size < 128 || entry_size > 4096 || (entry_size & 7) ||
        entries_lba < 2 || entries_lba >= disk_sectors ||
        (uint64_t)entries_count * entry_size > UINT64_MAX / 1u) return -1;
    uint8_t header[4096];
    for (uint32_t i = 0; i < header_size; ++i) header[i] = sector[i];
    for (unsigned i = 16; i < 20; ++i) header[i] = 0;
    if (sentinel_part_crc32(header, header_size) != header_crc) return -1;
    uint64_t array_bytes = (uint64_t)entries_count * entry_size;
    if (array_bytes > disk_bytes || entries_lba > UINT64_MAX / sector_size ||
        entries_lba * sector_size > disk_bytes ||
        array_bytes > disk_bytes - entries_lba * sector_size) return -1;
    /* The primary GPT entry array must live in the reserved GPT area before
     * the first usable LBA. Accepting it inside the usable range lets a
     * checksummed, malformed table describe a partition over its own
     * metadata. The byte-capacity check above also bounds this addition. */
    uint64_t array_sectors = array_bytes / sector_size +
        (array_bytes % sector_size != 0);
    if (entries_lba > UINT64_MAX - array_sectors ||
        entries_lba + array_sectors > first_usable) return -1;
    /* Validate the whole entry-array CRC in bounded chunks. */
    uint8_t chunk[4096]; uint32_t crc = 0xffffffffu;
    uint64_t pos = 0;
    while (pos < array_bytes) {
        uint32_t n = (uint32_t)(array_bytes - pos > sizeof(chunk) ? sizeof(chunk) : array_bytes - pos);
        if (read(context, entries_lba * sector_size + pos, chunk, n) < 0) return -2;
        for (uint32_t i = 0; i < n; ++i) {
            crc ^= chunk[i];
            for (unsigned b = 0; b < 8; ++b)
                crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1));
        }
        pos += n;
    }
    if (~crc != entries_crc) return -1;
    uint32_t n = 0;
    for (uint32_t i = 0; i < entries_count; ++i) {
        uint8_t entry[4096];
        uint64_t off = entries_lba * sector_size + (uint64_t)i * entry_size;
        if (read(context, off, entry, entry_size) < 0) return -2;
        if (sentinel_part_zero(entry, 16)) continue;
        uint64_t first = sentinel_part_u64(entry + 32), last = sentinel_part_u64(entry + 40);
        if (first < first_usable || last > last_usable || last < first ||
            last == UINT64_MAX || n >= out_capacity ||
            sentinel_part_overlap(out, n, first, last - first + 1)) return -1;
        SENTINEL_PARTITION *p = &out[n++];
        p->first_lba = first; p->sector_count = last - first + 1;
        p->mbr_type = 0; p->scheme = SENTINEL_PARTITION_GPT;
        for (unsigned j = 0; j < 16; ++j) { p->type_guid[j] = entry[j]; p->unique_guid[j] = entry[16 + j]; }
    }
    return (int)n;
}
