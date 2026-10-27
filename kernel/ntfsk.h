#pragma once
/*
 * SentinelOS read-only NTFS core.
 *
 * This is deliberately a block-device/partition-offset driver, independent of
 * the boot-services NTFS reader. It supports ordinary uncompressed NTFS files
 * whose MFT and unnamed $DATA streams fit in a single runlist attribute.
 * It does not implement writes, compression, encryption, ATTRIBUTE_LIST
 * continuation, or directory-index lookup. Callers must keep the block device
 * alive for the lifetime of the mounted volume.
 */
#include <stdint.h>
#include <stddef.h>
#include "block.h"
#include "heap.h"

#define NTFSK_MAX_RECORD 65536u
#define NTFSK_ATTR_END 0xffffffffu
#define NTFSK_ATTR_DATA 0x80u
#define NTFSK_ATTR_FILE_NAME 0x30u
#define NTFSK_FILE_IN_USE 0x0001u
#define NTFSK_FILE_DIRECTORY 0x0002u

typedef struct {
    SENTINEL_BLOCK_DEVICE *device;
    uint64_t partition_offset;
    uint64_t volume_bytes;
    uint64_t mft_lcn;
    uint64_t mft_bytes;
    uint8_t *mft_runs;
    uint32_t mft_runs_bytes;
    uint32_t sector_size;
    uint32_t cluster_size;
    uint32_t record_size;
    int mounted;
} NTFSK_VOLUME;

static uint16_t ntfsk_u16(const void *p) {
    const uint8_t *b=(const uint8_t *)p; return (uint16_t)(b[0]|((uint16_t)b[1]<<8));
}
static uint32_t ntfsk_u32(const void *p) {
    const uint8_t *b=(const uint8_t *)p;
    return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);
}
static uint64_t ntfsk_u64(const void *p) {
    return (uint64_t)ntfsk_u32(p)|((uint64_t)ntfsk_u32((const uint8_t *)p+4)<<32);
}
static int ntfsk_read(NTFSK_VOLUME *v,uint64_t off,void *dst,uint64_t len) {
    if(!v||!v->device||!v->device->online||!v->device->read||(!dst&&len))return -1;
    if(off>v->volume_bytes||len>v->volume_bytes-off)return -1;
    if(v->partition_offset>UINT64_MAX-off)return -1;
    return v->device->read(v->device->context,v->partition_offset+off,dst,len);
}
static int ntfsk_fixup(uint8_t *r,uint32_t n,uint32_t sector) {
    if(!r||n<512||!sector||n%sector||ntfsk_u32(r)!=0x454c4946u)return -1;
    uint16_t usa=ntfsk_u16(r+4),count=ntfsk_u16(r+6);
    if(count<2||(uint32_t)usa+(uint32_t)count*2>n||
       (uint32_t)(count-1)*sector!=n)return -1;
    uint16_t seq=ntfsk_u16(r+usa);
    for(uint16_t i=1;i<count;i++){
        uint32_t at=(uint32_t)i*sector-2;
        if(ntfsk_u16(r+at)!=seq)return -1;
    }
    for(uint16_t i=1;i<count;i++)
        r[(uint32_t)i*sector-2]=r[usa+(uint32_t)i*2],
        r[(uint32_t)i*sector-1]=r[usa+(uint32_t)i*2+1];
    return 0;
}
/* Read a byte range from an NTFS mapping-pairs runlist. */
static int ntfsk_runs_read(NTFSK_VOLUME *v,const uint8_t *runs,uint32_t rn,
                           uint64_t off,void *dst,uint64_t len) {
    if(!v||!runs||!rn||(!dst&&len)||off>UINT64_MAX-len)return -1;
    if(!len)return 0;
    uint64_t end=off+len,covered=off,vcn=0;
    int64_t lcn=0; uint32_t p=0;
    while(p<rn&&runs[p]){
        uint8_t h=runs[p++],nb=h&15,ob=h>>4;
        if(!nb||nb>8||ob>8||p+nb+ob>rn)return -1;
        uint64_t count=0;
        for(uint8_t i=0;i<nb;i++)count|=(uint64_t)runs[p+i]<<(8*i);
        p+=nb;if(!count||count>UINT64_MAX/v->cluster_size)return -1;
        int sparse=(ob==0);
        if(!sparse){
            uint64_t raw=0;for(uint8_t i=0;i<ob;i++)raw|=(uint64_t)runs[p+i]<<(8*i);
            if(ob<8&&(runs[p+ob-1]&0x80))raw|=(~0ULL)<<(ob*8);
            int64_t delta=(int64_t)raw;
            if((delta>0&&lcn>INT64_MAX-delta)||(delta<0&&lcn<INT64_MIN-delta))return -1;
            lcn+=delta;if(lcn<0)return -1;
        }
        p+=ob;
        if(vcn>UINT64_MAX/v->cluster_size)return -1;
        uint64_t start=vcn*v->cluster_size,bytes=count*v->cluster_size;
        if(start>UINT64_MAX-bytes)return -1;
        uint64_t finish=start+bytes;
        if(off<finish&&end>start){
            uint64_t a=off>start?off:start,b=end<finish?end:finish;
            if(a>covered)return -1;
            uint64_t disk=0;
            if(!sparse){
                if((uint64_t)lcn>UINT64_MAX/v->cluster_size)return -1;
                disk=(uint64_t)lcn*v->cluster_size;
                if(disk>UINT64_MAX-(a-start))return -1;
                disk+=a-start;
                if(ntfsk_read(v,disk,(uint8_t *)dst+(a-off),b-a)<0)return -1;
            }else memset((uint8_t *)dst+(a-off),0,(size_t)(b-a));
            if(b>covered)covered=b;
        }
        if(vcn>UINT64_MAX-count)return -1;
        vcn+=count;
    }
    return covered==end?0:-1;
}
static int ntfsk_record(NTFSK_VOLUME *v,uint64_t index,uint8_t *out) {
    if(!v||!v->mounted||index>=v->mft_bytes/v->record_size)return -1;
    if(index>UINT64_MAX/v->record_size)return -1;
    if(ntfsk_runs_read(v,v->mft_runs,v->mft_runs_bytes,index*v->record_size,
                       out,v->record_size)<0)return -1;
    return ntfsk_fixup(out,v->record_size,v->sector_size);
}
static void ntfsk_unmount(NTFSK_VOLUME *v) {
    if(!v)return;
    if(v->mft_runs)kfree(v->mft_runs);
    memset(v,0,sizeof(*v));
}
static int ntfsk_mount(NTFSK_VOLUME *v,SENTINEL_BLOCK_DEVICE *d,
                       uint64_t partition_offset,uint64_t partition_bytes) {
    if(!v||!d||!d->online||!d->read||!d->sector_size||
       d->sector_count>UINT64_MAX/d->sector_size)return -1;
    uint64_t device_bytes=d->sector_count*(uint64_t)d->sector_size;
    if(partition_offset>device_bytes||partition_bytes>device_bytes-partition_offset)
        return -1;
    memset(v,0,sizeof(*v));v->device=d;v->partition_offset=partition_offset;
    v->volume_bytes=partition_bytes;v->sector_size=d->sector_size;
    uint8_t boot[512];
    if(v->sector_size<512||ntfsk_read(v,0,boot,sizeof(boot))<0||
       boot[3]!='N'||boot[4]!='T'||boot[5]!='F'||boot[6]!='S'||
       ntfsk_u16(boot+510)!=0xaa55)goto fail;
    uint16_t bps=ntfsk_u16(boot+11);uint8_t spc=boot[13];
    if(bps!=v->sector_size||!spc||(spc&(spc-1))||
       (uint64_t)bps*spc>65536)goto fail;
    v->cluster_size=(uint32_t)bps*spc;v->mft_lcn=ntfsk_u64(boot+48);
    int8_t cr=(int8_t)boot[64];
    if(cr<0){if(cr < -16)goto fail;v->record_size=1u<<(-cr);}
    else {if(!cr||cr>127)goto fail;v->record_size=(uint32_t)cr*v->cluster_size;}
    if(v->record_size<512||v->record_size>NTFSK_MAX_RECORD||
       v->mft_lcn>UINT64_MAX/v->cluster_size)goto fail;
    uint64_t mft_off=v->mft_lcn*v->cluster_size;
    if(mft_off>v->volume_bytes||v->record_size>v->volume_bytes-mft_off)goto fail;
    uint8_t *r=kmalloc(v->record_size);if(!r)goto fail;
    int ok=ntfsk_read(v,mft_off,r,v->record_size)==0&&
           ntfsk_fixup(r,v->record_size,v->sector_size)==0;
    uint8_t *mapping=NULL;uint32_t mapping_bytes=0;uint64_t data_bytes=0;
    if(ok){
        uint16_t ao=ntfsk_u16(r+20);
        for(uint32_t p=ao;p+16<v->record_size;){
            uint32_t type=ntfsk_u32(r+p);if(type==NTFSK_ATTR_END)break;
            uint32_t al=ntfsk_u32(r+p+4);if(al<16||p+al>v->record_size)break;
            if(type==NTFSK_ATTR_DATA&&r[p+8]){
                uint16_t ro=ntfsk_u16(r+p+32);
                if(ro<al){mapping_bytes=al-ro;mapping=kmalloc(mapping_bytes);
                    if(mapping){memcpy(mapping,r+p+ro,mapping_bytes);data_bytes=ntfsk_u64(r+p+48);}}
                break;
            }
            p+=al;
        }
    }
    kfree(r);
    if(!ok||!mapping||!mapping_bytes||!data_bytes){if(mapping)kfree(mapping);goto fail;}
    v->mft_runs=mapping;v->mft_runs_bytes=mapping_bytes;v->mft_bytes=data_bytes;
    v->mounted=1;
    /* Prove the runlist maps record zero before publishing the mount. */
    r=kmalloc(v->record_size);
    if(!r||ntfsk_record(v,0,r)<0){if(r)kfree(r);goto fail;}
    kfree(r);return 0;
fail:
    ntfsk_unmount(v);return -1;
}
/* Find a direct child by scanning in-use MFT records. Name matching is ASCII,
 * case-insensitive; NTFS namespace 2 (DOS-only aliases) is ignored. */
static uint64_t ntfsk_lookup(NTFSK_VOLUME *v,uint64_t parent,const char *name) {
    if(!v||!v->mounted||!name||!name[0])return 0;
    uint8_t *r=kmalloc(v->record_size);if(!r)return 0;
    uint64_t found=0,max=v->mft_bytes/v->record_size;
    for(uint64_t idx=0;idx<max;idx++){
        if(ntfsk_record(v,idx,r)<0||(ntfsk_u16(r+22)&NTFSK_FILE_IN_USE)==0)continue;
        uint16_t ao=ntfsk_u16(r+20);
        for(uint32_t p=ao;p+16<v->record_size;){
            uint32_t type=ntfsk_u32(r+p);if(type==NTFSK_ATTR_END)break;
            uint32_t al=ntfsk_u32(r+p+4);if(al<16||p+al>v->record_size)break;
            if(type==NTFSK_ATTR_FILE_NAME&&!r[p+8]){
                uint16_t vo=ntfsk_u16(r+p+20);
                if(vo<=al&&al-vo>=66){
                    uint8_t *n=r+p+vo;uint8_t nl=n[64],ns=n[65];
                    if(ns!=2&&nl&&66u+(uint32_t)nl*2u<=al-vo&&
                       (ntfsk_u64(n)&0x0000ffffffffffffULL)==parent){
                        uint32_t i=0;int same=1;
                        while(i<nl&&name[i]){
                            uint8_t a=(uint8_t)name[i];uint16_t b=ntfsk_u16(n+66+i*2);
                            if(a>='A'&&a<='Z')a=(uint8_t)(a+32);
                            if(b>='A'&&b<='Z')b=(uint16_t)(b+32);
                            if(a!=b){same=0;break;}i++;
                        }
                        if(same&&i==nl&&!name[i]){found=idx;break;}
                    }
                }
            }
            p+=al;
        }
        if(found)break;
    }
    kfree(r);return found;
}
/* Read an unnamed $DATA stream. The caller owns *out and must kfree it. */
static int ntfsk_read_file(NTFSK_VOLUME *v,uint64_t index,void **out,uint64_t *out_size) {
    if(!v||!v->mounted||!out||!out_size)return -1;
    *out=NULL;*out_size=0;
    uint8_t *r=kmalloc(v->record_size);if(!r)return -1;
    if(ntfsk_record(v,index,r)<0){kfree(r);return -1;}
    uint16_t ao=ntfsk_u16(r+20);int result=-1;
    for(uint32_t p=ao;p+16<v->record_size;){
        uint32_t type=ntfsk_u32(r+p);if(type==NTFSK_ATTR_END)break;
        uint32_t al=ntfsk_u32(r+p+4);if(al<16||p+al>v->record_size)break;
        if(type==NTFSK_ATTR_DATA){
            if(r[p+8])break; /* non-resident stream */
            uint32_t n=ntfsk_u32(r+p+16);uint16_t o=ntfsk_u16(r+p+20);
            if(o>al||n>al-o)break;
            void *b=kmalloc(n?n:1);if(!b)break;
            memcpy(b,r+p+o,n);*out=b;*out_size=n;result=0;break;
        }
        p+=al;
    }
    kfree(r);
    if(result==0)return result;
    /* Retry for non-resident data, whose mapping-pairs are in this record. */
    r=kmalloc(v->record_size);if(!r)return -1;
    if(ntfsk_record(v,index,r)<0){kfree(r);return -1;}
    ao=ntfsk_u16(r+20);
    for(uint32_t p=ao;p+16<v->record_size;){
        uint32_t type=ntfsk_u32(r+p);if(type==NTFSK_ATTR_END)break;
        uint32_t al=ntfsk_u32(r+p+4);if(al<16||p+al>v->record_size)break;
        if(type==NTFSK_ATTR_DATA&&r[p+8]){
            uint16_t ro=ntfsk_u16(r+p+32);uint64_t size=ntfsk_u64(r+p+48);
            if(ro>=al||size>SIZE_MAX)break;
            uint32_t rn=al-ro;uint8_t *mapping=kmalloc(rn);if(!mapping)break;
            memcpy(mapping,r+p+ro,rn);void *b=kmalloc(size?(size_t)size:1);
            if(b&&ntfsk_runs_read(v,mapping,rn,0,b,size)==0){
                *out=b;*out_size=size;result=0;
            }else if(b)kfree(b);
            kfree(mapping);break;
        }
        p+=al;
    }
    kfree(r);return result;
}
