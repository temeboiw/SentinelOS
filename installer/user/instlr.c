#include "ntdll_user.h"
#include "font8x8.h"

typedef struct { int type,x,y,btn; char key; } InputEvent;
typedef struct { u64 va; u32 w,h,pitch; } IFB;
static void rect(u32*f,u32 p,u32 w,u32 h,int x,int y,int rw,int rh,u32 c){int x2=x+rw,y2=y+rh;if(x<0)x=0;if(y<0)y=0;if(x2>(int)w)x2=w;if(y2>(int)h)y2=h;if(x>=x2||y>=y2)return;for(int yy=y;yy<y2;yy++)for(int xx=x;xx<x2;xx++)f[(u64)yy*p+xx]=c;}
static void text(u32*f,u32 p,u32 w,u32 h,int x,int y,const char*s,u32 c,int z){for(;*s;s++,x+=8*z){unsigned char ch=(unsigned char)*s;if(ch>=128)ch='?';const unsigned char*g=font8x8_basic[ch];for(int j=0;j<8;j++)for(int i=0;i<8;i++)if(g[j]&(1u<<i))rect(f,p,w,h,x+i*z,y+j*z,z,z,c);}}
static void draw_cursor(u32 *f, const IFB *b, int mx, int my) {
 static const char shape[16][17] = {
  "*...............","**..............","*+*.............","*++*............",
  "*+++*...........","*++++*..........","*+++++*.........","*++++++*........",
  "*++++***........","*++*+*..........","*+*.*+*.........","**..*+*..........",
  "....*+*..........",".....*+*.........",".....***.........","................"
 };
 for(int y=0;y<16;y++) for(int x=0;x<16;x++) {
  int px=mx+x,py=my+y;
  if(px<0||py<0||px>=(int)b->w||py>=(int)b->h) continue;
  char ch=shape[y][x];
  if(ch=='*') f[(u64)py*b->pitch+px]=0xFF10212A;
  else if(ch=='+') f[(u64)py*b->pitch+px]=0xFFFFFFFF;
 }
}
static void paint(u32*f,IFB*b,int page,int accepted,int checked){
 u32 w=b->w,h=b->h,p=b->pitch;
 for(u32 y=0;y<h;y++){u32 c=0xFFEDF7FA-(y%16);for(u32 x=0;x<w;x++)f[(u64)y*p+x]=c;}
 rect(f,p,w,h,0,0,w,52,0xFF176B8C);rect(f,p,w,h,0,0,w,3,0xFFB6F4FC);
 text(f,p,w,h,18,15,"SentinelOS V.4 [Beta]",0xFFFFFFFF,2);
 int x=32,y=78,pw=(int)w-64,ph=(int)h-112;if(pw>900)pw=900;if(ph>600)ph=600;if(pw<250)pw=(int)w-24;if(ph<120)ph=(int)h-70;x=((int)w-pw)/2;y=((int)h-ph)/2+14;
 rect(f,p,w,h,x+5,y+7,pw,ph,0xFF9BB0B9);rect(f,p,w,h,x,y,pw,ph,0xFFFFFFFF);rect(f,p,w,h,x,y,pw,42,0xFFD9EEF5);
 text(f,p,w,h,x+20,y+14,page==0?"Welcome to SentinelOS Setup":"Choose installation target",0xFF174C63,2);
 if(page==0){
  text(f,p,w,h,x+24,y+76,"Please review these terms before continuing.",0xFF203944,1);
  text(f,p,w,h,x+24,y+112,"SentinelOS is provided as beta software.",0xFF203944,1);
  text(f,p,w,h,x+24,y+132,"Installing can erase data on the selected disk.",0xFF203944,1);
  text(f,p,w,h,x+24,y+152,"Back up important files before installation.",0xFF203944,1);
  rect(f,p,w,h,x+24,y+ph-94,24,24,checked?0xFF147BA2:0xFFFFFFFF);rect(f,p,w,h,x+24,y+ph-94,24,2,0xFF4D7787);
  if(checked){text(f,p,w,h,x+28,y+ph-90,"X",0xFFFFFFFF,1);}
  text(f,p,w,h,x+58,y+ph-91,"I WANT TO INSTALL THE OS",0xFF203944,1);
  rect(f,p,w,h,x+pw-142,y+ph-56,118,34,accepted?0xFF176B8C:0xFF9CAEB5);
  text(f,p,w,h,x+pw-110,y+ph-45,"Next",0xFFFFFFFF,1);
 }else{
  text(f,p,w,h,x+24,y+78,"Disk discovery is not yet connected to the kernel.",0xFF9B3B24,1);
  text(f,p,w,h,x+24,y+102,"No disks are listed; installation and formatting are disabled.",0xFF203944,1);
  text(f,p,w,h,x+24,y+126,"This prevents accidental writes to an unverified device.",0xFF203944,1);
  rect(f,p,w,h,x+24,y+162,pw-48,42,0xFFE3E9EC);
  text(f,p,w,h,x+38,y+178,"Target disk: unavailable",0xFF566B73,1);
  rect(f,p,w,h,x+pw-142,y+ph-56,118,34,0xFF9CAEB5);
  text(f,p,w,h,x+pw-124,y+ph-45,"Next",0xFFFFFFFF,1);
  rect(f,p,w,h,x+24,y+ph-56,118,34,0xFF176B8C);
  text(f,p,w,h,x+57,y+ph-45,"Back",0xFFFFFFFF,1);
 }
 text(f,p,w,h,16,(int)h-24,"SentinelOS Setup",0xFF466571,1);
}
void instlr_main(void){
 NtWriteConsole("instlr: installer session starting\n");
 GfxBootInfo gfx={0};u64 gok=NtInitializeGraphics(&gfx);FBInfo f={0};(void)NtMapFramebuffer(&f);
 if(gok&&gfx.backend==2&&gfx.surface_va){f.va=gfx.surface_va;f.w=gfx.width;f.h=gfx.height;f.pitch=gfx.pitch;}
 if(!f.va||f.w<320||f.h<200){NtWriteConsole("instlr: framebuffer unavailable; installer cannot continue\n");for(;;)(void)sy3(SYS_Yield,0,0,0);}
 f.pitch/=4;IFB b={f.va,f.w,f.h,f.pitch};u32*fb=(u32*)(u64)f.va;int page=0,checked=0,accepted=0;
 int mouse_x=(int)f.w/2,mouse_y=(int)f.h/2;
 paint(fb,&b,page,accepted,checked);draw_cursor(fb,&b,mouse_x,mouse_y);(void)NtFlushGraphics(0,0,f.w,f.h);
 for(;;){InputEvent ev={0};if(sy3(SYS_GetInput,(u64)&ev,0,0)){
  if(ev.type==2||ev.type==3){mouse_x=ev.x;mouse_y=ev.y;}
  if(ev.type==1&&(ev.key=='\r'||ev.key=='\n')){if(page==0&&checked){page=1;NtWriteConsole("instlr: terms accepted; awaiting disk enumeration support\n");}}
  if(ev.type==3){int pw=(int)f.w-64;if(pw>900)pw=900;if(pw<250)pw=(int)f.w-24;int ph=(int)f.h-112;if(ph>600)ph=600;if(ph<120)ph=(int)f.h-70;int x=((int)f.w-pw)/2,y=((int)f.h-ph)/2+14;
   if(page==0){if(ev.x>=x+24&&ev.x<x+300&&ev.y>=y+ph-100&&ev.y<y+ph-64){checked=!checked;accepted=checked;}if(checked&&ev.x>=x+pw-150&&ev.x<x+pw-18&&ev.y>=y+ph-62&&ev.y<y+ph-18){page=1;}}
   else if(ev.x>=x+24&&ev.x<x+150&&ev.y>=y+ph-62&&ev.y<y+ph-18){page=0;}
  }
  paint(fb,&b,page,accepted,checked);draw_cursor(fb,&b,mouse_x,mouse_y);(void)NtFlushGraphics(0,0,f.w,f.h);
 }(void)sy3(SYS_Yield,0,0,0);}
}
