/* mkfat.c - builds a minimal FAT12 disk image containing given files.
 *
 * Usage: mkfat <output.img> <image_size_mb> <src1:dst1> [<src2:dst2> ...]
 *
 * Produces a FAT12 filesystem with a boot sector, two FATs, a fixed root
 * directory, and a data region. Directories in dest paths are created
 * automatically. Intended for building UEFI ESP test images in an
 * environment that lacks mtools/mkfs.vfat.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#define SECTOR 512

typedef struct {
    FILE *f;
    uint8_t *fat;
    uint32_t fat_sectors;
    uint32_t fat_start;       /* LBA of first FAT */
    uint32_t root_start;      /* LBA of root directory */
    uint32_t root_sectors;
    uint32_t data_start;      /* LBA of data region (cluster 2) */
    uint32_t cluster_size;    /* sectors per cluster */
    uint32_t total_sectors;
    uint32_t next_cluster;    /* next free cluster */
    uint8_t *secbuf;
} Fat;

static void die(const char *m) { perror(m); exit(1); }

static void sec_write(Fat *fs, uint32_t lba, const void *data) {
    if (fseeko(fs->f, (off_t)lba * SECTOR, SEEK_SET) != 0) die("fseeko");
    if (fwrite(data, SECTOR, 1, fs->f) != 1) die("fwrite");
}

static void sec_read(Fat *fs, uint32_t lba, void *data) {
    if (fseeko(fs->f, (off_t)lba * SECTOR, SEEK_SET) != 0) die("fseeko");
    if (fread(data, SECTOR, 1, fs->f) != 1) die("fread");
}

static uint16_t fat12_get(Fat *fs, uint32_t cl) {
    uint32_t off = cl + cl / 2;
    uint8_t b0 = fs->fat[off];
    uint8_t b1 = fs->fat[off + 1];
    uint16_t v = (cl & 1) ? ((b0 >> 4) | (b1 << 4)) : (b0 | (b1 << 8));
    return v & 0xFFF;
}

static void fat12_set(Fat *fs, uint32_t cl, uint16_t v) {
    uint32_t off = cl + cl / 2;
    if (cl & 1) {
        fs->fat[off] = (fs->fat[off] & 0x0F) | ((v << 4) & 0xF0);
        fs->fat[off + 1] = (uint8_t)(v >> 4);
    } else {
        fs->fat[off] = (uint8_t)(v & 0xFF);
        fs->fat[off + 1] = (fs->fat[off + 1] & 0xF0) | ((v >> 8) & 0x0F);
    }
}

static uint32_t alloc_cluster(Fat *fs) {
    uint32_t c = fs->next_cluster++;
    fat12_set(fs, c, 0xFFF);
    return c;
}

static uint32_t write_file_data(Fat *fs, const uint8_t *data, uint32_t len) {
    if (len == 0) return 0;
    uint32_t first = alloc_cluster(fs);
    uint32_t cur = first;
    uint32_t off = 0;
    while (off < len) {
        uint32_t n = len - off;
        if (n > SECTOR) n = SECTOR;
        memset(fs->secbuf, 0, SECTOR);
        memcpy(fs->secbuf, data + off, n);
        sec_write(fs, fs->data_start + (cur - 2) * fs->cluster_size, fs->secbuf);
        off += n;
        if (off < len) {
            uint32_t nxt = alloc_cluster(fs);
            fat12_set(fs, cur, (uint16_t)nxt);
            cur = nxt;
        }
    }
    return first;
}

static void dir_write_entry(Fat *fs, uint32_t dircl, uint32_t entry_off,
                            const uint8_t *name11, uint8_t attr,
                            uint32_t first_cl, uint32_t fsize) {
    uint32_t sector = (dircl == 0) ? fs->root_start + (entry_off / SECTOR)
                                   : fs->data_start + (dircl - 2) * fs->cluster_size
                                   + (entry_off / SECTOR);
    uint32_t off_in = entry_off % SECTOR;
    sec_read(fs, sector, fs->secbuf);
    uint8_t *e = fs->secbuf + off_in;
    memcpy(e, name11, 11);
    e[11] = attr;
    memset(e + 12, 0, 10);
    e[26] = (uint8_t)(first_cl & 0xFF);
    e[27] = (uint8_t)((first_cl >> 8) & 0xFF);
    e[28] = 0;
    e[29] = 0;
    e[26] = (uint8_t)(first_cl & 0xFF);
    e[27] = (uint8_t)((first_cl >> 8) & 0xFF);
    e[28] = (uint8_t)(fsize & 0xFF);
    e[29] = (uint8_t)((fsize >> 8) & 0xFF);
    e[30] = (uint8_t)((fsize >> 16) & 0xFF);
    e[31] = (uint8_t)((fsize >> 24) & 0xFF);
    sec_write(fs, sector, fs->secbuf);
}

static void name_encode(const char *path, uint8_t *out11) {
    memset(out11, ' ', 11);
    const char *dot = strrchr(path, '.');
    const char *name_end = dot ? dot : path + strlen(path);
    int ni = 0;
    for (const char *p = path; p < name_end && ni < 8; p++) {
        if (*p == '/' || *p == '\\') continue;
        out11[ni++] = (uint8_t)toupper((unsigned char)*p);
    }
    if (dot) {
        int di = 0;
        for (const char *p = dot + 1; *p && di < 3; p++) {
            out11[8 + di++] = (uint8_t)toupper((unsigned char)*p);
        }
    }
}

static uint32_t ensure_dirs(Fat *fs, const char *dst) {
    char comp[64][64]; int ncomp = 0;
    char *t2 = strdup(dst);
    char *sv2 = NULL;
    char *t3 = strtok_r(t2, "/", &sv2);
    while (t3 && ncomp < 64) { strncpy(comp[ncomp++], t3, 63); t3 = strtok_r(NULL, "/", &sv2); }
    free(t2);

    uint32_t dir_cl = 0; /* 0 means root directory for FAT12 */
    for (int i = 0; i < ncomp; i++) {
        uint8_t n11[11]; name_encode(comp[i], n11);
        uint32_t entries = (dir_cl == 0) ? 256 : (SECTOR / 32);
        uint32_t base = (dir_cl == 0) ? fs->root_start
                                      : fs->data_start + (dir_cl - 2) * fs->cluster_size;
        uint32_t found = 0xFFFFFFFF;
        for (uint32_t e = 0; e < entries; e++) {
            uint32_t sec = base + (e * 32) / SECTOR;
            uint32_t off = (e * 32) % SECTOR;
            sec_read(fs, sec, fs->secbuf);
            uint8_t *ent = fs->secbuf + off;
            if (ent[0] == 0x00) break;
            if (ent[0] == 0xE5) continue;
            if (memcmp(ent, n11, 11) == 0) { found = e; break; }
        }
        if (found != 0xFFFFFFFF) {
            uint32_t sec = base + (found * 32) / SECTOR;
            uint32_t off = (found * 32) % SECTOR;
            sec_read(fs, sec, fs->secbuf);
            uint8_t *ent = fs->secbuf + off;
            dir_cl = ent[26] | (ent[27] << 8);
            continue;
        }
        uint32_t newcl = alloc_cluster(fs);
        memset(fs->secbuf, 0, SECTOR);
        uint8_t *dot_e = fs->secbuf;
        memcpy(dot_e, n11, 11); dot_e[11] = 0x10;
        dot_e[26] = (uint8_t)(newcl & 0xFF); dot_e[27] = (uint8_t)((newcl >> 8) & 0xFF);
        uint8_t *dotdot_e = fs->secbuf + 32;
        dotdot_e[0] = '.'; dotdot_e[1] = '.'; memset(dotdot_e + 2, ' ', 9);
        dotdot_e[11] = 0x10;
        uint32_t parent = (dir_cl == 0) ? 0 : dir_cl;
        dotdot_e[26] = (uint8_t)(parent & 0xFF); dotdot_e[27] = (uint8_t)((parent >> 8) & 0xFF);
        sec_write(fs, fs->data_start + (newcl - 2) * fs->cluster_size, fs->secbuf);
        uint32_t parent_entries = (dir_cl == 0) ? 256 : (SECTOR / 32);
        uint32_t pbase = (dir_cl == 0) ? fs->root_start
                                       : fs->data_start + (dir_cl - 2) * fs->cluster_size;
        uint32_t slot = 0xFFFFFFFF;
        for (uint32_t e = 0; e < parent_entries; e++) {
            uint32_t sec = pbase + (e * 32) / SECTOR;
            uint32_t off = (e * 32) % SECTOR;
            sec_read(fs, sec, fs->secbuf);
            uint8_t *ent = fs->secbuf + off;
            if (ent[0] == 0x00 || ent[0] == 0xE5) { slot = e; break; }
        }
        if (slot == 0xFFFFFFFF) { fprintf(stderr, "dir full\n"); exit(1); }
        dir_write_entry(fs, dir_cl, slot * 32, n11, 0x10, newcl, 0);
        dir_cl = newcl;
    }
    return dir_cl;
}

static void add_file(Fat *fs, const char *src, const char *dst) {
    FILE *sf = fopen(src, "rb");
    if (!sf) { fprintf(stderr, "cannot open %s\n", src); exit(1); }
    fseeko(sf, 0, SEEK_END);
    long fsize = ftello(sf);
    fseeko(sf, 0, SEEK_SET);
    uint8_t *data = malloc(fsize ? fsize : 1);
    if (fread(data, fsize, 1, sf) != 1 && fsize > 0) die("fread");
    fclose(sf);

    char dcopy[512]; strncpy(dcopy, dst, sizeof dcopy - 1); dcopy[sizeof dcopy - 1] = 0;
    char *lastslash = strrchr(dcopy, '/');
    char fname[64]; char dirstr[512] = "";
    if (lastslash) {
        *lastslash = 0;
        strncpy(dirstr, dcopy, sizeof dirstr - 1);
        strncpy(fname, lastslash + 1, sizeof fname - 1);
    } else {
        strncpy(fname, dcopy, sizeof fname - 1);
    }

    uint32_t dircl = ensure_dirs(fs, dirstr);
    uint8_t n11[11]; name_encode(fname, n11);
    uint32_t first_cl = write_file_data(fs, data, (uint32_t)fsize);
    free(data);

    uint32_t entries = (dircl == 0) ? 256 : (SECTOR / 32);
    uint32_t base = (dircl == 0) ? fs->root_start
                                 : fs->data_start + (dircl - 2) * fs->cluster_size;
    uint32_t slot = 0xFFFFFFFF;
    for (uint32_t e = 0; e < entries; e++) {
        uint32_t sec = base + (e * 32) / SECTOR;
        uint32_t off = (e * 32) % SECTOR;
        sec_read(fs, sec, fs->secbuf);
        uint8_t *ent = fs->secbuf + off;
        if (ent[0] == 0x00 || ent[0] == 0xE5) { slot = e; break; }
    }
    if (slot == 0xFFFFFFFF) { fprintf(stderr, "dir full for %s\n", dst); exit(1); }
    dir_write_entry(fs, dircl, slot * 32, n11, 0x20, first_cl, (uint32_t)fsize);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: mkfat <out.img> <size_mb> <src:dst> ...\n");
        return 1;
    }
    const char *out = argv[1];
    uint32_t size_mb = (uint32_t)atoi(argv[2]);
    uint32_t total_sectors = size_mb * 2048;
    uint32_t root_entries = 256;
    uint32_t root_sectors = (root_entries * 32 + SECTOR - 1) / SECTOR;
    uint32_t data_sectors = total_sectors - 1 - root_sectors;
    uint32_t clusters = data_sectors;
    uint32_t fat_sectors = (clusters * 3 / 2 + SECTOR - 1) / SECTOR;
    if (fat_sectors < 1) fat_sectors = 1;

    FILE *f = fopen(out, "wb");
    if (!f) die("fopen out");
    uint8_t *zero = calloc(1, SECTOR);
    for (uint32_t i = 0; i < total_sectors; i++)
        fwrite(zero, SECTOR, 1, f);
    fclose(f);

    f = fopen(out, "r+b");
    if (!f) die("fopen r+b");
    Fat fs;
    fs.fat_sectors = fat_sectors;
    fs.fat_start = 1;
    fs.root_start = 1 + 2 * fat_sectors;
    fs.root_sectors = root_sectors;
    fs.data_start = fs.root_start + root_sectors;
    fs.cluster_size = 1;
    fs.total_sectors = total_sectors;
    fs.next_cluster = 2;
    fs.secbuf = calloc(1, SECTOR);
    fs.fat = calloc(1, fat_sectors * SECTOR);

    fs.fat[0] = 0xF0; fs.fat[1] = 0xFF; fs.fat[2] = 0xFF;

    for (int copy = 0; copy < 2; copy++)
        sec_write(&fs, fs.fat_start + copy * fs.fat_sectors, fs.fat);

    memset(fs.secbuf, 0, SECTOR);
    uint8_t *b = fs.secbuf;
    b[0] = 0xEB; b[1] = 0x3C; b[2] = 0x90;
    memcpy(b + 3, "MKFAT12  ", 8);
    uint16_t bps = SECTOR; b[11] = (uint8_t)(bps & 0xFF); b[12] = (uint8_t)(bps >> 8);
    b[13] = 1;
    uint16_t reserved = 1; b[14] = (uint8_t)(reserved & 0xFF); b[15] = (uint8_t)(reserved >> 8);
    b[16] = 2;
    uint16_t rde = root_entries; b[17] = (uint8_t)(rde & 0xFF); b[18] = (uint8_t)(rde >> 8);
    uint16_t tot16 = (total_sectors > 0xFFFF) ? 0 : (uint16_t)total_sectors;
    b[19] = (uint8_t)(tot16 & 0xFF); b[20] = (uint8_t)(tot16 >> 8);
    b[21] = 0xF0;
    uint16_t fatspf = fs.fat_sectors; b[22] = (uint8_t)(fatspf & 0xFF); b[23] = (uint8_t)(fatspf >> 8);
    uint16_t spt = 18; b[24] = (uint8_t)(spt & 0xFF); b[25] = (uint8_t)(spt >> 8);
    uint16_t heads = 2; b[26] = (uint8_t)(heads & 0xFF); b[27] = (uint8_t)(heads >> 8);
    b[32] = 0; b[33] = 0; b[34] = 0; b[35] = 0;
    uint32_t tot32 = total_sectors;
    b[36] = (uint8_t)(tot32 & 0xFF); b[37] = (uint8_t)((tot32 >> 8) & 0xFF);
    b[38] = (uint8_t)((tot32 >> 16) & 0xFF); b[39] = (uint8_t)((tot32 >> 24) & 0xFF);
    b[40] = 0x80;
    b[41] = 0;
    b[42] = 0x29;
    uint32_t volid = 0x12345678;
    b[43] = (uint8_t)(volid & 0xFF); b[44] = (uint8_t)((volid >> 8) & 0xFF);
    b[45] = (uint8_t)((volid >> 16) & 0xFF); b[46] = (uint8_t)((volid >> 24) & 0xFF);
    memcpy(b + 47, "NO NAME    ", 11);
    memcpy(b + 54, "FAT12   ", 8);
    b[510] = 0x55; b[511] = 0xAA;
    sec_write(&fs, 0, b);

    for (int i = 3; i < argc; i++) {
        char *arg = strdup(argv[i]);
        char *colon = strchr(arg, ':');
        if (!colon) { fprintf(stderr, "bad arg %s\n", arg); return 1; }
        *colon = 0;
        add_file(&fs, arg, colon + 1);
        free(arg);
    }

    for (int copy = 0; copy < 2; copy++)
        sec_write(&fs, fs.fat_start + copy * fs.fat_sectors, fs.fat);

    fclose(f);
    return 0;
}
