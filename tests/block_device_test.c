/*
 * SentinelOS block device registry test.
 *
 * Exercises SentinelBlockRegister, SentinelBlockUnregister,
 * SentinelBlockReadDevice, SentinelBlockWriteDevice, SentinelBlockFind,
 * SentinelBlockSnapshot, and SentinelBlockCount using mock block devices.
 * Compiled with the host compiler; kernel headers are included directly
 * with spin-wait and thread-token macros overridden for the host.
 */
#define OB_SPIN_WAIT() ((void)0)
#define IO_SPIN_WAIT() ((void)0)
#define SENTINEL_BLOCK_SPIN_WAIT() ((void)0)
#define IO_CURRENT_THREAD_TOKEN() (NULL)
#define IO_CURRENT_CPU_TOKEN() (NULL)

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/block.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

#define BLOCK_SECTOR_SIZE 512u
#define BLOCK_SECTOR_COUNT 1024u

typedef struct {
    uint8_t data[BLOCK_SECTOR_COUNT * BLOCK_SECTOR_SIZE];
    int read_fail;
    int write_fail;
    int unregister_calls;
} TEST_BLOCK_STATE;

static int test_block_read(void *ctx, uint64_t offset, void *dst, uint64_t length) {
    TEST_BLOCK_STATE *st = (TEST_BLOCK_STATE *)ctx;
    if (st->read_fail) return -1;
    if (offset > (uint64_t)(BLOCK_SECTOR_COUNT * BLOCK_SECTOR_SIZE)) return -1;
    if (length > (uint64_t)(BLOCK_SECTOR_COUNT * BLOCK_SECTOR_SIZE) - offset) return -1;
    memcpy(dst, st->data + offset, (size_t)length);
    return 0;
}

static int test_block_write(void *ctx, uint64_t offset, const void *src, uint64_t length) {
    TEST_BLOCK_STATE *st = (TEST_BLOCK_STATE *)ctx;
    if (st->write_fail) return -1;
    if (offset > (uint64_t)(BLOCK_SECTOR_COUNT * BLOCK_SECTOR_SIZE)) return -1;
    if (length > (uint64_t)(BLOCK_SECTOR_COUNT * BLOCK_SECTOR_SIZE) - offset) return -1;
    memcpy(st->data + offset, src, (size_t)length);
    return 0;
}

static int run_register_unregister_test(void) {
    TEST_BLOCK_STATE st = {0};
    SENTINEL_BLOCK_DEVICE dev = {0};
    dev.size = sizeof(dev);
    dev.abi_version = SENTINEL_BLOCK_ABI_VERSION;
    dev.sector_size = BLOCK_SECTOR_SIZE;
    dev.sector_count = BLOCK_SECTOR_COUNT;
    dev.context = &st;
    dev.read = test_block_read;
    dev.write = test_block_write;
    dev.online = 1;
    memcpy(dev.name, "test0", 5);

    /* Register should succeed. */
    CHECK(SentinelBlockRegister(&dev) == 0);

    /* Duplicate registration should fail. */
    SENTINEL_BLOCK_DEVICE dev2 = dev;
    CHECK(SentinelBlockRegister(&dev2) == -2);

    /* Find by name. */
    SENTINEL_BLOCK_DEVICE *found = SentinelBlockFind("test0");
    CHECK(found != 0);
    CHECK(found->context == &st);

    /* Snapshot. */
    SENTINEL_BLOCK_DEVICE snap = {0};
    CHECK(SentinelBlockSnapshot(0, &snap) == 0);
    CHECK(snap.sector_size == BLOCK_SECTOR_SIZE);
    CHECK(snap.sector_count == BLOCK_SECTOR_COUNT);

    /* Count. */
    CHECK(SentinelBlockCount() == 1);

    /* Read/Write through the registry. */
    uint8_t write_buf[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    CHECK(SentinelBlockWriteDevice(&dev, 0, write_buf, 16) == 0);
    uint8_t read_buf[16] = {0};
    CHECK(SentinelBlockReadDevice(&dev, 0, read_buf, 16) == 0);
    CHECK(memcmp(read_buf, write_buf, 16) == 0);

    /* Unregister should succeed. */
    CHECK(SentinelBlockUnregister("test0", &st) == 0);

    /* Find should now fail. */
    found = SentinelBlockFind("test0");
    CHECK(found == 0);

    /* Count should be 0. */
    CHECK(SentinelBlockCount() == 0);

    /* Unregister again should fail (not found). */
    CHECK(SentinelBlockUnregister("test0", &st) == -2);

    printf("block device register/unregister test: PASS\n");
    return 0;
}

static int run_read_write_bounds_test(void) {
    TEST_BLOCK_STATE st = {0};
    SENTINEL_BLOCK_DEVICE dev = {0};
    dev.size = sizeof(dev);
    dev.abi_version = SENTINEL_BLOCK_ABI_VERSION;
    dev.sector_size = BLOCK_SECTOR_SIZE;
    dev.sector_count = BLOCK_SECTOR_COUNT;
    dev.context = &st;
    dev.read = test_block_read;
    dev.write = test_block_write;
    dev.online = 1;
    memcpy(dev.name, "test1", 5);

    CHECK(SentinelBlockRegister(&dev) == 0);

    /* Read beyond capacity should fail. */
    uint8_t buf[8] = {0};
    CHECK(SentinelBlockReadDevice(&dev, (uint64_t)BLOCK_SECTOR_COUNT * BLOCK_SECTOR_SIZE, buf, 1) == -1);

    /* Write beyond capacity should fail. */
    CHECK(SentinelBlockWriteDevice(&dev, (uint64_t)BLOCK_SECTOR_COUNT * BLOCK_SECTOR_SIZE, buf, 1) == -1);

    /* Read with NULL dst and nonzero length should fail. */
    CHECK(SentinelBlockReadDevice(&dev, 0, 0, 8) == -1);

    /* Write with NULL src and nonzero length should fail. */
    CHECK(SentinelBlockWriteDevice(&dev, 0, 0, 8) == -1);

    SentinelBlockUnregister("test1", &st);
    printf("block device bounds test: PASS\n");
    return 0;
}

static int run_read_write_fail_test(void) {
    TEST_BLOCK_STATE st = {0};
    st.read_fail = 1;
    SENTINEL_BLOCK_DEVICE dev = {0};
    dev.size = sizeof(dev);
    dev.abi_version = SENTINEL_BLOCK_ABI_VERSION;
    dev.sector_size = BLOCK_SECTOR_SIZE;
    dev.sector_count = BLOCK_SECTOR_COUNT;
    dev.context = &st;
    dev.read = test_block_read;
    dev.write = test_block_write;
    dev.online = 1;
    memcpy(dev.name, "test2", 5);

    CHECK(SentinelBlockRegister(&dev) == 0);

    uint8_t buf[8] = {0};
    CHECK(SentinelBlockReadDevice(&dev, 0, buf, 8) == -1);

    st.read_fail = 0;
    st.write_fail = 1;
    CHECK(SentinelBlockWriteDevice(&dev, 0, buf, 8) == -1);

    SentinelBlockUnregister("test2", &st);
    printf("block device read/write fail test: PASS\n");
    return 0;
}

static int run_invalid_device_test(void) {
    /* NULL device. */
    CHECK(SentinelBlockRegister(0) == -1);

    /* Wrong size. */
    SENTINEL_BLOCK_DEVICE dev = {0};
    dev.size = 12; /* too small */
    dev.abi_version = SENTINEL_BLOCK_ABI_VERSION;
    CHECK(SentinelBlockRegister(&dev) == -1);

    /* Wrong ABI version. */
    dev.size = sizeof(dev);
    dev.abi_version = 99u;
    CHECK(SentinelBlockRegister(&dev) == -1);

    /* Zero sector count. */
    dev.abi_version = SENTINEL_BLOCK_ABI_VERSION;
    dev.sector_size = BLOCK_SECTOR_SIZE;
    dev.sector_count = 0;
    dev.read = test_block_read;
    CHECK(SentinelBlockRegister(&dev) == -1);

    /* NULL read callback. */
    dev.sector_count = BLOCK_SECTOR_COUNT;
    dev.read = 0;
    CHECK(SentinelBlockRegister(&dev) == -1);

    printf("block device invalid device test: PASS\n");
    return 0;
}

int main(void) {
    if (run_invalid_device_test() != 0) return 1;
    if (run_register_unregister_test() != 0) return 1;
    if (run_read_write_bounds_test() != 0) return 1;
    if (run_read_write_fail_test() != 0) return 1;
    puts("block device tests: ALL PASS");
    return 0;
}