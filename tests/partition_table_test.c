#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/partition.h"

#define DISK_SECTORS 128u
static uint8_t disk[DISK_SECTORS * 512u];
static int disk_read(void *ctx, uint64_t off, void *dst, uint64_t len) {
    (void)ctx;
    if (off > sizeof(disk) || len > sizeof(disk) - off) return -1;
    memcpy(dst, disk + off, (size_t)len);
    return 0;
}
static void put32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
}
static void put64(uint8_t *p, uint64_t v) { put32(p,(uint32_t)v);put32(p+4,(uint32_t)(v>>32)); }
static void mbr_entry(uint8_t *p, unsigned n, uint8_t type, uint32_t first, uint32_t count) {
    uint8_t *e=p+446+n*16;e[4]=type;put32(e+8,first);put32(e+12,count);
}
static int fail(const char *what) { fprintf(stderr,"partition test failed: %s\n",what);return 1; }

static void make_gpt(void) {
    memset(disk,0,sizeof(disk));
    disk[510]=0x55;disk[511]=0xaa;mbr_entry(disk,0,0xee,1,DISK_SECTORS-1);
    uint8_t *header=disk+512;memcpy(header,"EFI PART",8);
    put32(header+8,0x00010000);put32(header+12,92);put64(header+24,1);
    put64(header+32,DISK_SECTORS-1);put64(header+40,3);put64(header+48,DISK_SECTORS-2);
    put64(header+72,2);put32(header+80,4);put32(header+84,128);
    uint8_t *entry=disk+2*512;entry[0]=0x28;entry[16]=0x42;
    put64(entry+32,8);put64(entry+40,31);
    put32(header+88,sentinel_part_crc32(entry,4*128));
    put32(header+16,0);put32(header+16,sentinel_part_crc32(header,92));
}

int main(void) {
    SENTINEL_PARTITION parts[SENTINEL_PARTITION_MAX];
    memset(disk,0,sizeof(disk));disk[510]=0x55;disk[511]=0xaa;
    mbr_entry(disk,0,0x07,4,20);mbr_entry(disk,1,0x83,32,40);
    int n=SentinelPartitionScan(disk_read,0,DISK_SECTORS,512,parts,SENTINEL_PARTITION_MAX);
    if(n!=2||parts[0].first_lba!=4||parts[1].first_lba!=32||parts[1].scheme!=SENTINEL_PARTITION_MBR)return fail("valid MBR");
    mbr_entry(disk,1,0x83,23,40);
    if(SentinelPartitionScan(disk_read,0,DISK_SECTORS,512,parts,SENTINEL_PARTITION_MAX)>=0)return fail("overlapping MBR rejected");
    memset(disk,0,sizeof(disk));disk[510]=0x55;disk[511]=0xaa;
    mbr_entry(disk,0,0x07,120,20);
    if(SentinelPartitionScan(disk_read,0,DISK_SECTORS,512,parts,SENTINEL_PARTITION_MAX)>=0)return fail("out-of-range MBR rejected");
    make_gpt();
    n=SentinelPartitionScan(disk_read,0,DISK_SECTORS,512,parts,SENTINEL_PARTITION_MAX);
    if(n!=1||parts[0].scheme!=SENTINEL_PARTITION_GPT||parts[0].first_lba!=8||parts[0].sector_count!=24)return fail("valid GPT");
    disk[512+24]^=1;
    if(SentinelPartitionScan(disk_read,0,DISK_SECTORS,512,parts,SENTINEL_PARTITION_MAX)>=0)return fail("bad GPT header CRC rejected");
    make_gpt();disk[2*512+3]^=1;
    if(SentinelPartitionScan(disk_read,0,DISK_SECTORS,512,parts,SENTINEL_PARTITION_MAX)>=0)return fail("bad GPT entry CRC rejected");
    make_gpt();put32(disk+446+8,2);
    if(SentinelPartitionScan(disk_read,0,DISK_SECTORS,512,parts,SENTINEL_PARTITION_MAX)>=0)return fail("bad protective MBR rejected");
    make_gpt();put64(disk+2*512+32,DISK_SECTORS);
    put32(disk+512+88,sentinel_part_crc32(disk+2*512,4*128));
    put32(disk+512+16,0);put32(disk+512+16,sentinel_part_crc32(disk+512,92));
    if(SentinelPartitionScan(disk_read,0,DISK_SECTORS,512,parts,SENTINEL_PARTITION_MAX)>=0)return fail("GPT partition outside usable range rejected");
    if(SentinelPartitionScan(disk_read,0,UINT64_MAX,4096,parts,SENTINEL_PARTITION_MAX)>=0)return fail("capacity overflow rejected");
    puts("MBR/GPT parser tests: PASS");
    return 0;
}
