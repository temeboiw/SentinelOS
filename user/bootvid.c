#include "ntdll_user.h"

typedef unsigned short u16;

#define LOGO_BMP_CAP 786486u
#define LOGO_DRAW_SIZE 320
static unsigned char logo_bmp[LOGO_BMP_CAP];

static u16 le16(const unsigned char *p) { return (u16)p[0] | ((u16)p[1] << 8); }
static u32 le32(const unsigned char *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static void rect(u32 *f,u32 p,u32 w,u32 h,int x,int y,int rw,int rh,u32 c) {
    int x1=x+rw,y1=y+rh;
    if(x<0)x=0; if(y<0)y=0;
    if(x1>(int)w)x1=(int)w; if(y1>(int)h)y1=(int)h;
    for(int j=y;j<y1;j++)for(int i=x;i<x1;i++)f[(u64)j*p+i]=c;
}
static void disk(u32 *f,u32 p,u32 w,u32 h,int cx,int cy,int r,u32 c) {
    for(int y=-r;y<=r;y++)for(int x=-r;x<=r;x++)
        if(x*x+y*y<=r*r)rect(f,p,w,h,cx+x,cy+y,1,1,c);
}
static int load_logo(u32 *fb,u32 pitch,u32 fw,u32 fh) {
    static const char path[]="/magic/logo.bmp";
    u64 handle=sy3(SYS_OpenFile,(u64)path,0,0);
    if(handle==(u64)-1 || handle==0)return 0;
    u64 got=sy3(SYS_ReadFile,handle,(u64)logo_bmp,sizeof(logo_bmp));
    int loaded=0;
    if(got<54 || logo_bmp[0]!='B' || logo_bmp[1]!='M')goto done;
    u32 offset=le32(&logo_bmp[10]),dib=le32(&logo_bmp[14]);
    if(dib<40 || offset>=got)goto done;
    int sw=(int)le32(&logo_bmp[18]),sh=(int)le32(&logo_bmp[22]);
    u16 planes=le16(&logo_bmp[26]),bits=le16(&logo_bmp[28]);
    u32 compression=le32(&logo_bmp[30]);
    if(sw<=0||sh<=0||sw>4096||sh>4096||planes!=1||bits!=24||compression!=0)goto done;
    u32 rowbytes=((u32)sw*3u+3u)&~3u;
    if((u64)offset+(u64)rowbytes*(u32)sh>got)goto done;
    int dw=LOGO_DRAW_SIZE,dh=LOGO_DRAW_SIZE;
    if(dw>(int)fw-16)dw=(int)fw-16;
    if(dh>(int)fh-120)dh=(int)fh-120;
    if(dw<1||dh<1)goto done;
    int left=((int)fw-dw)/2,top=((int)fh-dh)/2-(int)fh/14;
    if(top<0)top=0;
    for(int y=0;y<dh;y++) {
        int sy=(int)((u64)y*(u32)sh/(u32)dh);
        const unsigned char *src=logo_bmp+offset+(u64)(sh-1-sy)*rowbytes;
        for(int x=0;x<dw;x++) {
            int sx=(int)((u64)x*(u32)sw/(u32)dw);
            const unsigned char *q=src+(u64)sx*3u;
            fb[(u64)(top+y)*pitch+left+x]=0xFF000000u|((u32)q[2]<<16)|((u32)q[1]<<8)|q[0];
        }
    }
    loaded=1;
done:
    (void)sy3(22,(u64)handle,0,0);
    return loaded;
}
static void draw_spinner(u32 *fb,u32 pitch,u32 w,u32 h,u32 frame) {
    static const int dx[8]={0,17,24,17,0,-17,-24,-17};
    static const int dy[8]={-24,-17,0,17,24,17,0,-17};
    int cx=(int)w/2,cy=(int)h/2+(int)h/4;
    if(cy>(int)h-24)cy=(int)h-24;
    for(int i=0;i<8;i++) {
        int age=(i-(int)(frame%8)+8)%8;
        u32 c=age==0?0xFFFFFFFFu:age==1?0xFFE5E5E5u:age==2?0xFFBDBDBDu:
              age==3?0xFF969696u:age==4?0xFF707070u:age==5?0xFF505050u:
              age==6?0xFF353535u:0xFF222222u;
        disk(fb,pitch,w,h,cx+dx[i],cy+dy[i],3,c);
    }
}
void bootvid_main(void) {
    GfxBootInfo gfx={0};
    u64 gfx_ok=NtInitializeGraphics(&gfx);
    FBInfo f={0};
    if(NtMapFramebuffer(&f)&&f.va&&f.w>=320&&f.h>=200&&
       f.w<=UINT32_MAX/4u&&f.pitch%4u==0&&f.pitch>=f.w*4u) {
        if(gfx_ok&&gfx.backend==2&&gfx.surface_va) {
            f.va=gfx.surface_va;f.w=gfx.width;f.h=gfx.height;f.pitch=gfx.pitch;
        }
        if(!f.va||f.w<320||f.h<200||f.w>UINT32_MAX/4u||
           f.pitch%4u||f.pitch<f.w*4u) {
            f.va=0;
        }
    }
    if(f.va) {
        f.pitch/=4;
        u32 *fb=(u32 *)(u64)f.va;
        for(u32 y=0;y<f.h;y++) {
            u32 *row=fb+(u64)y*f.pitch;
            for(u32 x=0;x<f.w;x++)row[x]=0xFF000000u;
        }
        (void)load_logo(fb,f.pitch,f.w,f.h);
        (void)NtFlushGraphics(0,0,f.w,f.h);
        u64 start_tick=sy3(24,0,0,0);
        for(u32 frame=0;;frame++) {
            draw_spinner(fb,f.pitch,f.w,f.h,frame);
            (void)NtFlushGraphics(0,0,f.w,f.h);
            (void)sy3(SYS_Yield,0,0,0);
            int cx=(int)f.w/2,cy=(int)f.h/2+(int)f.h/4;
            if(cy>(int)f.h-24)cy=(int)f.h-24;
            rect(fb,f.pitch,f.w,f.h,cx-32,cy-32,64,64,0xFF000000u);
            u64 now=sy3(24,0,0,0);
            if(start_tick!=(u64)-1&&now!=(u64)-1) {
                if(now>=start_tick&&now-start_tick>=2500)break;
            } else if(frame>=179)break;
        }
        for(u32 y=0;y<f.h;y++) {
            u32 *row=fb+(u64)y*f.pitch;
            for(u32 x=0;x<f.w;x++)row[x]=0xFF000000u;
        }
        (void)NtFlushGraphics(0,0,f.w,f.h);
    }
    NtWriteConsole("bootvid: splash complete; launching sentlogon\n");
    char name[]={'s','e','n','t','l','o','g','o','n','.','e','x','e',0};
    NTSTATUS status=NtCreateProcess(name);
    if((u64)status==(u64)-1) {
        NtWriteConsole("bootvid: ERROR creating sentlogon.exe\n");
        for(;;)(void)sy3(SYS_Yield,0,0,0);
    }
    NtWriteConsole("bootvid: sentlogon.exe created\n");
    NtExit(0);
    for(;;)(void)sy3(SYS_Yield,0,0,0);
}
