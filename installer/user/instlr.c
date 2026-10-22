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
static void decstr(u64 value,char *out){char tmp[24];int n=0;if(!value)tmp[n++]='0';while(value&&n<(int)sizeof(tmp)){tmp[n++]=(char)('0'+value%10);value/=10;}int j=0;while(n>0)out[j++]=tmp[--n];out[j]=0;}
static u32 crc32_bytes(const unsigned char *p,u32 n){u32 c=0xFFFFFFFFu;for(u32 i=0;i<n;i++){c^=p[i];for(int b=0;b<8;b++)c=(c>>1)^((0u-(c&1u))&0xEDB88320u);}return ~c;}
static void put32(unsigned char*p,u32 v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);}
static void put64(unsigned char*p,u64 v){put32(p,(u32)v);put32(p+4,(u32)(v>>32));}
static void put_guid(unsigned char*p,const unsigned char*g){for(int i=0;i<16;i++)p[i]=g[i];}
static int write_lba(u64 lba,const unsigned char*sector){unsigned char copy[512];for(int i=0;i<512;i++)copy[i]=sector[i];return NtInstallerDiskIo(1,lba,copy)==0?0:-1;}
static int make_gpt(u64 sectors){
 if(sectors<262144ULL)return -1;
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

static int format_fat32(u64 start,u32 total){
 if(total<131072u)return -1;
 const u32 reserved=32,fats=2,spc=1;
 u32 fatsz=1;
 for(int i=0;i<8;i++){u32 clusters=(total-reserved-fats*fatsz)/spc;u32 need=(clusters+2+127)/128;if(need==fatsz)break;fatsz=need;}
 if(reserved+fats*fatsz>=total)return -2;
 u32 clusters=(total-reserved-fats*fatsz)/spc;
 unsigned char s[512];for(int i=0;i<512;i++)s[i]=0;
 s[0]=0xEB;s[1]=0x58;s[2]=0x90;
 const char oem[]="SENTOSFAT";for(int i=0;i<8;i++)s[3+i]=(unsigned char)oem[i];
 s[11]=0;s[12]=2;s[13]=1;s[14]=32;s[16]=2;s[21]=0xF8;s[24]=63;s[26]=255;
 put32(s+28,(u32)start);put32(s+32,total);put32(s+36,fatsz);put32(s+44,2);s[48]=1;s[50]=6;s[64]=0x80;s[66]=0x29;put32(s+67,0x53454E54);
 const char label[]="SENTINELOSF";for(int i=0;i<11;i++)s[71+i]=(unsigned char)label[i];
 const char fs[]="FAT32   ";for(int i=0;i<8;i++)s[82+i]=(unsigned char)fs[i];s[510]=0x55;s[511]=0xAA;
 if(write_lba(start,s)<0)return -3;if(write_lba(start+6,s)<0)return -4;
 for(int i=0;i<512;i++)s[i]=0;put32(s,0x41615252);put32(s+484,0x61417272);put32(s+488,clusters-1);put32(s+492,3);put32(s+508,0xAA550000);
 if(write_lba(start+1,s)<0||write_lba(start+7,s)<0)return -5;
 for(u32 i=0;i<reserved;i++){if(i==0||i==1||i==6||i==7)continue;for(int j=0;j<512;j++)s[j]=0;if(write_lba(start+i,s)<0)return -6;}
 u64 fat0=start+reserved;
 for(u32 i=0;i<fats*fatsz;i++){for(int j=0;j<512;j++)s[j]=0;if(write_lba(fat0+i,s)<0)return -7;}
 for(int copy=0;copy<2;copy++){for(int j=0;j<512;j++)s[j]=0;put32(s,0x0FFFFFF8);put32(s+4,0xFFFFFFFF);put32(s+8,0x0FFFFFFF);if(write_lba(fat0+(u64)copy*fatsz,s)<0)return -8;}
 if(NtInstallerDiskIo(0,start,s)!=0)return -9;
 if(s[510]!=0x55||s[511]!=0xAA||s[82]!='F'||s[13]!=1||s[14]!=32||s[16]!=2||s[48]!=1||s[50]!=6)return -10;
 if(NtInstallerDiskIo(0,start+1,s)!=0||s[0]!=0x52||s[1]!=0x52||s[2]!=0x61||s[3]!=0x41||s[510]!=0x00||s[511]!=0x00)return -11;
 if(NtInstallerDiskIo(0,start+reserved,s)!=0||s[0]!=0xF8||s[1]!=0xFF||s[2]!=0xFF||s[3]!=0x0F)return -12;
 return 0;
}

static void put16(unsigned char*p,unsigned short v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static int write_block(u64 block,const unsigned char*b){for(int i=0;i<8;i++)if(write_lba(block*8+(u64)i,b+i*512)<0)return -1;return 0;}
static int format_ext4(u64 start,u64 sectors){
 const u32 bpg=32768,ipg=1024,isize=256,itable_blocks=64;
 u64 blocks=sectors/8;if(blocks<65536)return -1;
 u32 groups=(u32)((blocks+bpg-1)/bpg);if(groups>32)return -2;
 u32 total_inodes=groups*ipg;u32 free_blocks=0;
 static unsigned char gd[4096];static unsigned char sb[1024];static unsigned char bm[4096];static unsigned char ibm[4096];static unsigned char blk[4096];
 for(u32 i=0;i<sizeof(gd);i++)gd[i]=0;
 for(u32 i=0;i<sizeof(sb);i++)sb[i]=0;
 for(u32 g=0;g<groups;g++){
  u64 gs=(u64)g*bpg;u32 actual=(u32)((blocks-gs)>bpg?bpg:(blocks-gs));
  int backup=(g==0||g==1||g==3||g==5||g==7||g==9||g==25);
  u32 reserve=backup?2:0;u32 bb=(u32)(gs+reserve),ib=bb+1,it=ib+1;
  u32 used=reserve+2+itable_blocks+(g==0?1:0);
  if(actual<used)return -3;
  unsigned char*d=gd+g*32;put32(d,bb);put32(d+4,ib);put32(d+8,it);
  put16(d+12,(unsigned short)(actual-used));put16(d+14,(unsigned short)(ipg-(g==0?10:0)));put16(d+16,(unsigned short)(g==0?1:0));
  free_blocks+=actual-used;
  for(u32 i=0;i<sizeof(bm);i++)bm[i]=0;
  for(u32 i=0;i<used;i++)bm[i>>3]|=(unsigned char)(1u<<(i&7));
  for(u32 i=actual;i<bpg;i++)bm[i>>3]|=(unsigned char)(1u<<(i&7));
  for(u32 i=0;i<sizeof(ibm);i++)ibm[i]=0;
  if(g==0)for(u32 i=0;i<10;i++)ibm[i>>3]|=(unsigned char)(1u<<(i&7));
  if(write_block(start/8+bb,bm)<0||write_block(start/8+ib,ibm)<0)return -4;
  for(u32 i=0;i<itable_blocks;i++){for(u32 j=0;j<sizeof(blk);j++)blk[j]=0;if(write_block(start/8+it+i,blk)<0)return -5;}
  if(g==0){
   /* inode 2: root directory, using the extent format read by SentinelOS ext4. */
   for(u32 j=0;j<sizeof(blk);j++)blk[j]=0;
   unsigned char*ino=blk+isize;put16(ino,0x41ED);put32(ino+4,4096);put16(ino+26,2);put32(ino+28,8);put32(ino+32,0x80000);
   put16(ino+40,0xF30A);put16(ino+42,1);put16(ino+44,4);put16(ino+46,0);put32(ino+52,0);put16(ino+56,1);put16(ino+58,0);put32(ino+60,gs+reserve+2+itable_blocks);
   if(write_block(start/8+it,blk)<0)return -6;
   for(u32 j=0;j<sizeof(blk);j++)blk[j]=0;
   put32(blk,2);put16(blk+4,12);blk[6]=1;blk[7]=2;blk[8]='.';
   put32(blk+12,2);put16(blk+16,4084);blk[18]=2;blk[19]=2;blk[20]='.';blk[21]='.';
   if(write_block(start/8+gs+reserve+2+itable_blocks,blk)<0)return -7;
  }
 }
 put32(sb,total_inodes);put32(sb+4,(u32)blocks);put32(sb+12,free_blocks);put32(sb+16,total_inodes-10);
 put32(sb+20,0);put32(sb+24,2);put32(sb+28,2);put32(sb+32,bpg);put32(sb+36,bpg);put32(sb+40,ipg);
 put16(sb+56,0xEF53);put16(sb+58,1);put16(sb+60,1);put32(sb+76,1);put32(sb+84,11);put16(sb+88,isize);
 put32(sb+92,0);put32(sb+96,0x42);put32(sb+100,0x1);put32(sb+104,0x53454E54);const char label[]="SENTINELOS";for(int i=0;i<10;i++)sb[120+i]=(unsigned char)label[i];
 put16(sb+254,32);
 /* Primary superblock starts 1024 bytes into the partition. */
 for(int i=0;i<2;i++)if(write_lba(start+2+(u64)i,sb+i*512)<0)return -8;
 /* Primary group descriptor table at block 1. */
 for(int i=0;i<8;i++)if(write_lba(start+8+(u64)i,gd+i*512)<0)return -9;
 /* Keep backup superblocks and descriptor tables coherent for sparse-super groups. */
 for(u32 g=1;g<groups;g++)if(g==1||g==3||g==5||g==7||g==9||g==25){
  u64 glba=start+(u64)g*bpg*8;
  for(int i=0;i<2;i++)if(write_lba(glba+2+(u64)i,sb+i*512)<0)return -10;
  for(int i=0;i<8;i++)if(write_lba(glba+8+(u64)i,gd+i*512)<0)return -11;
 }
 unsigned char verify[512];if(NtInstallerDiskIo(0,start+2,verify)!=0||verify[56]!=0x53||verify[57]!=0xEF)return -12;
 if(NtInstallerDiskIo(0,start+8*68,verify)!=0||verify[0]!=2||verify[8]!='.'||verify[20]!='.')return -13;
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
   text(f,p,w,h,x+24,y+78,"A VirtIO block disk was detected.",0xFF1B6C40,1);
   text(f,p,w,h,x+24,y+102,"Capacity (MiB):",0xFF203944,1);
   text(f,p,w,h,x+144,y+102,sizebuf,0xFF203944,1);
   text(f,p,w,h,x+24,y+126,"This is the dedicated QEMU test target.",0xFF203944,1);
   rect(f,p,w,h,x+24,y+162,pw-48,42,0xFFE3F3E8);
   text(f,p,w,h,x+38,y+178,install_state==1?"GPT ready; click to format EFI partition":install_state==2?"EFI FAT32 formatted; click to format system":install_state==3?"System ext4 formatted; OS copy pending":install_state<0?"Disk operation failed - check log":"Ready to partition the selected disk",install_state>=1?0xFF1B6C40:install_state<0?0xFF9B3B24:0xFF1B6C40,1);
  }else{
   text(f,p,w,h,x+24,y+78,"No supported VirtIO block disk was detected.",0xFF9B3B24,1);
   text(f,p,w,h,x+24,y+102,"Check that the VM has a VirtIO disk attached.",0xFF203944,1);
   text(f,p,w,h,x+24,y+126,"No disk writes will be attempted.",0xFF203944,1);
   rect(f,p,w,h,x+24,y+162,pw-48,42,0xFFE3E9EC);
   text(f,p,w,h,x+38,y+178,"Target disk: unavailable",0xFF566B73,1);
  }
  rect(f,p,w,h,x+pw-142,y+ph-56,118,34,disk_present&&install_state<3?0xFF176B8C:0xFF9CAEB5);
  text(f,p,w,h,x+pw-128,y+ph-45,install_state==3?"Formatted":install_state==2?"Format System":install_state==1?"Format ESP":install_state<0?"Retry":"Partition",0xFFFFFFFF,1);
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
 f.pitch/=4;IFB b={f.va,f.w,f.h,f.pitch};u32*fb=(u32*)(u64)f.va;int page=0,checked=0,accepted=0,install_state=0;InstallerDiskInfo disk={0};int disk_present=(NtQueryInstallerDisk(&disk)!=0&&disk.present);u64 disk_mib=disk.sector_size?disk.sectors*disk.sector_size/(1024*1024):0;
 int mouse_x=(int)f.w/2,mouse_y=(int)f.h/2;
 paint(fb,&b,page,accepted,checked,disk_present,disk_mib,install_state);draw_cursor(fb,&b,mouse_x,mouse_y);(void)NtFlushGraphics(0,0,f.w,f.h);
 for(;;){InputEvent ev={0};if(sy3(SYS_GetInput,(u64)&ev,0,0)){
  if(ev.type==2||ev.type==3){mouse_x=ev.x;mouse_y=ev.y;}
  if(ev.type==1&&(ev.key=='\r'||ev.key=='\n')){if(page==0&&checked){page=1;NtWriteConsole("instlr: terms accepted; disk query completed\n");}}
  if(ev.type==3){int pw=(int)f.w-64;if(pw>900)pw=900;if(pw<250)pw=(int)f.w-24;int ph=(int)f.h-112;if(ph>600)ph=600;if(ph<120)ph=(int)f.h-70;int x=((int)f.w-pw)/2,y=((int)f.h-ph)/2+14;
   if(page==0){if(ev.x>=x+24&&ev.x<x+300&&ev.y>=y+ph-100&&ev.y<y+ph-64){checked=!checked;accepted=checked;}if(checked&&ev.x>=x+pw-150&&ev.x<x+pw-18&&ev.y>=y+ph-62&&ev.y<y+ph-18){page=1;}}
   else if(ev.x>=x+24&&ev.x<x+150&&ev.y>=y+ph-62&&ev.y<y+ph-18){page=0;}
   else if(disk_present&&ev.x>=x+pw-150&&ev.x<x+pw-18&&ev.y>=y+ph-62&&ev.y<y+ph-18&&install_state<3){if(install_state<=0){NtWriteConsole("instlr: writing GPT partition table to selected disk\n");install_state=make_gpt(disk.sectors)==0?1:-1;NtWriteConsole(install_state==1?"instlr: GPT partition table verified\n":"instlr: GPT partitioning failed\n");}else if(install_state==1){NtWriteConsole("instlr: formatting EFI partition as FAT32\n");install_state=format_fat32(2048,262144)==0?2:-1;NtWriteConsole(install_state==2?"instlr: EFI FAT32 format verified\n":"instlr: EFI FAT32 format failed\n");}else if(install_state==2){NtWriteConsole("instlr: formatting system partition as ext4\n");install_state=format_ext4(264192,disk.sectors-264225)==0?3:-1;NtWriteConsole(install_state==3?"instlr: system ext4 format verified\n":"instlr: system ext4 format failed\n");}}
  }
  paint(fb,&b,page,accepted,checked,disk_present,disk_mib,install_state);draw_cursor(fb,&b,mouse_x,mouse_y);(void)NtFlushGraphics(0,0,f.w,f.h);
 }(void)sy3(SYS_Yield,0,0,0);}
}
