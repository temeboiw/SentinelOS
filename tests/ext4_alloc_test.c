#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define EXT4K_TEST 1
#include "../kernel/ext4k.h"

static uint8_t test_disk[256u * 1024u];

int ext4k_test_read(uint64_t off, void *dst, uint64_t len) {
    if (off > sizeof test_disk || len > sizeof test_disk - off) return -1;
    memcpy(dst, test_disk + off, (size_t)len);
    return 0;
}
int ext4k_test_write(uint64_t off, const void *src, uint64_t len) {
    if (off > sizeof test_disk || len > sizeof test_disk - off) return -1;
    memcpy(test_disk + off, src, (size_t)len);
    return 0;
}

static void setbit(uint8_t *p, uint32_t bit) { p[bit >> 3] |= (uint8_t)(1u << (bit & 7)); }
static int check(int ok, const char *what) {
    if (!ok) fprintf(stderr, "FAIL: %s\n", what);
    return ok;
}

int main(void) {
    Ext4k fs;
    memset(&fs, 0, sizeof fs);
    memset(test_disk, 0, sizeof test_disk);

    /* Minimal two-group ext4 image, intentionally without journal/checksums. */
    uint8_t *sb = test_disk + 1024;
    E32(sb, 0) = 64;       /* inode count */
    E32(sb, 4) = 256;      /* block count */
    E32(sb, 12) = 238;     /* free blocks */
    E32(sb, 16) = 54;      /* free inodes */
    E32(sb, 20) = 1;       /* first data block */
    E32(sb, 24) = 0;       /* 1 KiB blocks */
    E32(sb, 32) = 128;     /* blocks per group */
    E32(sb, 40) = 32;      /* inodes per group */
    E16(sb, 56) = 0xEF53;
    E16(sb, 88) = 128;     /* inode size */
    E32(sb, 92) = 0;       /* compatible features */
    E32(sb, 96) = 0x42;    /* filetype + extents */
    E32(sb, 100) = 0;      /* read-only compatible features */
    E32(sb, 84) = 11;      /* first non-reserved inode */

    uint8_t *gdt = test_disk + 2u * 1024u;
    E32(gdt, 0) = 3; E32(gdt, 4) = 4; E32(gdt, 8) = 10;
    E16(gdt, 12) = 111; E16(gdt, 14) = 22; E16(gdt, 16) = 0;
    E32(gdt + 32, 0) = 6; E32(gdt + 32, 4) = 7; E32(gdt + 32, 8) = 14;
    E16(gdt + 32, 12) = 127; E16(gdt + 32, 14) = 32; E16(gdt + 32, 16) = 0;

    /* Mark blocks 1..17 used in group zero; block 18 is the first free block. */
    for (uint32_t b = 0; b < 17; ++b) setbit(test_disk + 3u * 1024u, b);
    for (uint32_t i = 0; i < 10; ++i) setbit(test_disk + 4u * 1024u, i);

    int ok = 1;
    ok &= check(e4_mount(&fs, 0) == 0, "mount synthetic ext4");
    ok &= check(e4_can_mutate(&fs), "supported ext4 feature set accepted");
    fs.feature_incompat |= 0x200u; /* flex_bg is supported */
    ok &= check(e4_can_mutate(&fs), "flex_bg layout accepted");
    fs.feature_incompat |= 0x10000u; /* orphan_file is unsupported */
    ok &= check(!e4_can_mutate(&fs), "unknown incompat feature rejected");
    fs.feature_incompat = 0x42u;
    fs.feature_ro_compat = 0x400u;
    ok &= check(!e4_can_mutate(&fs), "metadata checksum layout rejected");
    fs.feature_ro_compat = 0;

    uint32_t ino = e4_alloc_inode(&fs, 0);
    ok &= check(ino == 11, "first free inode allocated");
    ok &= check((test_disk[4u * 1024u + 1] & 0x04u) != 0, "inode bitmap bit set");
    ok &= check(E16(gdt, 14) == 21 && E32(sb, 16) == 53, "free inode counters updated");

    uint64_t block = e4_alloc_block(&fs);
    ok &= check(block == 18, "first free data block allocated");
    ok &= check((test_disk[3u * 1024u + 2] & 0x02u) != 0, "block bitmap bit set");
    ok &= check(E16(gdt, 12) == 110 && E32(sb, 12) == 237, "free block counters updated");
    uint64_t block2 = e4_alloc_block(&fs);
    ok &= check(block2 == 19, "next adjacent data block allocated");
    ok &= check(E16(gdt, 12) == 109 && E32(sb, 12) == 236,
                "second allocation updates counters");

    uint8_t inode[128];
    memset(inode, 0, sizeof inode);
    E32(inode, 32) = 0x00080000u;
    E16(inode, 40) = 0xF30A;
    E16(inode, 42) = 0;
    E16(inode, 44) = 4;
    E16(inode, 46) = 0;
    ok &= check(e4_add_inline_extent(&fs, inode, 0, block) == 0, "first inline extent added");
    ok &= check(e4_add_inline_extent(&fs, inode, 1, block2) == 0, "adjacent extent coalesced");
    ok &= check(E16(inode + 40, 2) == 1 && (E16(inode + 52, 4) & 0x7FFFu) == 2,
                "adjacent extent length recorded");
    ok &= check(e4_map(&fs, inode, 0) == block && e4_map(&fs, inode, 1) == block2,
                "logical blocks map to allocated physical blocks");

    if (!ok) return 1;
    puts("ext4 allocation tests passed: feature guards, inode/block bitmaps, counters, inline extents.");
    return 0;
}
