#include "ntdll_user.h"
#include "font8x8.h"

typedef struct { int type, x, y, btn; char key; } InputEvent;
typedef struct { u64 va; u32 w, h, pitch; } LogonFB;

/* font_ttf.h shares this tiny rendering target contract with USER32. */
static u32 *g_front;
static u32 g_fw, g_fh, g_pitch;
#include "font_ttf.h"

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
    if(!s)return;
    for(;*s;s++) {
        unsigned char ch=(unsigned char)*s;
        if(u32_ttf_font.valid) {
            unsigned int px=scale<=1?14u:(scale==2?20u:40u);
            int adv=u32_ttf_draw(&u32_ttf_font,x,y+(int)px,ch,px,color);
            if(adv>=0) { x+=adv>0?adv:1; continue; }
        }
        if(ch>=128)ch='?';
        const unsigned char *g=font8x8_basic[ch];
        for(int row=0;row<8;row++)for(int col=0;col<8;col++)
            if(g[row]&(1u<<col))rect(fb,pitch,w,h,x+col*scale,y+row*scale,scale,scale,color);
        x+=8*scale;
    }
}
/* Fast row-major wallpaper. Curved shoreline thresholds are computed once per column,
 * avoiding integer division and quadratic math in the per-pixel hot loop. */
static void blend_pixel(u32 *fb,u32 pitch,u32 w,u32 h,int x,int y,u32 color,unsigned int alpha) {
    if(!alpha||x<0||y<0||x>=(int)w||y>=(int)h)return;
    if(alpha>=255u){fb[(u64)y*pitch+(u32)x]=color;return;}
    u32 *p=&fb[(u64)y*pitch+(u32)x], old=*p;
    unsigned int r=(((color>>16)&255u)*alpha+((old>>16)&255u)*(255u-alpha))/255u;
    unsigned int g=(((color>>8)&255u)*alpha+((old>>8)&255u)*(255u-alpha))/255u;
    unsigned int b=((color&255u)*alpha+(old&255u)*(255u-alpha))/255u;
    *p=0xFF000000u|(r<<16)|(g<<8)|b;
}
/* True circular corners with 4x4 sub-pixel coverage, not diagonal bevels. */
static void round_rect(u32 *fb,u32 pitch,u32 w,u32 h,int x,int y,int rw,int rh,int radius,u32 color) {
    if(rw<=0||rh<=0)return;
    if(radius>rw/2)radius=rw/2;if(radius>rh/2)radius=rh/2;if(radius<0)radius=0;
    if(radius==0){rect(fb,pitch,w,h,x,y,rw,rh,color);return;}
    for(int yy=0;yy<rh;yy++) {
        int corner=(yy<radius||yy>=rh-radius);
        if(!corner){rect(fb,pitch,w,h,x,y+yy,rw,1,color);continue;}
        int cy=yy<radius?radius:rh-radius;
        rect(fb,pitch,w,h,x+radius,y+yy,rw-2*radius,1,color);
        for(int side=0;side<2;side++)for(int xx=0;xx<radius;xx++) {
            int px=side?x+rw-radius+xx:x+xx;
            int cx=side?rw-radius:radius;
            unsigned int hits=0;
            for(int sy=0;sy<4;sy++)for(int sx=0;sx<4;sx++) {
                int dx8=(px-x)*8+sx*2+1-cx*8;
                int dy8=yy*8+sy*2+1-cy*8;
                if((long long)dx8*dx8+(long long)dy8*dy8<=(long long)radius*radius*64)hits++;
            }
            if(hits)blend_pixel(fb,pitch,w,h,px,y+yy,color,hits*255u/16u);
        }
    }
}
static void wallpaper(u32 *fb,u32 pitch,u32 w,u32 h) {
    for(u32 y=0;y<h;y++) {
        u32 t=(u32)((u64)y*95/(h>1?h-1:1));
        u32 c=mix(0xFF202630,0xFF0B0D11,t);
        u32 *row=fb+(u64)y*pitch;
        for(u32 x=0;x<w;x++) {
            int dx=(int)x-(int)w/2, dy=(int)y-(int)h/3;
            u32 px=c;
            int glow=dx<0?-dx:dx;
            if(glow<(int)w/3 && dy>-((int)h/3) && dy<(int)h/2) {
                u32 strength=(u32)(72-(glow*72/((int)w/3+1)));
                if(strength>0) px=mix(c,0xFF35191D,strength);
            }
            if((x%48u)==0 || (y%48u)==0) px=mix(px,0xFF343B46,44);
            if((y%4u)==0) px=mix(px,0xFF050609,36);
            row[x]=px;
        }
    }
    rect(fb,pitch,w,h,28,28,42,3,0xFFFF3B30);
    rect(fb,pitch,w,h,28,31,3,26,0xFFFF3B30);
    if(w>80) {
        rect(fb,pitch,w,h,(int)w-70,28,42,3,0xFFFF3B30);
        rect(fb,pitch,w,h,(int)w-31,28,3,26,0xFFFF3B30);
    }
}
static void paint(u32 *fb,LogonFB *f) {
    wallpaper(fb,f->pitch,f->w,f->h);
    int pw=(int)f->w-80;if(pw>720)pw=720;if(pw<300)pw=(int)f->w-16;
    int ph=340;if(ph>(int)f->h-24)ph=(int)f->h-24;
    int x=((int)f->w-pw)/2,y=((int)f->h-ph)/2;
    round_rect(fb,f->pitch,f->w,f->h,x+10,y+12,pw,ph,18,0xFF050609);
    round_rect(fb,f->pitch,f->w,f->h,x-2,y-2,pw+4,ph+4,18,0xFFFF3B30);
    round_rect(fb,f->pitch,f->w,f->h,x,y,pw,ph,16,0xFF171B22);
    round_rect(fb,f->pitch,f->w,f->h,x+2,y+2,pw-4,ph-4,14,0xFF1D222A);
    round_rect(fb,f->pitch,f->w,f->h,x,y,pw,48,15,0xFF101318);
    rect(fb,f->pitch,f->w,f->h,x,y+15,pw,33,0xFF101318);
    round_rect(fb,f->pitch,f->w,f->h,x,y,pw,3,1,0xFFFF3B30);
    rect(fb,f->pitch,f->w,f->h,x+18,y+15,18,18,0xFFFF3B30);
    rect(fb,f->pitch,f->w,f->h,x+22,y+19,10,10,0xFF171B22);
    text(fb,f->pitch,f->w,f->h,x+46,y+15,"SENTINEL",0xFFF1F3F6,2);
    text(fb,f->pitch,f->w,f->h,x+pw-160,y+19,"SECURE SESSION",0xFF9AA3B1,1);
    rect(fb,f->pitch,f->w,f->h,x,y+48,pw,1,0xFF3A414C);
    int rail=pw/3;
    round_rect(fb,f->pitch,f->w,f->h,x+1,y+49,rail,ph-50,12,0xFF12151A);
    rect(fb,f->pitch,f->w,f->h,x+rail-12,y+49,12,ph-50,0xFF12151A);
    rect(fb,f->pitch,f->w,f->h,x+rail,y+49,1,ph-50,0xFF3A414C);
    text(fb,f->pitch,f->w,f->h,x+22,y+76,"SYSTEM ACCESS",0xFFFF3B30,1);
    text(fb,f->pitch,f->w,f->h,x+22,y+111,"S",0xFFF1F3F6,5);
    rect(fb,f->pitch,f->w,f->h,x+22,y+169,104,4,0xFFFF3B30);
    text(fb,f->pitch,f->w,f->h,x+22,y+190,"BUILT FROM ZERO",0xFFD9DEE6,1);
    text(fb,f->pitch,f->w,f->h,x+22,y+210,"SENTINELOS",0xFF8B94A3,1);
    for(int i=0;i<rail-30;i+=18) {
        rect(fb,f->pitch,f->w,f->h,x+18+i,y+ph-45,9,8,0xFFFF3B30);
        rect(fb,f->pitch,f->w,f->h,x+27+i,y+ph-45,9,8,0xFF2B323D);
    }
    int rx=x+rail+24;
    text(fb,f->pitch,f->w,f->h,rx,y+77,"WELCOME BACK",0xFFF1F3F6,2);
    rect(fb,f->pitch,f->w,f->h,rx,y+102,140,2,0xFFFF3B30);
    text(fb,f->pitch,f->w,f->h,rx,y+124,"SELECT ACCOUNT",0xFF8B94A3,1);
    round_rect(fb,f->pitch,f->w,f->h,rx,y+148,54,54,12,0xFFFF3B30);
    round_rect(fb,f->pitch,f->w,f->h,rx+3,y+151,48,48,10,0xFF2B323D);
    text(fb,f->pitch,f->w,f->h,rx+17,y+168,"A",0xFFF1F3F6,2);
    text(fb,f->pitch,f->w,f->h,rx+70,y+153,"ADMINISTRATOR",0xFFF1F3F6,1);
    text(fb,f->pitch,f->w,f->h,rx+70,y+174,"LOCAL PROFILE",0xFF8B94A3,1);
    int bx=x+pw-218,by=y+232,bw=190,bh=38;
    round_rect(fb,f->pitch,f->w,f->h,bx+5,by+5,bw,bh,10,0xFF080A0D);
    round_rect(fb,f->pitch,f->w,f->h,bx,by,bw,bh,10,0xFFFF3B30);
    round_rect(fb,f->pitch,f->w,f->h,bx+3,by+3,bw-6,bh-6,8,0xFFDB3028);
    text(fb,f->pitch,f->w,f->h,bx+26,by+14,"SIGN IN",0xFFFFFFFF,1);
    rect(fb,f->pitch,f->w,f->h,bx+bw-28,by+12,12,12,0xFFFFFFFF);
    rect(fb,f->pitch,f->w,f->h,bx+bw-24,by+16,8,4,0xFFDB3028);
    text(fb,f->pitch,f->w,f->h,rx,y+292,"ENTER / CLICK TO CONTINUE",0xFF8B94A3,1);
    rect(fb,f->pitch,f->w,f->h,x,y+ph-18,pw,18,0xFF101318);
    rect(fb,f->pitch,f->w,f->h,x,y+ph-18,pw,1,0xFF3A414C);
    rect(fb,f->pitch,f->w,f->h,x+12,y+ph-11,5,5,0xFF3DDC84);
    text(fb,f->pitch,f->w,f->h,x+24,y+ph-13,"SESSION READY",0xFF9AA3B1,1);
    text(fb,f->pitch,f->w,f->h,x+pw-112,y+ph-13,"BETA / V5",0xFF8B94A3,1);
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
    (void)NtMapFramebuffer((FBInfo *)&fbinfo);
    if(gfx_ok && gfx.backend==2 && gfx.surface_va) {
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
        g_front=pixels;g_fw=fbinfo.w;g_fh=fbinfo.h;g_pitch=fbinfo.pitch;
        u32_ttf_load();
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
                        int pw=(int)fbinfo.w-80;if(pw>720)pw=720;if(pw<300)pw=(int)fbinfo.w-16;
                        int ph=340;if(ph>(int)fbinfo.h-24)ph=(int)fbinfo.h-24;
                        int x=((int)fbinfo.w-pw)/2,y=((int)fbinfo.h-ph)/2;
                        int bx=x+pw-218,by=y+232;
                        if(mx>=bx&&mx<bx+190&&my>=by&&my<by+38)break;
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
    u64 explorer_pid=(u64)NtCreateProcess(name);
    if(explorer_pid==0 || explorer_pid==(u64)-1)
        NtWriteConsole("sentlogon: explorer launch failed\n");
    for(;;)(void)sy3(SYS_Yield,0,0,0);
}
