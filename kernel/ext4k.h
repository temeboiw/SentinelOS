#pragma once
#include "ata.h"
#include "virtio_blk.h"

#define E16(p,o) (*(uint16_t *)((uint8_t *)(p) + (o)))
#define E32(p,o) (*(uint32_t *)((uint8_t *)(p) + (o)))

typedef struct {
    uint64_t part_off;
    uint32_t blksz, ipg, isize, dsize, first_data, bpg;
    uint64_t total_blocks,free_blocks,reserved_blocks;
    uint32_t total_inodes,free_inodes,volume_serial;
    char volume_label[17];
    int is64;
    uint8_t blk[4096];
    int ok;
} Ext4k;

static int e4_read(Ext4k *fs, uint64_t off, void *dst, uint64_t len) {
    if (vb_ok) return blk_read(fs->part_off + off, dst, len);
    return ata_read(fs->part_off + off, dst, len);
}

/*
 * Byte-range disk writes use the same device selection as reads. The block
 * backends perform sector read/modify/write for unaligned ranges, which is
 * required for ext4 metadata and file tails.
 */
static int e4_write(Ext4k *fs, uint64_t off, const void *src, uint64_t len) {
    if (!fs || !fs->ok || (!src && len)) return -1;
    if (!len) return 0;
    if (off > UINT64_MAX - len) return -1;
    if (vb_ok) return blk_write(fs->part_off + off, src, len);
    return ata_write(fs->part_off + off, src, len);
}

static uint64_t e4_size(uint8_t *inode);
static uint64_t e4_map(Ext4k *fs, uint8_t *inode, uint32_t lb);

/*
 * Write bytes into already allocated extents of an existing inode.
 * This deliberately does not allocate blocks, grow files, or create names;
 * callers must stay within the inode's current size until the allocator and
 * directory transaction code are in place.
 */
static int e4_write_inode_at(Ext4k *fs, uint8_t *inode,
                             uint64_t pos, const void *src, uint64_t len) {
    if (!fs || !fs->ok || !inode || (!src && len)) return -1;
    uint64_t size = e4_size(inode);
    if (pos > size || len > size - pos) return -1;
    const uint8_t *in = (const uint8_t *)src;
    uint8_t block[4096];
    while (len) {
        uint32_t in_block = (uint32_t)(pos % fs->blksz);
        uint32_t n = fs->blksz - in_block;
        if ((uint64_t)n > len) n = (uint32_t)len;
        uint64_t physical = e4_map(fs, inode, (uint32_t)(pos / fs->blksz));
        if (!physical) return -1;
        if (in_block == 0 && n == fs->blksz) {
            if (e4_write(fs, physical * fs->blksz, in, n) < 0) return -1;
        } else {
            if (e4_read(fs, physical * fs->blksz, block, fs->blksz) < 0)
                return -1;
            for (uint32_t i = 0; i < n; ++i) block[in_block + i] = in[i];
            if (e4_write(fs, physical * fs->blksz, block, fs->blksz) < 0)
                return -1;
        }
        pos += n;
        in += n;
        len -= n;
    }
    return 0;
}

static int e4_mount(Ext4k *fs, uint64_t part_lba) {
    uint8_t sb[1024];
    fs->part_off = part_lba * 512;
    fs->ok = 0;
    if (e4_read(fs, 1024, sb, 1024) < 0) return -1;
    if (E16(sb, 56) != 0xEF53) return -1;
    fs->blksz = 1024u << E32(sb, 24);
    fs->first_data = E32(sb, 20);
    fs->bpg = E32(sb, 32);
    fs->feature_compat = E32(sb, 92);
    fs->feature_incompat = E32(sb, 96);
    fs->feature_ro_compat = E32(sb, 100);
    fs->bpg = E32(sb, 32);
    fs->ipg = E32(sb, 40);
    fs->isize = E16(sb, 88);
    if (fs->isize < 128) fs->isize = 128;
    fs->is64 = (E32(sb, 96) & 0x80) != 0;
    fs->total_blocks=E32(sb,4);fs->free_blocks=E32(sb,12);fs->reserved_blocks=E32(sb,8);
    if(fs->is64){fs->total_blocks|=(uint64_t)E32(sb,0x150)<<32;fs->free_blocks|=(uint64_t)E32(sb,0x158)<<32;fs->reserved_blocks|=(uint64_t)E32(sb,0x154)<<32;}
    fs->total_inodes=E32(sb,0);fs->free_inodes=E32(sb,16);fs->volume_serial=E32(sb,104);
    for(int i=0;i<16;i++)fs->volume_label[i]=(char)sb[120+i];fs->volume_label[16]=0;
    fs->dsize = fs->is64 ? E16(sb, 254) : 32;
    if (!fs->dsize) fs->dsize = 32;
    if (fs->blksz > 4096 || !fs->ipg) return -1;
    fs->ok = 1;
    return 0;
}


/*
 * Allocate one data block using ext4's block bitmap.
 * SentinelOS's formatter disables the journal, metadata_csum, and 64bit
 * features. Refuse other incompatible layouts instead of writing metadata
 * with assumptions that do not hold for that filesystem.
 */
static int e4_alloc_block(Ext4k *fs, uint64_t *block_out) {
    if (!fs || !fs->ok || !block_out || !fs->bpg ||
        fs->is64 || fs->blksz != 4096 || fs->dsize != 32)
        return -1;

    uint8_t sb[1024];
    if (e4_read(fs, 1024, sb, sizeof(sb)) < 0) return -1;
    uint32_t incompat = E32(sb, 96);
    uint32_t ro_compat = E32(sb, 100);
    /* Allow filetype, extents and flex_bg, which the formatter may enable. */
    if ((incompat & ~0x00000242u) != 0 ||
        (ro_compat & 0x00000400u) != 0)
        return -1;

    uint64_t groups = (fs->total_blocks + fs->bpg - 1) / fs->bpg;
    uint64_t gdt_off = (uint64_t)(fs->first_data + 1) * fs->blksz;
    for (uint64_t g = 0; g < groups; ++g) {
        uint8_t gd[64];
        if (e4_read(fs, gdt_off + g * fs->dsize, gd, fs->dsize) < 0)
            return -1;
        uint32_t bitmap_block = E32(gd, 0);
        if (!bitmap_block) continue;
        uint16_t group_free = E16(gd, 12);
        if (!group_free) continue;

        uint8_t bitmap[4096];
        if (e4_read(fs, (uint64_t)bitmap_block * fs->blksz,
                    bitmap, fs->blksz) < 0) return -1;
        uint64_t first = g * fs->bpg;
        uint64_t count = fs->total_blocks - first;
        if (count > fs->bpg) count = fs->bpg;
        for (uint64_t bit = 0; bit < count; ++bit) {
            uint64_t block = first + bit;
            if (block < fs->first_data) continue;
            if (bitmap[bit >> 3] & (uint8_t)(1u << (bit & 7))) continue;

            uint32_t sb_free = E32(sb, 12);
            if (!sb_free) return -1;
            bitmap[bit >> 3] |= (uint8_t)(1u << (bit & 7));
            if (e4_write(fs, (uint64_t)bitmap_block * fs->blksz,
                         bitmap, fs->blksz) < 0) return -1;

            E16(gd, 12) = (uint16_t)(group_free - 1);
            if (e4_write(fs, gdt_off + g * fs->dsize, gd, fs->dsize) < 0)
                return -1;
            E32(sb, 12) = sb_free - 1;
            if (e4_write(fs, 1024, sb, sizeof(sb)) < 0) return -1;

            fs->free_blocks = sb_free - 1;
            *block_out = block;
            return 0;
        }
    }
    return -1;
}

static int e4_inode(Ext4k *fs, uint32_t ino, uint8_t *out) {
    uint32_t g = (ino - 1) / fs->ipg, idx = (ino - 1) % fs->ipg;
    uint8_t gd[64];
    uint64_t gdt = (uint64_t)(fs->first_data + 1) * fs->blksz;
    if (e4_read(fs, gdt + (uint64_t)g * fs->dsize, gd, fs->dsize) < 0) return -1;
    uint64_t itab = E32(gd, 8);
    if (fs->is64) itab |= (uint64_t)E32(gd, 0x28) << 32;
    uint32_t n = fs->isize > 256 ? 256 : fs->isize;
    return e4_read(fs, itab * fs->blksz + (uint64_t)idx * fs->isize, out, n);
}

/*
 * Reserve an inode number using the ext4 inode bitmap. This is only a
 * reservation primitive: callers must initialize and persist the inode
 * before publishing a directory entry. Unsupported metadata-checksum and
 * bigalloc variants are refused rather than silently corrupting metadata.
 */
static int e4_alloc_inode(Ext4k *fs, uint32_t *out_ino) {
    if (!fs || !fs->ok || !out_ino || !fs->ipg || !fs->total_inodes ||
        fs->dsize < 32 || fs->dsize > 64 || fs->isize > 256)
        return -1;
    if (fs->feature_ro_compat & (0x10u | 0x200u | 0x400u)) return -2;
    if (!fs->free_inodes) return -3;

    uint64_t groups = ((uint64_t)fs->total_inodes + fs->ipg - 1) / fs->ipg;
    uint64_t gdt_off = (uint64_t)(fs->first_data + 1) * fs->blksz;
    uint8_t gd[64], sb[1024];
    for (uint64_t g = 0; g < groups; ++g) {
        if (e4_read(fs, gdt_off + g * fs->dsize, gd, fs->dsize) < 0)
            return -1;
        uint32_t free_count = E16(gd, 14);
        if (fs->is64) free_count |= (uint32_t)E16(gd, 46) << 16;
        if (!free_count) continue;

        uint64_t bitmap = E32(gd, 4);
        if (fs->is64) bitmap |= (uint64_t)E32(gd, 36) << 32;
        if (!bitmap || bitmap >= fs->total_blocks ||
            e4_read(fs, bitmap * fs->blksz, fs->blk, fs->blksz) < 0)
            return -1;

        uint64_t first_ino = g * fs->ipg;
        uint64_t span = fs->total_inodes > first_ino
                      ? fs->total_inodes - first_ino : 0;
        if (span > fs->ipg) span = fs->ipg;
        for (uint64_t bit = 0; bit < span; ++bit) {
            uint8_t mask = (uint8_t)(1u << (bit & 7));
            if (fs->blk[bit >> 3] & mask) continue;
            fs->blk[bit >> 3] |= mask;
            if (e4_write(fs, bitmap * fs->blksz, fs->blk, fs->blksz) < 0)
                return -1;

            --free_count;
            E16(gd, 14) = (uint16_t)free_count;
            if (fs->is64) E16(gd, 46) = (uint16_t)(free_count >> 16);
            if (e4_write(fs, gdt_off + g * fs->dsize, gd, fs->dsize) < 0)
                return -1;

            if (e4_read(fs, 1024, sb, sizeof(sb)) < 0) return -1;
            uint64_t super_free = E32(sb, 16);
            if (fs->is64) super_free |= (uint64_t)E32(sb, 0x160) << 32;
            if (!super_free) return -1;
            --super_free;
            E32(sb, 16) = (uint32_t)super_free;
            if (fs->is64) E32(sb, 0x160) = (uint32_t)(super_free >> 32);
            if (e4_write(fs, 1024, sb, sizeof(sb)) < 0) return -1;

            fs->free_inodes = (uint32_t)super_free;
            *out_ino = (uint32_t)(first_ino + bit + 1);
            return 0;
        }
        /* A contradictory group counter/bitmap is not safe to work around. */
        return -1;
    }
    return -3;
}


static int e4_inode_write(Ext4k *fs, uint32_t ino, const uint8_t *inode);

/*
 * Append one logical block to an inode whose extents are stored in the inode
 * root. Supports up to four root extents, merges physically adjacent blocks,
 * and refuses external extent trees rather than rewriting them incorrectly.
 * The caller owns rollback if a later write fails.
 */
static int e4_extent_append_root(Ext4k *fs, uint8_t *inode,
                                 uint32_t logical, uint64_t physical) {
    if (!fs || !fs->ok || !inode || !physical ||
        !(E32(inode, 32) & 0x80000u))
        return -1;
    uint8_t *root = inode + 40;
    if (E16(root, 0) != 0xF30A || E16(root, 6) != 0)
        return -2;
    uint16_t entries = E16(root, 2), max = E16(root, 4);
    if (max > 4 || entries > max || entries > 4) return -2;
    uint8_t *last = entries ? root + 12 + (entries - 1) * 12 : 0;
    if (last) {
        uint32_t start = E32(last, 0);
        uint16_t count = E16(last, 4) & 0x7FFFu;
        uint64_t pstart = ((uint64_t)E16(last, 6) << 32) | E32(last, 8);
        if (count && start + count == logical && pstart + count == physical &&
            count < 0x7FFFu) {
            E16(last, 4) = (uint16_t)(count + 1);
            return 0;
        }
        if (logical <= start) return -1;
    }
    if (entries >= max || entries >= 4) return -2;
    uint8_t *e = root + 12 + entries * 12;
    E32(e, 0) = logical;
    E16(e, 4) = 1;
    E16(e, 6) = (uint16_t)(physical >> 32);
    E32(e, 8) = (uint32_t)physical;
    E16(root, 2) = (uint16_t)(entries + 1);
    return 0;
}

/*
 * Grow a regular file and write bytes using inline extent-root entries.
 * Limited to files represented by at most four root extents. This intentionally
 * rejects unsupported extent trees and files larger than that format can map.
 * Allocation and metadata updates are not yet journaled, so failures may leak
 * a block; callers must not treat this as crash-atomic installation.
 */
static int e4_write_inode_grow(Ext4k *fs, uint32_t ino,
                               uint64_t pos, const void *src, uint64_t len) {
    if (!fs || !fs->ok || !src || !len || !ino ||
        fs->blksz != 4096 || fs->is64 || pos > UINT32_MAX ||
        len > UINT32_MAX - pos)
        return -1;
    uint8_t inode[256];
    if (e4_inode(fs, ino, inode) < 0 ||
        (E16(inode, 0) & 0xF000u) != 0x8000u ||
        !(E32(inode, 32) & 0x80000u))
        return -1;
    uint64_t old_size = e4_size(inode), end = pos + len;
    uint64_t old_blocks = (old_size + fs->blksz - 1) / fs->blksz;
    uint64_t new_blocks = (end + fs->blksz - 1) / fs->blksz;
    if (new_blocks > 0xFFFFFFFFu) return -1;

    /* Ensure the root has room before allocating any data blocks. */
    uint8_t *root = inode + 40;
    if (E16(root, 0) != 0xF30A || E16(root, 6) != 0 ||
        E16(root, 2) > 4 || E16(root, 4) > 4)
        return -2;
    /* Extent capacity is checked against actual physical adjacency below. */

    uint8_t zero[4096];
    for (uint32_t i = 0; i < fs->blksz; ++i) zero[i] = 0;
    for (uint64_t lb = old_blocks; lb < new_blocks; ++lb) {
        uint64_t physical = 0;
        if (e4_alloc_block(fs, &physical) < 0) return -3;
        if (e4_write(fs, physical * fs->blksz, zero, fs->blksz) < 0)
            return -1;
        if (e4_extent_append_root(fs, inode, (uint32_t)lb, physical) < 0)
            return -2;
        E32(inode, 28) += fs->blksz / 512;
    }

    if (end > old_size) {
        E32(inode, 4) = (uint32_t)end;
        if (fs->isize >= 112) E32(inode, 108) = (uint32_t)(end >> 32);
    }
    if (e4_inode_write(fs, ino, inode) < 0) return -1;
    if (e4_write_inode_at(fs, inode, pos, src, len) < 0) return -1;
    /* Persist size/accounting again after data I/O; errors remain non-atomic. */
    return e4_inode_write(fs, ino, inode);
}


/* Persist an inode record without disturbing adjacent inode-table entries. */
static int e4_inode_write(Ext4k *fs, uint32_t ino, const uint8_t *inode) {
    if (!fs || !fs->ok || !inode || !ino || ino > fs->total_inodes ||
        fs->isize > 256)
        return -1;
    uint32_t group = (ino - 1) / fs->ipg;
    uint32_t index = (ino - 1) % fs->ipg;
    uint8_t gd[64];
    uint64_t gdt = (uint64_t)(fs->first_data + 1) * fs->blksz;
    if (e4_read(fs, gdt + (uint64_t)group * fs->dsize, gd, fs->dsize) < 0)
        return -1;
    uint64_t inode_table = E32(gd, 8);
    if (fs->is64) inode_table |= (uint64_t)E32(gd, 0x28) << 32;
    return e4_write(fs,
                    inode_table * fs->blksz + (uint64_t)index * fs->isize,
                    inode, fs->isize);
}

/*
 * Allocate and initialize an empty regular-file inode. This does not publish
 * a name in a directory and must not be mistaken for complete file creation.
 * Bitmap/counter reservation and inode write are not transactional yet.
 */
static int e4_create_empty_file_inode(Ext4k *fs, uint32_t *out_ino) {
    if (!fs || !fs->ok || !out_ino || fs->isize < 128 || fs->isize > 256)
        return -1;
    uint32_t ino = 0;
    int rc = e4_alloc_inode(fs, &ino);
    if (rc < 0) return rc;

    uint8_t inode[256];
    for (uint32_t i = 0; i < fs->isize; ++i) inode[i] = 0;
    E16(inode, 0) = 0100644; /* regular file, rw-r--r-- */
    E32(inode, 4) = 0;
    E16(inode, 26) = 1;
    E32(inode, 28) = 0;
    E32(inode, 32) = 0x00080000u; /* EXT4_EXTENTS_FL */
    E16(inode, 40) = 0xF30A;
    E16(inode, 42) = 0;
    E16(inode, 44) = 4;
    E16(inode, 46) = 0;
    E32(inode, 48) = 0;
    if (fs->isize >= 130)
        E16(inode, 128) = (uint16_t)(fs->isize - 128 >= 32 ? 32 : fs->isize - 128);
    if (e4_inode_write(fs, ino, inode) < 0) return -1;
    *out_ino = ino;
    return 0;
}

/*
 * Allocate one ext4 data block from an existing block group's bitmap.
 * Deliberately refuses metadata-checksummed and bigalloc filesystems because
 * those require checksum/cluster accounting updates that this small VFS does
 * not yet implement. Callers must subsequently attach the block to an inode.
 */
static int e4_alloc_block(Ext4k *fs, uint64_t *out_block) {
    if (!fs || !fs->ok || !out_block || !fs->bpg ||
        fs->blksz < 1024 || fs->blksz > sizeof(fs->blk))
        return -1;
    /* RO_COMPAT_GDT_CSUM=0x10, RO_COMPAT_BIGALLOC=0x200,
       RO_COMPAT_METADATA_CSUM=0x400. Never corrupt unsupported metadata. */
    if (fs->feature_ro_compat & (0x10u | 0x200u | 0x400u)) return -2;
    if (!fs->free_blocks) return -3;

    uint64_t groups = (fs->total_blocks + fs->bpg - 1) / fs->bpg;
    uint8_t gd[64];
    uint8_t sb[1024];
    uint64_t gdt_off = (uint64_t)(fs->first_data + 1) * fs->blksz;
    for (uint64_t g = 0; g < groups; ++g) {
        if (e4_read(fs, gdt_off + g * fs->dsize, gd, fs->dsize) < 0)
            return -1;
        uint32_t free_lo = E16(gd, 12);
        uint32_t free_count = free_lo;
        if (fs->is64) free_count |= (uint32_t)E16(gd, 44) << 16;
        if (!free_count) continue;

        uint64_t bitmap = E32(gd, 0);
        if (fs->is64) bitmap |= (uint64_t)E32(gd, 32) << 32;
        if (!bitmap || bitmap >= fs->total_blocks ||
            e4_read(fs, bitmap * fs->blksz, fs->blk, fs->blksz) < 0)
            return -1;

        uint64_t first = (uint64_t)fs->first_data + g * fs->bpg;
        uint64_t span = fs->total_blocks > first ? fs->total_blocks - first : 0;
        if (span > fs->bpg) span = fs->bpg;
        for (uint64_t bit = 0; bit < span; ++bit) {
            uint8_t mask = (uint8_t)(1u << (bit & 7));
            if (fs->blk[bit >> 3] & mask) continue;
            fs->blk[bit >> 3] |= mask;
            if (e4_write(fs, bitmap * fs->blksz, fs->blk, fs->blksz) < 0)
                return -1;

            free_count--;
            E16(gd, 12) = (uint16_t)free_count;
            if (fs->is64) E16(gd, 44) = (uint16_t)(free_count >> 16);
            if (e4_write(fs, gdt_off + g * fs->dsize, gd, fs->dsize) < 0)
                return -1;
            if (e4_read(fs, 1024, sb, sizeof(sb)) < 0) return -1;
            uint64_t super_free = E32(sb, 12);
            if (fs->is64) super_free |= (uint64_t)E32(sb, 0x158) << 32;
            if (!super_free) return -1;
            --super_free;
            E32(sb, 12) = (uint32_t)super_free;
            if (fs->is64) E32(sb, 0x158) = (uint32_t)(super_free >> 32);
            if (e4_write(fs, 1024, sb, sizeof(sb)) < 0) return -1;
            fs->free_blocks = super_free;
            *out_block = first + bit;
            return 0;
        }
        /* A group counter that claims free blocks but has none in its bitmap
           indicates inconsistent metadata; do not silently keep scanning. */
        return -1;
    }
    return -3;
}

static uint64_t e4_size(uint8_t *inode) {
    return E32(inode, 4) | ((uint64_t)E32(inode, 108) << 32);
}

static uint64_t e4_map(Ext4k *fs, uint8_t *inode, uint32_t lb) {
    if (!(E32(inode, 32) & 0x80000)) return 0;
    uint8_t *node = inode + 40;
    for (int depth = 0; depth < 5; depth++) {
        if (E16(node, 0) != 0xF30A) return 0;
        uint16_t n = E16(node, 2);
        uint16_t depth_v = E16(node, 6);
        uint8_t *e = node + 12;
        if (depth_v == 0) {
            for (uint16_t i = 0; i < n; i++, e += 12) {
                uint32_t ee = E32(e, 0);
                uint16_t len = E16(e, 4) & 0x7FFF;
                if (lb >= ee && lb < ee + len) {
                    uint64_t start = ((uint64_t)E16(e, 6) << 32) | E32(e, 8);
                    return start + (lb - ee);
                }
            }
            return 0;
        }
        uint64_t leaf = 0;
        for (uint16_t i = 0; i < n; i++, e += 12) {
            if (E32(e, 0) <= lb)
                leaf = ((uint64_t)E16(e, 8) << 32) | E32(e, 4);
        }
        if (!leaf || e4_read(fs, leaf * fs->blksz, fs->blk, fs->blksz) < 0) return 0;
        node = fs->blk;
    }
    return 0;
}

static uint32_t e4_lookup(Ext4k *fs, uint32_t dir, const char *name) {
    uint8_t inode[256];
    if (e4_inode(fs, dir, inode) < 0) return 0;
    int nl = 0; while (name[nl]) nl++;
    uint64_t size = e4_size(inode);
    static uint8_t buf[4096];
    for (uint64_t pos = 0; pos < size; pos += fs->blksz) {
        uint64_t pb = e4_map(fs, inode, (uint32_t)(pos / fs->blksz));
        if (!pb || e4_read(fs, pb * fs->blksz, buf, fs->blksz) < 0) continue;
        for (uint32_t o = 0; o + 8 <= fs->blksz; ) {
            uint16_t rl = E16(buf, o + 4);
            if (rl < 8) break;
            uint8_t namelen = buf[o + 6];
            if (E32(buf, o) && namelen == nl) {
                int i = 0;
                while (i < nl) {
                    char a = (char)buf[o + 8 + i];
                    char b = name[i];
                    if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
                    if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
                    if (a != b) break;
                    i++;
                }
                if (i == nl) return E32(buf, o);
            }
            o += rl;
        }
    }
    return 0;
}



/* Best-effort inverse for a block successfully returned by e4_alloc_block. */
static int e4_free_block(Ext4k *fs, uint64_t block) {
    if (!fs || !fs->ok || fs->is64 || fs->blksz != 4096 ||
        block < fs->first_data || block >= fs->total_blocks)
        return -1;
    uint64_t group = block / fs->bpg, bit = block % fs->bpg;
    uint64_t gdt = (uint64_t)(fs->first_data + 1) * fs->blksz;
    uint8_t gd[64], sb[1024], bitmap[4096];
    if (e4_read(fs, gdt + group * fs->dsize, gd, fs->dsize) < 0) return -1;
    uint32_t bb = E32(gd, 0);
    if (!bb || e4_read(fs, (uint64_t)bb * fs->blksz, bitmap, fs->blksz) < 0)
        return -1;
    uint8_t mask = (uint8_t)(1u << (bit & 7));
    if (!(bitmap[bit >> 3] & mask)) return 0;
    bitmap[bit >> 3] &= (uint8_t)~mask;
    if (e4_write(fs, (uint64_t)bb * fs->blksz, bitmap, fs->blksz) < 0) return -1;
    E16(gd, 12) = (uint16_t)(E16(gd, 12) + 1);
    if (e4_write(fs, gdt + group * fs->dsize, gd, fs->dsize) < 0) return -1;
    if (e4_read(fs, 1024, sb, sizeof(sb)) < 0) return -1;
    E32(sb, 12) = E32(sb, 12) + 1;
    if (e4_write(fs, 1024, sb, sizeof(sb)) < 0) return -1;
    fs->free_blocks = E32(sb, 12);
    return 0;
}

/* Best-effort inverse for an inode successfully returned by e4_alloc_inode. */
static int e4_free_inode(Ext4k *fs, uint32_t ino) {
    if (!fs || !fs->ok || !ino || ino > fs->total_inodes ||
        fs->is64 || fs->dsize != 32)
        return -1;
    uint32_t group = (ino - 1) / fs->ipg, bit = (ino - 1) % fs->ipg;
    uint64_t gdt = (uint64_t)(fs->first_data + 1) * fs->blksz;
    uint8_t gd[64], sb[1024], bitmap[4096];
    if (e4_read(fs, gdt + (uint64_t)group * fs->dsize, gd, fs->dsize) < 0)
        return -1;
    uint32_t ib = E32(gd, 4);
    if (!ib || e4_read(fs, (uint64_t)ib * fs->blksz, bitmap, fs->blksz) < 0)
        return -1;
    uint8_t mask = (uint8_t)(1u << (bit & 7));
    if (!(bitmap[bit >> 3] & mask)) return 0;
    bitmap[bit >> 3] &= (uint8_t)~mask;
    if (e4_write(fs, (uint64_t)ib * fs->blksz, bitmap, fs->blksz) < 0) return -1;
    E16(gd, 14) = (uint16_t)(E16(gd, 14) + 1);
    if (e4_write(fs, gdt + (uint64_t)group * fs->dsize, gd, fs->dsize) < 0)
        return -1;
    if (e4_read(fs, 1024, sb, sizeof(sb)) < 0) return -1;
    E32(sb, 16) = E32(sb, 16) + 1;
    if (e4_write(fs, 1024, sb, sizeof(sb)) < 0) return -1;
    fs->free_inodes = E32(sb, 16);
    return 0;
}

/*
 * Insert a regular-file name into slack space in an existing directory block.
 * Does not grow directories; returns failure if no safe slot exists. Caller
 * must initialize the child inode first and handle rollback on failure.
 */
static int e4_dir_add_file(Ext4k *fs, uint32_t dir_ino,
                           const char *name, uint32_t child_ino) {
    if (!fs || !fs->ok || !name || !child_ino || !dir_ino ||
        fs->blksz < 1024 || fs->blksz > 4096)
        return -1;
    uint32_t nl = 0;
    while (name[nl] && nl < 256) {
        if (name[nl] == '/' || name[nl] == '\\') return -1;
        ++nl;
    }
    if (!nl || nl > 255 || name[0] == '.' || e4_lookup(fs, dir_ino, name))
        return -1;

    uint8_t dir[256];
    if (e4_inode(fs, dir_ino, dir) < 0) return -1;
    if ((E16(dir, 0) & 0xF000u) != 0x4000u) return -1;
    uint64_t size = e4_size(dir);
    uint32_t need = (8u + nl + 3u) & ~3u;
    uint8_t block[4096];

    for (uint64_t pos = 0; pos < size; pos += fs->blksz) {
        uint64_t pb = e4_map(fs, dir, (uint32_t)(pos / fs->blksz));
        if (!pb || e4_read(fs, pb * fs->blksz, block, fs->blksz) < 0)
            return -1;
        for (uint32_t off = 0; off + 8 <= fs->blksz; ) {
            uint32_t ino = E32(block, off);
            uint16_t rec = E16(block, off + 4);
            uint8_t old_nl = block[off + 6];
            if (rec < 8 || (rec & 3u) || rec > fs->blksz - off)
                return -1;
            uint32_t used = (8u + old_nl + 3u) & ~3u;
            if (ino == 0 && rec >= need) {
                E32(block, off) = child_ino;
                E16(block, off + 4) = rec;
                block[off + 6] = (uint8_t)nl;
                block[off + 7] = 1; /* EXT4_FT_REG_FILE */
                for (uint32_t j = 0; j < nl; ++j) block[off + 8 + j] = (uint8_t)name[j];
                if (e4_write(fs, pb * fs->blksz, block, fs->blksz) < 0) return -1;
                return 0;
            }
            if (ino != 0 && used <= rec && rec - used >= need) {
                E16(block, off + 4) = (uint16_t)used;
                uint32_t noff = off + used;
                E32(block, noff) = child_ino;
                E16(block, noff + 4) = (uint16_t)(rec - used);
                block[noff + 6] = (uint8_t)nl;
                block[noff + 7] = 1;
                for (uint32_t j = 0; j < nl; ++j) block[noff + 8 + j] = (uint8_t)name[j];
                if (e4_write(fs, pb * fs->blksz, block, fs->blksz) < 0) return -1;
                return 0;
            }
            off += rec;
        }
    }
    return -2; /* directory has no suitable slack; growth is not implemented */
}



/*
 * Extend a directory by one block and put a single file entry in that block.
 * Inline extent-root only; metadata rollback is best-effort and cannot recover
 * from power loss or a failed rollback write.
 */
static int e4_dir_grow_add_file(Ext4k *fs, uint32_t dir_ino,
                                const char *name, uint32_t child_ino) {
    if (!fs || !fs->ok || !name || !child_ino || fs->is64 ||
        fs->blksz != 4096) return -1;
    uint8_t dir[256], block[4096];
    if (e4_inode(fs, dir_ino, dir) < 0 ||
        (E16(dir, 0) & 0xF000u) != 0x4000u) return -1;
    uint64_t old_size = e4_size(dir);
    if (old_size % fs->blksz) return -1;
    uint64_t lb = old_size / fs->blksz;
    if (lb > UINT32_MAX) return -1;
    uint64_t physical = 0;
    if (e4_alloc_block(fs, &physical) < 0) return -1;
    for (uint32_t i = 0; i < fs->blksz; ++i) block[i] = 0;
    E32(block, 0) = child_ino;
    E16(block, 4) = (uint16_t)fs->blksz;
    uint32_t nl = 0;
    while (name[nl] && nl < 255) ++nl;
    if (!nl || name[nl]) { e4_free_block(fs, physical); return -1; }
    block[6] = (uint8_t)nl;
    block[7] = 1;
    for (uint32_t i = 0; i < nl; ++i) block[8 + i] = (uint8_t)name[i];
    if (e4_write(fs, physical * fs->blksz, block, fs->blksz) < 0) {
        e4_free_block(fs, physical); return -1;
    }
    if (e4_extent_append_root(fs, dir, (uint32_t)lb, physical) < 0) {
        e4_free_block(fs, physical); return -1;
    }
    E32(dir, 4) = (uint32_t)(old_size + fs->blksz);
    E32(dir, 28) += fs->blksz / 512;
    if (e4_inode_write(fs, dir_ino, dir) < 0) {
        e4_free_block(fs, physical); return -1;
    }
    return 0;
}

/*
 * Compose the existing inode and directory primitives into a named empty
 * regular file. Directory space is checked before reserving the inode, but
 * disk I/O failures can still leave an allocated, unreachable inode; full
 * journaling/rollback and directory growth remain future work.
 */
static int e4_create_empty_file(Ext4k *fs, uint32_t dir_ino,
                                const char *name, uint32_t *out_ino) {
    if (!fs || !fs->ok || !name || !out_ino || !dir_ino) return -1;
    uint32_t nl = 0;
    while (name[nl] && nl < 256) {
        if (name[nl] == '/' || name[nl] == '\\') return -1;
        ++nl;
    }
    if (!nl || nl > 255 || name[0] == '.' ||
        e4_lookup(fs, dir_ino, name))
        return -1;

    uint8_t dir[256], block[4096];
    if (e4_inode(fs, dir_ino, dir) < 0 ||
        (E16(dir, 0) & 0xF000u) != 0x4000u)
        return -1;
    uint64_t size = e4_size(dir);
    uint32_t need = (8u + nl + 3u) & ~3u;
    int space = 0;
    for (uint64_t pos = 0; pos < size && !space; pos += fs->blksz) {
        uint64_t pb = e4_map(fs, dir, (uint32_t)(pos / fs->blksz));
        if (!pb || e4_read(fs, pb * fs->blksz, block, fs->blksz) < 0)
            return -1;
        for (uint32_t off = 0; off + 8 <= fs->blksz; ) {
            uint32_t ino = E32(block, off);
            uint16_t rec = E16(block, off + 4);
            uint8_t old_nl = block[off + 6];
            if (rec < 8 || (rec & 3u) || rec > fs->blksz - off)
                return -1;
            uint32_t used = (8u + old_nl + 3u) & ~3u;
            if ((!ino && rec >= need) ||
                (ino && used <= rec && rec - used >= need)) {
                space = 1;
                break;
            }
            off += rec;
        }
    }
    if (!space) return -2;

    uint32_t ino = 0;
    if (e4_create_empty_file_inode(fs, &ino) < 0) return -1;
    int drc = e4_dir_add_file(fs, dir_ino, name, ino);\n    if (drc == -2) drc = e4_dir_grow_add_file(fs, dir_ino, name, ino);\n    if (drc < 0) { e4_free_inode(fs, ino); return -1; }
    *out_ino = ino;
    return 0;
}


/*
 * Create a named file and populate it. Metadata writes are not journaled;
 * failures may leave an incomplete published entry and require recovery.
 */
static int e4_create_file_with_data(Ext4k *fs, uint32_t dir_ino,
                                    const char *name, const void *data,
                                    uint64_t len, uint32_t *out_ino) {
    if (!fs || !name || !data || !len || !out_ino) return -1;
    uint32_t ino = 0;
    if (e4_create_empty_file(fs, dir_ino, name, &ino) < 0) return -1;
    if (e4_write_inode_grow(fs, ino, 0, data, len) < 0) return -1;
    *out_ino = ino;
    return 0;
}

#define VFS_MAX_ENT 256
typedef struct { char name[256]; uint8_t is_dir; uint32_t ino; } VfsEnt;
static VfsEnt vfs_ents[VFS_MAX_ENT];
static int vfs_nent;
static Ext4k g_ext4;
static int g_ext4_ok;
static uint32_t vfs_parent_ino;
#define VFS_MAX_DEPTH 64
static uint32_t vfs_dir_stack[VFS_MAX_DEPTH];
static int vfs_depth;
static char vfs_cwd[128];
static uint32_t vfs_cwd_ino = 2;

static int vfs_listdir(uint32_t dir_ino) {
    uint8_t inode[256];
    vfs_nent = 0;
    if (e4_inode(&g_ext4, dir_ino, inode) < 0) return -1;
    uint64_t size = e4_size(inode);
    static uint8_t buf[4096];
    for (uint64_t pos = 0; pos < size && vfs_nent < VFS_MAX_ENT; pos += g_ext4.blksz) {
        uint64_t pb = e4_map(&g_ext4, inode, (uint32_t)(pos / g_ext4.blksz));
        if (!pb || e4_read(&g_ext4, pb * g_ext4.blksz, buf, g_ext4.blksz) < 0) continue;
        for (uint32_t o = 0; o + 8 <= g_ext4.blksz && vfs_nent < VFS_MAX_ENT; ) {
            uint16_t rl = E16(buf, o + 4);
            if (rl < 8) break;
            uint32_t ino = E32(buf, o);
            uint8_t nl = buf[o + 6];
            uint8_t ft = buf[o + 7];
            if (ino && nl > 0) {
                if (!(nl == 1 && buf[o + 8] == '.') &&
                    !(nl == 2 && buf[o + 8] == '.' && buf[o + 9] == '.')) {
                    VfsEnt *e = &vfs_ents[vfs_nent++];
                    for (int i = 0; i < nl; i++) e->name[i] = (char)buf[o + 8 + i];
                    e->name[nl] = 0;
                    e->ino = ino;
                    /*
                     * Do not rely solely on the directory-entry file_type.
                     * Some ext filesystems omit that field or have entries
                     * whose type is unknown. The inode's i_mode is authoritative.
                     */
                    e->is_dir = (ft == 2);
                    {
                        uint8_t in2[256];
                        if (e4_inode(&g_ext4, ino, in2) == 0) {
                            uint16_t mode = E16(in2, 0) & 0xF000;
                            if (mode == 0x4000) e->is_dir = 1;
                            else if (mode == 0x8000 || mode == 0xA000 ||
                                     mode == 0x2000 || mode == 0x6000 ||
                                     mode == 0x1000 || mode == 0xC000)
                                e->is_dir = 0;
                        }
                    }
                }
            }
            o += rl;
        }
    }
    return vfs_nent;
}

static uint64_t vfs_find_ext4_lba(void) {
    uint8_t ent[512];
    if ((vb_ok ? blk_read_lba(2, ent) : ata_read_lba(2, ent)) == 0) {
        for (int i = 0; i < 4; i++) {
            uint8_t *e = ent + i * 128;
            uint64_t first = *(uint64_t *)(e + 32);
            uint64_t last  = *(uint64_t *)(e + 40);
            if (first && last > first) {
                Ext4k t;
                if (e4_mount(&t, first) == 0) return first;
            }
        }
    }
    return 264192;
}


static int vfs_chdir(const char *name) {
    if (!name || !name[0]) return -1;
    /* parent */
    if (name[0]=='.' && name[1]=='.' && name[2]==0) {
        if (vfs_depth <= 0) return 0;
        vfs_cwd_ino = vfs_dir_stack[--vfs_depth];
        vfs_parent_ino = vfs_depth > 0 ? vfs_dir_stack[vfs_depth - 1] : 2;
        return vfs_listdir(vfs_cwd_ino) >= 0 ? 0 : -1;
    }
    for (int i = 0; i < vfs_nent; i++) {
        const char *a = vfs_ents[i].name, *b = name;
        int ok = 1;
        while (*a || *b) { if (*a++ != *b++) { ok = 0; break; } }
        if (!ok) continue;
        if (!vfs_ents[i].is_dir) return -2;
        if (vfs_depth >= VFS_MAX_DEPTH) return -1;
        vfs_dir_stack[vfs_depth++] = vfs_cwd_ino;
        vfs_parent_ino = vfs_cwd_ino;
        vfs_cwd_ino = vfs_ents[i].ino;
        return vfs_listdir(vfs_cwd_ino) >= 0 ? 0 : -1;
    }
    return -1;
}

static int vfs_mount_c(void) {
    uint64_t lba = vfs_find_ext4_lba();
    if (e4_mount(&g_ext4, lba) < 0) {
        DbgPrint("fs: ATA/ext4 mount failed");
        return -1;
    }
    vfs_cwd_ino = 2;
    vfs_parent_ino = 2;
    vfs_depth = 0;
    vfs_cwd[0] = 67;
    vfs_cwd[1] = 58;
    vfs_cwd[2] = 92;
    vfs_cwd[3] = 0;
    vfs_listdir(2);
    g_ext4_ok = 1;
    DbgPrint("fs: C: volume root");
    return 0;
}
