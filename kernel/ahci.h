#pragma once
/* Polling AHCI/SATA block driver. It publishes only SATA devices that report
 * LBA48 and 512-byte logical sectors. It does not claim ATAPI or port-multiplier
 * support. */
#include "block.h"

#define AHCI_MAX_PORTS 32
#define AHCI_PCI_CLASS 0x01
#define AHCI_PCI_SUBCLASS 0x06
#define AHCI_PCI_PROGIF 0x01

typedef volatile uint32_t ahci_reg;
typedef struct {
    ahci_reg clb, clbu, fb, fbu, is, ie, cmd, rsv0, tfd, sig, ssts, sctl, serr, sact, ci, sntf, fbs, devslp;
    uint32_t rsv1[10]; uint32_t vendor[4];
} AHCI_PORT_REGS;
typedef struct { ahci_reg cap, ghc, is, pi, vs, ccc_ctl, ccc_ports, em_loc, em_ctl, cap2, bohc; uint8_t rsv[0x74]; } AHCI_HBA_REGS;
/* AHCI structures are laid out explicitly to avoid compiler bitfield ABI. */
typedef struct __attribute__((packed, aligned(128))) {
    uint16_t flags, prdtl; volatile uint32_t prdbc; uint32_t ctba, ctbau; uint32_t rsv[4];
} AHCI_CMD_HDR;
typedef struct __attribute__((packed, aligned(128))) {
    uint8_t cfis[64]; uint8_t acmd[16]; uint8_t rsv[48];
    struct __attribute__((packed)) { uint32_t dba, dbau, rsv, dbc; } prdt[1];
} AHCI_CMD_TBL;
typedef struct {
    AHCI_PORT_REGS *regs; AHCI_CMD_HDR *hdr; AHCI_CMD_TBL *tbl;
    uint64_t hdr_pa, tbl_pa, buf_pa, fb_pa, sectors;
    volatile uint32_t lock;
    char name[16], model[41]; int online;
} AHCI_DISK;
static AHCI_DISK ahci_disks[AHCI_MAX_PORTS];
static AHCI_HBA_REGS *ahci_hba;
static int ahci_dma64;

static int ahci_wait_clear(volatile uint32_t *reg, uint32_t mask) {
    for (uint32_t i=0;i<10000000;i++) { if (!(*reg & mask)) return 0; __asm__ volatile("pause"); }
    return -1;
}
static void ahci_disk_lock(AHCI_DISK *d) {
    while (__atomic_exchange_n(&d->lock, 1u, __ATOMIC_ACQUIRE))
        while (__atomic_load_n(&d->lock, __ATOMIC_RELAXED)) __asm__ volatile("pause");
}
static void ahci_disk_unlock(AHCI_DISK *d) {
    __atomic_store_n(&d->lock, 0u, __ATOMIC_RELEASE);
}
static void ahci_map(uint64_t pa, uint64_t len) {
    for (uint64_t o=0;o<len;o+=4096) map_4k(HHDM_BASE+pa+o,pa+o,PTE_RW|PTE_PCD|PTE_PWT);
}
static uint64_t ahci_bar5(uint8_t bus,uint8_t slot) {
    uint32_t lo=pci_cfg_read(bus,slot,0,0x24);
    /* BAR5 cannot be the lower half of a 64-bit BAR. */
    if ((lo&1)||((lo&6)!=0)) return 0;
    return (uint64_t)(lo&~0xFu);
}
static int ahci_stop_port(AHCI_PORT_REGS *p) {
    p->cmd &= ~1u; if (ahci_wait_clear(&p->cmd,1u<<15)<0) return -1;
    p->cmd &= ~(1u<<4); return ahci_wait_clear(&p->cmd,1u<<14);
}
static int ahci_issue(AHCI_DISK *d, uint8_t command, uint64_t lba, int write) {
    AHCI_PORT_REGS *p=d->regs;
    if (ahci_wait_clear(&p->tfd,0x88u)<0 || ahci_wait_clear(&p->ci,1u)<0) return -1;
    p->is=0xFFFFFFFFu; p->serr=0xFFFFFFFFu;
    AHCI_CMD_HDR *h=&d->hdr[0]; AHCI_CMD_TBL *t=d->tbl;
    h->flags=(uint16_t)(5u|(write?1u<<6:0)); h->prdtl=1; h->prdbc=0;
    for(unsigned i=0;i<64;i++)t->cfis[i]=0;
    t->cfis[0]=0x27; t->cfis[1]=0x80; t->cfis[2]=command;
    t->cfis[4]=(uint8_t)lba; t->cfis[5]=(uint8_t)(lba>>8); t->cfis[6]=(uint8_t)(lba>>16);
    t->cfis[7]=0x40; t->cfis[8]=(uint8_t)(lba>>24); t->cfis[9]=(uint8_t)(lba>>32); t->cfis[10]=(uint8_t)(lba>>40);
    t->cfis[12]=1; t->cfis[13]=0;
    t->prdt[0].dba=(uint32_t)d->buf_pa; t->prdt[0].dbau=(uint32_t)(d->buf_pa>>32);
    t->prdt[0].rsv=0; t->prdt[0].dbc=511u|(1u<<31);
    __asm__ volatile("sfence" ::: "memory"); p->ci=1u;
    for(uint32_t i=0;i<10000000;i++) {
        uint32_t is=p->is;
        if(is&((1u<<30)|(1u<<29)|(1u<<28)|(1u<<27)|(1u<<26)|(1u<<24))) {
            p->is=is; p->serr=0xFFFFFFFFu; return -1;
        }
        if(!(p->ci&1u)) {
            __asm__ volatile("lfence" ::: "memory");
            return (p->tfd&1u) || d->hdr[0].prdbc!=512 ? -1 : 0;
        }
        __asm__ volatile("pause");
    }
    d->online=0;
    (void)ahci_stop_port(p);
    return -1;
}
static int ahci_read_sector_locked(AHCI_DISK *d,uint64_t lba,void *dst) {
    if(!d||!d->online||!dst||lba>=d->sectors)return -1;
    int rc=ahci_issue(d,0x25,lba,0);
    if(!rc){uint8_t *s=(uint8_t *)(HHDM_BASE+d->buf_pa),*o=(uint8_t *)dst;for(unsigned i=0;i<512;i++)o[i]=s[i];}
    return rc;
}
static int ahci_write_sector_locked(AHCI_DISK *d,uint64_t lba,const void *src) {
    if(!d||!d->online||!src||lba>=d->sectors)return -1;
    uint8_t *s=(uint8_t *)(HHDM_BASE+d->buf_pa);const uint8_t *in=(const uint8_t *)src;for(unsigned i=0;i<512;i++)s[i]=in[i];
    return ahci_issue(d,0x35,lba,1);
}
static int ahci_block_read(void *ctx,uint64_t off,void *dst,uint64_t len) {
    AHCI_DISK *d=(AHCI_DISK *)ctx; uint8_t sec[512],*out=(uint8_t *)dst;
    if(!d||!d->online||(!dst&&len)||d->sectors>UINT64_MAX/512ULL||off>d->sectors*512ULL||len>d->sectors*512ULL-off)return -1;
    ahci_disk_lock(d);
    while(len){uint64_t lba=off/512;uint32_t skip=(uint32_t)(off%512),n=512-skip;if(n>len)n=(uint32_t)len;
        if(ahci_read_sector_locked(d,lba,sec)<0){ahci_disk_unlock(d);return -1;}for(uint32_t i=0;i<n;i++)out[i]=sec[skip+i];out+=n;off+=n;len-=n;}ahci_disk_unlock(d);return 0;
}
static int ahci_block_write(void *ctx,uint64_t off,const void *src,uint64_t len) {
    AHCI_DISK *d=(AHCI_DISK *)ctx;uint8_t sec[512];const uint8_t *in=(const uint8_t *)src;
    if(!d||!d->online||(!src&&len)||d->sectors>UINT64_MAX/512ULL||off>d->sectors*512ULL||len>d->sectors*512ULL-off)return -1;
    ahci_disk_lock(d);
    while(len){uint64_t lba=off/512;uint32_t skip=(uint32_t)(off%512),n=512-skip;if(n>len)n=(uint32_t)len;
        if(skip||n!=512){if(ahci_read_sector_locked(d,lba,sec)<0){ahci_disk_unlock(d);return -1;}}for(uint32_t i=0;i<n;i++)sec[skip+i]=in[i];
        if(ahci_write_sector_locked(d,lba,sec)<0){ahci_disk_unlock(d);return -1;}in+=n;off+=n;len-=n;}ahci_disk_unlock(d);return 0;
}
static void ahci_discard_port(AHCI_DISK *d,AHCI_PORT_REGS *p) {
    d->online=0;
    if(p&&ahci_stop_port(p)<0)return; /* retain DMA pages if the engine won't stop */
    if(p){p->clb=0;p->clbu=0;p->fb=0;p->fbu=0;}
    if(d->hdr_pa)pmm_free(d->hdr_pa);
    if(d->tbl_pa)pmm_free(d->tbl_pa);
    if(d->buf_pa)pmm_free(d->buf_pa);
    if(d->fb_pa)pmm_free(d->fb_pa);
    d->hdr_pa=d->tbl_pa=d->buf_pa=d->fb_pa=0;
    d->hdr=0;d->tbl=0;d->regs=0;d->sectors=0;
}
static int ahci_setup_port(AHCI_PORT_REGS *p,unsigned port) {
    if((p->ssts&0x0F)!=3 || ((p->ssts>>8)&0x0F)!=1 || (p->sig!=0x00000101u))return -1; /* active SATA disk */
    AHCI_DISK *d=&ahci_disks[port]; d->regs=p;
    if(ahci_stop_port(p)<0)return -1;
    d->hdr_pa=pmm_alloc_zeroed();d->tbl_pa=pmm_alloc_zeroed();d->buf_pa=pmm_alloc_zeroed();
    if(!d->hdr_pa||!d->tbl_pa||!d->buf_pa)goto fail;
    if(!ahci_dma64 && (d->hdr_pa>0xffffffffULL||d->tbl_pa>0xffffffffULL||d->buf_pa>0xffffffffULL))goto fail;
    d->hdr=(AHCI_CMD_HDR *)(HHDM_BASE+d->hdr_pa);d->tbl=(AHCI_CMD_TBL *)(HHDM_BASE+d->tbl_pa);
    ahci_map(d->hdr_pa,4096);ahci_map(d->tbl_pa,4096);ahci_map(d->buf_pa,4096);
    d->hdr[0].ctba=(uint32_t)d->tbl_pa;d->hdr[0].ctbau=(uint32_t)(d->tbl_pa>>32);
    p->clb=(uint32_t)d->hdr_pa;p->clbu=(uint32_t)(d->hdr_pa>>32);
    uint64_t fb=pmm_alloc_zeroed();if(!fb)goto fail;d->fb_pa=fb;if(!ahci_dma64&&fb>0xffffffffULL)goto fail;ahci_map(fb,4096);p->fb=(uint32_t)fb;p->fbu=(uint32_t)(fb>>32);
    p->cmd|=(1u<<4)|(1u<<2)|1u;
    uint8_t *id=(uint8_t *)(HHDM_BASE+d->buf_pa);for(unsigned i=0;i<512;i++)id[i]=0;
    if(ahci_issue(d,0xEC,0,0)<0)goto fail;
    uint16_t *w=(uint16_t *)id;
    if(!(w[83]&(1u<<10)))goto fail; /* READ/WRITE DMA EXT require LBA48. */
    if((w[106]&0xC000u)==0x4000u && (w[106]&(1u<<12))) {
        uint32_t logical_words=(uint32_t)w[117]|((uint32_t)w[118]<<16);
        if(logical_words!=256)goto fail;
    }
    d->sectors=(uint64_t)w[100]|((uint64_t)w[101]<<16)|((uint64_t)w[102]<<32)|((uint64_t)w[103]<<48);
    if(!d->sectors||d->sectors>(1ULL<<48))goto fail;
    for(unsigned i=0;i<20;i++){uint16_t v=w[27+i];d->model[i*2]=(char)(v>>8);d->model[i*2+1]=(char)v;}
    d->model[40]=0;
    for(int i=39;i>=0&&d->model[i]==' ';--i)d->model[i]=0;
    d->name[0]='s';d->name[1]='a';d->name[2]='t';d->name[3]='a';d->name[4]=(char)('0'+port/10);d->name[5]=(char)('0'+port%10);d->name[6]=0;d->online=1;
    SENTINEL_BLOCK_DEVICE b={0};b.size=sizeof(b);b.abi_version=SENTINEL_BLOCK_ABI_VERSION;
    for(unsigned i=0;i<sizeof(d->name);i++)b.name[i]=d->name[i];
    for(unsigned i=0;i<sizeof(b.model)-1&&d->model[i];i++)b.model[i]=d->model[i];
    b.sector_size=512;b.sector_count=d->sectors;b.context=d;b.read=ahci_block_read;b.write=ahci_block_write;
    int rc=SentinelBlockRegister(&b);
    if(rc==0)DbgPrint("ahci: %s model='%s' sectors=%llu logical-sector=512\n",d->name,d->model,(unsigned long long)d->sectors);
    else goto fail;
    return rc;
fail:
    ahci_discard_port(d,p);
    return -1;
}
static int ahci_init(void) {
    for(uint16_t bus=0;bus<256;bus++)for(uint8_t slot=0;slot<32;slot++){
        uint32_t id=pci_cfg_read((uint8_t)bus,slot,0,0);if((uint16_t)id==0xFFFF)continue;
        uint32_t cls=pci_cfg_read(bus,slot,0,8);if(((cls>>24)&255)!=AHCI_PCI_CLASS||((cls>>16)&255)!=AHCI_PCI_SUBCLASS||((cls>>8)&255)!=AHCI_PCI_PROGIF)continue;
        uint64_t bar=ahci_bar5((uint8_t)bus,slot);if(!bar||(bar&0xFFFu))continue;
        uint32_t cmd=pci_cfg_read((uint8_t)bus,slot,0,4);pci_cfg_write((uint8_t)bus,slot,0,4,cmd|6u);
        ahci_map(bar,0x1100);ahci_hba=(AHCI_HBA_REGS *)(HHDM_BASE+bar);
        if(ahci_hba->cap2&1u) {
            ahci_hba->bohc|=(1u<<1);
            uint32_t wait=0;
            while((ahci_hba->bohc&1u)&&wait++<10000000u)__asm__ volatile("pause");
            if(ahci_hba->bohc&1u){DbgPrint("ahci: BIOS ownership handoff timed out\n");continue;}
        }
        ahci_hba->ghc&=~(1u<<1);ahci_hba->ghc|=1u;
        uint32_t wait=0;
        while((ahci_hba->ghc&1u)&&wait++<10000000u)__asm__ volatile("pause");
        if(ahci_hba->ghc&1u){DbgPrint("ahci: HBA reset timed out\n");continue;}
        ahci_hba->ghc|=(1u<<31);ahci_hba->ghc&=~(1u<<1);
        uint32_t pi=ahci_hba->pi;int count=0;
        ahci_dma64=(ahci_hba->cap&(1u<<31))!=0;
        for(unsigned p=0;p<AHCI_MAX_PORTS;p++)if(pi&(1u<<p))if(ahci_setup_port((AHCI_PORT_REGS *)((uintptr_t)ahci_hba+0x100+p*0x80),p)==0)count++;
        DbgPrint("ahci: controller %u:%u version=%x ports=%x registered %d SATA disk(s)\n",bus,slot,ahci_hba->vs,pi,count);
        return count?0:-1;
    }
    DbgPrint("ahci: no PCI AHCI controller\n");return -1;
}
