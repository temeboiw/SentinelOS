#pragma once
#include "ata.h"
#include "virtio_blk.h"

#define E16(p,o) (*(uint16_t *)((uint8_t *)(p) + (o)))
#define E32(p,o) (*(uint32_t *)((uint8_t *)(p) + (o)))

typedef struct {
    uint64_t part_off;
    uint32_t blksz, ipg, isize, dsize, first_data;
    uint32_t inode_count, blocks_count, blocks_per_group;
    uint32_t feature_compat, feature_incompat, feature_ro_compat;
    uint32_t alloc_hint;
    int is64;
    uint8_t blk[4096];
    int ok;
} Ext4k;

static int e4_read(Ext4k *fs, uint64_t off, void *dst, uint64_t len) {
    if (vb_ok) return blk_read(fs->part_off + off, dst, len);
    return ata_read(fs->part_off + off, dst, len);
}

/* Low-level write primitive for future ext4 mutation routines.
 * This deliberately does not allocate ext4 blocks or modify directory entries. */
static int e4_write(Ext4k *fs, uint64_t off, const void *src, uint64_t len) {
    if (vb_ok) return blk_write(fs->part_off + off, src, len);
    return ata_write(fs->part_off + off, src, len);
}

static int e4_mount(Ext4k *fs, uint64_t part_lba) {
    uint8_t sb[1024];
    fs->part_off = part_lba * 512;
    fs->ok = 0;
    if (e4_read(fs, 1024, sb, 1024) < 0) return -1;
    if (E16(sb, 56) != 0xEF53) return -1;
    fs->blksz = 1024u << E32(sb, 24);
    fs->inode_count = E32(sb, 0);
    fs->blocks_count = E32(sb, 4);
    fs->first_data = E32(sb, 20);
    fs->blocks_per_group = E32(sb, 32);
    fs->ipg = E32(sb, 40);
    fs->isize = E16(sb, 88);
    if (fs->isize < 128) fs->isize = 128;
    fs->feature_compat = E32(sb, 92);
    fs->feature_incompat = E32(sb, 96);
    fs->feature_ro_compat = E32(sb, 100);
    fs->alloc_hint = fs->first_data + 1;
    fs->is64 = (fs->feature_incompat & 0x80) != 0;
    fs->dsize = fs->is64 ? E16(sb, 254) : 32;
    if (!fs->dsize) fs->dsize = 32;
    if (fs->blksz > 4096 || !fs->ipg) return -1;
    fs->ok = 1;
    return 0;
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

/* Mutation is intentionally restricted to a small ext4-compatible subset.
 * We refuse layouts needing checksums, 64-bit descriptors, journaling recovery,
 * flex/meta block groups, or other unimplemented metadata semantics. */
static int e4_can_mutate(Ext4k *fs) {
    if (!fs || !fs->ok || fs->is64 || fs->dsize != 32 ||
        fs->isize < 128 || fs->isize > 256 ||
        !fs->inode_count || !fs->blocks_count || !fs->blocks_per_group ||
        !fs->ipg || fs->ipg > fs->blksz*8u || (fs->blksz != 1024 && fs->blksz != 2048 &&
                     fs->blksz != 4096)) return 0;
    /* Mutations require extent-backed files and plain 32-byte group
       descriptors. Unknown incompat features are not safe to write around. */
    if ((fs->feature_incompat & 0x42u) != 0x42u) return 0; /* FILETYPE + EXTENTS */
    if (fs->feature_incompat & ~0x42u) return 0;
    if (fs->feature_compat & 0x4u) return 0; /* journal needs transactions */
    if (fs->feature_ro_compat & (0x10u | 0x200u | 0x400u))
        return 0; /* gdt_csum, bigalloc, metadata_csum */
    return 1;
}

static int e4_write_inode(Ext4k *fs, uint32_t ino, const uint8_t *src) {
    if (!e4_can_mutate(fs) || !ino || ino > fs->inode_count) return -1;
    uint32_t group = (ino - 1) / fs->ipg;
    uint32_t index = (ino - 1) % fs->ipg;
    uint8_t gd[32];
    uint64_t gdt = (uint64_t)(fs->first_data + 1) * fs->blksz;
    if (e4_read(fs, gdt + (uint64_t)group * 32, gd, sizeof gd) < 0) return -1;
    uint32_t table = E32(gd, 8);
    if (!table) return -1;
    return e4_write(fs, (uint64_t)table * fs->blksz +
                    (uint64_t)index * fs->isize, src, fs->isize);
}

/* Allocate one inode from a group's inode bitmap. The caller owns rollback if
 * a later directory update fails. Returns inode number or 0 on failure. */
static uint32_t e4_alloc_inode(Ext4k *fs, int is_dir) {
    if (!e4_can_mutate(fs)) return 0;
    uint8_t sb[1024], gd[32], bitmap[4096];
    if (e4_read(fs, 1024, sb, sizeof sb) < 0) return 0;
    uint32_t groups = (fs->inode_count + fs->ipg - 1) / fs->ipg;
    uint64_t gdt = (uint64_t)(fs->first_data + 1) * fs->blksz;
    if (!E32(sb,16)) return 0;
    uint32_t first_ino = E32(sb, 84);
    if (first_ino < 11) first_ino = 11;
    for (uint32_t g = 0; g < groups; ++g) {
        if (e4_read(fs, gdt + (uint64_t)g * 32, gd, sizeof gd) < 0) return 0;
        if (E16(gd, 14) == 0) continue;
        uint32_t bitmap_block = E32(gd, 4);
        if (!bitmap_block || e4_read(fs, (uint64_t)bitmap_block * fs->blksz,
                                     bitmap, fs->blksz) < 0) return 0;
        uint32_t valid = fs->ipg;
        uint32_t remain = fs->inode_count - g * fs->ipg;
        if (valid > remain) valid = remain;
        if (valid > fs->blksz*8u) return 0;
        for (uint32_t b = 0; b < valid; ++b) {
            uint32_t ino = g * fs->ipg + b + 1;
            if (ino < first_ino || (bitmap[b >> 3] & (1u << (b & 7)))) continue;
            bitmap[b >> 3] |= (uint8_t)(1u << (b & 7));
            if (e4_write(fs, (uint64_t)bitmap_block * fs->blksz,
                         bitmap, fs->blksz) < 0) return 0;
            E16(gd, 14) = (uint16_t)(E16(gd, 14) - 1);
            if (is_dir) E16(gd, 16) = (uint16_t)(E16(gd, 16) + 1);
            if (e4_write(fs, gdt + (uint64_t)g * 32, gd, sizeof gd) < 0) return 0;
            E32(sb, 16) = E32(sb, 16) - 1;
            if (e4_write(fs, 1024, sb, sizeof sb) < 0) return 0;
            return ino;
        }
    }
    return 0;
}

static uint64_t e4_alloc_block(Ext4k *fs) {
    if (!e4_can_mutate(fs) || fs->blocks_count <= fs->first_data) return 0;
    uint8_t sb[1024], gd[32], bitmap[4096];
    if (e4_read(fs, 1024, sb, sizeof sb) < 0 || !E32(sb,12)) return 0;
    uint32_t groups = (fs->blocks_count - fs->first_data +
                       fs->blocks_per_group - 1) / fs->blocks_per_group;
    uint64_t gdt = (uint64_t)(fs->first_data + 1) * fs->blksz;
    uint32_t hint_group = fs->alloc_hint >= fs->first_data
        ? (fs->alloc_hint - fs->first_data) / fs->blocks_per_group : 0;
    if (hint_group >= groups) hint_group = 0;
    for (uint32_t step = 0; step < groups; ++step) {
        uint32_t g = (hint_group + step) % groups;
        if (e4_read(fs, gdt + (uint64_t)g * 32, gd, sizeof gd) < 0) return 0;
        if (!E16(gd, 12)) continue;
        uint32_t bb = E32(gd, 0);
        if (!bb || e4_read(fs, (uint64_t)bb * fs->blksz, bitmap, fs->blksz) < 0) return 0;
        uint64_t group_start = (uint64_t)fs->first_data +
                               (uint64_t)g * fs->blocks_per_group;
        uint32_t valid = fs->blocks_per_group;
        if (group_start + valid > fs->blocks_count)
            valid = (uint32_t)(fs->blocks_count - group_start);
        if (valid > fs->blksz * 8u) return 0;
        uint32_t begin = 0;
        if (step == 0 && fs->alloc_hint > group_start)
            begin = (uint32_t)(fs->alloc_hint - group_start);
        for (uint32_t pass = 0; pass < (step == 0 && begin ? 2u : 1u); ++pass) {
            uint32_t lo = pass == 0 ? begin : 0;
            uint32_t hi = pass == 0 && begin ? valid : (pass == 1 ? begin : valid);
            for (uint32_t b = lo; b < hi; ++b) {
                uint64_t blockno = group_start + b;
                if (blockno < fs->first_data ||
                    (bitmap[b >> 3] & (1u << (b & 7)))) continue;
                bitmap[b >> 3] |= (uint8_t)(1u << (b & 7));
                if (e4_write(fs, (uint64_t)bb * fs->blksz, bitmap, fs->blksz) < 0) return 0;
                E16(gd, 12) = (uint16_t)(E16(gd, 12) - 1);
                if (e4_write(fs, gdt + (uint64_t)g * 32, gd, sizeof gd) < 0) return 0;
                E32(sb, 12) = E32(sb, 12) - 1;
                if (e4_write(fs, 1024, sb, sizeof sb) < 0) return 0;
                fs->alloc_hint = blockno + 1;
                if (fs->alloc_hint >= fs->blocks_count) fs->alloc_hint = fs->first_data;
                return blockno;
            }
        }
    }
    return 0;
}

/* Insert one initialized data block into an inode's inline extent root.
 * Tree expansion is deliberately refused; callers must not create corrupt trees. */
static int e4_add_inline_extent(Ext4k *fs, uint8_t *inode,
                                uint32_t logical, uint64_t physical) {
    if (!e4_can_mutate(fs) || !(E32(inode, 32) & 0x80000)) return -1;
    uint8_t *root = inode + 40;
    if (E16(root, 0) != 0xF30A || E16(root, 6) != 0) return -1;
    uint16_t n = E16(root, 2), max = E16(root, 4);
    if (max > 4 || n > max || physical > 0xFFFFFFFFFFFFULL) return -1;
    uint8_t *ent = root + 12;
    for (uint16_t i = 0; i < n; ++i, ent += 12) {
        uint32_t start = E32(ent, 0);
        uint16_t count = E16(ent, 4) & 0x7FFF;
        uint64_t pstart = ((uint64_t)E16(ent, 6) << 32) | E32(ent, 8);
        if (logical >= start && logical < start + count) return 0;
        if (logical == start + count && physical == pstart + count && count < 0x7FFF) {
            E16(ent, 4) = (uint16_t)(count + 1);
            return 0;
        }
        if (start == logical + 1 && physical + 1 == pstart && count < 0x7FFF) {
            E32(ent, 0) = logical;
            E16(ent, 6) = (uint16_t)(physical >> 32);
            E32(ent, 8) = (uint32_t)physical;
            E16(ent, 4) = (uint16_t)(count + 1);
            return 0;
        }
    }
    if (n >= max) return -1;
    uint16_t at = n;
    while (at && E32(root + 12 + (at - 1) * 12, 0) > logical) {
        for (int j = 0; j < 12; ++j)
            root[12 + at * 12 + j] = root[12 + (at - 1) * 12 + j];
        --at;
    }
    ent = root + 12 + at * 12;
    for (int j = 0; j < 12; ++j) ent[j] = 0;
    E32(ent, 0) = logical;
    E16(ent, 4) = 1;
    E16(ent, 6) = (uint16_t)(physical >> 32);
    E32(ent, 8) = (uint32_t)physical;
    E16(root, 2) = (uint16_t)(n + 1);
    return 0;
}

static int e4_free_block(Ext4k *fs, uint64_t blockno) {
    if (!e4_can_mutate(fs) || blockno < fs->first_data ||
        blockno >= fs->blocks_count) return -1;
    uint32_t rel=(uint32_t)(blockno-fs->first_data);
    uint32_t g=rel/fs->blocks_per_group, b=rel%fs->blocks_per_group;
    uint8_t sb[1024],gd[32],bitmap[4096];
    uint64_t gdt=(uint64_t)(fs->first_data+1)*fs->blksz;
    if (e4_read(fs,1024,sb,sizeof sb)<0 ||
        e4_read(fs,gdt+(uint64_t)g*32,gd,sizeof gd)<0) return -1;
    uint32_t bb=E32(gd,0);
    if (!bb || e4_read(fs,(uint64_t)bb*fs->blksz,bitmap,fs->blksz)<0 ||
        b>=fs->blksz*8u || !(bitmap[b>>3]&(1u<<(b&7)))) return -1;
    if (E16(gd,12)==0xFFFFu || E32(sb,12)==0xFFFFFFFFu) return -1;
    bitmap[b>>3]&=(uint8_t)~(1u<<(b&7));
    if (e4_write(fs,(uint64_t)bb*fs->blksz,bitmap,fs->blksz)<0) return -1;
    E16(gd,12)=(uint16_t)(E16(gd,12)+1);
    if (e4_write(fs,gdt+(uint64_t)g*32,gd,sizeof gd)<0) return -1;
    E32(sb,12)=E32(sb,12)+1;
    return e4_write(fs,1024,sb,sizeof sb);
}

static int e4_free_inode_bit(Ext4k *fs, uint32_t ino, int is_dir) {
    if (!e4_can_mutate(fs) || !ino || ino > fs->inode_count) return -1;
    uint32_t g = (ino - 1) / fs->ipg, b = (ino - 1) % fs->ipg;
    uint8_t sb[1024], gd[32], bitmap[4096];
    uint64_t gdt = (uint64_t)(fs->first_data + 1) * fs->blksz;
    if (e4_read(fs, gdt + (uint64_t)g * 32, gd, sizeof gd) < 0 ||
        e4_read(fs, 1024, sb, sizeof sb) < 0) return -1;
    uint32_t bb = E32(gd, 4);
    if (!bb || b>=fs->blksz*8u ||
        e4_read(fs, (uint64_t)bb * fs->blksz, bitmap, fs->blksz) < 0 ||
        !(bitmap[b >> 3] & (1u << (b & 7))) ||
        E16(gd,14)==0xFFFFu || E32(sb,16)==0xFFFFFFFFu ||
        (is_dir && !E16(gd,16))) return -1;
    bitmap[b >> 3] &= (uint8_t)~(1u << (b & 7));
    if (e4_write(fs, (uint64_t)bb * fs->blksz, bitmap, fs->blksz) < 0) return -1;
    E16(gd, 14) = (uint16_t)(E16(gd, 14) + 1);
    if (is_dir) E16(gd,16) = (uint16_t)(E16(gd,16)-1);
    if (e4_write(fs, gdt + (uint64_t)g * 32, gd, sizeof gd) < 0) return -1;
    E32(sb, 16) = E32(sb, 16) + 1;
    return e4_write(fs, 1024, sb, sizeof sb);
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
                while (i < nl && buf[o + 8 + i] == (uint8_t)name[i]) i++;
                if (i == nl) return E32(buf, o);
            }
            o += rl;
        }
    }
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
