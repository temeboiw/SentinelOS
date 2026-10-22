#include "ntdll_user.h"
#include "font8x8.h"

typedef struct { int type,x,y,btn; char key; } InputEvent;
typedef struct { u64 va; u32 w,h,pitch; } IFB;
static int installer_payloads_ready;
static u64 install_progress_done, install_progress_total;
static int install_countdown = 10;
static void rect(u32*f,u32 p,u32 w,u32 h,int x,int y,int rw,int rh,u32 c){int x2=x+rw,y2=y+rh;if(x<0)x=0;if(y<0)y=0;if(x2>(int)w)x2=w;if(y2>(int)h)y2=h;if(x>=x2||y>=y2)return;for(int yy=y;yy<y2;yy++)for(int xx=x;xx<x2;xx++)f[(u64)yy*p+xx]=c;}
static void text(u32*f,u32 p,u32 w,u32 h,int x,int y,const char*s,u32 c,int z){for(;*s;s++,x+=8*z){unsigned char ch=(unsigned char)*s;if(ch>=128)ch='?';const unsigned char*g=font8x8_basic[ch];for(int j=0;j<8;j++)for(int i=0;i<8;i++)if(g[j]&(1u<<i))rect(f,p,w,h,x+i*z,y+j*z,z,z,c);}}
static void draw_cursor(u32 *f, const IFB *b, int mx, int my) {
 static const char shape[16][17] = {
  "*...............","**..............","*+*.............","*++*............",
  "*+++*...........","*++++*..........","*+++++*.........","*++++++*........",
  "*++++***........","*++*+*..........","*+*.*+*.........","**..*+*.........",
  "....*+*.........",".....*+*........",".....***........","................"
 };
 for(int y=0;y<16;y++) for(int x=0;x<16;x++) {
  int px=mx+x,py=my+y;
  if(px<0||py<0||px>=(int)b->w||py>=(int)b->h) continue;
  char ch=shape[y][x];
  if(ch=='*') f[(u64)py*b->pitch+px]=0xFF10212A;
  else if(ch=='+') f[(u64)py*b->pitch+px]=0xFFFFFFFF;
 }
}
static int payload_image_valid(u64 image,u64 total){
 if((total&511ULL)!=0)return 0;
 if(image==0)return total==128ULL*1024*1024;
 return image==1&&total>=512ULL*1024*1024&&total<=1024ULL*1024*1024;
}
static void decstr(u64 value,char *out){char tmp[24];int n=0;if(!value)tmp[n++]='0';while(value&&n<(int)sizeof(tmp)){tmp[n++]=(char)('0'+value%10);value/=10;}int j=0;while(n>0)out[j++]=tmp[--n];out[j]=0;}
static u32 crc32_bytes(const unsigned char *p,u32 n){u32 c=0xFFFFFFFFu;for(u32 i=0;i<n;i++){c^=p[i];for(int b=0;b<8;b++)c=(c>>1)^((0u-(c&1u))&0xEDB88320u);}return ~c;}
static void put32(unsigned char*p,u32 v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);}
static void put64(unsigned char*p,u64 v){put32(p,(u32)v);put32(p+4,(u32)(v>>32));}
static void put_guid(unsigned char*p,const unsigned char*g){for(int i=0;i<16;i++)p[i]=g[i];}
static int write_lba(u64 lba,const unsigned char*sector){unsigned char copy[512];for(int i=0;i<512;i++)copy[i]=sector[i];return NtInstallerDiskIo(1,lba,copy)==0?0:-1;}
static int make_gpt(u64 sectors){
 if(sectors<1312802ULL)return -1;
 const u64 esp_first=2048,esp_count=262144,sys_first=esp_first+esp_count,backup=sectors-1,usable_last=sectors-34;
 if(sys_first>=usable_last)return -2;
 static const unsigned char efi_type[16]={0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B};
 static const unsigned char linux_type[16]={0xAF,0x3D,0xC6,0x0F,0x83,0x84,0x72,0x47,0x8E,0x79,0x3D,0x69,0xD8,0x47,0x7D,0xE4};
 static const unsigned char disk_guid[16]={0x53,0x45,0x4E,0x54,0x49,0x4E,0x45,0x4C,0xA4,0x11,0x42,0x90,0x33,0x7A,0x10,0x01};
 static const unsigned char esp_guid[16]={0x53,0x45,0x4E,0x54,0x45,0x53,0x50,0x01,0xA4,0x11,0x42,0x90,0x33,0x7A,0x10,0x02};
 static const unsigned char sys_guid[16]={0x53,0x45,0x4E,0x54,0x53,0x59,0x53,0x01,0xA4,0x11,0x42,0x90,0x33,0x7A,0x10,0x03};
 static unsigned char entries[16384];for(u32 i=0;i<sizeof(entries);i++)entries[i]=0;
 put_guid(entries,efi_type);put_guid(entries+16,esp_guid);put64(entries+32,esp_first);put64(entries+40,esp_first+esp_count-1);
 const char ename[]="Sentinel ESP";for(u32 i=0;i<sizeof(ename)-1;i++)entries[56+i*2]=(unsigned char)ename[i];
 unsigned char *e=entries+128;put_guid(e,linux_type);put_guid(e+16,sys_guid);put64(e+32,sys_first);put64(e+40,usable_last);
 const char sname[]="SentinelOS";for(u32 i=0;i<sizeof(sname)-1;i++)e[56+i*2]=(unsigned char)sname[i];
 u32 entries_crc=crc32_bytes(entries,sizeof(entries));
 unsigned char sector[512];for(int i=0;i<512;i++)sector[i]=0;
 sector[446+4]=0xEE;sector[446+1]=0;sector[446+2]=2;sector[446+3]=0;put32(sector+446+8,1);put32(sector+446+12,(u32)((sectors-1)>0xFFFFFFFFULL?0xFFFFFFFFu:sectors-1));sector[510]=0x55;sector[511]=0xAA;
 if(write_lba(0,sector)<0)return -3;
 for(u32 i=0;i<32;i++)if(write_lba(2+i,entries+i*512)<0)return -4;
 for(u32 i=0;i<32;i++)if(write_lba(backup-32+i,entries+i*512)<0)return -5;
 for(int copy=0;copy<2;copy++){
  for(int i=0;i<512;i++)sector[i]=0;
  const char sig[]="EFI PART";for(int i=0;i<8;i++)sector[i]=(unsigned char)sig[i];
  put32(sector+8,0x00010000);put32(sector+12,92);
  put64(sector+24,copy?backup:1);put64(sector+32,copy?1:backup);
  put64(sector+40,34);put64(sector+48,usable_last);put_guid(sector+56,disk_guid);
  put64(sector+72,copy?backup-32:2);put32(sector+80,128);put32(sector+84,128);put32(sector+88,entries_crc);put32(sector+16,0);put32(sector+16,crc32_bytes(sector,92));
  if(write_lba(copy?backup:1,sector)<0)return -6;
 }
 if(NtInstallerDiskIo(0,1,sector)!=0)return -7;
 if(sector[0]!='E'||sector[1]!='F'||sector[2]!='I'||sector[3]!=' '||sector[4]!='P'||sector[5]!='A'||sector[6]!='R'||sector[7]!='T')return -8;
 return 0;
}

static void paint(u32*f,IFB*b,int page,int accepted,int checked,int disk_present,u64 disk_mib,int install_state){
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
  if(disk_present){
   char sizebuf[24];decstr(disk_mib,sizebuf);
   text(f,p,w,h,x+24,y+78,"Detected install target: VirtIO block disk.",0xFF1B6C40,1);
   text(f,p,w,h,x+24,y+102,"Capacity (MiB):",0xFF203944,1);
   text(f,p,w,h,x+144,y+102,sizebuf,0xFF203944,1);
   text(f,p,w,h,x+24,y+126,"WARNING: all data on this disk will be erased.",0xFF203944,1);
   text(f,p,w,h,x+24,y+146,installer_payloads_ready<0?"Checking image integrity...":installer_payloads_ready?"Image integrity: SHA-256 verified":"Image integrity: failed/missing",installer_payloads_ready==1?0xFF1B6C40:installer_payloads_ready<0?0xFF174C63:0xFF9B3B24,1);
   rect(f,p,w,h,x+24,y+162,pw-48,42,0xFFE3F3E8);
   text(f,p,w,h,x+38,y+178,install_state==1?"Ready to install SentinelOS":install_state==2?"Installing SentinelOS...":install_state==3?"Installation finished. Remove USB media.":install_state<0?"Installation failed - see setup log":"Ready to install SentinelOS",install_state>=1?0xFF1B6C40:install_state<0?0xFF9B3B24:0xFF1B6C40,1);
   if(install_state==2||install_state==3){
    u64 pct=install_progress_total?(install_progress_done*100ULL)/install_progress_total:0;
    if(pct>100)pct=100;
    rect(f,p,w,h,x+24,y+220,pw-48,22,0xFFD5E0E5);
    rect(f,p,w,h,x+24,y+220,(int)(((u64)(pw-48)*pct)/100ULL),22,0xFF1685A7);
    char pctbuf[24];decstr(pct,pctbuf);
    text(f,p,w,h,x+24,y+248,"Progress:",0xFF203944,1);
    text(f,p,w,h,x+88,y+248,pctbuf,0xFF203944,1);
    text(f,p,w,h,x+112,y+248,"%",0xFF203944,1);
    if(install_state==3){
      char secbuf[24];decstr((u64)install_countdown,secbuf);
      text(f,p,w,h,x+24,y+278,"Restarting in",0xFF174C63,1);
      text(f,p,w,h,x+128,y+278,secbuf,0xFF174C63,1);
      text(f,p,w,h,x+144,y+278,"seconds. Remove installation media (USB).",0xFF174C63,1);
    }
   }
  }else{
   text(f,p,w,h,x+24,y+78,"No supported VirtIO block disk was detected.",0xFF9B3B24,1);
   text(f,p,w,h,x+24,y+102,"Check that the VM has a VirtIO disk attached.",0xFF203944,1);
   text(f,p,w,h,x+24,y+126,"No disk writes will be attempted.",0xFF203944,1);
   rect(f,p,w,h,x+24,y+162,pw-48,42,0xFFE3E9EC);
   text(f,p,w,h,x+38,y+178,"Target disk: unavailable",0xFF566B73,1);
  }
  rect(f,p,w,h,x+pw-142,y+ph-56,118,34,disk_present&&installer_payloads_ready&&install_state<3?0xFF176B8C:0xFF9CAEB5);
  text(f,p,w,h,x+pw-128,y+ph-45,install_state==3?"Installed":install_state<0?"Retry":"Install OS",0xFFFFFFFF,1);
  rect(f,p,w,h,x+24,y+ph-56,118,34,0xFF176B8C);
  text(f,p,w,h,x+57,y+ph-45,"Back",0xFFFFFFFF,1);
 }
 text(f,p,w,h,16,(int)h-24,"SentinelOS Setup",0xFF466571,1);
}
static void paint_loading(u32 *f, const IFB *b, const char *status, u32 pct) {
 u32 w=b->w,h=b->h,p=b->pitch;
 for(u32 y=0;y<h;y++){u32 c=0xFFEDF7FA-(y%16);for(u32 x=0;x<w;x++)f[(u64)y*p+x]=c;}
 rect(f,p,w,h,0,0,w,58,0xFF176B8C);
 rect(f,p,w,h,0,0,w,3,0xFFB6F4FC);
 text(f,p,w,h,24,19,"SentinelOS Setup",0xFFFFFFFF,2);
 int pw=(int)w-96,ph=230;if(pw>760)pw=760;if(pw<260)pw=(int)w-32;
 int x=((int)w-pw)/2,y=((int)h-ph)/2;
 rect(f,p,w,h,x+5,y+7,pw,ph,0xFF9BB0B9);
 rect(f,p,w,h,x,y,pw,ph,0xFFFFFFFF);
 rect(f,p,w,h,x,y,pw,46,0xFFD9EEF5);
 text(f,p,w,h,x+24,y+16,"Preparing your computer",0xFF174C63,2);
 text(f,p,w,h,x+24,y+82,"SentinelOS Setup is starting.",0xFF203944,1);
 text(f,p,w,h,x+24,y+110,status,0xFF174C63,1);
 rect(f,p,w,h,x+24,y+148,pw-48,24,0xFFD5E0E5);
 if(pct>100)pct=100;
 rect(f,p,w,h,x+24,y+148,(int)(((u64)(pw-48)*pct)/100ULL),24,0xFF1685A7);
 char percent[24];decstr(pct,percent);
 text(f,p,w,h,x+24,y+184,percent,0xFF203944,1);
 text(f,p,w,h,x+40,y+184,"%",0xFF203944,1);
 text(f,p,w,h,16,(int)h-24,"SentinelOS",0xFF466571,1);
}
void instlr_main(void){
 NtWriteConsole("instlr: installer session starting\n");
 GfxBootInfo gfx={0};u64 gok=NtInitializeGraphics(&gfx);FBInfo f={0};(void)NtMapFramebuffer(&f);
 if(gok&&gfx.backend==2&&gfx.surface_va){f.va=gfx.surface_va;f.w=gfx.width;f.h=gfx.height;f.pitch=gfx.pitch;}
 if(!f.va||f.w<320||f.h<200){NtWriteConsole("instlr: framebuffer unavailable; installer cannot continue\n");for(;;)(void)sy3(SYS_Yield,0,0,0);}
 f.pitch/=4;IFB b={f.va,f.w,f.h,f.pitch};u32*fb=(u32*)(u64)f.va;int page=0,checked=0,accepted=0,install_state=0;InstallerDiskInfo disk={0};int disk_present=(NtQueryInstallerDisk(&disk)!=0&&disk.present);u64 disk_mib=disk.sector_size?disk.sectors*disk.sector_size/(1024*1024):0;
 /* Paint immediately, before hashing the large embedded images. */
 int mouse_x=(int)f.w/2,mouse_y=(int)f.h/2;
 installer_payloads_ready=-1;
 paint_loading(fb,&b,"Loading installer components...",18);
 (void)NtFlushGraphics(0,0,f.w,f.h);
 paint_loading(fb,&b,"Verifying installation images (SHA-256)...",42);
 (void)NtFlushGraphics(0,0,f.w,f.h);
 /* Require both embedded filesystem images and their hashes before disk writes. */
 u64 esp_package_size=0,system_package_size=0;
 int esp_query=NtQueryInstallerPayload(0,&esp_package_size)==0;
 int sys_query=NtQueryInstallerPayload(1,&system_package_size)==0;
 installer_payloads_ready=esp_query&&sys_query&&
     payload_image_valid(0,esp_package_size)&&payload_image_valid(1,system_package_size)&&
     NtVerifyInstallerPayload(0)==0&&NtVerifyInstallerPayload(1)==0;
 NtWriteConsole(installer_payloads_ready?"instlr: filesystem images and SHA-256 verified\n":"instlr: image integrity check failed; disk writes disabled\n");
 paint(fb,&b,page,accepted,checked,disk_present,disk_mib,install_state);
 draw_cursor(fb,&b,mouse_x,mouse_y);
 (void)NtFlushGraphics(0,0,f.w,f.h);
 for(;;){InputEvent ev={0};if(sy3(SYS_GetInput,(u64)&ev,0,0)){
  if(ev.type==2||ev.type==3){mouse_x=ev.x;mouse_y=ev.y;}
  if(ev.type==1&&(ev.key=='\r'||ev.key=='\n')){if(page==0&&checked){page=1;NtWriteConsole("instlr: terms accepted; disk query completed\n");}}
  if(ev.type==3){int pw=(int)f.w-64;if(pw>900)pw=900;if(pw<250)pw=(int)f.w-24;int ph=(int)f.h-112;if(ph>600)ph=600;if(ph<120)ph=(int)f.h-70;int x=((int)f.w-pw)/2,y=((int)f.h-ph)/2+14;
   if(page==0){if(ev.x>=x+24&&ev.x<x+300&&ev.y>=y+ph-100&&ev.y<y+ph-64){checked=!checked;accepted=checked;}if(checked&&ev.x>=x+pw-150&&ev.x<x+pw-18&&ev.y>=y+ph-62&&ev.y<y+ph-18){page=1;}}
   else if(ev.x>=x+24&&ev.x<x+150&&ev.y>=y+ph-62&&ev.y<y+ph-18){page=0;}
   else if(disk_present&&installer_payloads_ready&&ev.x>=x+pw-150&&ev.x<x+pw-18&&ev.y>=y+ph-62&&ev.y<y+ph-18&&install_state<3){if(install_state<=0){
     install_state=1;install_progress_done=0;install_progress_total=esp_package_size+system_package_size;
     paint(fb,&b,page,accepted,checked,disk_present,disk_mib,install_state);(void)NtFlushGraphics(0,0,f.w,f.h);
     if(disk.sectors<264192ULL+system_package_size/512ULL+34ULL){install_state=-1;NtWriteConsole("instlr: target disk is too small for the selected system image\n");}
     else if(make_gpt(disk.sectors)!=0){install_state=-1;NtWriteConsole("instlr: GPT creation failed\n");}
     else {
      NtWriteConsole("instlr: GPT verified; installing SentinelOS\n");
      install_state=2;install_progress_done=0;
      paint(fb,&b,page,accepted,checked,disk_present,disk_mib,install_state);(void)NtFlushGraphics(0,0,f.w,f.h);
      int failed=0;
      for(u64 image=0;image<2&&!failed;image++){
       u64 total=image==0?esp_package_size:system_package_size;
       for(u64 off=0;off<total;){
        u64 chunk=total-off;if(chunk>4ULL*1024ULL*1024ULL)chunk=4ULL*1024ULL*1024ULL;
        if((off==0?(image==0?NtInstallerInstallImage(0):NtInstallerInstallImage(1)):NtInstallerInstallImageChunk(image,off,chunk))!=0){failed=1;break;}
        off+=chunk;install_progress_done+=chunk;
        /* A full 1280x800 repaint after every 4 MiB chunk costs more CPU than
           the UI needs. Refresh every 32 MiB and at each image boundary. */
        if ((off % (32ULL*1024ULL*1024ULL))==0 || off==total) {
         paint(fb,&b,page,accepted,checked,disk_present,disk_mib,install_state);
         (void)NtFlushGraphics(0,0,f.w,f.h);
        }
        (void)sy3(SYS_Yield,0,0,0);
       }
      }
      if(failed){install_state=-1;NtWriteConsole("instlr: disk image write or verification failed\n");}
      else {
       install_state=3;install_progress_done=install_progress_total;install_countdown=10;
       NtWriteConsole("instlr: installation finished; restarting in 10 seconds\n");
       /* Full-screen final page, with no cursor or input handling. */
       for(int sec=10;sec>=1;sec--){
        install_countdown=sec;
        paint(fb,&b,page,accepted,checked,disk_present,disk_mib,install_state);
        (void)NtFlushGraphics(0,0,f.w,f.h);
        u64 until=sy3(SYS_GetTickCount,0,0,0)+1000ULL;
        while(sy3(SYS_GetTickCount,0,0,0)<until)(void)sy3(SYS_Yield,0,0,0);
       }
       for(u64 i=0;i<(u64)f.h*f.pitch;i++)fb[i]=0xFF000000;
       (void)NtFlushGraphics(0,0,f.w,f.h);
       (void)sy3(SYS_Shutdown,1,0,0);
       for(;;)(void)sy3(SYS_Yield,0,0,0);
      }
     }
    }else if(install_state<0){NtWriteConsole("instlr: retry requested; press Install OS again\n");install_state=0;}}
  }
  paint(fb,&b,page,accepted,checked,disk_present,disk_mib,install_state);if(install_state!=3)draw_cursor(fb,&b,mouse_x,mouse_y);(void)NtFlushGraphics(0,0,f.w,f.h);
 }(void)sy3(SYS_Yield,0,0,0);}
}
