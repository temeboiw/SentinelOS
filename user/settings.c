#include "ntdll_user.h"

typedef int (*BOOL)(int);
typedef struct { u32 hwnd, message; u64 wParam; i64 lParam; u32 time; int x, y; } MSG;
typedef void *(*CreateWindowExA_t)(u32,const char*,const char*,u32,int,int,int,int,void*,void*,void*,void*);
typedef int (*ShowWindow_t)(void*,int);
typedef int (*GetMessageA_t)(MSG*,void*,u32,u32);
typedef int (*DispatchMessageA_t)(const MSG*);
typedef int (*TextOutA_t)(void*,int,int,const char*,int);
typedef int (*InvalidateRect_t)(void*,void*,int);
typedef struct SettingsRect { int left,top,right,bottom; } SettingsRect;
typedef int (*GetWindowRect_t)(void*,SettingsRect*);

enum { SYS_NtNetworkInfo=49, SYS_NtQueryVolumeMetadata=31 };
typedef struct {
    u32 ip,mask,gateway,dns,dhcp_state,link_up;
    unsigned char mac[6]; unsigned short reserved;
} SettingsNetworkInfo;
typedef struct {
    u64 total_blocks,free_blocks,reserved_blocks;
    u32 block_size,volume_serial,total_inodes,free_inodes;
    char volume_label[17];
} SettingsVolumeInfo;

static const char *const categories[10]={
    "System","Display","Personalization","Mouse & Input","Performance",
    "Storage","Network & Internet","Applications","Accounts & Users","About SentinelOS"
};
static void *g_window;
static TextOutA_t g_text;
static InvalidateRect_t g_invalidate;
static unsigned g_page;
static SettingsNetworkInfo g_network;
static SettingsVolumeInfo g_volume;
static NT_PROCESS_SNAPSHOT g_processes[32];
static u64 g_process_count;
static GfxBootInfo g_gfx;
static FBInfo g_fb;
static int g_network_ok,g_volume_ok,g_process_ok,g_gfx_ok,g_fb_ok;

static void u32_text(char *out,u32 value){char tmp[12];unsigned n=0;if(!value){out[0]='0';out[1]=0;return;}while(value&&n<sizeof(tmp)){tmp[n++]=(char)('0'+value%10);value/=10;}for(unsigned i=0;i<n;i++)out[i]=tmp[n-i-1];out[n]=0;}
static void u64_text(char *out,u64 value){char tmp[24];unsigned n=0;if(!value){out[0]='0';out[1]=0;return;}while(value&&n<sizeof(tmp)){tmp[n++]=(char)('0'+value%10);value/=10;}for(unsigned i=0;i<n;i++)out[i]=tmp[n-i-1];out[n]=0;}
static void line(unsigned row,const char *s){if(g_text&&g_window&&row<40)(void)g_text(g_window,0,(int)row,s,-1);}
static void content(unsigned row,const char *s){if(g_text&&g_window&&row<40)(void)g_text(g_window,27,(int)row,s,-1);}
static void clear_content(void){static const char blank[]="                                                    ";for(unsigned y=2;y<38;y++)content(y,blank);}
static void cpuid(u32 leaf,u32 subleaf,u32 *a,u32 *b,u32 *c,u32 *d){
    u32 ra,rb,rc,rd;
    __asm__ volatile("cpuid":"=a"(ra),"=b"(rb),"=c"(rc),"=d"(rd):"a"(leaf),"c"(subleaf));
    *a=ra;*b=rb;*c=rc;*d=rd;
}
static void cpu_vendor(char out[13]){u32 a,b,c,d;cpuid(0,0,&a,&b,&c,&d);(void)a;for(unsigned i=0;i<4;i++){out[i]=(char)(b>>(i*8));out[4+i]=(char)(d>>(i*8));out[8+i]=(char)(c>>(i*8));}out[12]=0;}
static void ipv4_text(char *out,u32 ip){
    char n[12];unsigned at=0;
    for(unsigned i=0;i<4;i++){u32_text(n,(ip>>(i*8))&255u);for(unsigned j=0;n[j];j++)out[at++]=n[j];if(i<3)out[at++]='.';}
    out[at]=0;
}
static void refresh_data(void){
    g_gfx=(GfxBootInfo){0};
    g_network=(SettingsNetworkInfo){0};g_volume=(SettingsVolumeInfo){0};
    g_gfx_ok=NtInitializeGraphics(&g_gfx)!=0;
    /* The kernel maps the framebuffer once at a fixed user address. Preserve
       that mapping across page refreshes; a second map request correctly
       fails because the address range is already occupied. */
    if(!g_fb_ok){g_fb=(FBInfo){0};g_fb_ok=NtMapFramebuffer(&g_fb)!=0&&g_fb.w&&g_fb.h;}
    g_network_ok=sy3(SYS_NtNetworkInfo,(u64)&g_network,0,0)!=0;
    g_volume_ok=sy3(SYS_NtQueryVolumeMetadata,(u64)&g_volume,sizeof(g_volume),0)!=0;
    g_process_count=NtQueryProcessList(g_processes,32);
    g_process_ok=(i64)g_process_count>=0;
    if(g_process_ok&&g_process_count>32)g_process_count=32;
}
static void page_system(void){
    char vendor[13],row[80],n[24];u32 a,b,c,d;
    cpu_vendor(vendor);
    content(4,"SentinelOS V5 foundation - development build");
    content(6,"Architecture: x86_64");
    row[0]=0;
    const char *prefix="CPU vendor: ";unsigned at=0;while(prefix[at]){row[at]=prefix[at];at++;}for(unsigned i=0;i<12;i++)row[at++]=vendor[i];row[at]=0;content(8,row);
    cpuid(1,0,&a,&b,&c,&d);(void)b;(void)c;(void)d;
    u32_text(n,(a>>8)&15u);row[0]=0;at=0;prefix="CPUID family: ";while(prefix[at]){row[at]=prefix[at];at++;}for(unsigned i=0;n[i];i++)row[at++]=n[i];row[at]=0;content(9,row);
    content(11,"Settings build compiled: " __DATE__ " " __TIME__);
    content(13,"Memory capacity and uptime APIs are not exposed.");
}
static void page_display(void){
    char row[80],w[16],h[16],pitch[16];
    if(g_fb_ok){u32_text(w,g_fb.w);u32_text(h,g_fb.h);u32_text(pitch,g_fb.pitch);row[0]=0;unsigned n=0;const char *p="Current mode: ";while(*p)row[n++]=*p++;for(unsigned i=0;w[i];i++)row[n++]=w[i];row[n++]='x';for(unsigned i=0;h[i];i++)row[n++]=h[i];row[n]=0;content(4,row);row[0]=0;n=0;p="Framebuffer pitch: ";while(*p)row[n++]=*p++;for(unsigned i=0;pitch[i];i++)row[n++]=pitch[i];row[n]=0;content(6,row);}
    else content(4,"Current framebuffer mode is unavailable.");
    if(g_gfx_ok){const char *backend=g_gfx.backend==1?"firmware framebuffer":g_gfx.backend==2?"VirtIO-GPU":"unknown";row[0]=0;unsigned n=0;const char *p="Graphics backend: ";while(*p)row[n++]=*p++;for(unsigned i=0;backend[i];i++)row[n++]=backend[i];row[n]=0;content(8,row);}
    content(11,"Mode enumeration and dynamic resolution changes are unsupported.");
}
static void page_personalization(void){content(4,"Desktop theme and accent settings are not exposed.");content(6,"Wallpaper controls are unavailable in this shell.");content(8,"No persistent appearance preference store is available.");}
static void page_input(void){content(4,"The input stack exposes no pointer-speed setting.");content(6,"Keyboard layout and repeat preferences are not configurable.");content(8,"Pointer appearance remains controlled by the desktop.");}
static void page_performance(void){
    char row[80],n[16];
    if(g_process_ok){u32_text(n,(u32)g_process_count);row[0]=0;unsigned at=0;const char *p="Live process snapshot entries: ";while(*p)row[at++]=*p++;for(unsigned i=0;n[i];i++)row[at++]=n[i];row[at]=0;content(4,row);}
    else content(4,"Kernel process snapshot is unavailable.");
    content(6,"Memory usage and CPU utilization counters are not exposed.");
    content(8,"No power or performance profile API is implemented.");
}
static void page_storage(void){
    char row[80],n[24];
    if(g_volume_ok){u64_text(n,g_volume.total_blocks);row[0]=0;unsigned at=0;const char *p="Volume blocks: ";while(*p)row[at++]=*p++;for(unsigned i=0;n[i];i++)row[at++]=n[i];row[at]=0;content(4,row);u64_text(n,g_volume.free_blocks);row[0]=0;at=0;p="Free blocks: ";while(*p)row[at++]=*p++;for(unsigned i=0;n[i];i++)row[at++]=n[i];row[at]=0;content(6,row);}
    else content(4,"Volume metadata is unavailable from the current filesystem API.");
    content(9,"This page is read-only; no format or partition actions exist.");
}
static void page_network(void){
    char row[80],ip[20];
    if(!g_network_ok){content(4,"Kernel network information is unavailable.");return;}
    content(4,g_network.link_up?"Adapter initialized: yes":"Adapter initialized: no");
    if(g_network.ip){ipv4_text(ip,g_network.ip);row[0]=0;unsigned at=0;const char *p="IPv4 address: ";while(*p)row[at++]=*p++;for(unsigned i=0;ip[i];i++)row[at++]=ip[i];row[at]=0;content(6,row);}
    else content(6,"IPv4 address: none assigned");
    if(g_network.dhcp_state==2)content(8,"DHCP state: bound");else if(g_network.dhcp_state==1)content(8,"DHCP state: requesting");else content(8,"DHCP state: discovery / unavailable");
    if(g_network.gateway){ipv4_text(ip,g_network.gateway);row[0]=0;unsigned at=0;const char *p="Gateway: ";while(*p)row[at++]=*p++;for(unsigned i=0;ip[i];i++)row[at++]=ip[i];row[at]=0;content(10,row);}
    content(12,"Connection configuration and ping controls are not exposed here.");
}
static void page_apps(void){
    content(4,"Installed applications are launched from Start or File Explorer.");
    content(6,"The directory API is bound to the shell's current directory.");
    content(8,"Application inventory and removal are not available here.");
    content(10,"No application files were changed.");
}
static void page_accounts(void){content(4,"No authenticated user identity is exposed by the OS.");content(6,"Account creation and permissions are not implemented.");content(8,"This system does not provide secure multi-user isolation.");}
static void page_about(void){
    char row[80];content(4,"Product: SentinelOS V5 foundation (development)");content(6,"Architecture: x86_64");
    if(g_gfx_ok){const char *backend=g_gfx.backend==1?"firmware framebuffer":g_gfx.backend==2?"VirtIO-GPU":"unknown";row[0]=0;unsigned at=0;const char *p="Graphics backend: ";while(*p)row[at++]=*p++;for(unsigned i=0;backend[i];i++)row[at++]=backend[i];row[at]=0;content(8,row);}
    else content(8,"Graphics diagnostics: unavailable");
    content(10,"Kernel version is not exposed by a version interface.");
    content(12,"Settings build: " __DATE__ " " __TIME__);
    content(14,"Report this build date and the failing application or device.");
}
static void draw(void){
    clear_content();line(0,"SENTINELOS SETTINGS");line(1,"Select a category | Up/Down, Enter or 1-0");
    for(unsigned i=0;i<10;i++){char item[28];unsigned n=0;if(i==g_page)item[n++]='>';else item[n++]=' ';for(unsigned j=0;categories[i][j]&&n<25;j++)item[n++]=categories[i][j];item[n]=0;line(2+i*2,item);}
    content(1,categories[g_page]);
    switch(g_page){case 0:page_system();break;case 1:page_display();break;case 2:page_personalization();break;case 3:page_input();break;case 4:page_performance();break;case 5:page_storage();break;case 6:page_network();break;case 7:page_apps();break;case 8:page_accounts();break;default:page_about();break;}
    content(35,"Unsupported controls are described instead of simulated.");
    if(g_invalidate)(void)g_invalidate(g_window,0,0);
}
static void choose(unsigned page){if(page>=10)return;g_page=page;refresh_data();draw();}

void mainCRTStartup(void){
    const char dll[]="user32.dll";
    CreateWindowExA_t create=(CreateWindowExA_t)NtGetProcAddress(dll,"CreateWindowExA");
    ShowWindow_t show=(ShowWindow_t)NtGetProcAddress(dll,"ShowWindow");
    GetMessageA_t get=(GetMessageA_t)NtGetProcAddress(dll,"GetMessageA");
    DispatchMessageA_t dispatch=(DispatchMessageA_t)NtGetProcAddress(dll,"DispatchMessageA");
    g_text=(TextOutA_t)NtGetProcAddress(dll,"TextOutA");
    g_invalidate=(InvalidateRect_t)NtGetProcAddress(dll,"InvalidateRect");
    GetWindowRect_t get_rect=(GetWindowRect_t)NtGetProcAddress(dll,"GetWindowRect");
    if(!create||!show||!get||!dispatch||!g_text){NtWriteConsole("settings: required USER32 APIs are unavailable\n");NtExit(1);return;}
    g_window=create(0,"SentinelSettings","Settings - SentinelOS",0x10CF0000u,90,50,900,650,0,0,0,0);
    if(!g_window){NtWriteConsole("settings: native window creation failed\n");NtExit(2);return;}
    if(show)(void)show(g_window,1);
    refresh_data();draw();
    SettingsRect rect={0};MSG msg;
    for(;;){
        if(!get(&msg,0,0,0))continue;
        if(dispatch)(void)dispatch(&msg);
        if(msg.message==0x0010u)break;
        if(msg.message==0x0100u){unsigned key=(unsigned)msg.wParam;if(key==0x26u)choose(g_page?g_page-1:9);else if(key==0x28u)choose((g_page+1)%10);else if(key==0x0Du)choose(g_page);else if(key>='1'&&key<='9')choose(key-'1');else if(key=='0')choose(9);}
        else if(msg.message==0x0201u&&get_rect&&get_rect(g_window,&rect)){
            int cx=msg.x-rect.left,cy=msg.y-(rect.top+44);
            if(cx>=0&&cx<26&&cy>=32){unsigned page=(unsigned)((cy-32)/32);if(page<10)choose(page);}
        }else if(msg.message==0x000Fu){refresh_data();draw();}
    }
    NtExit(0);
}
