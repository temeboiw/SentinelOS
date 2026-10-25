#pragma once
/* SentinelOS VirtIO-GPU 2D backend. PCI discovery, modern virtio-pci transport,
 * control virtqueue, 2D resource/scanout and explicit flush are implemented
 * here. Unsupported devices retain the firmware framebuffer path. */
#include <stdint.h>
#include "serial.h"

#define GFX_BOOTINFO_VERSION 1u
#define GFX_BACKEND_NONE 0u
#define GFX_BACKEND_FRAMEBUFFER 1u
#define GFX_BACKEND_VIRTIO_GPU 2u
#define GFX_CAP_FRAMEBUFFER (1u << 0)
#define GFX_CAP_PCI_SCAN    (1u << 1)
#define GFX_CAP_VIRTIO_GPU  (1u << 2)
#define GFX_CAP_ACCEL_2D    (1u << 3)
#define GFX_CAP_ACCEL_3D    (1u << 4)
#define GFX_GPU_MAX_W 1920u
#define GFX_GPU_MAX_H 1080u
#define GFX_VQ_MAX 16u
#define GFX_RESOURCE_ID 1u
#define GFX_VIRGL_TARGET_ID 2u
#define GFX_VIRGL_BUFFER_ID 3u
#define GFX_VIRGL_BUFFER_BYTES (1024u*1024u)
#define GFX_VIRGL_BUFFER_PAGES (GFX_VIRGL_BUFFER_BYTES/4096u)
#define GFX_VIRTIO_VENDOR 0x1AF4u
#define GFX_VIRTIO_GPU_ID 0x1050u
#define GFX_VIRTIO_GPU_LEGACY_ID 0x1005u
#define GFX_PCI_CAP_VENDOR 0x09u
#define GFX_CAP_COMMON 1u
#define GFX_CAP_NOTIFY 2u
#define GFX_GPU_CMD_CREATE_2D 0x0101u
#define GFX_GPU_CMD_SET_SCANOUT 0x0103u
#define GFX_GPU_CMD_FLUSH 0x0104u
#define GFX_GPU_CMD_TRANSFER 0x0105u
#define GFX_GPU_CMD_ATTACH_BACKING 0x0106u
#define GFX_GPU_CMD_CTX_CREATE 0x0200u
#define GFX_GPU_CMD_CTX_ATTACH_RESOURCE 0x0202u
#define GFX_GPU_CMD_CREATE_3D 0x0204u
#define GFX_GPU_CMD_TRANSFER_TO_3D 0x0205u
#define GFX_GPU_CMD_TRANSFER_FROM_3D 0x0206u
#define GFX_GPU_CMD_SUBMIT_3D 0x0207u
#define GFX_VIRTIO_GPU_F_VIRGL (1u << 0)
#define GFX_VIRTIO_GPU_F_CONTEXT_INIT (1u << 4)
#define GFX_GPU_RESP_OK_NODATA 0x1100u
#define GFX_GPU_FORMAT_B8G8R8X8_UNORM 2u

typedef struct {
    uint32_t size, version, backend, capabilities;
    uint32_t width, height, pitch;
    uint32_t pci_bus, pci_device, pci_function;
    uint16_t vendor_id, device_id;
    uint8_t pci_class, pci_subclass, pci_prog_if, initialized;
    uint64_t surface_va;
    uint64_t virgl_buffer_va;
    uint32_t virgl_buffer_resource_id;
    uint32_t virgl_buffer_size;
} GfxBootInfo;

typedef struct { uint64_t addr; uint32_t len; uint16_t flags, next; } GfxDesc;
typedef struct { uint16_t flags, idx, ring[GFX_VQ_MAX]; } GfxAvail;
typedef struct { uint32_t id, len; } GfxUsedElem;
typedef struct { uint16_t flags, idx; GfxUsedElem ring[GFX_VQ_MAX]; } GfxUsed;
typedef struct {
    uint32_t type, flags;
    uint64_t fence_id;
    uint32_t ctx_id, padding;
} GfxCtrlHdr;
typedef struct { uint64_t addr; uint32_t length, padding; } GfxMemEntry;
typedef struct {
    GfxCtrlHdr hdr;
    uint32_t resource_id, format, width, height;
} GfxCreate2D;
typedef struct {
    GfxCtrlHdr hdr;
    uint32_t resource_id, nr_entries;
    GfxMemEntry entries[1];
} GfxAttachBacking;
typedef struct {
    GfxCtrlHdr hdr;
    uint32_t x, y, width, height, scanout_id, resource_id;
} GfxSetScanout;
typedef struct {
    GfxCtrlHdr hdr;
    uint32_t x, y, width, height;
    uint64_t offset;
    uint32_t resource_id, padding;
} GfxTransfer;
typedef struct {
    GfxCtrlHdr hdr;
    uint32_t x, y, width, height, resource_id, padding;
} GfxFlush;
typedef struct { GfxCtrlHdr hdr; } GfxResponse;

static uint8_t gfx_gpu_ready;
static uint8_t gfx_gpu_failed;
static uint8_t gfx_virgl_supported;
static uint8_t gfx_context_init_supported;
static uint8_t gfx_3d_ready;
static uint64_t gfx_surface_pa, gfx_surface_bytes, gfx_surface_pages;
static uint64_t gfx_virgl_buffer_pa;
static uint32_t gfx_width, gfx_height, gfx_pitch;
static volatile uint8_t *gfx_common, *gfx_notify;
static uint32_t gfx_notify_mult;
static uint16_t gfx_queue_size, gfx_avail_idx, gfx_last_used, gfx_notify_off;
static uint64_t gfx_desc_pa, gfx_avail_pa, gfx_used_pa, gfx_req_pa, gfx_resp_pa;
static GfxDesc *gfx_desc;
static GfxAvail *gfx_avail;
static volatile GfxUsed *gfx_used;
static uint8_t *gfx_req;
static uint8_t gfx_submit_buffer[4096];
static volatile GfxResponse *gfx_resp;
static uint32_t gfx_pci_bus, gfx_pci_dev, gfx_pci_fn;
static uint16_t gfx_pci_vendor, gfx_pci_device;
static uint8_t gfx_pci_class, gfx_pci_subclass, gfx_pci_prog_if;

static inline void gfx_outl(uint16_t p, uint32_t v) {
    __asm__ volatile("outl %0, %1" :: "a"(v), "Nd"(p));
}
static inline uint32_t gfx_inl(uint16_t p) {
    uint32_t v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v;
}
static uint32_t gfx_cfg_read(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    gfx_outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) |
        ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (off & 0xFC));
    return gfx_inl(0xCFC);
}
static void gfx_cfg_write(uint8_t bus, uint8_t dev, uint8_t fn,
                          uint8_t off, uint32_t value) {
    gfx_outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) |
        ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (off & 0xFC));
    gfx_outl(0xCFC, value);
}
static uint8_t gfx_cfg_read8(uint8_t b, uint8_t d, uint8_t f, uint8_t o) {
    return (uint8_t)(gfx_cfg_read(b,d,f,o) >> ((o & 3u) * 8u));
}
static uint64_t gfx_bar_addr(uint8_t b, uint8_t d, uint8_t f, uint8_t bar) {
    uint32_t lo = gfx_cfg_read(b,d,f,(uint8_t)(0x10u + bar*4u));
    if (lo & 1u) return 0;
    uint64_t pa = lo & ~0xFu;
    if ((lo & 6u) == 4u && bar < 5u)
        pa |= (uint64_t)gfx_cfg_read(b,d,f,(uint8_t)(0x14u + bar*4u)) << 32;
    return pa;
}
/* PCI BARs may sit inside a 2 MiB HHDM mapping established by vmm_init.
   Never try to split that direct-map large page just to access a device BAR:
   give each MMIO range its own 4 KiB virtual alias with uncached attributes. */
static uint64_t gfx_mmio_next_va = 0xFFFFC10000000000ULL;
static void *gfx_map_mmio(uint64_t pa, uint64_t bytes) {
    if (!pa || !bytes || bytes > 0x100000ULL || pa > UINT64_MAX - bytes)
        return 0;
    uint64_t first = pa & ~0xFFFULL;
    uint64_t last = (pa + bytes + 0xFFFULL) & ~0xFFFULL;
    uint64_t span = last - first;
    if (!span || gfx_mmio_next_va > 0xFFFFC1FFFFFFFFFFULL - span)
        return 0;
    uint64_t va = gfx_mmio_next_va;
    for (uint64_t off = 0; off < span; off += 0x1000ULL)
        map_4k(va + off, first + off, PTE_RW | PTE_PCD | PTE_PWT);
    gfx_mmio_next_va += span;
    return (void *)(va + (pa - first));
}
/* Enumerate PCI display controllers without touching their BARs. This is
 * PCI inventory only: this scan identifies display-class hardware but does not
 * initialize vendor-specific drivers. The active VirtIO-GPU/VirGL backend is
 * detected and reported separately; it can provide 3D acceleration when the
 * device and host configuration support VirGL. */
static void gfx_log_display_adapters(void) {
    unsigned int found=0;
    for(uint32_t b=0;b<256;b++) {
        for(uint32_t d=0;d<32;d++) {
            uint32_t id0=gfx_cfg_read((uint8_t)b,(uint8_t)d,0,0);
            if((uint16_t)id0==0 || (uint16_t)id0==0xFFFFu) continue;
            uint32_t hdr=gfx_cfg_read((uint8_t)b,(uint8_t)d,0,0x0C);
            uint32_t nf=((hdr>>16)&0x80u)?8u:1u;
            for(uint32_t fn=0;fn<nf;fn++) {
                uint32_t id=fn?gfx_cfg_read((uint8_t)b,(uint8_t)d,(uint8_t)fn,0):id0;
                uint16_t vendor=(uint16_t)id,device=(uint16_t)(id>>16);
                if(!vendor||vendor==0xFFFFu)continue;
                uint32_t cl=gfx_cfg_read((uint8_t)b,(uint8_t)d,(uint8_t)fn,8);
                uint8_t base=(uint8_t)(cl>>24),sub=(uint8_t)(cl>>16);
                if(base!=0x03u)continue;
                const char *name="other";
                if(vendor==0x8086u)name="Intel";
                else if(vendor==0x10DEu)name="NVIDIA";
                else if(vendor==0x1002u || vendor==0x1022u)name="AMD";
                else if(vendor==GFX_VIRTIO_VENDOR)name="VirtIO";
                DbgPrint("gfx: PCI display #%u %s %04x:%04x at %02x:%02x.%u class=%02x/%02x (PCI inventory; active graphics backend reported separately)\n",
                    found,name,vendor,device,b,d,fn,base,sub);
                found++;
            }
        }
    }
    if(!found)DbgPrint("gfx: no PCI display-class adapter found; framebuffer fallback remains available\n");
}
static int gfx_find_device(void) {
    for (uint32_t b=0;b<256;b++) for (uint32_t d=0;d<32;d++) {
        uint32_t id0=gfx_cfg_read((uint8_t)b,(uint8_t)d,0,0);
        uint16_t v0=(uint16_t)id0;
        if (!v0 || v0==0xFFFFu) continue;
        uint32_t hdr=gfx_cfg_read((uint8_t)b,(uint8_t)d,0,0x0C);
        uint32_t nf=((hdr>>16)&0x80u)?8u:1u;
        for(uint32_t fn=0;fn<nf;fn++) {
            uint32_t id=fn?gfx_cfg_read((uint8_t)b,(uint8_t)d,(uint8_t)fn,0):id0;
            uint16_t v=(uint16_t)id, dev=(uint16_t)(id>>16);
            if(v!=GFX_VIRTIO_VENDOR ||
               (dev!=GFX_VIRTIO_GPU_ID && dev!=GFX_VIRTIO_GPU_LEGACY_ID)) continue;
            gfx_pci_bus=b;gfx_pci_dev=d;gfx_pci_fn=fn;gfx_pci_vendor=v;gfx_pci_device=dev;
            uint32_t cl=gfx_cfg_read((uint8_t)b,(uint8_t)d,(uint8_t)fn,8);
            gfx_pci_class=(uint8_t)(cl>>24);gfx_pci_subclass=(uint8_t)(cl>>16);gfx_pci_prog_if=(uint8_t)(cl>>8);
            return 1;
        }
    }
    return 0;
}
static int gfx_find_caps(void) {
    uint8_t b=gfx_pci_bus,d=gfx_pci_dev,f=gfx_pci_fn;
    uint32_t command=gfx_cfg_read(b,d,f,4);
    gfx_cfg_write(b,d,f,4,(command&0xFFFFu)|0x0006u); /* memory decode + bus master */
    uint8_t cap=gfx_cfg_read8(b,d,f,0x34);
    for(unsigned int guard=0;cap && cap!=0xFF && guard<48;guard++) {
        uint8_t id=gfx_cfg_read8(b,d,f,cap);
        uint8_t next=gfx_cfg_read8(b,d,f,(uint8_t)(cap+1));
        if(id==GFX_PCI_CAP_VENDOR) {
            uint8_t typ=gfx_cfg_read8(b,d,f,(uint8_t)(cap+3));
            uint8_t bar=gfx_cfg_read8(b,d,f,(uint8_t)(cap+4));
            uint32_t off=gfx_cfg_read(b,d,f,(uint8_t)(cap+8));
            uint64_t base=gfx_bar_addr(b,d,f,bar);
            if(base) {
                volatile uint8_t *p=(volatile uint8_t*)gfx_map_mmio(base+(off&~0xFFFu),0x1000);
                /* Capability offset may be unaligned within the mapped page. */
                p=(volatile uint8_t*)((uint64_t)p+(off&0xFFFu));
                if(typ==GFX_CAP_COMMON) gfx_common=p;
                else if(typ==GFX_CAP_NOTIFY) {
                    gfx_notify=p;
                    gfx_notify_mult=gfx_cfg_read(b,d,f,(uint8_t)(cap+16));
                }
            }
        }
        cap=next;
    }
    if(!gfx_notify_mult) gfx_notify_mult=1;
    return gfx_common && gfx_notify;
}
static int gfx_queue_init(void) {
    volatile uint8_t *c=gfx_common;
    c[20]=0; /* reset */
    for(unsigned int i=0;i<1000000 && c[20]!=0;i++) __asm__ volatile("pause");
    c[20]=1; /* ACKNOWLEDGE */
    c[20]=3; /* DRIVER */
    /* Modern VirtIO devices require VIRTIO_F_VERSION_1 (feature bit 32).
       Do not mark FEATURES_OK unless the device advertises it. */
    *(volatile uint32_t*)(c+0)=0; /* device feature selector: low word */
    uint32_t device_features_lo=*(volatile uint32_t*)(c+4);
    gfx_virgl_supported=(device_features_lo&GFX_VIRTIO_GPU_F_VIRGL)?1:0;
    gfx_context_init_supported=(device_features_lo&GFX_VIRTIO_GPU_F_CONTEXT_INIT)?1:0;
    *(volatile uint32_t*)(c+0)=1; /* device feature selector: high word */
    uint32_t device_features_hi=*(volatile uint32_t*)(c+4);
    if(!(device_features_hi&1u)) {
        DbgPrint("gfx: device lacks VIRTIO_F_VERSION_1\n");
        return 0;
    }
    *(volatile uint32_t*)(c+8)=0; /* driver feature selector: low word */
    uint32_t driver_features_lo=gfx_virgl_supported?GFX_VIRTIO_GPU_F_VIRGL:0;
    if(gfx_context_init_supported)driver_features_lo|=GFX_VIRTIO_GPU_F_CONTEXT_INIT;
    *(volatile uint32_t*)(c+12)=driver_features_lo;
    *(volatile uint32_t*)(c+8)=1; /* driver feature selector: high word */
    *(volatile uint32_t*)(c+12)=1; /* negotiate VIRTIO_F_VERSION_1 */
    c[20]=11; /* FEATURES_OK */
    if(!(c[20]&8u)) { DbgPrint("gfx: device rejected required feature set\n"); return 0; }
    *(volatile uint16_t*)(c+22)=0; /* control queue */
    uint16_t q=*(volatile uint16_t*)(c+24);
    if(q<2) { DbgPrint("gfx: control queue unavailable\n"); return 0; }
    if(q>GFX_VQ_MAX) q=GFX_VQ_MAX;
    /* The queue memory below is allocated for at most GFX_VQ_MAX entries.
       VirtIO's queue_size register is the device-visible ring size, so write
       the capped value back before publishing descriptor/avail/used addresses.
       Otherwise a device advertising a larger queue can overrun our 16-entry
       rings even though the driver indexes them modulo the smaller value. */
    *(volatile uint16_t*)(c+24)=q;
    if(*(volatile uint16_t*)(c+24)!=q) {
        DbgPrint("gfx: device rejected control queue size %u\n",(unsigned)q);
        return 0;
    }
    gfx_queue_size=q;
    gfx_desc_pa=pmm_alloc_zeroed();gfx_avail_pa=pmm_alloc_zeroed();gfx_used_pa=pmm_alloc_zeroed();
    gfx_req_pa=pmm_alloc_zeroed();gfx_resp_pa=pmm_alloc_zeroed();
    if(!gfx_desc_pa||!gfx_avail_pa||!gfx_used_pa||!gfx_req_pa||!gfx_resp_pa) return 0;
    gfx_desc=(GfxDesc*)(HHDM_BASE+gfx_desc_pa);
    gfx_avail=(GfxAvail*)(HHDM_BASE+gfx_avail_pa);
    gfx_used=(volatile GfxUsed*)(HHDM_BASE+gfx_used_pa);
    gfx_req=(uint8_t*)(HHDM_BASE+gfx_req_pa);
    gfx_resp=(volatile GfxResponse*)(HHDM_BASE+gfx_resp_pa);
    *(volatile uint64_t*)(c+32)=gfx_desc_pa;
    *(volatile uint64_t*)(c+40)=gfx_avail_pa;
    *(volatile uint64_t*)(c+48)=gfx_used_pa;
    *(volatile uint16_t*)(c+28)=1; /* enable queue */
    gfx_notify_off=*(volatile uint16_t*)(c+30);
    gfx_avail_idx=0;gfx_last_used=gfx_used->idx;
    c[20]=15; /* DRIVER_OK */
    return 1;
}
static int gfx_command(const void *request, uint32_t request_len) {
    if(!gfx_gpu_ready && !gfx_common) return 0;
    if(!gfx_queue_size || !gfx_avail || !gfx_used || !gfx_desc || !gfx_req || !gfx_resp ||
       request_len < sizeof(GfxCtrlHdr) || request_len > 4096u) return 0;
    for(unsigned int i=0;i<request_len;i++)gfx_req[i]=((const uint8_t*)request)[i];
    gfx_resp->hdr.type=0;gfx_resp->hdr.flags=0;gfx_resp->hdr.fence_id=0;
    gfx_resp->hdr.ctx_id=0;gfx_resp->hdr.padding=0;
    gfx_desc[0].addr=gfx_req_pa;gfx_desc[0].len=request_len;
    gfx_desc[0].flags=1;gfx_desc[0].next=1;
    gfx_desc[1].addr=gfx_resp_pa;gfx_desc[1].len=sizeof(GfxResponse);
    gfx_desc[1].flags=2;gfx_desc[1].next=0;
    uint16_t at=gfx_avail_idx;
    gfx_avail->ring[at%gfx_queue_size]=0;
    __asm__ volatile("sfence" ::: "memory");
    gfx_avail->idx=++gfx_avail_idx;
    __asm__ volatile("sfence" ::: "memory");
    *(volatile uint16_t*)(gfx_notify+(uint32_t)gfx_notify_off*gfx_notify_mult)=0;
    int done=0;
    for(unsigned int i=0;i<50000000u;i++) {
        if(gfx_used->idx!=gfx_last_used) {gfx_last_used=gfx_used->idx;done=1;break;}
        __asm__ volatile("pause");
    }
    if(!done) {DbgPrint("gfx: command timeout type=%x\n",((const GfxCtrlHdr*)request)->type);return 0;}
    __asm__ volatile("lfence" ::: "memory");
    if(gfx_resp->hdr.type!=GFX_GPU_RESP_OK_NODATA) {
        DbgPrint("gfx: command %x returned response %x\n",
            ((const GfxCtrlHdr*)request)->type,gfx_resp->hdr.type);
        return 0;
    }
    return 1;
}
static int gfx_create_virgl_context(void) {
    if(!gfx_gpu_ready || !gfx_common || !gfx_width || !gfx_height ||
       gfx_width>GFX_GPU_MAX_W || gfx_height>GFX_GPU_MAX_H) return 0;
    struct {
        GfxCtrlHdr hdr;
        uint32_t nlen, context_init;
        char debug_name[64];
    } ctx={0};
    ctx.hdr.type=GFX_GPU_CMD_CTX_CREATE;
    ctx.hdr.ctx_id=1;
    /* Capset 1 is VirGL; context-init selection is only used if negotiated. */
    ctx.context_init=gfx_context_init_supported?1u:0u;
    const char name[]="SentinelOS VirGL";
    ctx.nlen=(uint32_t)(sizeof(name)-1);
    for(uint32_t i=0;i<sizeof(name);i++)ctx.debug_name[i]=name[i];
    return gfx_command(&ctx,sizeof ctx);
}
static int gfx_submit_virgl(const uint8_t *stream,uint32_t length) {
    /* VirGL command words are 32-bit little-endian dwords. Reject partial
       words before they can reach the host renderer. */
    if(!gfx_3d_ready||!stream||!length||(length&3u)||length>4096u-sizeof(GfxCtrlHdr)-8u)return 0;
    struct { GfxCtrlHdr hdr; uint32_t size,padding; } submit={0};
    submit.hdr.type=GFX_GPU_CMD_SUBMIT_3D;
    submit.hdr.ctx_id=1;
    submit.size=length;
    for(uint32_t i=0;i<sizeof(submit);i++)gfx_submit_buffer[i]=((const uint8_t*)&submit)[i];
    for(uint32_t i=0;i<length;i++)gfx_submit_buffer[sizeof(submit)+i]=stream[i];
    return gfx_command(gfx_submit_buffer,(uint32_t)sizeof(submit)+length);
}
static int gfx_flush(uint32_t x,uint32_t y,uint32_t w,uint32_t h);
static int gfx_create_virgl_target(void) {
    /* The 3D target aliases the framebuffer backing. Never expose an invalid
       or undersized surface to the host GPU renderer. */
    if(!gfx_surface_pa || !gfx_surface_bytes || !gfx_width || !gfx_height ||
       gfx_surface_bytes<(uint64_t)gfx_width*gfx_height*4ULL ||
       gfx_width>GFX_GPU_MAX_W || gfx_height>GFX_GPU_MAX_H) return 0;
    struct {
        GfxCtrlHdr hdr;
        uint32_t resource_id,target,format,bind,width,height,depth;
        uint32_t array_size,last_level,nr_samples,flags,padding;
    } cr={0};
    cr.hdr.type=GFX_GPU_CMD_CREATE_3D;cr.hdr.ctx_id=1;
    cr.resource_id=GFX_VIRGL_TARGET_ID;cr.target=2; /* PIPE_TEXTURE_2D */
    cr.format=GFX_GPU_FORMAT_B8G8R8X8_UNORM;
    cr.bind=(1u<<1)|(1u<<3); /* render target + sampler view */
    cr.width=gfx_width;cr.height=gfx_height;cr.depth=1;cr.array_size=1;
    cr.last_level=0;cr.nr_samples=0;cr.flags=1;cr.padding=0;
    if(!gfx_command(&cr,sizeof cr))return 0;

    struct { GfxCtrlHdr hdr;uint32_t resource_id,nr_entries;GfxMemEntry entry; } attach={0};
    attach.hdr.type=GFX_GPU_CMD_ATTACH_BACKING;attach.hdr.ctx_id=1;
    attach.resource_id=GFX_VIRGL_TARGET_ID;attach.nr_entries=1;
    attach.entry.addr=gfx_surface_pa;attach.entry.length=(uint32_t)gfx_surface_bytes;
    if(!gfx_command(&attach,sizeof attach))return 0;

    struct { GfxCtrlHdr hdr;uint32_t resource_id,padding; } bind={0};
    bind.hdr.type=GFX_GPU_CMD_CTX_ATTACH_RESOURCE;bind.hdr.ctx_id=1;
    bind.resource_id=GFX_VIRGL_TARGET_ID;
    if(!gfx_command(&bind,sizeof bind))return 0;
    return 1;
}
static int gfx_create_virgl_buffer(void) {
    if(!gfx_gpu_ready || !gfx_common || !gfx_width || !gfx_height ||
       gfx_width>GFX_GPU_MAX_W || gfx_height>GFX_GPU_MAX_H) return 0;
    gfx_virgl_buffer_pa=pmm_alloc_n(GFX_VIRGL_BUFFER_PAGES);
    if(!gfx_virgl_buffer_pa)return 0;
    for(uint64_t p=0;p<GFX_VIRGL_BUFFER_PAGES;p++) {
        volatile uint64_t *page=(volatile uint64_t*)(gfx_virgl_buffer_pa+p*4096ULL);
        for(unsigned int i=0;i<512;i++)page[i]=0;
    }
    struct {
        GfxCtrlHdr hdr;
        uint32_t resource_id,target,format,bind,width,height,depth;
        uint32_t array_size,last_level,nr_samples,flags,padding;
    } cr={0};
    cr.hdr.type=GFX_GPU_CMD_CREATE_3D;cr.hdr.ctx_id=1;cr.resource_id=GFX_VIRGL_BUFFER_ID;
    /* Gallium's enum pipe_texture_target defines PIPE_BUFFER = 0.
       The vertex/index/constant-buffer bind mask is valid only for this
       buffer target; 5 is PIPE_TEXTURE_RECT and causes host-side rejection. */
    cr.target=0; /* PIPE_BUFFER */
    cr.format=0; /* PIPE_BUFFER resources use format 0 */
    cr.bind=(1u<<4)|(1u<<5)|(1u<<6); /* vertex, index, constant buffers */
    cr.width=GFX_VIRGL_BUFFER_BYTES;cr.height=1;cr.depth=1;cr.array_size=1;
    if(!gfx_command(&cr,sizeof cr))return 0;
    struct { GfxCtrlHdr hdr;uint32_t resource_id,nr_entries;GfxMemEntry entry; } attach={0};
    attach.hdr.type=GFX_GPU_CMD_ATTACH_BACKING;attach.hdr.ctx_id=1;
    attach.resource_id=GFX_VIRGL_BUFFER_ID;attach.nr_entries=1;
    attach.entry.addr=gfx_virgl_buffer_pa;attach.entry.length=GFX_VIRGL_BUFFER_BYTES;
    if(!gfx_command(&attach,sizeof attach))return 0;
    struct { GfxCtrlHdr hdr;uint32_t resource_id,padding; } bind={0};
    bind.hdr.type=GFX_GPU_CMD_CTX_ATTACH_RESOURCE;bind.hdr.ctx_id=1;
    bind.resource_id=GFX_VIRGL_BUFFER_ID;
    return gfx_command(&bind,sizeof bind);
}
static int gfx_map_virgl_buffer_for_process(KPROCESS *proc,uint64_t *uva_out) {
    if(!gfx_3d_ready||!gfx_virgl_buffer_pa||!proc||!uva_out)return 0;
    uint64_t bytes=(uint64_t)GFX_VIRGL_BUFFER_PAGES*4096ULL;
    for(uint64_t uva=0x72000000ULL;uva<0x7F000000ULL;uva+=0x01000000ULL) {
        if(!MmUserRangeUnmapped(proc,uva,bytes)) {
            uint64_t first=MmLookupUserPte(proc,uva,0);
            uint64_t last=MmLookupUserPte(proc,uva+bytes-4096ULL,0);
            if((first&PTE_ADDR)==gfx_virgl_buffer_pa &&
               (last&PTE_ADDR)==gfx_virgl_buffer_pa+(GFX_VIRGL_BUFFER_PAGES-1)*4096ULL &&
               (first&PTE_U)&&(last&PTE_U)) {
                *uva_out=uva;return 1;
            }
            continue;
        }
        KPROCESS *old=MmCurrentProcess;
        MmSwitchToProcess(PspSystemProcess);
        for(uint64_t i=0;i<GFX_VIRGL_BUFFER_PAGES;i++)
            map_4k_in(proc->Pml4,uva+i*4096ULL,gfx_virgl_buffer_pa+i*4096ULL,PTE_U|PTE_RW);
        MmSwitchToProcess(old);
        *uva_out=uva;return 1;
    }
    return 0;
}
static int gfx_upload_virgl_buffer(uint32_t bytes) {
    if(!gfx_3d_ready||!gfx_virgl_buffer_pa||!bytes||(bytes&3u)||bytes>GFX_VIRGL_BUFFER_BYTES)return 0;
    struct {
        GfxCtrlHdr hdr;
        uint32_t x,y,z,width,height,depth;
        uint64_t offset;
        uint32_t resource_id,level,stride,layer_stride;
    } tr={0};
    tr.hdr.type=GFX_GPU_CMD_TRANSFER_TO_3D;tr.hdr.ctx_id=1;
    tr.width=bytes;tr.height=1;tr.depth=1;
    tr.resource_id=GFX_VIRGL_BUFFER_ID;tr.level=0;
    return gfx_command(&tr,sizeof tr);
}
static int gfx_present_virgl(void) {
    if(!gfx_surface_pa || !gfx_surface_bytes || !gfx_width || !gfx_height ||
       gfx_surface_bytes<(uint64_t)gfx_width*gfx_height*4ULL) return 0;
    if(!gfx_3d_ready)return 0;
    struct {
        GfxCtrlHdr hdr;
        uint32_t x,y,z,width,height,depth;
        uint64_t offset;
        uint32_t resource_id,level,stride,layer_stride;
    } tr={0};
    tr.hdr.type=GFX_GPU_CMD_TRANSFER_FROM_3D;tr.hdr.ctx_id=1;
    tr.width=gfx_width;tr.height=gfx_height;tr.depth=1;
    tr.resource_id=GFX_VIRGL_TARGET_ID;tr.level=0;tr.stride=0;
    tr.layer_stride=0;
    if(!gfx_command(&tr,sizeof tr))return 0;
    return gfx_flush(0,0,gfx_width,gfx_height);
}
/* Issue a real VirGL clear, copy the rendered 3D target into the shared
 * scanout backing, then transfer/flush it through the 2D display resource.
 * This is a startup validation pattern, not a replacement scene renderer. */
/* Submit a real VirGL clear, copy the 3D target to guest backing, then
 * transfer and flush that backing through the existing 2D scanout resource.
 * This validates clear/presentation only; it is not the Maze scene renderer. */
static int gfx_virgl_clear_present_test(void) {
    if(!gfx_3d_ready || !gfx_width || !gfx_height)return 0;
    /* CREATE_OBJECT(SURFACE): 6 payload dwords; SET_FRAMEBUFFER_STATE: 3;
       CLEAR: 7 (mask, RGBA, depth float, stencil). */
    uint32_t stream[19]={
        0x00060801u, /* CREATE_OBJECT, SURFACE, 6 dwords */
        1u, GFX_VIRGL_TARGET_ID, 2u, 0u, 0u, 0u, /* handle,res,format,level,layers */
        0x00030005u, /* SET_FRAMEBUFFER_STATE, 3 dwords */
        1u, 0u, 1u,  /* one color buffer, no depth, surface handle 1 */
        0x00070007u, /* CLEAR, 7 dwords */
        0x00000004u, /* PIPE_CLEAR_COLOR0 */
        0x3D23D70Au, /* R = 0.04 */
        0x3DCCCCCDu, /* G = 0.10 */
        0x3F19999Au, /* B = 0.60 */
        0x3F800000u, /* A = 1.00 */
        0x3F800000u, /* depth = 1.0f */
        0u            /* stencil */
    };
    if(!gfx_submit_virgl((const uint8_t*)stream,sizeof(stream))) {
        DbgPrint("gfx: VirGL clear test failed: host rejected command stream\n");
        return 0;
    }
    if(!gfx_present_virgl()) {
        DbgPrint("gfx: VirGL clear accepted, but 3D-to-scanout transfer/presentation failed\n");
        return 0;
    }
    /* The virtio response confirms command completion, but this driver does
       not read back or inspect pixel values. Do not claim visual validation. */
    DbgPrint("gfx: VirGL clear commands completed and scanout flushed; pixel contents not read back/verified\n");
    return 1;
}
static int gfx_create_resource(void) {
    GfxCreate2D cr={0};
    cr.hdr.type=GFX_GPU_CMD_CREATE_2D;cr.resource_id=GFX_RESOURCE_ID;
    cr.format=GFX_GPU_FORMAT_B8G8R8X8_UNORM;
    cr.width=gfx_width;cr.height=gfx_height;
    if(!gfx_command(&cr,sizeof cr)) return 0;
    struct { GfxCtrlHdr hdr;uint32_t resource_id,nr_entries;GfxMemEntry entry; } attach={0};
    attach.hdr.type=GFX_GPU_CMD_ATTACH_BACKING;attach.resource_id=GFX_RESOURCE_ID;
    attach.nr_entries=1;attach.entry.addr=gfx_surface_pa;
    attach.entry.length=(uint32_t)gfx_surface_bytes;
    if(!gfx_command(&attach,sizeof attach)) return 0;
    GfxSetScanout ss={0};ss.hdr.type=GFX_GPU_CMD_SET_SCANOUT;
    ss.width=gfx_width;ss.height=gfx_height;ss.scanout_id=0;ss.resource_id=GFX_RESOURCE_ID;
    if(!gfx_command(&ss,sizeof ss)) return 0;
    GfxTransfer tr={0};tr.hdr.type=GFX_GPU_CMD_TRANSFER;
    tr.width=gfx_width;tr.height=gfx_height;tr.resource_id=GFX_RESOURCE_ID;
    if(!gfx_command(&tr,sizeof tr)) return 0;
    GfxFlush fl={0};fl.hdr.type=GFX_GPU_CMD_FLUSH;
    fl.width=gfx_width;fl.height=gfx_height;fl.resource_id=GFX_RESOURCE_ID;
    if(!gfx_command(&fl,sizeof fl)) return 0;
    return 1;
}
static int gfx_driver_init(uint32_t width,uint32_t height) {
    if(gfx_gpu_ready)return 1;
    if(gfx_gpu_failed)return 0;
    if(width<320u||height<200u||width>GFX_GPU_MAX_W||height>GFX_GPU_MAX_H) {
        DbgPrint("gfx: unsupported mode %ux%u for VirtIO-GPU\n",width,height);gfx_gpu_failed=1;return 0;
    }
    if(!gfx_find_device()) {gfx_gpu_failed=1;return 0;}
    DbgPrint("gfx: VirtIO-GPU at %02x:%02x.%u id=%04x:%04x\n",
        gfx_pci_bus,gfx_pci_dev,gfx_pci_fn,gfx_pci_vendor,gfx_pci_device);
    if(!gfx_find_caps()||!gfx_queue_init()) {gfx_gpu_failed=1;return 0;}
    gfx_width=width;gfx_height=height;gfx_pitch=width*4u;
    gfx_surface_bytes=(uint64_t)gfx_pitch*height;
    gfx_surface_pages=(gfx_surface_bytes+4095u)/4096u;
    gfx_surface_pa=pmm_alloc_n(gfx_surface_pages);
    if(!gfx_surface_pa) {DbgPrint("gfx: unable to allocate contiguous scanout backing\n");gfx_gpu_failed=1;return 0;}
    for(uint64_t p=0;p<gfx_surface_pages;p++) {
        volatile uint64_t *page=(volatile uint64_t*)(gfx_surface_pa+p*4096u);
        for(unsigned int i=0;i<512;i++)page[i]=0;
    }
    if(!gfx_create_resource()) {
        DbgPrint("gfx: resource setup failed; retaining firmware framebuffer\n");
        gfx_gpu_failed=1;return 0;
    }
    gfx_gpu_ready=1;
    DbgPrint("gfx: VirtIO feature negotiation: VIRGL=%u CONTEXT_INIT=%u\n",
        (unsigned)gfx_virgl_supported,(unsigned)gfx_context_init_supported);
    if(gfx_virgl_supported) {
        if(!gfx_create_virgl_context()) {
            DbgPrint("gfx: VirGL context creation failed (CTX_CREATE rejected; verify host VirGL/capset 1 support)\n");
        } else if(!gfx_create_virgl_target()) {
            DbgPrint("gfx: VirGL render-target creation/attachment failed (CREATE_3D, ATTACH_BACKING, or CTX_ATTACH_RESOURCE)\n");
        } else if(!gfx_create_virgl_buffer()) {
            DbgPrint("gfx: VirGL shared buffer creation/attachment failed (buffer CREATE_3D/ATTACH_BACKING)\n");
        } else {
            gfx_3d_ready=1;
            DbgPrint("gfx: VirGL context, render target, and shared buffer ready; raw command submission enabled\n");
            (void)gfx_virgl_clear_present_test();
        }
    } else {
        DbgPrint("gfx: VirGL is not negotiated; this VM is using a 2D-only VirtIO-GPU device. Start with SENTINELOS_DISPLAY=window SENTINELOS_VIRGL=1 bash scripts/run.sh and host OpenGL support.\n");
    }
    DbgPrint("gfx: VirtIO-GPU 2D scanout active %ux%u, %llu backing pages\n",
        gfx_width,gfx_height,(unsigned long long)gfx_surface_pages);
    return 1;
}
/* Sparse timing telemetry: RDTSC ticks measure the synchronous command /
 * transfer / flush path. Ticks are intentionally not mislabeled as nanoseconds:
 * without calibrated TSC frequency this is latency instrumentation, not FPS. */
static uint64_t gfx_dbg_flush_count;
static uint64_t gfx_dbg_flush_pixels;
static uint64_t gfx_dbg_flush_tsc_total;
static uint64_t gfx_dbg_flush_tsc_max;
static uint64_t gfx_dbg_flush_tsc_hist[16];
static inline uint64_t gfx_read_tsc(void) {
    uint32_t lo,hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo),"=d"(hi) :: "memory");
    return ((uint64_t)hi<<32)|lo;
}
static unsigned int gfx_tsc_bucket(uint64_t ticks) {
    unsigned int b=0;
    while(ticks>1u && b<15u) { ticks>>=1; b++; }
    return b;
}
static int gfx_flush(uint32_t x,uint32_t y,uint32_t w,uint32_t h) {
    if(!gfx_gpu_ready)return 0;
    if(x>=gfx_width||y>=gfx_height||!w||!h)return 0;
    if(w>gfx_width-x)w=gfx_width-x;if(h>gfx_height-y)h=gfx_height-y;
    uint64_t start_tsc=gfx_read_tsc();
    GfxTransfer tr={0};tr.hdr.type=GFX_GPU_CMD_TRANSFER;
    tr.x=x;tr.y=y;tr.width=w;tr.height=h;
    /* TRANSFER_TO_HOST_2D reads the rectangle from the guest backing starting
     * at this byte offset. It must point at the rectangle's own top-left pixel;
     * leaving it 0 copies the top-left of the screen into every damaged rect. */
    tr.offset=(uint64_t)y*gfx_pitch+(uint64_t)x*4u;
    tr.resource_id=GFX_RESOURCE_ID;
    if(!gfx_command(&tr,sizeof tr))return 0;
    GfxFlush fl={0};fl.hdr.type=GFX_GPU_CMD_FLUSH;
    fl.x=x;fl.y=y;fl.width=w;fl.height=h;fl.resource_id=GFX_RESOURCE_ID;
    if(!gfx_command(&fl,sizeof fl))return 0;
    uint64_t elapsed=gfx_read_tsc()-start_tsc;
    uint64_t count=++gfx_dbg_flush_count;
    uint64_t pixels=(uint64_t)w*(uint64_t)h;
    gfx_dbg_flush_pixels+=pixels;
    gfx_dbg_flush_tsc_total+=elapsed;
    if(elapsed>gfx_dbg_flush_tsc_max)gfx_dbg_flush_tsc_max=elapsed;
    unsigned int bucket=gfx_tsc_bucket(elapsed);
    gfx_dbg_flush_tsc_hist[bucket]++;
    if(count<=4u||(count%256u)==0u)
        DbgPrint("[gfx] virtio-2d flush #%llu rect=%u,%u %ux%u pixels=%llu cumulative=%llu latency_tsc=%llu avg_tsc=%llu max_tsc=%llu bucket=%u (command+transfer+flush; TSC ticks are not milliseconds or FPS)\\n",
            (unsigned long long)count,x,y,w,h,
            (unsigned long long)pixels,
            (unsigned long long)gfx_dbg_flush_pixels,
            (unsigned long long)elapsed,
            (unsigned long long)(gfx_dbg_flush_tsc_total/count),
            (unsigned long long)gfx_dbg_flush_tsc_max,bucket);
    return 1;
}
static int gfx_map_surface_for_process(KPROCESS *proc,uint64_t *uva_out) {
    if(!gfx_gpu_ready||!proc||!uva_out)return 0;
    uint64_t bytes=gfx_surface_pages*4096ULL;
    uint64_t uva=0x71000000ULL;
    if(!MmUserRangeUnmapped(proc,uva,bytes)) {
        /* Repeated initialization in the same process reuses its mapping. */
        uint64_t first=MmLookupUserPte(proc,uva,0);
        uint64_t last=MmLookupUserPte(proc,uva+bytes-4096ULL,0);
        if((first&PTE_ADDR)==gfx_surface_pa &&
           (last&PTE_ADDR)==gfx_surface_pa+(gfx_surface_pages-1)*4096ULL &&
           (first&PTE_U) && (last&PTE_U)) {
            *uva_out=uva;return 1;
        }
        /* Avoid colliding with an application's existing allocation. */
        for(uva=0x72000000ULL;uva<0x7F000000ULL;uva+=0x01000000ULL)
            if(MmUserRangeUnmapped(proc,uva,bytes))break;
        if(uva>=0x7F000000ULL)return 0;
    }
    KPROCESS *old=MmCurrentProcess;
    MmSwitchToProcess(PspSystemProcess);
    for(uint64_t i=0;i<gfx_surface_pages;i++)
        map_4k_in(proc->Pml4,uva+i*4096ULL,gfx_surface_pa+i*4096ULL,PTE_U|PTE_RW);
    MmSwitchToProcess(old);
    *uva_out=uva;return 1;
}
static void gfx_boot_initialize(GfxBootInfo *out,uint32_t fb_width,
                                uint32_t fb_height,uint32_t fb_pitch) {
    if(!out)return;
    *out=(GfxBootInfo){0};out->size=sizeof(*out);out->version=GFX_BOOTINFO_VERSION;
    out->width=fb_width;out->height=fb_height;out->pitch=fb_pitch;
    if(fb_width>=320u&&fb_height>=200u&&fb_pitch>=fb_width*4u) {
        out->capabilities|=GFX_CAP_FRAMEBUFFER;out->backend=GFX_BACKEND_FRAMEBUFFER;out->initialized=1;
    }
    out->capabilities|=GFX_CAP_PCI_SCAN;
    gfx_log_display_adapters();
    KPROCESS *caller=MmCurrentProcess;
    if(caller!=PspSystemProcess)MmSwitchToProcess(PspSystemProcess);
    int gpu=gfx_driver_init(fb_width,fb_height);
    if(caller!=PspSystemProcess)MmSwitchToProcess(caller);
    if(gpu) {
        out->capabilities|=GFX_CAP_VIRTIO_GPU|GFX_CAP_ACCEL_2D;
        if(gfx_3d_ready)out->capabilities|=GFX_CAP_ACCEL_3D;
        out->backend=GFX_BACKEND_VIRTIO_GPU;out->pitch=gfx_pitch;out->initialized=1;
        out->pci_bus=gfx_pci_bus;out->pci_device=gfx_pci_dev;out->pci_function=gfx_pci_fn;
        out->vendor_id=gfx_pci_vendor;out->device_id=gfx_pci_device;
        out->pci_class=gfx_pci_class;out->pci_subclass=gfx_pci_subclass;out->pci_prog_if=gfx_pci_prog_if;
        if(gfx_map_surface_for_process(caller,&out->surface_va)) {
            DbgPrint("gfx: mapped GPU surface into pid=%lu at %p\n",
                (unsigned long)caller->Pid,(void*)out->surface_va);
            if(gfx_3d_ready && gfx_map_virgl_buffer_for_process(caller,&out->virgl_buffer_va)) {
                out->virgl_buffer_resource_id=GFX_VIRGL_BUFFER_ID;
                out->virgl_buffer_size=GFX_VIRGL_BUFFER_BYTES;
            } else {
                out->capabilities&=~GFX_CAP_ACCEL_3D;
                out->virgl_buffer_va=0;out->virgl_buffer_resource_id=0;out->virgl_buffer_size=0;
            }
        } else {
            out->backend=GFX_BACKEND_FRAMEBUFFER;
            out->capabilities&=~(GFX_CAP_VIRTIO_GPU|GFX_CAP_ACCEL_2D|GFX_CAP_ACCEL_3D);
            out->surface_va=0;out->virgl_buffer_va=0;
            out->virgl_buffer_resource_id=0;out->virgl_buffer_size=0;
            DbgPrint("gfx: GPU is ready but user surface mapping failed; using framebuffer\n");
        }
    } else {
        DbgPrint("gfx: using firmware framebuffer fallback\n");
    }
}
