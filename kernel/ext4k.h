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
                while (i < nl) {
                    uint8_t a = buf[o + 8 + i], b = (uint8_t)name[i];
                    if (a >= 'A' && a <= 'Z') a = (uint8_t)(a + ('a' - 'A'));
                    if (b >= 'A' && b <= 'Z') b = (uint8_t)(b + ('a' - 'A'));
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

#define VFS_MAX_ENT 64
typedef struct { char name[32]; uint8_t is_dir; uint32_t ino; } VfsEnt;
static VfsEnt vfs_ents[VFS_MAX_ENT];
static int vfs_nent;
static Ext4k g_ext4;
static int g_ext4_ok;
static uint32_t vfs_parent_ino;
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
            if (ino && nl > 0 && nl < 32) {
                if (!(nl == 1 && buf[o + 8] == '.') &&
                    !(nl == 2 && buf[o + 8] == '.' && buf[o + 9] == '.')) {
                    VfsEnt *e = &vfs_ents[vfs_nent++];
                    for (int i = 0; i < nl; i++) e->name[i] = (char)buf[o + 8 + i];
                    e->name[nl] = 0;
                    e->ino = ino;
                    e->is_dir = (ft == 2);
                    if (!ft) {
                        uint8_t in2[256];
                        if (e4_inode(&g_ext4, ino, in2) == 0)
                            e->is_dir = ((E16(in2, 0) & 0xF000) == 0x4000);
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


/*
 * Resolve paths on the C: volume. Supports C:\absolute paths, either slash,
 * relative paths, "." and "..". Directory lookups are case-insensitive.
 */
static uint32_t vfs_resolve_path(const char *path) {
    if (!g_ext4_ok || !path || !path[0]) return 0;
    uint32_t dir = vfs_cwd_ino, i = 0;
    if ((path[0] == 'C' || path[0] == 'c') && path[1] == ':') {
        i = 2;
        if (path[i] == '\\' || path[i] == '/') dir = 2;
    } else if (path[0] == '\\' || path[0] == '/') {
        dir = 2;
    }
    while (path[i] == '\\' || path[i] == '/') i++;

    char component[64];
    while (path[i]) {
        uint32_t n = 0;
        while (path[i] && path[i] != '\\' && path[i] != '/') {
            if (n + 1 >= sizeof component) return 0;
            component[n++] = path[i++];
        }
        component[n] = 0;
        while (path[i] == '\\' || path[i] == '/') i++;
        if (!n || (n == 1 && component[0] == '.')) continue;
        if (n == 2 && component[0] == '.' && component[1] == '.') {
            uint32_t parent = e4_lookup(&g_ext4, dir, "..");
            if (parent) dir = parent;
            continue;
        }
        uint32_t child = e4_lookup(&g_ext4, dir, component);
        if (!child) return 0;
        uint8_t inode[256];
        if (e4_inode(&g_ext4, child, inode) < 0) return 0;
        if (path[i] && ((E16(inode, 0) & 0xF000) != 0x4000)) return 0;
        dir = child;
    }
    return dir;
}

/* Change CWD to an existing directory using a Windows-style path. */
static int vfs_chdir(const char *name) {
    if (!name || !name[0]) return -1;
    uint32_t ino = vfs_resolve_path(name);
    if (!ino) return -1;
    uint8_t inode[256];
    if (e4_inode(&g_ext4, ino, inode) < 0 ||
        (E16(inode, 0) & 0xF000) != 0x4000)
        return -2;

    char normalized[128];
    int out = 0, i = 0;
    normalized[out++] = 'C'; normalized[out++] = ':'; normalized[out++] = '\\';
    if ((name[0] == 'C' || name[0] == 'c') && name[1] == ':') i = 2;
    while (name[i] == '\\' || name[i] == '/') i++;
    while (name[i] && out < (int)sizeof(normalized) - 1) {
        char ch = name[i++];
        if (ch == '/') ch = '\\';
        if (ch == '\\') {
            if (out > 3 && normalized[out - 1] == '\\') continue;
            if (out > 3 && normalized[out - 1] != '\\') normalized[out++] = '\\';
        } else {
            normalized[out++] = ch;
        }
    }
    while (out > 3 && normalized[out - 1] == '\\') out--;
    normalized[out] = 0;
    for (int j = 0; j <= out; j++) vfs_cwd[j] = normalized[j];

    vfs_cwd_ino = ino;
    vfs_parent_ino = e4_lookup(&g_ext4, ino, "..");
    if (!vfs_parent_ino) vfs_parent_ino = 2;
    return vfs_listdir(vfs_cwd_ino) >= 0 ? 0 : -1;
}

/* Win32-compatible basic file attributes for the read-only ext4 volume. */
static uint32_t vfs_get_attributes(const char *path) {
    uint32_t ino = vfs_resolve_path(path);
    uint8_t inode[256];
    if (!ino || e4_inode(&g_ext4, ino, inode) < 0) return 0xFFFFFFFFu;
    uint16_t mode = E16(inode, 0) & 0xF000;
    if (mode == 0x4000) return 0x10u; /* FILE_ATTRIBUTE_DIRECTORY */
    if (mode == 0x8000) return 0x80u; /* FILE_ATTRIBUTE_NORMAL */
    return 0xFFFFFFFFu;
}

#define VFS_OPEN_MAX 32
typedef struct { uint32_t ino; uint64_t offset; int used; } VfsOpenFile;
static VfsOpenFile vfs_open_files[VFS_OPEN_MAX];
static uint64_t vfs_file_size(const char *path) {
    uint32_t ino = vfs_resolve_path(path);
    uint8_t inode[256];
    if (!ino || e4_inode(&g_ext4, ino, inode) < 0 ||
        (E16(inode, 0) & 0xF000) != 0x8000) return 0;
    return e4_size(inode);
}
static int vfs_open_read(const char *path) {
    uint32_t ino=vfs_resolve_path(path); if(!ino)return -1;
    uint8_t inode[256]; if(e4_inode(&g_ext4,ino,inode)<0 || (E16(inode,0)&0xF000)!=0x8000)return -1;
    for(int i=0;i<VFS_OPEN_MAX;i++)if(!vfs_open_files[i].used){vfs_open_files[i].used=1;vfs_open_files[i].ino=ino;vfs_open_files[i].offset=0;return i+1;}
    return -1;
}
static int64_t vfs_read_open(int handle,void *dst,uint32_t length) {
    if(handle<1 || handle>VFS_OPEN_MAX || !dst || !length || !vfs_open_files[handle-1].used)return -1;
    VfsOpenFile *f=&vfs_open_files[handle-1];uint8_t inode[256];if(e4_inode(&g_ext4,f->ino,inode)<0)return -1;
    uint64_t size=e4_size(inode);if(f->offset>=size)return 0;if(length>size-f->offset)length=(uint32_t)(size-f->offset);
    uint8_t block[4096];uint32_t copied=0;
    while(copied<length){uint64_t pos=f->offset;uint32_t off=(uint32_t)(pos%g_ext4.blksz),take=g_ext4.blksz-off;if(take>length-copied)take=length-copied;
        uint64_t physical=e4_map(&g_ext4,inode,(uint32_t)(pos/g_ext4.blksz));
        if(!physical || e4_read(&g_ext4,physical*g_ext4.blksz,block,g_ext4.blksz)<0)return copied?(int64_t)copied:-1;
        for(uint32_t j=0;j<take;j++)((uint8_t*)dst)[copied+j]=block[off+j];copied+=take;f->offset+=take;
    }return copied;
}
static int vfs_close_open(int handle){if(handle<1||handle>VFS_OPEN_MAX||!vfs_open_files[handle-1].used)return -1;vfs_open_files[handle-1].used=0;return 0;}

static int vfs_mount_c(void) {
    uint64_t lba = vfs_find_ext4_lba();
    if (e4_mount(&g_ext4, lba) < 0) {
        DbgPrint("fs: ATA/ext4 mount failed");
        return -1;
    }
    vfs_cwd_ino = 2;
    vfs_parent_ino = 2;
    vfs_cwd[0] = 67;
    vfs_cwd[1] = 58;
    vfs_cwd[2] = 92;
    vfs_cwd[3] = 0;
    vfs_listdir(2);
    g_ext4_ok = 1;
    DbgPrint("fs: C: volume root");
    return 0;
}
