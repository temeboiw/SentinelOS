/*
 * SentinelOS 3D maze prototype.
 * Freestanding C++ PE: no CRT, exceptions, RTTI, or import libraries.
 * Draws perspective wall slices into the OS framebuffer using the existing
 * software 3D surface API. It probes SentinelOS' Vulkan loader at startup;
 * the current Vulkan runtime does not yet expose a swapchain/present path,
 * so this version does NOT claim Vulkan is the scene renderer.
 */
#include <stdint.h>

using u64 = unsigned long long;
using u32 = unsigned int;
using i32 = int;
using BOOL = int;
using LPCSTR = const char *;

enum : u64 { SYS_TERMINATE_PROCESS=0, SYS_WRITE_FILE=1, SYS_GET_PROC_ADDRESS=9,
             SYS_GET_INPUT=10, SYS_GET_TICK_COUNT=24, SYS_GRAPHICS_INFO=32,
             SYS_FLUSH_GRAPHICS=33, SYS_YIELD=21, SYS_GAME_KEY_STATE=38, SYS_GAME_MOUSE_DELTA=39 };

static u64 syscall3(u64 n,u64 a,u64 b,u64 c) {
    u64 r;
    __asm__ volatile("mov %[n], %%rax\nmov %[a], %%rdi\nmov %[b], %%rsi\nmov %[c], %%rdx\nint $0x80\nmov %%rax, %[r]"
      : [r]"=r"(r):[n]"r"(n),[a]"r"(a),[b]"r"(b),[c]"r"(c):"rax","rdi","rsi","rdx","memory","cc");
    return r;
}
static void *get_proc(const char *dll,const char *name) {
    return (void*)(uintptr_t)syscall3(SYS_GET_PROC_ADDRESS,(u64)dll,(u64)name,0);
}
static void print(const char *s) {
    u64 n=0; while(s[n]) ++n; (void)syscall3(SYS_WRITE_FILE,1,(u64)s,n);
}

using CreateWindowExA_t=void*(*)(u32,LPCSTR,LPCSTR,u32,int,int,int,int,void*,void*,void*,void*);
using ShowWindow_t=BOOL(*)(void*,int);
using GetMessageA_t=BOOL(*)(void*,void*,u32,u32);
using PeekMessageA_t=BOOL(*)(void*,void*,u32,u32,u32);
using TranslateMessage_t=BOOL(*)(const void*);
using DispatchMessageA_t=long long(*)(const void*);
using UpdateWindow_t=BOOL(*)(void*);
struct ClipRect { int left,top,right,bottom; };
using ShowCursor_t=int(*)(BOOL);
using SetCursorPos_t=BOOL(*)(int,int);
using ClipCursor_t=BOOL(*)(const ClipRect*);
using VkCreateInstance_t=int(__attribute__((ms_abi)) *)(const void*,const void*,void**);
using VkEnumerateInstanceVersion_t=int(__attribute__((ms_abi)) *)(u32*);
using VkGetInstanceProcAddr_t=void*(__attribute__((ms_abi)) *)(void*,const char*);

/* Match user32.c MSG exactly: HWND is a 32-bit handle in this OS ABI,
   followed by message, then naturally aligned WPARAM/LPARAM fields. */
struct Message {
    u32 hwnd;
    u32 message;
    u64 wParam;
    long long lParam;
    u32 time;
    int x, y;
};
struct SurfaceInfo { u64 va; u32 w,h,pitch; };
struct GraphicsBootInfo {
    u32 size,version,backend,capabilities,width,height,pitch;
    u32 pci_bus,pci_device,pci_function;
    unsigned short vendor_id,device_id;
    unsigned char pci_class,pci_subclass,pci_prog_if,initialized;
    u64 surface_va,virgl_buffer_va;
    u32 virgl_buffer_resource_id,virgl_buffer_size;
};
struct Rect { u32 x,y,w,h; };

static const int MW=16,MH=16;
static const char maze[MH][MW+1]={
"################",
"#..............#",
"#..##.....##...#",
"#..##.....##...#",
"#......##......#",
"####...##...####",
"#..............#",
"#..####..####..#",
"#..#........#..#",
"#..#..##....#..#",
"#.....##.......#",
"###..........###",
"#..............#",
"#....####......#",
"#.............E#",
"################"
};
static u32 *pixels;
static u32 screen_w,screen_h,pitch;
static int px=2,py=2,heading=0,won=0;
static int mouse_turn_accum=0;
static void *window_handle;
static UpdateWindow_t update_window;
static PeekMessageA_t peek_message;
static SetCursorPos_t set_cursor_pos;
static ClipCursor_t clip_cursor;

static const int dirx[4]={0,1,0,-1};
static const int diry[4]={-1,0,1,0};
static int blocked(int x,int y) {
    return x<0||x>=MW||y<0||y>=MH||maze[y][x]=='#';
}
static u64 read_tsc() {
    u32 lo,hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi<<32)|lo;
}
static u64 tsc_frequency() {
    u32 a,b,c,d;
    u64 hz=0;
    __asm__ volatile("cpuid" : "=a"(a),"=b"(b),"=c"(c),"=d"(d) : "a"(0x15),"c"(0));
    if(a&&b&&c) hz=((u64)c*b)/a;
    if(!hz) {
        __asm__ volatile("cpuid" : "=a"(a),"=b"(b),"=c"(c),"=d"(d) : "a"(0x16),"c"(0));
        if(a) hz=(u64)a*1000000ULL;
    }
    /* Conservative fallback for virtual CPUs that omit TSC frequency leaves. */
    return hz?hz:1000000000ULL;
}
static u32 rgb(u32 r,u32 g,u32 b) { return 0xff000000u|(r<<16)|(g<<8)|b; }
static void put_pixel(int x,int y,u32 c) {
    if(pixels&&x>=0&&y>=0&&(u32)x<screen_w&&(u32)y<screen_h)
        pixels[(u64)y*pitch+(u32)x]=c;
}
static void rect(int x,int y,int w,int h,u32 c) {
    if(w<=0||h<=0)return;
    for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++)put_pixel(xx,yy,c);
}
static void draw_frame() {
    if(!pixels||!screen_w||!screen_h)return;
    int horizon=(int)screen_h/2;
    rect(0,0,(int)screen_w,horizon,rgb(24,45,75));
    rect(0,horizon,(int)screen_w,(int)screen_h-horizon,rgb(38,35,31));
    /* Distance slices create a perspective 3D corridor with shaded wall faces. */
    /* Paint distant geometry first so near walls cover it instead of bleeding through. */
    for(int d=8;d>=1;d--) {
        int wx=px,wy=py;
        for(int step=1;step<=d;step++) { wx+=dirx[heading]; wy+=diry[heading]; }
        int sideL=heading==0?3:heading==1?0:heading==2?1:2;
        int sideR=(sideL+2)&3;
        int leftx=px,lefty=py,rightx=px,righty=py;
        for(int s=1;s<=d;s++) {
            leftx+=dirx[sideL]; lefty+=diry[sideL];
            rightx+=dirx[sideR]; righty+=diry[sideR];
        }
        int half=(int)screen_w/(d+2);
        int top=horizon-(int)screen_h/(d+2);
        int bottom=horizon+(int)screen_h/(d+2);
        if(blocked(wx,wy)) {
            rect((int)screen_w/2-half,top,half*2,bottom-top,
                 rgb(115/(u32)(d/2+1),94/(u32)(d/2+1),72/(u32)(d/2+1)));
            for(int k=1;k<4;k++) {
                int yy=top+(bottom-top)*k/4;
                rect((int)screen_w/2-half,yy,half*2,2,rgb(55,46,38));
            }
            continue;
        }
        /* Side-wall faces, narrowed by distance to suggest perspective. */
        if(blocked(leftx,lefty)) {
            for(int x=0;x<half;x++) {
                int edge=(int)screen_w/2-half+x;
                int shade=70+x*25/(half?half:1);
                rect(edge,top,1,bottom-top,rgb(shade,shade-8,shade-18));
            }
        }
        if(blocked(rightx,righty)) {
            for(int x=0;x<half;x++) {
                int edge=(int)screen_w/2+half-x;
                int shade=62+x*20/(half?half:1);
                rect(edge,top,1,bottom-top,rgb(shade,shade-7,shade-16));
            }
        }
        /* Floor/ceiling depth bands. */
        rect((int)screen_w/2-half,top,half*2,1,rgb(42,58,74));
        rect((int)screen_w/2-half,bottom,half*2,1,rgb(88,76,61));
    }
    /* Small center reticle. */
    rect((int)screen_w/2-5,horizon,11,1,rgb(235,235,220));
    rect((int)screen_w/2,horizon-5,1,11,rgb(235,235,220));
    /* Status bar rendered as colored glyph blocks to avoid unsupported text APIs. */
    rect(0,(int)screen_h-18,(int)screen_w,18,rgb(12,18,26));
    for(int i=0;i<4;i++) rect(10+i*10,(int)screen_h-12,6,6,
       i==heading?rgb(80,220,170):rgb(75,85,95));
    if(won) {
        rect((int)screen_w/2-80,(int)screen_h/2-18,160,36,rgb(35,150,90));
        rect((int)screen_w/2-65,(int)screen_h/2-3,130,6,rgb(220,255,225));
    }
    Rect dirty={0,0,screen_w,screen_h};
    (void)syscall3(SYS_FLUSH_GRAPHICS,(u64)&dirty,0,0);
}
static void step(int dx,int dy) {
    if(won)return;
    int nx=px+dx,ny=py+dy;
    if(blocked(nx,ny))return;
    px=nx;py=ny;
    if(maze[py][px]=='E')won=1;
    draw_frame();
}
/* Camera heading stays fixed: A/D strafe instead of rotating the view. */
static void move_relative(int forward,int strafe) {
    int fx=dirx[heading], fy=diry[heading];
    int sx=dirx[(heading+1)&3], sy=diry[(heading+1)&3];
    step(fx*forward+sx*strafe,fy*forward+sy*strafe);
}

extern "C" void maze_mainCRTStartup() {
    /* Verify that the SentinelOS Vulkan runtime is present and exposes its
       global entry points. This is capability detection, not scene rendering. */
    auto vkCreateInstance=(VkCreateInstance_t)get_proc("vulkan.dll","vkCreateInstance");
    auto vkEnumerateVersion=(VkEnumerateInstanceVersion_t)get_proc("vulkan.dll","vkEnumerateInstanceVersion");
    auto vkGetInstanceProcAddr=(VkGetInstanceProcAddr_t)get_proc("vulkan.dll","vkGetInstanceProcAddr");
    u32 vkVersion=0;
    int vulkan_available=vkCreateInstance&&vkGetInstanceProcAddr;
    if(vkEnumerateVersion && vkEnumerateVersion(&vkVersion)!=0) vkVersion=0;

    auto create_window=(CreateWindowExA_t)get_proc("user32.dll","CreateWindowExA");
    auto show_window=(ShowWindow_t)get_proc("user32.dll","ShowWindow");
    auto show_cursor=(ShowCursor_t)get_proc("user32.dll","ShowCursor");
    set_cursor_pos=(SetCursorPos_t)get_proc("user32.dll","SetCursorPos");
    clip_cursor=(ClipCursor_t)get_proc("user32.dll","ClipCursor");
    update_window=(UpdateWindow_t)get_proc("user32.dll","UpdateWindow");
    auto get_message=(GetMessageA_t)get_proc("user32.dll","GetMessageA");
    peek_message=(PeekMessageA_t)get_proc("user32.dll","PeekMessageA");
    auto translate=(TranslateMessage_t)get_proc("user32.dll","TranslateMessage");
    auto dispatch=(DispatchMessageA_t)get_proc("user32.dll","DispatchMessageA");
    if(!create_window||!show_window||!get_message||!dispatch||!peek_message) {
        print("maze.exe: required USER32 functions missing\n");
        (void)syscall3(SYS_TERMINATE_PROCESS,1,0,0); for(;;)__asm__ volatile("pause");
    }
    window_handle=create_window(0,"STATIC","SentinelOS - 3D Maze",0x00CF0000u,70,50,900,620,0,0,0,0);
    if(!window_handle) {
        print("maze.exe: window creation failed\n");
        (void)syscall3(SYS_TERMINATE_PROCESS,1,0,0); for(;;)__asm__ volatile("pause");
    }
    (void)show_window(window_handle,1);
    /* Hide the desktop pointer while the fixed-camera game is active. */
    if(show_cursor) {
        for(int i=0;i<8;i++) {
            if(show_cursor(0)<0) break;
        }
    }
    /* Confine pointer to the screen while the game owns mouse-look. */
    if(clip_cursor) { ClipRect clip={0,0,1280,800}; (void)clip_cursor(&clip); }
    if(update_window)(void)update_window(window_handle);

    GraphicsBootInfo info{}; info.size=sizeof(info); info.version=1;
    if(syscall3(SYS_GRAPHICS_INFO,(u64)&info,0,0)&&info.surface_va&&info.width&&info.height&&info.pitch>=info.width*4) {
        pixels=(u32*)(uintptr_t)info.surface_va; screen_w=info.width;screen_h=info.height;pitch=info.pitch/4;
    }
    if(!pixels) {
        print("maze.exe: no direct graphics surface; Vulkan loader probe completed, software scene unavailable\n");
    } else {
        draw_frame();
    }
    print("maze.exe: mouse-look enabled; pointer hidden and centered; software scene\n");
    Message msg{};
    u64 hz=tsc_frequency();
    u64 frame_cycles=hz/60ULL;
    u64 step_cycles=hz/9ULL;
    u64 last_frame=read_tsc();
    u64 last_step=last_frame;
    for(;;) {
        /* Poll messages without blocking the simulation thread. */
        while(peek_message(&msg,nullptr,0,0,1)) {
            if(msg.message==0x0010||msg.message==0x0012) goto game_exit;
            if(msg.message==0x0100 && (u32)msg.wParam==0x1b) goto game_exit;
            if(translate)(void)translate(&msg);
            (void)dispatch(&msg);
        }

        /* A make/break-aware kernel state keeps movement continuous while
         * a physical key is held, without depending on keyboard typematic. */
        u64 now=read_tsc();
        if(now-last_frame>=frame_cycles) {
            last_frame=now; /* 60 Hz input/simulation polling cadence */
            u64 keys=syscall3(SYS_GAME_KEY_STATE,0,0,0);
            u32 mouse=(u32)syscall3(SYS_GAME_MOUSE_DELTA,0,0,0);
            int mdx=(int)(short)(mouse&0xffffu);
            mouse_turn_accum+=mdx;
            /* Grid renderer currently turns in quarter-turn increments. */
            while(mouse_turn_accum>=28) { heading=(heading+1)&3; mouse_turn_accum-=28; draw_frame(); }
            while(mouse_turn_accum<=-28) { heading=(heading+3)&3; mouse_turn_accum+=28; draw_frame(); }
            if(set_cursor_pos && screen_w && screen_h)
                (void)set_cursor_pos((int)screen_w/2,(int)screen_h/2);
            if(keys&(1u<<4)) goto game_exit;
            if(now-last_step>=step_cycles) {
                int forward=0,strafe=0;
                if(keys&(1u<<0)) forward=1;
                else if(keys&(1u<<2)) forward=-1;
                if(keys&(1u<<1)) strafe=-1;
                else if(keys&(1u<<3)) strafe=1;
                if(forward||strafe) {
                    move_relative(forward,strafe);
                    last_step=now;
                }
            }
        }
        (void)syscall3(SYS_YIELD,0,0,0);
    }
game_exit:
    /* Do not leave the desktop pointer clipped/hidden after the game exits. */
    if(clip_cursor) (void)clip_cursor(nullptr);
    if(show_cursor) { for(int i=0;i<8;i++) if(show_cursor(1)>=0) break; }
    (void)syscall3(SYS_TERMINATE_PROCESS,0,0,0);
    for(;;)__asm__ volatile("pause");
}
