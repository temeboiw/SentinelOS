#include "ntdll_user.h"
#include "font8x8.h"

static u32 mix(u32 a,u32 b,u32 t){u32 r=(((a>>16)&255)*(255-t)+((b>>16)&255)*t)/255;u32 g=(((a>>8)&255)*(255-t)+((b>>8)&255)*t)/255;u32 z=((a&255)*(255-t)+(b&255)*t)/255;return 0xFF000000u|(r<<16)|(g<<8)|z;}
static void rect(u32*f,u32 p,u32 w,u32 h,int x,int y,int rw,int rh,u32 c){int x1=x+rw,y1=y+rh;if(x<0)x=0;if(y<0)y=0;if(x1>(int)w)x1=w;if(y1>(int)h)y1=h;for(int j=y;j<y1;j++)for(int i=x;i<x1;i++)f[(u64)j*p+i]=c;}
static void txt(u32*f,u32 p,u32 w,u32 h,int x,int y,const char*s,u32 c,int z){for(;*s;s++,x+=8*z){unsigned char q=*s;if(q>=128)q='?';const unsigned char*g=font8x8_basic[q];for(int j=0;j<8;j++)for(int i=0;i<8;i++)if(g[j]&(1u<<i))rect(f,p,w,h,x+i*z,y+j*z,z,z,c);}}
static u32 saved[1920u*1080u];
void bootvid_main(void){
 FBInfo f={0};
 if(NtMapFramebuffer(&f)&&f.va&&f.w>=320&&f.h>=200){
  f.pitch/=4;u32*fb=(u32*)(u64)f.va;
  for(u32 y=0;y<f.h;y++){u32 c=mix(0xFF071725,0xFF147AA4,(u32)((u64)y*255/(f.h>1?f.h-1:1)));rect(fb,f.pitch,f.w,f.h,0,y,f.w,1,c);}
  txt(fb,f.pitch,f.w,f.h,(int)f.w/2-40,(int)f.h/2-52,"SENTINELOS",0xFFFFFFFF,2);
  txt(fb,f.pitch,f.w,f.h,(int)f.w/2-60,(int)f.h/2-22,"Starting system",0xFFB7D7E7,1);
  static const int dx[12]={0,13,23,27,23,13,0,-13,-23,-27,-23,-13},dy[12]={-27,-23,-13,0,13,23,27,23,13,0,-13,-23};
  int cx=f.w/2,cy=f.h/2+22;u64 start=sy3(24,0,0,0);if(start==(u64)-1)start=0;
  for(u32 frame=0;frame<100;frame++){for(int yy=cy-34;yy<cy+34;yy++){u32 bg=mix(0xFF071725,0xFF147AA4,(u32)((u64)(yy<0?0:yy)*255/(f.h>1?f.h-1:1)));rect(fb,f.pitch,f.w,f.h,cx-34,yy,68,1,bg);}for(int i=0;i<12;i++){int age=(i-(int)(frame%12)+12)%12;u32 c=age==0?0xFFFFFFFF:age<4?0xFF80D8F5:age<7?0xFF397FA3:0xFF20465D;rect(fb,f.pitch,f.w,f.h,cx+dx[i]-3,cy+dy[i]-3,6,6,c);}(void)sy3(SYS_Yield,0,0,0);u64 now=sy3(24,0,0,0);if(now!=(u64)-1&&now-start>=1500)break;}
  u64 n=(u64)f.pitch*f.h;
  if(f.w<=1920&&f.h<=1080&&n<=1920ULL*1080ULL){for(u64 i=0;i<n;i++)saved[i]=fb[i];for(u32 step=16;step>0;step--){u32 a=(step-1)*255/16;for(u32 y=0;y<f.h;y++)for(u32 x=0;x<f.w;x++){u64 i=(u64)y*f.pitch+x;u32 c=saved[i];fb[i]=0xFF000000|((((c>>16)&255)*a/255)<<16)|((((c>>8)&255)*a/255)<<8)|((c&255)*a/255);}u64 t=sy3(24,0,0,0);if(t!=(u64)-1)while(sy3(24,0,0,0)-t<12)(void)sy3(SYS_Yield,0,0,0);}}
  else rect(fb,f.pitch,f.w,f.h,0,0,f.w,f.h,0xFF000000);
 }
 NtWriteConsole("bootvid: splash complete; launching logon\n");
 char name[]={'s','e','n','t','l','o','g','o','n','.','e','x','e',0};(void)NtCreateProcess(name);NtExit(0);for(;;)(void)sy3(SYS_Yield,0,0,0);
}
