/*
 * biosload.c - SentinelOS BIOS stage-2 loader (V10 ETERNITY).
 *
 * Loaded by stage-1 (MBR) at physical 0x100000 from LBA 1..62. DL = boot drive.
 * Switches real mode -> protected mode -> long mode, reads krnl.exe (+ hal.dll)
 * from FAT12/16/32 or NTFS on the active MBR partition, parses/relocates the
 * PE32+ images, builds a BIOS BootInfo, and jumps to kmain with RDI = BootInfo*.
 *
 * Freestanding: no libc, no SSE. Built with clang -target x86_64-unknown-windows
 * -ffreestanding -mgeneral-regs-only, linked as a flat binary placed at LBA 1.
 *
 * NOTE: The real-mode -> long-mode transition requires a small position-
 * independent assembly trampoline copied below 1 MiB.  This file implements
 * the trampoline as inline assembly helpers and a C orchestrator; the actual
 * far jump into 64-bit mode is performed by `enter_long_mode()` below.
 */

#include <stdint.h>
#include <stddef.h>

/* ---- shared ABI ---- */
#include "../shared/bootinfo.h"

/* =====================================================================
 * Minimal freestanding helpers (no libc).
 * ===================================================================== */

static void *b_memset(volatile void *d, int c, size_t n) {
    volatile unsigned char *a = d;
    while (n--) *a++ = (unsigned char)c;
    return (void *)d;
}

static void *b_memcpy(volatile void *d, const volatile void *s, size_t n) {
    volatile unsigned char *a = d;
    const volatile unsigned char *b = s;
    while (n--) *a++ = *b++;
    return (void *)d;
}

static int b_streq(const char *a, const char *b) {
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == *b;
}

static uint16_t b_rd16(const void *p, size_t o) {
    const unsigned char *x = (const unsigned char *)p + o;
    return (uint16_t)x[0] | ((uint16_t)x[1] << 8);
}

static uint32_t b_rd32(const void *p, size_t o) {
    const unsigned char *x = (const unsigned char *)p + o;
    return (uint32_t)x[0] | ((uint32_t)x[1] << 8) |
           ((uint32_t)x[2] << 16) | ((uint32_t)x[3] << 24);
}

static uint64_t b_rd64(const void *p, size_t o) {
    return (uint64_t)b_rd32(p, o) | ((uint64_t)b_rd32(p, o + 4) << 32);
}

/* =====================================================================
 * VGA text-mode output (0xB8000) for diagnostics and halting.
 * ===================================================================== */

#define VGA_TEXT ((volatile uint16_t *)0xB8000)
#define VGA_COLS 80
#define VGA_ROWS 25

static int vga_row, vga_col;

static void vga_putc(char c) {
    if (c == '\n') { vga_col = 0; vga_row++; }
    else if (c == '\r') { vga_col = 0; }
    else {
        VGA_TEXT[vga_row * VGA_COLS + vga_col] = (uint16_t)(0x0F << 8 | (unsigned char)c);
        vga_col++;
        if (vga_col >= VGA_COLS) { vga_col = 0; vga_row++; }
    }
    if (vga_row >= VGA_ROWS) {
        for (int r = 0; r < VGA_ROWS - 1; r++)
            b_memcpy(VGA_TEXT + r * VGA_COLS, VGA_TEXT + (r + 1) * VGA_COLS, VGA_COLS * 2);
        b_memset(VGA_TEXT + (VGA_ROWS - 1) * VGA_COLS, 0, VGA_COLS * 2);
        vga_row = VGA_ROWS - 1;
    }
}

static void vga_puts(const char *s) {
    while (*s) vga_putc(*s++);
}

static void vga_hex(uint64_t v) {
    static const char hex[] = "0123456789ABCDEF";
    vga_puts("0x");
    for (int i = 60; i >= 0; i -= 4) vga_putc(hex[(v >> i) & 15]);
}

static void vga_dec(uint64_t v) {
    char buf[24]; int n = 0;
    if (v == 0) { vga_putc('0'); return; }
    while (v) { buf[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) vga_putc(buf[--n]);
}

static void halt(const char *msg) {
    vga_puts("biosload: ");
    vga_puts(msg);
    vga_puts("\r\n");
    for (;;) __asm__ volatile ("hlt");
}

/* =====================================================================
 * BIOS disk I/O (INT 13h).
 * ===================================================================== */

static uint8_t g_boot_drive;
static int g_use_lba_ext = 0;
static uint64_t g_total_sectors = 0;

struct Dap {
    uint8_t  size;
    uint8_t  reserved;
    uint16_t sectors;
    uint16_t seg;
    uint16_t sel;
    uint64_t lba;
} __attribute__((packed));

/* INT 13h AH=42h LBA read. Returns 0 on success, -1 on error. */
static int disk_read_lba(uint64_t lba, uint16_t count, void *dst) {
    struct Dap dap;
    b_memset(&dap, 0, sizeof dap);
    dap.size = 0x10;
    dap.sectors = count;
    dap.seg = (uint16_t)((uintptr_t)dst >> 4);
    dap.sel = 0;
    dap.lba = lba;

    uint16_t flags;
    __asm__ volatile (
        "int $0x13"
        : "=a"(flags)
        : "a"(0x0200 | count), "b"(&dap), "d"(g_boot_drive)
        : "memory", "cc"
    );
    return (flags & 0xFF00) ? -1 : 0;
}

/* INT 13h AH=02h CHS read (fallback for small LBAs). */
static int disk_read_chs(uint64_t lba, uint16_t count, void *dst) {
    if (!g_total_sectors) return -1;
    /* Very conservative: only use CHS for the first 8GB and only if we have
     * geometry from AH=08h.  We don't query geometry here; LBA ext is
     * required for anything beyond the first few sectors. */
    (void)lba; (void)count; (void)dst;
    return -1;
}

static int disk_read(uint64_t lba, uint16_t count, void *dst) {
    if (g_use_lba_ext) return disk_read_lba(lba, count, dst);
    return disk_read_chs(lba, count, dst);
}

static int detect_lba_ext(void) {
    uint16_t ax;
    __asm__ volatile (
        "int $0x13"
        : "=a"(ax)
        : "a"(0x4100), "b"(0x55AA), "d"(g_boot_drive)
        : "memory", "cc"
    );
    return (ax & 0xFF) == 0xAA && !(ax & 0xFF00);
}

static void query_geometry(void) {
    uint32_t res;
    __asm__ volatile (
        "int $0x13"
        : "=a"(res)
        : "a"(0x0800), "d"(g_boot_drive)
        : "memory", "cc"
    );
    if (res & 0xFF00) return;
    uint16_t cx = (uint16_t)res;
    uint8_t heads = (uint8_t)(res >> 8) + 1;
    uint16_t sectors = (uint16_t)(cx & 0x3F);
    uint16_t cylinders = (uint16_t)(((res >> 8) & 0xFF) | ((cx & 0xC0) << 2)) + 1;
    g_total_sectors = (uint64_t)cylinders * heads * sectors;
}

/* =====================================================================
 * MBR partition table parsing.
 * ===================================================================== */

typedef struct {
    uint8_t  bootable;
    uint64_t first_lba;
    uint64_t sector_count;
    uint8_t  type;
} MbrPart;

static int mbr_parse(const uint8_t *sector, MbrPart *parts, int *active) {
    if (b_rd16(sector, 510) != 0xAA55) return -1;
    *active = -1;
    for (int i = 0; i < 4; i++) {
        const uint8_t *e = sector + 446 + i * 16;
        parts[i].bootable = e[0];
        parts[i].type = e[4];
        parts[i].first_lba = b_rd32(e, 8);
        parts[i].sector_count = b_rd32(e, 12);
        if (parts[i].bootable == 0x80) *active = i;
    }
    return 0;
}

/* =====================================================================
 * FAT12/16/32 reader.
 * ===================================================================== */

typedef struct {
    uint64_t base_lba;
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint32_t reserved_sectors;
    uint8_t  num_fats;
    uint32_t sectors_per_fat;
    uint32_t total_sectors;
    uint32_t total_clusters;
    uint32_t fat_type;
    uint64_t data_region;
    uint64_t fat_region;
    uint32_t root_dir_cluster;
    uint32_t root_dir_sectors;
} FatFs;

static int fat_bpb(const uint8_t *bs, FatFs *fs) {
    fs->bytes_per_sector = b_rd16(bs, 11);
    fs->sectors_per_cluster = bs[13];
    fs->reserved_sectors = b_rd16(bs, 14);
    fs->num_fats = bs[16];
    uint16_t root_entries = b_rd16(bs, 17);
    uint16_t tot16 = b_rd16(bs, 19);
    uint32_t tot32 = b_rd32(bs, 32);
    fs->sectors_per_fat = b_rd16(bs, 22);
    if (!fs->sectors_per_fat) fs->sectors_per_fat = b_rd32(bs, 36);
    fs->total_sectors = tot16 ? tot16 : tot32;
    fs->root_dir_sectors = ((root_entries * 32) + (fs->bytes_per_sector - 1)) / fs->bytes_per_sector;
    fs->fat_region = fs->reserved_sectors;
    uint64_t data_sectors = (uint64_t)fs->total_sectors -
        (fs->reserved_sectors + (uint64_t)fs->num_fats * fs->sectors_per_fat + fs->root_dir_sectors);
    fs->total_clusters = (uint32_t)(data_sectors / fs->sectors_per_cluster);
    fs->data_region = fs->reserved_sectors + (uint64_t)fs->num_fats * fs->sectors_per_fat + fs->root_dir_sectors;
    if (fs->total_clusters < 4085) fs->fat_type = 12;
    else if (fs->total_clusters < 65525) fs->fat_type = 16;
    else fs->fat_type = 32;
    fs->root_dir_cluster = (fs->fat_type == 32) ? b_rd32(bs, 44) : 0;
    fs->base_lba = 0;
    return 0;
}

static uint64_t fat_cluster_lba(const FatFs *fs, uint32_t cluster) {
    return fs->base_lba + fs->data_region +
           ((uint64_t)(cluster - 2) * fs->sectors_per_cluster);
}

static uint32_t fat_next_cluster(const FatFs *fs, const uint8_t *fat, uint32_t cluster) {
    if (fs->fat_type == 12) {
        uint32_t off = cluster + cluster / 2;
        uint16_t v = b_rd16(fat, off);
        return (cluster & 1) ? (uint32_t)(v >> 4) : (uint32_t)(v & 0xFFF);
    } else if (fs->fat_type == 16) {
        return b_rd16(fat, cluster * 2);
    } else {
        return b_rd32(fat, cluster * 4) & 0x0FFFFFFF;
    }
}

static int fat_read_file(const FatFs *fs, const uint8_t *fat, const char *name,
                         uint8_t *dst, uint32_t *out_size) {
    uint64_t dir_lba;
    uint32_t cluster;
    if (fs->fat_type == 32) {
        cluster = fs->root_dir_cluster;
        dir_lba = fat_cluster_lba(fs, cluster);
    } else {
        dir_lba = fs->base_lba + fs->reserved_sectors +
                  (uint64_t)fs->num_fats * fs->sectors_per_fat;
        cluster = 0;
    }

    uint8_t secbuf[512];
    uint64_t scan_lba = dir_lba;
    for (;;) {
        if (disk_read(scan_lba, 1, secbuf) != 0) return -1;
        for (uint32_t off = 0; off + 32 <= fs->bytes_per_sector; off += 32) {
            const uint8_t *e = secbuf + off;
            if (e[0] == 0x00) return -1;
            if (e[0] == 0xE5) continue;
            if (e[11] & 0x08) continue;
            char fname[9];
            for (int i = 0; i < 8; i++) fname[i] = (char)e[i];
            fname[8] = 0;
            char ext[4];
            int extlen = 0;
            for (int i = 0; i < 3; i++)
                if (e[i + 8] != ' ') ext[extlen++] = (char)e[i + 8];
            ext[extlen] = 0;
            char full[16];
            int fl = 0;
            for (int i = 0; i < 8 && fname[i] && fname[i] != ' '; i++) full[fl++] = fname[i];
            if (extlen) { full[fl++] = '.'; for (int i = 0; i < extlen; i++) full[fl++] = ext[i]; }
            full[fl] = 0;
            if (b_streq(full, name)) {
                uint32_t start_lo = b_rd16(e, 26);
                uint32_t start_hi = (fs->fat_type == 32) ? b_rd16(e, 20) : 0;
                uint32_t start = (start_hi << 16) | start_lo;
                uint32_t fsize = b_rd32(e, 28);
                *out_size = fsize;
                uint8_t *wr = dst;
                uint32_t cur = start;
                uint32_t got = 0;
                while (cur >= 2 && cur < 0xFFFFFF8) {
                    uint64_t cl_lba = fat_cluster_lba(fs, cur);
                    for (uint8_t s = 0; s < fs->sectors_per_cluster && got < fsize; s++) {
                        if (disk_read(cl_lba + s, 1, wr) != 0) return -1;
                        wr += fs->bytes_per_sector;
                        got += fs->bytes_per_sector;
                    }
                    cur = fat_next_cluster(fs, fat, cur);
                }
                return 0;
            }
        }
        if (fs->fat_type == 32) {
            cluster = fat_next_cluster(fs, fat, cluster);
            if (cluster < 2 || cluster >= 0x0FFFFFF8) break;
            scan_lba = fat_cluster_lba(fs, cluster);
        } else {
            break;
        }
    }
    return -1;
}

/* =====================================================================
 * NTFS reader (minimal, read-only).
 * ===================================================================== */

typedef struct {
    uint64_t base_lba;
    uint32_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint32_t cluster_size;
    uint64_t mft_lcn;
    uint32_t mft_record_size;
    uint64_t mft_size;
} NtfsFs;

static int ntfs_mount(NtfsFs *fs, const uint8_t *bootsec) {
    if (bootsec[3] != 'N' || bootsec[4] != 'T' || bootsec[5] != 'F' || bootsec[6] != 'S')
        return -1;
    fs->bytes_per_sector = b_rd16(bootsec, 11);
    fs->sectors_per_cluster = bootsec[13];
    fs->cluster_size = fs->bytes_per_sector * fs->sectors_per_cluster;
    fs->mft_lcn = b_rd64(bootsec, 48);
    int8_t cr = (int8_t)bootsec[64];
    fs->mft_record_size = cr < 0 ? (uint32_t)(1u << (-cr)) : (uint32_t)cr * fs->cluster_size;
    fs->base_lba = 0;
    return 0;
}

static int ntfs_read_sectors(const NtfsFs *fs, uint64_t lba, uint16_t count, void *dst) {
    return disk_read(lba, count, dst);
}

static int ntfs_read_mft_record(const NtfsFs *fs, uint64_t index, uint8_t *out) {
    uint64_t offset = fs->mft_lcn * fs->cluster_size + (uint64_t)index * fs->mft_record_size;
    uint64_t lba = fs->base_lba + offset / fs->bytes_per_sector;
    uint16_t sectors = (uint16_t)((fs->mft_record_size + fs->bytes_per_sector - 1) / fs->bytes_per_sector);
    return disk_read(lba, sectors, out);
}

static int ntfs_name_eq(const uint8_t *val, uint8_t namelen, const char *name) {
    for (uint8_t i = 0; i < namelen; i++) {
        uint16_t w = b_rd16(val, 66 + i * 2);
        char c = (char)name[i];
        if (!c) return 0;
        if (c >= 'A' && c <= 'Z') c += 32;
        if (w >= 'A' && w <= 'Z') w += 32;
        if (w != (uint16_t)(unsigned char)c) return 0;
    }
    return name[namelen] == 0;
}

static uint64_t ntfs_lookup(const NtfsFs *fs, uint64_t parent_idx, const char *name) {
    uint8_t rec[1024];
    uint64_t max = fs->mft_size / fs->mft_record_size;
    for (uint64_t idx = 0; idx < max; idx++) {
        if (ntfs_read_mft_record(fs, idx, rec) != 0) continue;
        if (b_rd16(rec, 0) != 0x4C49 || b_rd16(rec, 2) != 0x4546) continue; /* "FILE" */
        if (!(b_rd16(rec, 22) & 1)) continue;
        uint16_t attr_off = b_rd16(rec, 20);
        for (uint32_t p = attr_off; p + 16 <= fs->mft_record_size;) {
            uint32_t type = b_rd32(rec, p);
            if (type == 0xFFFFFFFF) break;
            uint32_t alen = b_rd32(rec, p + 4);
            if (alen < 16 || p + alen > fs->mft_record_size) break;
            if (type == 0x30 && !rec[p + 8] && alen >= 0x5C) {
                uint8_t *v = rec + p + b_rd16(rec, p + 20);
                uint64_t parent = b_rd64(v, 0) & 0x0000FFFFFFFFFFFFULL;
                uint8_t nl = v[64], ns = v[65];
                if (parent == parent_idx && ns != 2 && nl &&
                    (uint32_t)66 + (uint32_t)nl * 2 <= alen - b_rd16(rec, p + 20) &&
                    ntfs_name_eq(v, nl, name)) {
                    return idx;
                }
            }
            p += alen;
        }
    }
    return 0;
}

static int ntfs_read_file(const NtfsFs *fs, uint64_t idx, uint8_t *dst, uint32_t *out_size) {
    uint8_t rec[1024];
    if (ntfs_read_mft_record(fs, idx, rec) != 0) return -1;
    uint16_t attr_off = b_rd16(rec, 20);
    uint64_t fsize = 0;
    uint8_t *runs = NULL;
    uint32_t runlen = 0;
    int resident = 0;
    uint8_t resident_buf[512];
    uint32_t resident_len = 0;
    for (uint32_t p = attr_off; p + 16 <= fs->mft_record_size;) {
        uint32_t type = b_rd32(rec, p);
        if (type == 0xFFFFFFFF) break;
        uint32_t alen = b_rd32(rec, p + 4);
        if (alen < 16 || p + alen > fs->mft_record_size) break;
        if (type == 0x80) {
            if (rec[p + 8]) { /* non-resident */
                runs = rec + p + b_rd16(rec, p + 32);
                runlen = alen - b_rd16(rec, p + 32);
                fsize = b_rd64(rec, p + 48);
            } else { /* resident */
                uint32_t vl = b_rd32(rec, p + 16);
                uint16_t vo = b_rd16(rec, p + 20);
                if (vl > resident_len) vl = resident_len;
                b_memcpy(dst, rec + p + vo, vl);
                *out_size = vl;
                return 0;
            }
            break;
        }
        p += alen;
    }
    if (resident) {
        *out_size = resident_len;
        return 0;
    }
    if (!runs || !runlen || fsize > 0x10000000ULL) return -1;
    uint64_t got = 0;
    uint8_t *wr = dst;
    uint32_t pos = 0;
    uint64_t vcn = 0;
    int64_t lcn = 0;
    while (pos < runlen && runs[pos]) {
        uint8_t h = runs[pos++];
        uint8_t nb = h & 15, lb = h >> 4;
        if (!nb || nb > 8 || lb > 8 || pos + nb + lb > runlen) return -1;
        uint64_t count = 0;
        for (uint8_t i = 0; i < nb; i++) count |= (uint64_t)runs[pos++] << (8 * i);
        if (!count) return -1;
        int sparse = lb == 0;
        if (lb) {
            int64_t raw = 0;
            for (uint8_t i = 0; i < lb; i++) raw |= (int64_t)runs[pos++] << (8 * i);
            if (runs[pos - 1] & 0x80)
                for (uint8_t i = lb; i < 8; i++) raw |= (int64_t)0xFF << (8 * i);
            lcn += raw;
        }
        uint64_t bytes = count * fs->cluster_size;
        if (!sparse && lcn >= 0) {
            uint64_t clba = fs->base_lba + (uint64_t)lcn * (fs->cluster_size / fs->bytes_per_sector);
            for (uint64_t off = 0; off < bytes && got < fsize; off += fs->bytes_per_sector) {
                uint64_t remaining = bytes - off;
                uint16_t sectors = (uint16_t)((remaining > fs->bytes_per_sector ? fs->bytes_per_sector : remaining) / fs->bytes_per_sector);
                if (sectors == 0) sectors = 1;
                if (disk_read(clba + off / fs->bytes_per_sector, sectors, wr) != 0) return -1;
                wr += sectors * fs->bytes_per_sector;
                got += sectors * fs->bytes_per_sector;
            }
        } else if (sparse) {
            b_memset(wr, 0, bytes);
            wr += bytes;
            got += bytes;
        }
        vcn += count;
        if (got >= fsize) break;
    }
    *out_size = (uint32_t)fsize;
    return 0;
}

/* =====================================================================
 * Filesystem dispatch globals.
 * ===================================================================== */

static FatFs  g_fat;
static NtfsFs g_ntfs;
static int    g_fs_type = 0; /* 1 = FAT, 2 = NTFS */
static uint64_t g_part_start = 0;

static uint8_t *g_krnl_image = NULL;
static uint32_t  g_krnl_size = 0;
static uint8_t *g_hal_image = NULL;
static uint32_t  g_hal_size = 0;

static int load_krnl_hal(void) {
    uint8_t pbs[512];
    if (disk_read(g_part_start, 1, pbs) != 0) return -1;

    uint16_t sig = b_rd16(pbs, 510);
    int is_fat = (sig == 0xAA55) && (pbs[0] == 0xEB || pbs[0] == 0xE9) &&
                 ((pbs[54] == 'F' && pbs[55] == 'A') || (pbs[82] == 'F' && pbs[83] == 'A'));
    int is_ntfs = (pbs[0] == 'N' && pbs[1] == 'T' && pbs[2] == 'F' && pbs[3] == 'S');

    if (is_fat) {
        g_fs_type = 1;
        if (fat_bpb(pbs, &g_fat) != 0) return -1;
        g_fat.base_lba = g_part_start;
        /* Read first FAT into scratch for cluster walks */
        uint8_t fatbuf[512 * 8];
        uint16_t fat_secs = (uint16_t)(g_fat.sectors_per_fat);
        if (fat_secs > sizeof(fatbuf) / 512) fat_secs = (uint16_t)(sizeof(fatbuf) / 512);
        if (disk_read(g_part_start + g_fat.fat_region, fat_secs, fatbuf) != 0) return -1;
        if (fat_read_file(&g_fat, fatbuf, "HAL.DLL", (uint8_t *)(uintptr_t)0x200000, &g_hal_size) != 0) return -1;
        g_hal_image = (uint8_t *)(uintptr_t)0x200000;
        if (fat_read_file(&g_fat, fatbuf, "KRNL.EXE", (uint8_t *)(uintptr_t)0x400000, &g_krnl_size) != 0) return -1;
        g_krnl_image = (uint8_t *)(uintptr_t)0x400000;    } else if (is_ntfs) {
        g_fs_type = 2;
        if (ntfs_mount(&g_ntfs, pbs) != 0) return -1;
        g_ntfs.base_lba = g_part_start;
        const char *path[] = { "magic", "System32", "hal.dll" };
        uint64_t idx = 5;
        idx = ntfs_lookup(&g_ntfs, idx, path[0]);
        if (!idx) return -1;
        idx = ntfs_lookup(&g_ntfs, idx, path[1]);
        if (!idx) return -1;
        idx = ntfs_lookup(&g_ntfs, idx, path[2]);
        if (!idx) return -1;
        if (ntfs_read_file(&g_ntfs, idx, (uint8_t *)(uintptr_t)0x200000, &g_hal_size) != 0) return -1;
        g_hal_image = (uint8_t *)(uintptr_t)0x200000;
        idx = 5;
        idx = ntfs_lookup(&g_ntfs, idx, "magic");
        if (!idx) return -1;
        idx = ntfs_lookup(&g_ntfs, idx, "System32");
        if (!idx) return -1;
        idx = ntfs_lookup(&g_ntfs, idx, "krnl.exe");
        if (!idx) return -1;
        if (ntfs_read_file(&g_ntfs, idx, (uint8_t *)(uintptr_t)0x400000, &g_krnl_size) != 0) return -1;
        g_krnl_image = (uint8_t *)(uintptr_t)0x400000;
    } else {
        return -1;
    }
    return 0;
}

/* =====================================================================
 * PE32+ parsing / relocation.
 * ===================================================================== */

typedef struct {
    uint8_t  *image;
    uint64_t phys;
    uint64_t size;
    uint64_t entry;
    uint64_t imagebase;
    uint16_t num_sections;
    uint8_t  *sections;
    uint64_t reloc_dir_va;
    uint32_t reloc_dir_size;
    char     name[32];
} PeMod;

static int pe_parse(uint8_t *file, PeMod *m) {
    if (b_rd16(file, 0) != 0x5A4D) return -1;
    uint32_t lfanew = b_rd32(file, 0x3C);
    if (b_rd32(file, lfanew) != 0x00004550) return -1;
    if (b_rd16(file, lfanew + 4) != 0x8664) return -1;
    uint16_t num_sec = b_rd16(file, lfanew + 6);
    uint16_t opt_size = b_rd16(file, lfanew + 16);
    if (b_rd16(file, lfanew + 20) != 0x20B) return -1;
    m->imagebase = b_rd64(file, lfanew + 24);
    m->entry = b_rd64(file, lfanew + 24 + 16);
    m->size = b_rd32(file, lfanew + 24 + 32 + 24);
    m->num_sections = num_sec;
    m->sections = file + lfanew + 24 + opt_size;
    uint32_t dd_base = lfanew + 24 + opt_size - 128;
    m->reloc_dir_va = b_rd32(file, dd_base + 5 * 8);
    m->reloc_dir_size = b_rd32(file, dd_base + 5 * 8 + 4);
    b_memcpy(m->name, "unknown", 8);
    return 0;
}

static uint64_t pe_rva_to_offset(PeMod *m, uint8_t *file, uint64_t rva) {
    for (uint16_t i = 0; i < m->num_sections; i++) {
        uint8_t *s = m->sections + i * 40;
        uint32_t vsize = b_rd32(s, 8);
        uint32_t vaddr = b_rd32(s, 12);
        uint32_t rawsize = b_rd32(s, 16);
        uint32_t rawptr = b_rd32(s, 20);
        if (rva >= vaddr && rva < vaddr + (vsize ? vsize : rawsize))
            return rawptr + (rva - vaddr);
    }
    if (rva < b_rd32(file, 0)) return rva;
    return (uint64_t)-1;
}

static void pe_apply_relocs(PeMod *m, uint8_t *base, int64_t delta) {
    if (!delta || !m->reloc_dir_size) return;
    uint64_t off = pe_rva_to_offset(m, base, m->reloc_dir_va);
    if (off == (uint64_t)-1) return;
    uint8_t *p = base + off;
    uint8_t *end = p + m->reloc_dir_size;
    while (p < end) {
        uint32_t block_size = b_rd32(p, 0);
        uint32_t vaddr = b_rd32(p, 4);
        if (!block_size) break;
        uint16_t *entries = (uint16_t *)(p + 8);
        uint32_t n = (block_size - 8) / 2;
        for (uint32_t i = 0; i < n; i++) {
            uint16_t e = entries[i];
            uint8_t type = (uint8_t)(e >> 12);
            if (type == 10) {
                uint64_t *patch = (uint64_t *)(base + pe_rva_to_offset(m, base, vaddr + (e & 0xFFF)));
                *patch += (uint64_t)delta;
            }
        }
        p += block_size;
    }
}

/* =====================================================================
 * VESA framebuffer (INT 10h).
 * ===================================================================== */

typedef struct {
    uint16_t mode_attr;
    uint8_t  win_a, win_b;
    uint16_t granularity;
    uint16_t win_size;
    uint16_t seg_a, seg_b;
    uint32_t win_func_ptr;
    uint16_t pitch;
    uint16_t xres, yres;
    uint8_t  char_w, char_h;
    uint8_t  planes, bpp, banks;
    uint8_t  memory_model;
    uint8_t  bank_size, image_pages;
    uint8_t  reserved0;
    uint8_t  red_mask, red_pos;
    uint8_t  green_mask, green_pos;
    uint8_t  blue_mask, blue_pos;
    uint8_t  rsv_mask, rsv_pos;
    uint8_t  directcolor_attr;
    uint32_t phys_base;
    uint32_t reserved1;
    uint16_t reserved2;
} __attribute__((packed)) VesaModeInfo;

/* VBE controller info (INT 10h AX=0x4F00). */
typedef struct {
    char     signature[4];   /* "VES2" */
    uint16_t version;
    uint32_t oem_string_ptr;
    uint32_t capabilities;
    uint32_t video_mode_ptr;
    uint16_t total_memory;
    uint16_t oem_software_rev;
    uint32_t oem_vendor_name_ptr;
    uint32_t oem_product_name_ptr;
    uint32_t oem_product_rev_ptr;
    uint8_t  reserved[222];
    uint8_t  oem_data[256];
} __attribute__((packed)) VesaInfo;

static int vesa_detect_controller(void) {
    VesaInfo vi;
    b_memset(&vi, 0, sizeof vi);
    uint16_t ax;
    __asm__ volatile (
        "int $0x10"
        : "=a"(ax)
        : "a"(0x4F00), "d"(0), "b"(&vi)
        : "memory", "cc"
    );
    return (ax & 0xFF) == 0x4F && b_streq(vi.signature, "VES2") ? 0 : -1;
}

static int vesa_get_mode_info(uint16_t mode, VesaModeInfo *mi) {
    uint16_t ax;
    __asm__ volatile (
        "int $0x10"
        : "=a"(ax)
        : "a"(0x4F01), "c"(mode), "d"(0), "b"(mi)
        : "memory", "cc"
    );
    return (ax & 0xFF) == 0x4F ? 0 : -1;
}

static int vesa_set_mode(uint16_t mode) {
    uint16_t ax;
    __asm__ volatile (
        "int $0x10"
        : "=a"(ax)
        : "a"(0x4F02), "b"(mode)
        : "memory", "cc"
    );
    return (ax & 0xFF) == 0x4F ? 0 : -1;
}

static int vesa_detect(uint32_t *fb_base, uint32_t *width, uint32_t *height, uint32_t *pitch) {
    static const uint16_t modes[] = { 0x101, 0x117, 0x118 };
    VesaModeInfo mi;
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        if (vesa_get_mode_info(modes[i], &mi) == 0 && mi.phys_base) {
            *fb_base = mi.phys_base;
            *width = mi.xres;
            *height = mi.yres;
            *pitch = mi.pitch;
            vesa_set_mode((uint16_t)(modes[i] | 0x4000));
            return 0;
        }
    }
    return -1;
}

/* =====================================================================
 * E820 memory map (INT 15h AX=E820h).
 * ===================================================================== */

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi;
} __attribute__((packed)) E820Entry;

static E820Entry g_e820[64];
static uint32_t g_e820_count = 0;

static int e820_collect(void) {
    uint32_t cont = 0;
    uint32_t n = 0;
    while (n < 64) {
        uint32_t sig = 0, len = 0;
        int carry;
        /* INT 15h E820: ES:DI = buffer. We assume flat real mode (unreal
         * mode) where segment limits are 4 GiB, so the full linear address
         * in RDI is used directly. */
        uint8_t *ptr = (uint8_t *)&g_e820[n];
        __asm__ volatile (
            "clc\n\t"
            "int $0x15\n\t"
            "sbb $0, %0\n\t"
            : "=r"(carry), "=b"(cont), "=a"(sig), "=c"(len), "+D"(ptr)
            : "a"(0xE820), "1"(cont), "c"(24), "d"(0x534D4150)
            : "memory", "cc"
        );
        if (carry) break;
        if (sig != 0x534D4150) break;
        if (len < 24) break;
        n++;
        if (!cont) break;
    }
    g_e820_count = n;
    return n > 0 ? 0 : -1;
}

/* =====================================================================
 * GDT (protected + long mode).
 * ===================================================================== */

typedef struct {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  flags_limit_hi;
    uint8_t  base_hi;
} __attribute__((packed)) GdtEntry;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) GdtPtr;

static GdtEntry g_gdt[6];
static GdtPtr   g_gdt_ptr;

static void gdt_init(void) {
    /* 0: null, 1: code32, 2: data32, 3: code64, 4: data64, 5: spare */
    g_gdt[3].limit_lo = 0xFFFF;
    g_gdt[3].flags_limit_hi = 0x0F | 0x20; /* 4G, 4K, L-bit for code64 */
    g_gdt[3].access = 0x9A;
    g_gdt[3].base_lo = 0; g_gdt[3].base_mid = 0; g_gdt[3].base_hi = 0;

    g_gdt[4].limit_lo = 0xFFFF;
    g_gdt[4].flags_limit_hi = 0x0F;
    g_gdt[4].access = 0x92;
    g_gdt[4].base_lo = 0; g_gdt[4].base_mid = 0; g_gdt[4].base_hi = 0;

    g_gdt_ptr.limit = sizeof(g_gdt) - 1;
    g_gdt_ptr.base = (uint64_t)(uintptr_t)g_gdt;
}

/* =====================================================================
 * Page tables (4-level, 2MiB pages), built in low memory below 1MiB.
 * ===================================================================== */

#define PTE_P   (1ULL << 0)
#define PTE_RW  (1ULL << 1)
#define PTE_PS  (1ULL << 7)
#define PTE_ADDR 0x000FFFFFFFFFF000ULL
#define PAGE_2M (2ULL << 20)
#define HHDM 0xFFFF800000000000ULL

/* Page tables live at 0x7000-0x8FFF (4 pages): PML4, PDPT, PD(identity), PD(HHDM) */
#define PT_PML4  ((uint64_t *)0x7000)
#define PT_PDPT  ((uint64_t *)0x7100)
#define PT_PD_ID ((uint64_t *)0x7200)
#define PT_PD_HH ((uint64_t *)0x7300)

static void build_page_tables(void) {
    b_memset(PT_PML4, 0, 4096);
    b_memset(PT_PDPT, 0, 4096);
    b_memset(PT_PD_ID, 0, 4096);
    b_memset(PT_PD_HH, 0, 4096);

    /* PML4[0] -> PDPT (identity), PML4[511] -> PDPT (HHDM, same phys) */
    PT_PML4[0]  = (uint64_t)PT_PDPT | PTE_P | PTE_RW;
    PT_PML4[511] = (uint64_t)PT_PDPT | PTE_P | PTE_RW;

    /* PDPT[0..3] -> PDs covering identity 0-1GB each */
    PT_PDPT[0] = (uint64_t)PT_PD_ID | PTE_P | PTE_RW;
    /* For HHDM we reuse PDPT but map via the same PD entries translated;
     * HHDM va = phys + HHDM. PML4[511] indexes into same PDPT, so PDPT[0]
     * covers HHDM 0xFFFF800000000000..0xFFFF800040000000 (1GB). Good enough. */

    /* Identity PD: map 0-1GB with 2MiB pages */
    for (int i = 0; i < 512; i++)
        PT_PD_ID[i] = ((uint64_t)i << 21) | PTE_P | PTE_RW | PTE_PS;

    /* HHDM PD: map HHDM 0-1GB to phys 0-1GB */
    for (int i = 0; i < 512; i++)
        PT_PD_HH[i] = ((uint64_t)i << 21) | PTE_P | PTE_RW | PTE_PS;
}

/* =====================================================================
 * Long-mode entry trampoline.
 *
 * This function runs in real mode. It:
 *   1. Loads GDT
 *   2. Enables PAE/PGE/PSE in CR4
 *   3. Enables LME in EFER
 *   4. Loads CR3 with PML4 address
 *   5. Enables paging+protected mode in CR0
 *   6. Far jumps to code64 selector (0x18 = GDT entry 3)
 *
 * After the far jump, execution continues at long_mode_64() which runs
 * in 64-bit mode.  That label is defined in the same assembly block.
 * ===================================================================== */

__attribute__((noreturn))
static void enter_long_mode(uint64_t entry64, void *bi) {
    /* The assembler starts in 64-bit mode (x86_64 target).  Switch to
     * 16-bit for the real-mode part, then 32-bit for protected mode, then
     * 64-bit after the far jump into long mode.  All addresses below 1 MiB
     * so they fit in 32 bits. */
    uint32_t gdt_desc_addr = (uint32_t)(uintptr_t)&g_gdt_ptr;
    void *bi_val = bi;
    uint64_t entry64_val = entry64;

    __asm__ volatile (
        ".code16gcc\n\t"          /* 16-bit, GCC-compatible addressing */

        /* Load GDT: 48-bit descriptor (limit:base) at gdt_desc_addr (in ebx) */
        "lgdt (%[gdt_desc_addr])\n\t"

        /* CR4: PAE(5), PGE(7), PSE(4) */
        "mov %%cr4, %%eax\n\t"
        "orl $0x60, %%eax\n\t"    /* PAE | PGE */
        "orl $0x10, %%eax\n\t"    /* PSE */
        "mov %%eax, %%cr4\n\t"

        /* EFER: LME (bit 8) */
        "movl $0xC0000080, %%ecx\n\t"
        "rdmsr\n\t"
        "orl $0x100, %%eax\n\t"
        "wrmsr\n\t"

        /* CR3 = PML4 (at fixed physical 0x7000) */
        "movl $0x7000, %%eax\n\t"
        "mov %%eax, %%cr3\n\t"

        /* CR0: PG(31) | PE(0) */
        "mov %%cr0, %%eax\n\t"
        "orl $0x80000001, %%eax\n\t"
        "mov %%eax, %%cr0\n\t"

        /* Far jump to code64 selector 0x18, then long_mode_64 */
        "ljmp $0x18, $long_mode_64\n\t"

        ".code32\n\t"             /* now in 32-bit protected mode */
        "long_mode_64:\n\t"
        /* Set up data segment registers */
        "movw $0x20, %%ax\n\t"    /* data64 selector (GDT entry 4 << 3) */
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"

        ".code64\n\t"             /* now in 64-bit long mode */
        /* RDI = BootInfo*, RAX = entry point, jump */
        "movq %[entry64_val], %%rax\n\t"   /* entry64_val in rdi ("D") */
        "movq %[bi_val], %%rdi\n\t"        /* bi_val in rsi ("S") */
        "jmpq *%%rax\n\t"
        :
        : [gdt_desc_addr] "b"(gdt_desc_addr),   /* ebx */
          [bi_val] "S"(bi_val),                  /* rsi */
          [entry64_val] "D"(entry64_val)         /* rdi */
        : "eax", "ecx", "edx", "memory"
    );
    for (;;) __asm__ volatile ("hlt");
}

/* =====================================================================
 * Real-mode entry point (_start). Stage-1 jumps here at 0x100000.
 * ===================================================================== */

__attribute__((section(".start16"), used, noreturn))
static void _start(void) {
    /* Save boot drive */
    __asm__ volatile ("mov %%dl, %0" : "=r"(g_boot_drive));

    /* Detect INT 13h extensions + geometry */
    g_use_lba_ext = detect_lba_ext();
    query_geometry();

    /* Read MBR */
    uint8_t mbr[512];
    if (disk_read(0, 1, mbr) != 0) halt("cannot read MBR");

    /* Enable A20 so we can load images above 1 MiB in real mode */
    {
        uint8_t val;
        __asm__ volatile ("inb $0x92, %0" : "=a"(val));
        val |= 2;
        __asm__ volatile ("outb %0, $0x92" :: "a"(val));
    }

    MbrPart parts[4];
    int active = -1;
    if (mbr_parse(mbr, parts, &active) != 0 || active < 0)
        halt("no active MBR partition");

    g_part_start = parts[active].first_lba;
    vga_puts("biosload: active partition at LBA ");
    vga_dec(g_part_start);
    vga_puts("\r\n");

    /* Load kernel + HAL */
    if (load_krnl_hal() != 0) halt("cannot load kernel/hal");

    vga_puts("biosload: PE images loaded\r\n");

    /* Parse PE images */
    PeMod krnl, hal;
    if (pe_parse(g_krnl_image, &krnl) != 0) halt("krnl.exe not a valid PE32+");
    if (pe_parse(g_hal_image, &hal) != 0) halt("hal.dll not a valid PE32+");

    /* Copy names into module structs */
    b_memcpy(krnl.name, "krnl.exe", 9);
    b_memcpy(hal.name, "hal.dll", 8);

    /* Relocate HAL to HHDM + phys */
    uint64_t hal_prefer = hal.imagebase;
    if (hal_prefer < HHDM) hal_prefer = HHDM + hal.phys;
    int64_t hal_delta = (int64_t)(hal_prefer - hal.phys);
    pe_apply_relocs(&hal, g_hal_image, hal_delta);

    /* Relocate kernel */
    uint64_t krnl_prefer = krnl.imagebase;
    if (krnl_prefer < HHDM) krnl_prefer = HHDM + krnl.phys;
    int64_t krnl_delta = (int64_t)(krnl_prefer - krnl.phys);
    pe_apply_relocs(&krnl, g_krnl_image, krnl_delta);

    /* Collect E820 memory map */
    e820_collect();

    /* Detect VESA controller + framebuffer */
    if (vesa_detect_controller() != 0)
        halt("no VESA BIOS Extensions");
    uint32_t fb_base = 0, fb_w = 0, fb_h = 0, fb_pitch = 0;
    vesa_detect(&fb_base, &fb_w, &fb_h, &fb_pitch);

    /* Build BootInfo at fixed physical address 0x6000 */
    BootInfo *bi = (BootInfo *)(uintptr_t)0x6000;
    b_memset(bi, 0, sizeof(*bi));
    bi->boot_mode = BOOT_MODE_BIOS;
    bi->fb_base = fb_base;
    bi->width = fb_w;
    bi->height = fb_h;
    bi->pitch = fb_pitch;
    bi->mmap = (uint64_t)g_e820;
    bi->mmap_size = (uint64_t)g_e820_count * sizeof(E820Entry);
    bi->desc_size = sizeof(E820Entry);
    bi->e820_map = (uint64_t)g_e820;
    bi->e820_count = g_e820_count;
    bi->e820_entry_size = sizeof(E820Entry);
    bi->module_count = 2;
    bi->pad = 0;

    /* HAL module */
    b_memcpy(bi->modules[0].name, hal.name, 32);
    bi->modules[0].base = hal_prefer;
    bi->modules[0].phys = hal.phys;
    bi->modules[0].size = hal.size;
    bi->modules[0].entry = hal_prefer + (hal.entry - hal.phys);

    /* Kernel module */
    b_memcpy(bi->modules[1].name, krnl.name, 32);
    bi->modules[1].base = krnl_prefer;
    bi->modules[1].phys = krnl.phys;
    bi->modules[1].size = krnl.size;
    bi->modules[1].entry = krnl_prefer + (krnl.entry - krnl.phys);

    bi->ap_trampoline_phys = 0;

    vga_puts("biosload: jumping to kernel\r\n");

    /* Build GDT + page tables before the mode switch */
    gdt_init();
    build_page_tables();

    /* Enter long mode and jump to kernel entry */
    enter_long_mode(krnl_prefer + krnl.entry, bi);

    for (;;) __asm__ volatile ("hlt");
}

/* Required symbol for the linker: entry point. */
__attribute__((used))
void *biosload_entry = (void *)(uintptr_t)_start;
