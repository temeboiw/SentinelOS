#include "ntdll_user.h"
#include "font8x8.h"

typedef struct { int type, x, y, btn; char key; } InputEvent;
typedef struct { u64 va; u32 w, h, pitch; } LogonFB;

static u32 mix(u32 a,u32 b,u32 t) {
    u32 r=(((a>>16)&255)*(255-t)+((b>>16)&255)*t)/255;
    u32 g=(((a>>8)&255)*(255-t)+((b>>8)&255)*t)/255;
    u32 bl=((a&255)*(255-t)+(b&255)*t)/255;
    return 0xFF000000u|(r<<16)|(g<<8)|bl;
}
static void rect(u32 *fb,u32 pitch,u32 w,u32 h,int x,int y,int rw,int rh,u32 color) {
    int x1=x+rw,y1=y+rh;
    if(x<0)x=0;if(y<0)y=0;
    if(x1>(int)w)x1=(int)w;if(y1>(int)h)y1=(int)h;
    if(x1<=x||y1<=y)return;
    for(int yy=y;yy<y1;yy++)for(int xx=x;xx<x1;xx++)fb[(u64)yy*pitch+(u32)xx]=color;
}
static void text(u32 *fb,u32 pitch,u32 w,u32 h,int x,int y,const char *s,u32 color,int scale) {
    for(;*s;s++,x+=8*scale) {
        unsigned char ch=(unsigned char)*s;
        if(ch>=128)ch='?';
        const unsigned char *g=font8x8_basic[ch];
        for(int row=0;row<8;row++)for(int col=0;col<8;col++)
            if(g[row]&(1u<<col))rect(fb,pitch,w,h,x+col*scale,y+row*scale,scale,scale,color);
    }
}
/* Fast row-major wallpaper. Curved shoreline thresholds are computed once per column,
 * avoiding integer division and quadratic math in the per-pixel hot loop. */
static void wallpaper(u32 *fb,u32 pitch,u32 w,u32 h) {
    enum { MAX_WALLPAPER_WIDTH = 4096 };
    static u32 wave1[MAX_WALLPAPER_WIDTH];
    static u32 wave2[MAX_WALLPAPER_WIDTH];
    int base=(int)(h*2/3);
    if(w>MAX_WALLPAPER_WIDTH) {
        for(u32 y=0;y<h;y++) {
            u32 c=mix(0xFFDDF7FC,0xFF3C9DC5,(u32)((u64)y*255/(h>1?h-1:1)));
            u32 *row=fb+(u64)y*pitch;
            for(u32 x=0;x<w;x++)row[x]=c;
        }
        return;
    }
    for(u32 x=0;x<w;x++) {
        int dx=(int)x-(int)w/2;
        wave1[x]=(u32)(base+(int)((long long)dx*dx/(w>0?w*14:1)));
        wave2[x]=(u32)(base+28+(int)((long long)dx*dx/(w>0?w*19:1)));
    }
    for(u32 y=0;y<h;y++) {
        u32 sky=mix(0xFFDDF7FC,0xFF3C9DC5,(u32)((u64)y*255/(h>1?h-1:1)));
        u32 land=mix(0xFF80D5E9,0xFF2B8CB9,(u32)((u64)y*100/(h?h:1)));
        u32 *row=fb+(u64)y*pitch;
        for(u32 x=0;x<w;x++) {
            u32 c=(y>=wave1[x])?land:sky;
            if(y>=wave2[x])c=mix(c,0xFF2E9DC2,48);
            row[x]=c;
        }
    }
    rect(fb,pitch,w,h,0,0,(int)w,2,0xFFFFFFFF);
}
static void paint(u32 *fb,LogonFB *f) {
    wallpaper(fb,f->pitch,f->w,f->h);
    int pw=(int)f->w-80;if(pw>620)pw=620;if(pw<300)pw=(int)f->w-16;
    int ph=278;if(ph>(int)f->h-24)ph=(int)f->h-24;
    int x=((int)f->w-pw)/2,y=((int)f->h-ph)/2;
    rect(fb,f->pitch,f->w,f->h,x+9,y+11,pw,ph,0x50304455);
    rect(fb,f->pitch,f->w,f->h,x,y,pw,ph,0xFFEAF8FC);
    rect(fb,f->pitch,f->w,f->h,x,y,pw,42,0xFF246F91);
    rect(fb,f->pitch,f->w,f->h,x,y,pw,2,0xFFB8F4FC);
    text(fb,f->pitch,f->w,f->h,x+18,y+14,"SENTINELOS",0xFFFFFFFF,2);
    text(fb,f->pitch,f->w,f->h,x+26,y+63,"WELCOME BACK",0xFF246F91,2);
    /* Glossy user tile, styled to match the Aero desktop. */
    rect(fb,f->pitch,f->w,f->h,x+28,y+106,72,72,0xFF246F91);
    rect(fb,f->pitch,f->w,f->h,x+32,y+110,64,64,0xFF8BD5E9);
    rect(fb,f->pitch,f->w,f->h,x+42,y+120,44,44,0xFF246F91);
    text(fb,f->pitch,f->w,f->h,x+51,y+138,"A",0xFFFFFFFF,2);
    text(fb,f->pitch,f->w,f->h,x+120,y+112,"ADMINISTRATOR",0xFF173B52,2);
    text(fb,f->pitch,f->w,f->h,x+120,y+148,"PRESS ENTER TO SIGN IN",0xFF246F91,1);
    rect(fb,f->pitch,f->w,f->h,x+120,y+176,176,25,0xFF246F91);
    rect(fb,f->pitch,f->w,f->h,x+121,y+177,174,23,0xFF3C9DC5);
    text(fb,f->pitch,f->w,f->h,x+158,y+185,"SIGN IN",0xFFFFFFFF,1);
    text(fb,f->pitch,f->w,f->h,x+20,y+ph-22,"SENTLOGON SESSION MANAGER",0xFF5D8295,1);
}
static const char cursor_shape[16][17] = {
 "#...............",
 "##..............",
 "#w+#............",
 "#ww#............",
 "#www#...........",
 "#wwww#..........",
 "#wwwww#.........",
 "#wwwwww#........",
 "#wwwwwww#.......",
 "#wwwwwwww#......",
 "#wwww#####......",
 "#ww#ww#.........",
 "#w#.#ww#........",
 "##...#ww#.......",
 "#.....#ww#......",
 "......##........"
};
static u32 cursor_save[16*16];
static int cursor_x,cursor_y,cursor_saved;
static void cursor_restore(u32 *fb,u32 pitch,u32 w,u32 h) {
    if(!cursor_saved)return;
    for(int y=0;y<16;y++)for(int x=0;x<16;x++) {
        int px=cursor_x+x,py=cursor_y+y;
        if(px>=0&&py>=0&&px<(int)w&&py<(int)h)fb[(u64)py*pitch+(u32)px]=cursor_save[y*16+x];
    }
    cursor_saved=0;
}
static void cursor_draw(u32 *fb,u32 pitch,u32 w,u32 h,int x,int y) {
    cursor_x=x;cursor_y=y;
    for(int yy=0;yy<16;yy++)for(int xx=0;xx<16;xx++) {
        int px=x+xx,py=y+yy;
        if(px>=0&&py>=0&&px<(int)w&&py<(int)h)cursor_save[yy*16+xx]=fb[(u64)py*pitch+(u32)px];
        else cursor_save[yy*16+xx]=0;
    }
    cursor_saved=1;
    for(int yy=0;yy<16;yy++)for(int xx=0;xx<16;xx++) {
        int px=x+xx,py=y+yy; char p=cursor_shape[yy][xx];
        if(px<0||py<0||px>=(int)w||py>=(int)h)continue;
        if(p=='#')fb[(u64)py*pitch+(u32)px]=0xFF000000;
        else if(p=='w')fb[(u64)py*pitch+(u32)px]=0xFFFFFFFF;
        else if(p=='+')fb[(u64)py*pitch+(u32)px]=0xFF8DEBFA;
    }
}
void sentlogon_main(void) {
    /* Each process has its own address space. Initialize graphics here so the
       kernel maps the VirtIO-GPU backing surface into sentlogon.exe too; the
       firmware framebuffer returned by NtMapFramebuffer is not the VirtIO
       scanout surface and flushing it cannot update the active display. */
    GfxBootInfo gfx={0};
    u64 gfx_ok=NtInitializeGraphics(&gfx);
    LogonFB fbinfo={0};
    if(NtMapFramebuffer((FBInfo *)&fbinfo) && gfx_ok &&
       gfx.backend==2 && gfx.surface_va) {
        fbinfo.va=gfx.surface_va;
        fbinfo.w=gfx.width;
        fbinfo.h=gfx.height;
        fbinfo.pitch=gfx.pitch;
    }
    if(!fbinfo.va||fbinfo.w<320||fbinfo.h<200) {
        NtWriteConsole("sentlogon: framebuffer unavailable; Enter to continue\n");
        for(;;) {
            InputEvent ev={0};
            if(sy3(SYS_GetInput,(u64)&ev,0,0)&&ev.type==1&&(ev.key=='\r'||ev.key=='\n'))break;
            (void)sy3(SYS_Yield,0,0,0);
        }
    } else {
        fbinfo.pitch/=4;
        u32 *pixels=(u32 *)(u64)fbinfo.va;
        paint(pixels,&fbinfo);
        int mx=(int)fbinfo.w/2,my=(int)fbinfo.h/2;
        cursor_draw(pixels,fbinfo.pitch,fbinfo.w,fbinfo.h,mx,my);
        /* VirtIO-GPU uses a backing surface: CPU writes are not visible until
         * the dirty rectangle is submitted to the device. */
        (void)NtFlushGraphics(0,0,fbinfo.w,fbinfo.h);
        for(;;) {
            InputEvent ev={0};
            if(sy3(SYS_GetInput,(u64)&ev,0,0)) {
                if(ev.type==1&&(ev.key=='\r'||ev.key=='\n'))break;
                if(ev.type==2||ev.type==3) {
                    cursor_restore(pixels,fbinfo.pitch,fbinfo.w,fbinfo.h);
                    mx=ev.x;my=ev.y;
                    cursor_draw(pixels,fbinfo.pitch,fbinfo.w,fbinfo.h,mx,my);
                    (void)NtFlushGraphics(0,0,fbinfo.w,fbinfo.h);
                    if(ev.type==3) {
                        int pw=(int)fbinfo.w-80;if(pw>620)pw=620;if(pw<300)pw=(int)fbinfo.w-16;
                        int ph=278;if(ph>(int)fbinfo.h-24)ph=(int)fbinfo.h-24;
                        int x=((int)fbinfo.w-pw)/2,y=((int)fbinfo.h-ph)/2;
                        if(mx>=x+120&&mx<x+300&&my>=y+176&&my<y+204)break;
                    }
                }
            }
            (void)sy3(SYS_Yield,0,0,0);
        }
        cursor_restore(pixels,fbinfo.pitch,fbinfo.w,fbinfo.h);
        (void)NtFlushGraphics(0,0,fbinfo.w,fbinfo.h);
    }
    NtWriteConsole("sentlogon: interactive session accepted\n");
    char name[]={'e','x','p','l','o','r','e','r','.','e','x','e',0};
    if((u64)NtCreateProcess(name)==0)NtWriteConsole("sentlogon: explorer launch failed\n");
    for(;;)(void)sy3(SYS_Yield,0,0,0);
}
