/*
 * SentinelOS storage integration test.
 *
 * Tests the integration of SentinelBlockDevice (block layer) with
 * SentinelPartitionScan (MBR/GPT partition discovery) using a simulated
 * disk image. Compiled with the host compiler.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/block.h"
#include "../kernel/partition.h"

#define DISK_SECTORS 256u
#define SECTOR_SIZE 512u

typedef struct {
    uint8_t data[DISK_SECTORS * SECTOR_SIZE];
} TEST_DISK;

static TEST_DISK g_disk;

static int disk_read(void *ctx, uint64_t off, void *dst, uint64_t len) {
    (void)ctx;
    if (off > (uint64_t)sizeof(g_disk) || len > (uint64_t)sizeof(g_disk) - off)
        return -1;
    memcpy(dst, (uint8_t *)&g_disk + off, (size_t)len);
    return 0;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void put64(uint8_t *p, uint64_t v) { put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32)); }

static void mbr_entry(uint8_t *p, unsigned n, uint8_t type, uint32_t first, uint32_t count) {
    uint8_t *e = p + 446 + n * 16;
    e[4] = type; put32(e + 8, first); put32(e + 12, count);
}

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int fail(const char *what) {
    fprintf(stderr, "storage integration test failed: %s\n", what);
    return 1;
}

/* Build a block device backed by g_disk. */
static SENTINEL_BLOCK_DEVICE make_block_device(void) {
    SENTINEL_BLOCK_DEVICE b = {0};
    b.size = sizeof(b);
    b.abi_version = SENTINEL_BLOCK_ABI_VERSION;
    memcpy(b.name, "hd0", 4);
    b.sector_size = SECTOR_SIZE;
    b.sector_count = DISK_SECTORS;
    b.context = 0;
    b.read = disk_read;
    b.write = 0;
    b.online = 1;
    return b;
}

static int run_mbr_integration_test(void) {
    memset(&g_disk, 0, sizeof(g_disk));
    g_disk.data[510] = 0x55;
    g_disk.data[511] = 0xaa;
    mbr_entry(g_disk.data, 0, 0x07, 64, 100);   /* NTFS */
    mbr_entry(g_disk.data, 1, 0x83, 200, 50);   /* Linux */

    SENTINEL_PARTITION parts[SENTINEL_PARTITION_MAX];
    int n = SentinelPartitionScan(disk_read, 0, DISK_SECTORS, SECTOR_SIZE,
                                  parts, SENTINEL_PARTITION_MAX);
    if (n != 2) return fail("MBR: expected 2 partitions");
    if (parts[0].first_lba != 64 || parts[0].sector_count != 100) return fail("MBR: part0 wrong");
    if (parts[1].first_lba != 200 || parts[1].sector_count != 50) return fail("MBR: part1 wrong");
    if (parts[0].scheme != SENTINEL_PARTITION_MBR) return fail("MBR: scheme wrong");
    if (parts[0].mbr_type != 0x07) return fail("MBR: type wrong");

    /* Also register the disk as a block device and verify reads. */
    SENTINEL_BLOCK_DEVICE b = make_block_device();
    if (SentinelBlockRegister(&b) != 0) return fail("block register failed");

    uint8_t sector[SECTOR_SIZE];
    if (SentinelBlockReadDevice(&b, 0, sector, SECTOR_SIZE) != 0) return fail("block read failed");
    if (sector[510] != 0x55 || sector[511] != 0xaa) return fail("MBR signature not readable via block device");

    SentinelBlockUnregister("hd0", 0);
    printf("MBR integration test: PASS\n");
    return 0;
}

static int run_gpt_integration_test(void) {
    memset(&g_disk, 0, sizeof(g_disk));
    g_disk.data[510] = 0x55;
    g_disk.data[511] = 0xaa;
    g_disk.data[446] = 0xee;                          /* protective MBR */
    put32(g_disk.data + 454, 1);                      /* first LBA = 1 */
    put32(g_disk.data + 458, DISK_SECTORS - 1);       /* count */

    /* GPT header at LBA 1. */
    uint8_t *hdr = g_disk.data + SECTOR_SIZE;
    memcpy(hdr, "EFI PART", 8);
    put32(hdr + 12, 92);                              /* header size */
    put64(hdr + 24, 1);                               /* my LBA */
    put64(hdr + 32, DISK_SECTORS - 1);                /* backup LBA */
    put64(hdr + 40, 3);                               /* first usable */
    put64(hdr + 48, DISK_SECTORS - 2);                /* last usable */
    put64(hdr + 72, 2);                               /* entries LBA */
    put32(hdr + 80, 4);                               /* entries count */
    put32(hdr + 84, 128);                             /* entry size */

    /* GPT entry array at LBA 2. */
    uint8_t *entry = g_disk.data + 2 * SECTOR_SIZE;
    entry[0] = 0x28; entry[16] = 0x42;                /* Basic Data GUID start */
    put64(entry + 32, 8);                             /* first LBA */
    put64(entry + 40, 31);                            /* last LBA */
    put32(hdr + 88, sentinel_part_crc32(entry, 4 * 128));
    put32(hdr + 16, 0);
    put32(hdr + 16, sentinel_part_crc32(hdr, 92));

    SENTINEL_PARTITION parts[SENTINEL_PARTITION_MAX];
    int n = SentinelPartitionScan(disk_read, 0, DISK_SECTORS, SECTOR_SIZE,
                                  parts, SENTINEL_PARTITION_MAX);
    if (n != 1) return fail("GPT: expected 1 partition");
    if (parts[0].first_lba != 8 || parts[0].sector_count != 24) return fail("GPT: partition wrong");
    if (parts[0].scheme != SENTINEL_PARTITION_GPT) return fail("GPT: scheme wrong");

    SENTINEL_BLOCK_DEVICE b = make_block_device();
    if (SentinelBlockRegister(&b) != 0) return fail("block register failed");

    uint8_t gpt_hdr[SECTOR_SIZE];
    if (SentinelBlockReadDevice(&b, 1 * SECTOR_SIZE, gpt_hdr, SECTOR_SIZE) != 0) return fail("GPT header read failed");
    if (memcmp(gpt_hdr, "EFI PART", 8) != 0) return fail("GPT signature not found via block device");

    SentinelBlockUnregister("hd0", 0);
    printf("GPT integration test: PASS\n");
    return 0;
}

static int run_block_device_capacity_test(void) {
    SENTINEL_BLOCK_DEVICE b = make_block_device();
    if (SentinelBlockRegister(&b) != 0) return fail("block register failed");

    uint64_t capacity = 0;
    CHECK(SentinelBlockCapacity(&b, &capacity) == 0);
    CHECK(capacity == (uint64_t)DISK_SECTORS * SECTOR_SIZE);

    /* Snapshot the device. */
    SENTINEL_BLOCK_DEVICE snap = {0};
    if (SentinelBlockSnapshot(0, &snap) != 0) return fail("block snapshot failed");
    if (snap.sector_count != DISK_SECTORS) return fail("snapshot sector count wrong");

    SentinelBlockUnregister("hd0", 0);
    printf("block device capacity test: PASS\n");
    return 0;
}

int main(void) {
    if (run_mbr_integration_test() != 0) return 1;
    if (run_gpt_integration_test() != 0) return 1;
    if (run_block_device_capacity_test() != 0) return 1;
    puts("storage integration tests: ALL PASS");
    return 0;
}