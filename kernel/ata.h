#pragma once
/* Legacy IDE optional – fail instantly on q35 (no 0x1F0). */
static inline void ata_outb(uint16_t p, uint8_t v) {
    __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p));
}
static inline uint8_t ata_inb(uint16_t p) {
    uint8_t v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v;
}
static inline void ata_insw(uint16_t p, void *buf, int count) {
    __asm__ volatile("rep insw" : "+D"(buf), "+c"(count) : "d"(p) : "memory");
}
static inline void ata_outsw(uint16_t p, const void *buf, int count) {
    __asm__ volatile("rep outsw" : "+S"(buf), "+c"(count) : "d"(p) : "memory");
}
#define ATA_DATA 0x1F0
#define ATA_SEC  0x1F2
#define ATA_LBA0 0x1F3
#define ATA_LBA1 0x1F4
#define ATA_LBA2 0x1F5
#define ATA_DEV  0x1F6
#define ATA_CMD  0x1F7
#define ATA_STAT 0x1F7
static int ata_present;
static int ata_init(void) {
    uint8_t s = ata_inb(ATA_STAT);
    if (s == 0xFF || s == 0x00) {
        DbgPrint("ata: no legacy IDE (ok on q35/virtio)\n");
        return -1;
    }
    for (int i = 0; i < 100000; i++) {
        s = ata_inb(ATA_STAT);
        if (!(s & 0x80)) break;
    }
    if (s & 0x80) { DbgPrint("ata: busy, skip\n"); return -1; }
    ata_present = 1;
    DbgPrint("ata: ready status=0x%x\n", s);
    return 0;
}
static int ata_read_lba(uint32_t lba, uint8_t *buf) {
    if (!ata_present) return -1;
    ata_outb(ATA_DEV, 0xE0 | ((lba >> 24) & 0x0F));
    ata_outb(ATA_SEC, 1);
    ata_outb(ATA_LBA0, (uint8_t)lba);
    ata_outb(ATA_LBA1, (uint8_t)(lba >> 8));
    ata_outb(ATA_LBA2, (uint8_t)(lba >> 16));
    ata_outb(ATA_CMD, 0x20);
    for (int i = 0; i < 500000; i++) {
        uint8_t st = ata_inb(ATA_STAT);
        if (st & 1) return -1;
        if (!(st & 0x80) && (st & 0x08)) {
            ata_insw(ATA_DATA, buf, 256);
            return 0;
        }
    }
    return -1;
}
static int ata_read(uint64_t off, void *dst, uint64_t len) {
    uint8_t sec[512];
    uint8_t *out = (uint8_t *)dst;
    while (len) {
        uint32_t lba = (uint32_t)(off / 512);
        uint32_t skip = (uint32_t)(off % 512);
        uint32_t n = 512 - skip;
        if (n > len) n = (uint32_t)len;
        if (ata_read_lba(lba, sec) < 0) return -1;
        for (uint32_t i = 0; i < n; i++) out[i] = sec[skip + i];
        out += n; off += n; len -= n;
    }
    return 0;
}

/* Write one 512-byte sector using legacy IDE LBA28 PIO. */
static int ata_write_lba(uint32_t lba, const uint8_t *buf) {
    if (!ata_present || lba >= 0x10000000u) return -1;
    ata_outb(ATA_DEV, 0xE0 | ((lba >> 24) & 0x0F));
    ata_outb(ATA_SEC, 1);
    ata_outb(ATA_LBA0, (uint8_t)lba);
    ata_outb(ATA_LBA1, (uint8_t)(lba >> 8));
    ata_outb(ATA_LBA2, (uint8_t)(lba >> 16));
    ata_outb(ATA_CMD, 0x30);
    for (int i = 0; i < 500000; i++) {
        uint8_t st = ata_inb(ATA_STAT);
        if (st & 1) return -1;
        if (!(st & 0x80) && (st & 0x08)) {
            ata_outsw(ATA_DATA, buf, 256);
            for (int j = 0; j < 500000; j++) {
                uint8_t done = ata_inb(ATA_STAT);
                if (done & 1) return -1;
                if (!(done & 0x80)) return 0;
            }
            return -1;
        }
    }
    return -1;
}

/* Byte-range write with read/modify/write for partial sectors. */
static int ata_write(uint64_t off, const void *src, uint64_t len) {
    uint8_t sec[512];
    const uint8_t *in = (const uint8_t *)src;
    while (len) {
        uint32_t skip = (uint32_t)(off % 512);
        uint32_t n = 512 - skip;
        if (n > len) n = (uint32_t)len;
        uint32_t lba = (uint32_t)(off / 512);
        if (skip || n != 512) {
            if (ata_read_lba(lba, sec) < 0) return -1;
        }
        for (uint32_t i = 0; i < n; i++) sec[skip + i] = in[i];
        if (ata_write_lba(lba, sec) < 0) return -1;
        in += n; off += n; len -= n;
    }
    return 0;
}
