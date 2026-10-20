#pragma once
#include "ata.h"
#include "virtio_blk.h"

#define E16(p,o) (*(uint16_t *)((uint8_t *)(p) + (o)))
#define E32(p,o) (*(uint32_t *)((uint8_t *)(p) + (o)))

typedef struct {
    uint64_t part_off;
    uint32_t blksz, ipg, isize, dsize, first_data;
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
    fs->first_data = E32(sb, 20);
    fs->ipg = E32(sb, 40);
    fs->isize = E16(sb, 88);
    if (fs->isize < 128) fs->isize = 128;
    fs->is64 = (E32(sb, 96) & 0x80) != 0;
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
