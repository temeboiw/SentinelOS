#include "soft3d.h"

typedef struct { int32_t x, y; int64_t z; uint32_t color; } S3D_PROJECTED;

static int64_t s3d_edge(int32_t ax,int32_t ay,int32_t bx,int32_t by,
                        int32_t px,int32_t py) {
    return (int64_t)(px-ax)*(by-ay)-(int64_t)(py-ay)*(bx-ax);
}
static int32_t s3d_clamp_screen(int64_t v) {
    if(v < -16384) return -16384;
    if(v > 16384) return 16384;
    return (int32_t)v;
}
static int s3d_project(const S3D_CONTEXT *ctx,const S3D_VERTEX *in,
                       S3D_PROJECTED *out) {
    if(!ctx||!in||!out||in->z<=4096)return 0; /* near clip: 1/16 unit */
    int64_t sx=((int64_t)in->x*ctx->focal_length)/(int64_t)in->z;
    int64_t sy=((int64_t)in->y*ctx->focal_length)/(int64_t)in->z;
    sx=(int64_t)(ctx->width/2)+(sx>>16);
    sy=(int64_t)(ctx->height/2)-(sy>>16);
    out->x=s3d_clamp_screen(sx);
    out->y=s3d_clamp_screen(sy);
    out->z=in->z;
    out->color=in->color;
    return 1;
}
static uint32_t s3d_lerp_channel(uint32_t a,uint32_t b,uint32_t c,
                                 int64_t wa,int64_t wb,int64_t wc,
                                 int64_t area) {
    int64_t n=(int64_t)a*wa+(int64_t)b*wb+(int64_t)c*wc;
    if(n<0)return 0;
    uint64_t q=(uint64_t)(n/area);
    return q>255u?255u:(uint32_t)q;
}
typedef struct {
    uint32_t size,version,backend,capabilities;
    uint32_t width,height,pitch;
    uint32_t pci_bus,pci_device,pci_function;
    uint16_t vendor_id,device_id;
    uint8_t pci_class,pci_subclass,pci_prog_if,initialized;
    uint64_t surface_va;
    uint64_t virgl_buffer_va;
    uint32_t virgl_buffer_resource_id,virgl_buffer_size;
} S3D_GFX_INFO;
typedef struct { uint64_t va; uint32_t w,h,pitch; } S3D_FB_INFO;

static uint64_t s3d_syscall3(uint64_t n,uint64_t a,uint64_t b,uint64_t c);

int S3DGetSurface(S3D_SURFACE *surface) {
    if(!surface)return 0;
    surface->pixels=0;surface->width=0;surface->height=0;
    surface->pitch=0;surface->backend=0;surface->virgl_buffer=0;
    surface->virgl_buffer_resource_id=0;surface->virgl_buffer_size=0;
    S3D_GFX_INFO info={0};info.size=sizeof(info);info.version=1;
    if(s3d_syscall3(32,(uint64_t)&info,0,0) && info.backend==2 &&
       info.surface_va && info.width && info.height && info.pitch>=info.width*4u) {
        surface->pixels=(uint32_t*)(uintptr_t)info.surface_va;
        surface->width=info.width;surface->height=info.height;
        surface->pitch=info.pitch/4u;surface->backend=2;
        if(info.capabilities&(1u<<4)) {
            surface->virgl_buffer=(void*)(uintptr_t)info.virgl_buffer_va;
            surface->virgl_buffer_resource_id=info.virgl_buffer_resource_id;
            surface->virgl_buffer_size=info.virgl_buffer_size;
        }
        return 1;
    }
    S3D_FB_INFO fb={0};
    if(!s3d_syscall3(8,(uint64_t)&fb,0,0)||!fb.va||!fb.w||!fb.h||fb.pitch<fb.w*4u)
        return 0;
    surface->pixels=(uint32_t*)(uintptr_t)fb.va;
    surface->width=fb.w;surface->height=fb.h;
    surface->pitch=fb.pitch/4u;surface->backend=1;
    return 1;
}
int S3DInitializeSurface(S3D_CONTEXT *ctx,const S3D_SURFACE *surface,
                         uint32_t *depth,int32_t focal) {
    if(!surface||!surface->pixels)return 0;
    if(!S3DInitialize(ctx,surface->pixels,depth,surface->width,surface->height,
                      surface->pitch,focal))return 0;
    ctx->backend=surface->backend;
    return 1;
}

int S3DInitialize(S3D_CONTEXT *ctx,uint32_t *color,uint32_t *depth,
                  uint32_t width,uint32_t height,uint32_t pitch,
                  int32_t focal) {
    if(!ctx||!color||!depth||width<1||height<1||width>1920||height>1080||
       pitch<width||focal<=0)return 0;
    ctx->color=color;ctx->depth=depth;ctx->width=width;ctx->height=height;
    ctx->pitch=pitch;ctx->focal_length=focal;ctx->backend=0;
    return 1;
}
void S3DClear(S3D_CONTEXT *ctx,uint32_t argb,uint32_t depth_value) {
    if(!ctx||!ctx->color||!ctx->depth||ctx->pitch<ctx->width)return;
    for(uint32_t y=0;y<ctx->height;y++) {
        uint64_t row=(uint64_t)y*ctx->pitch;
        uint64_t zrow=(uint64_t)y*ctx->width;
        for(uint32_t x=0;x<ctx->width;x++) {
            ctx->color[row+x]=argb;
            ctx->depth[zrow+x]=depth_value;
        }
    }
}
int S3DDrawTriangle(S3D_CONTEXT *ctx,const S3D_VERTEX *a,
                    const S3D_VERTEX *b,const S3D_VERTEX *c) {
    if(!ctx||!ctx->color||!ctx->depth||ctx->pitch<ctx->width)return 0;
    S3D_PROJECTED v[3];
    if(!s3d_project(ctx,a,&v[0])||!s3d_project(ctx,b,&v[1])||
       !s3d_project(ctx,c,&v[2]))return 0;
    int64_t area=s3d_edge(v[0].x,v[0].y,v[1].x,v[1].y,v[2].x,v[2].y);
    if(!area)return 0;
    if(area<0) {
        S3D_PROJECTED tmp=v[1];v[1]=v[2];v[2]=tmp;
        area=-area;
    }
    int32_t minx=v[0].x,maxx=v[0].x,miny=v[0].y,maxy=v[0].y;
    for(int i=1;i<3;i++) {
        if(v[i].x<minx)minx=v[i].x;if(v[i].x>maxx)maxx=v[i].x;
        if(v[i].y<miny)miny=v[i].y;if(v[i].y>maxy)maxy=v[i].y;
    }
    if(minx<0)minx=0;if(miny<0)miny=0;
    if(maxx>=(int32_t)ctx->width)maxx=(int32_t)ctx->width-1;
    if(maxy>=(int32_t)ctx->height)maxy=(int32_t)ctx->height-1;
    if(minx>maxx||miny>maxy)return 0;
    uint32_t drawn=0;
    for(int32_t y=miny;y<=maxy;y++)for(int32_t x=minx;x<=maxx;x++) {
        int64_t w0=s3d_edge(v[1].x,v[1].y,v[2].x,v[2].y,x,y);
        int64_t w1=s3d_edge(v[2].x,v[2].y,v[0].x,v[0].y,x,y);
        int64_t w2=s3d_edge(v[0].x,v[0].y,v[1].x,v[1].y,x,y);
        if(w0<0||w1<0||w2<0)continue;
        uint64_t depth=(uint64_t)((w0*v[0].z+w1*v[1].z+w2*v[2].z)/area);
        uint64_t zi=(uint64_t)(uint32_t)y*ctx->width+(uint32_t)x;
        if(depth>=ctx->depth[zi])continue;
        uint64_t ci=(uint64_t)(uint32_t)y*ctx->pitch+(uint32_t)x;
        uint32_t ca=v[0].color,cb=v[1].color,cc=v[2].color;
        uint32_t aa=s3d_lerp_channel(ca>>24,cb>>24,cc>>24,w0,w1,w2,area);
        uint32_t rr=s3d_lerp_channel((ca>>16)&255u,(cb>>16)&255u,(cc>>16)&255u,w0,w1,w2,area);
        uint32_t gg=s3d_lerp_channel((ca>>8)&255u,(cb>>8)&255u,(cc>>8)&255u,w0,w1,w2,area);
        uint32_t bb=s3d_lerp_channel(ca&255u,cb&255u,cc&255u,w0,w1,w2,area);
        ctx->depth[zi]=(uint32_t)depth;
        ctx->color[ci]=(aa<<24)|(rr<<16)|(gg<<8)|bb;
        drawn++;
    }
    return (int)drawn;
}
static uint64_t s3d_syscall3(uint64_t n,uint64_t a,uint64_t b,uint64_t c) {
    uint64_t r;
    __asm__ volatile(
        "mov %[n], %%rax\nmov %[a], %%rdi\nmov %[b], %%rsi\nmov %[c], %%rdx\nint $0x80\nmov %%rax, %[r]"
        : [r]"=r"(r) : [n]"r"(n),[a]"r"(a),[b]"r"(b),[c]"r"(c)
        : "rax","rdi","rsi","rdx","memory","cc");
    return r;
}
int S3DPresent(S3D_CONTEXT *ctx) {
    if(!ctx||!ctx->color||!ctx->width||!ctx->height)return 0;
    struct {uint32_t x,y,w,h;} rect={0,0,ctx->width,ctx->height};
    if(ctx->backend==1)return 1;
    return s3d_syscall3(33,(uint64_t)&rect,0,0)!=0;
}

int S3DSubmitVirGL(const void *commands,uint32_t length) {
    if(!commands||!length||length>S3D_VIRGL_MAX_COMMAND_BYTES)return 0;
    return s3d_syscall3(34,(uint64_t)commands,(uint64_t)length,0)!=0;
}

int S3DPresentVirGL(void) {
    return s3d_syscall3(35,0,0,0)!=0;
}

static uint32_t s3d_u8_to_float_bits(uint32_t c) {
    if(!c)return 0;
    uint64_t q=((uint64_t)c<<31)/255u;
    int exponent=0;
    while(q<(1ULL<<31)){q<<=1;exponent--;}
    uint32_t mantissa=(uint32_t)((q-(1ULL<<31))>>8);
    if(mantissa>0x7FFFFFu)mantissa=0x7FFFFFu;
    return ((uint32_t)(127+exponent)<<23)|mantissa;
}
int S3DClearVirGL(uint32_t argb) {
    /* VirGL context resource 2 is the attached B8G8R8X8 render target. */
    uint32_t stream[] = {
        (1u|(8u<<8)|(5u<<16)), 1u, S3D_VIRGL_RENDER_TARGET_ID, 2u, 0u, 0u,
        (2u|(8u<<8)|(1u<<16)), 1u,
        (4u|(3u<<16)), 1u, 0u, 1u,
        (6u|(8u<<16)), 4u,
        s3d_u8_to_float_bits((argb>>16)&255u),
        s3d_u8_to_float_bits((argb>>8)&255u),
        s3d_u8_to_float_bits(argb&255u),
        s3d_u8_to_float_bits((argb>>24)&255u),
        0u, 0x3FF00000u, 0u,
        (3u|(8u<<8)|(1u<<16)), 1u
    };
    if(!S3DSubmitVirGL(stream,sizeof(stream)))return 0;
    return S3DPresentVirGL();
}
