#include "ntdll_user.h"
#include "font8x8.h"

static u32 mix(u32 a,u32 b,u32 t){u32 r=(((a>>16)&255)*(255-t)+((b>>16)&255)*t)/255;u32 g=(((a>>8)&255)*(255-t)+((b>>8)&255)*t)/255;u32 z=((a&255)*(255-t)+(b&255)*t)/255;return 0xFF000000u|(r<<16)|(g<<8)|z;}
static void rect(u32*f,u32 p,u32 w,u32 h,int x,int y,int rw,int rh,u32 c){int x1=x+rw,y1=y+rh;if(x<0)x=0;if(y<0)y=0;if(x1>(int)w)x1=w;if(y1>(int)h)y1=h;for(int j=y;j<y1;j++)for(int i=x;i<x1;i++)f[(u64)j*p+i]=c;}
static void txt(u32*f,u32 p,u32 w,u32 h,int x,int y,const char*s,u32 c,int z){for(;*s;s++,x+=8*z){unsigned char q=*s;if(q>=128)q='?';const unsigned char*g=font8x8_basic[q];for(int j=0;j<8;j++)for(int i=0;i<8;i++)if(g[j]&(1u<<i))rect(f,p,w,h,x+i*z,y+j*z,z,z,c);}}
void bootvid_main(void){
 GfxBootInfo gfx={0};
 /* Ask the kernel to discover graphics hardware while bootvid is running.
    Unsupported devices keep the firmware framebuffer active. */
 u64 gfx_ok=NtInitializeGraphics(&gfx);
 FBInfo f={0};
 if(NtMapFramebuffer(&f)&&f.va&&f.w>=320&&f.h>=200&&f.pitch>=f.w*4){
  f.pitch/=4;u32*fb=(u32*)(u64)f.va;
  for(u32 y=0;y<f.h;y++){u32 c=mix(0xFF071725,0xFF147AA4,(u32)((u64)y*255/(f.h>1?f.h-1:1)));u32 *row=fb+(u64)y*f.pitch;for(u32 x=0;x<f.w;x++)row[x]=c;}
  txt(fb,f.pitch,f.w,f.h,(int)f.w/2-40,(int)f.h/2-52,"SENTINELOS",0xFFFFFFFF,2);
  txt(fb,f.pitch,f.w,f.h,(int)f.w/2-60,(int)f.h/2-22,"Starting system",0xFFB7D7E7,1);
  if(gfx_ok && (gfx.capabilities&GFX_CAP_VIRTIO_GPU))
   txt(fb,f.pitch,f.w,f.h,(int)f.w/2-76,(int)f.h/2+62,"VirtIO-GPU 2D active",0xFFB7D7E7,1);
  else
   txt(fb,f.pitch,f.w,f.h,(int)f.w/2-76,(int)f.h/2+62,"Framebuffer graphics active",0xFFB7D7E7,1);
  static const int dx[12]={0,13,23,27,23,13,0,-13,-23,-27,-23,-13},dy[12]={-27,-23,-13,0,13,23,27,23,13,0,-13,-23};
  int cx=f.w/2,cy=f.h/2+22;
  /* SYS_NtGetTickCount is RTC-backed and advances in whole-second steps.
   * Keep the splash visible for about three seconds; never use a zero tick
   * as an "uninitialized" sentinel because zero is a valid boot tick. */
  u64 start_tick=sy3(24,0,0,0);
  for(u32 frame=0;;frame++){
   for(int yy=cy-34;yy<cy+34;yy++){if(yy<0||yy>=(int)f.h)continue;u32 bg=mix(0xFF071725,0xFF147AA4,(u32)((u64)yy*255/(f.h>1?f.h-1:1)));u32 *row=fb+(u64)yy*f.pitch;int x0=cx-34;if(x0<0)x0=0;int x1=cx+34;if(x1>(int)f.w)x1=(int)f.w;for(int xx=x0;xx<x1;xx++)row[xx]=bg;}
   for(int i=0;i<12;i++){int age=(i-(int)(frame%12)+12)%12;u32 c=age==0?0xFFFFFFFF:age<4?0xFF80D8F5:age<7?0xFF397FA3:0xFF20465D;rect(fb,f.pitch,f.w,f.h,cx+dx[i]-3,cy+dy[i]-3,6,6,c);}
   (void)sy3(SYS_Yield,0,0,0);
   u64 now=sy3(24,0,0,0);
   if(start_tick!=(u64)-1&&now!=(u64)-1) {
    if(now>=start_tick&&now-start_tick>=2500)break;
   } else if(frame>=179) {
    break;
   }
  }
  for(u32 y=0;y<f.h;y++){u32 *row=fb+(u64)y*f.pitch;for(u32 x=0;x<f.w;x++)row[x]=0xFF000000;}
 }
 NtWriteConsole("bootvid: splash complete; launching sentlogon\\n");
 char name[]={'s','e','n','t','l','o','g','o','n','.','e','x','e',0};
 NTSTATUS status=NtCreateProcess(name);
 if((u64)status==(u64)-1){NtWriteConsole("bootvid: ERROR creating sentlogon.exe\\n");for(;;)(void)sy3(SYS_Yield,0,0,0);}
 NtWriteConsole("bootvid: sentlogon.exe created\\n");
 NtExit(0);for(;;)(void)sy3(SYS_Yield,0,0,0);
}
