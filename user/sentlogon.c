#include "ntdll_user.h"

typedef struct { int type, x, y, btn; char key; } InputEvent;
typedef struct { u64 va; u32 w, h, pitch; } LogonFB;

static const unsigned char font[38][7] = {
{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,14},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},{1,1,1,1,17,17,14},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},{17,27,21,17,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{14,17,16,14,1,17,14},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,27,17},{17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31},{14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,6,8,16,31},{31,2,4,2,1,17,14},{2,6,10,18,31,2,2},{31,16,30,1,1,17,14},{6,8,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,2,12},{0,0,0,0,0,0,0},{0,0,4,0,4,0,0}
};
static int glyph(char c) {
 if(c>='A'&&c<='Z') return c-'A';
 if(c>='a'&&c<='z') return c-'a';
 if(c>='0'&&c<='9') return 26+c-'0';
 if(c==':') return 37;
 return 36;
}
static void rect(u32 *fb,u32 pitch,u32 w,u32 h,int x,int y,int rw,int rh,u32 color) {
 if(x<0){rw+=x;x=0;} if(y<0){rh+=y;y=0;}
 if(x+(u32)rw>w)rw=(int)w-x; if(y+(u32)rh>h)rh=(int)h-y;
 if(rw<=0||rh<=0)return;
 for(int yy=y;yy<y+rh;yy++)for(int xx=x;xx<x+rw;xx++)fb[(u64)yy*pitch+xx]=color;
}
static void text(u32 *fb,u32 pitch,u32 w,u32 h,int x,int y,const char *s,u32 color,int scale) {
 for(;*s;s++,x+=6*scale){const unsigned char *g=font[glyph(*s)];for(int r=0;r<7;r++)for(int c=0;c<5;c++)if(g[r]&(16>>c))rect(fb,pitch,w,h,x+c*scale,y+r*scale,scale,scale,color);}
}
static void paint(u32 *fb,LogonFB *f) {
 rect(fb,f->pitch,f->w,f->h,0,0,(int)f->w,(int)f->h,0xFF07152D);
 rect(fb,f->pitch,f->w,f->h,0,0,(int)f->w,8,0xFF31A7E8);
 rect(fb,f->pitch,f->w,f->h,0,(int)f->h-8,(int)f->w,8,0xFF31A7E8);
 int pw=520,ph=260,x=((int)f->w-pw)/2,y=((int)f->h-ph)/2;
 rect(fb,f->pitch,f->w,f->h,x+8,y+10,pw,ph,0xFF030914);
 rect(fb,f->pitch,f->w,f->h,x,y,pw,ph,0xFF123B69);
 rect(fb,f->pitch,f->w,f->h,x+2,y+2,pw-4,ph-4,0xFF0C2445);
 rect(fb,f->pitch,f->w,f->h,x+18,y+18,pw-36,2,0xFF42B9F5);
 text(fb,f->pitch,f->w,f->h,x+30,y+35,"SENTINELOS",0xFFFFFFFF,3);
 text(fb,f->pitch,f->w,f->h,x+32,y+83,"WELCOME BACK",0xFF8ED7FF,2);
 rect(fb,f->pitch,f->w,f->h,x+30,y+125,64,64,0xFF1685C5);
 rect(fb,f->pitch,f->w,f->h,x+42,y+137,40,40,0xFF0B2E52);
 text(fb,f->pitch,f->w,f->h,x+110,y+132,"USER: ADMINISTRATOR",0xFFFFFFFF,2);
 text(fb,f->pitch,f->w,f->h,x+110,y+163,"PRESS ENTER TO SIGN IN",0xFFB7DDF6,1);
 text(fb,f->pitch,f->w,f->h,x+30,y+220,"SENTLOGON SESSION MANAGER",0xFF6EA8D4,1);
}
void sentlogon_main(void) {
 LogonFB fbinfo={0};
 if(!NtMapFramebuffer((FBInfo *)&fbinfo) || !fbinfo.va || fbinfo.w<320 || fbinfo.h<200) {
   NtWriteConsole("sentlogon: framebuffer unavailable; Enter to continue\n");
 } else { fbinfo.pitch /= 4; paint((u32 *)(u64)fbinfo.va,&fbinfo); }
 for(;;) {
   InputEvent ev={0};
   if(sy3(SYS_GetInput,(u64)&ev,0,0) && ev.type==1 && (ev.key=='\r'||ev.key=='\n')) break;
   (void)sy3(SYS_Yield,0,0,0);
 }
 NtWriteConsole("sentlogon: interactive session accepted\n");
 char name[]={'e','x','p','l','o','r','e','r','.','e','x','e',0};
 if((u64)NtCreateProcess(name)==0) NtWriteConsole("sentlogon: explorer launch failed\n");
 for(;;) (void)sy3(SYS_Yield,0,0,0);
}
