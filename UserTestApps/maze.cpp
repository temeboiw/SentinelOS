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
static float player_x=2.5f, player_y=2.5f;
static float camera_yaw=-1.57079632679f;
static int won=0;
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
/* x87 trigonometry avoids a CRT/libm dependency in this freestanding PE. */
static float fsin(float angle) {
    double a=(double)angle,r;
    __asm__ volatile("fldl %1; fsin; fstpl %0":"=m"(r):"m"(a));
    return (float)r;
}
static float fcos(float angle) {
    double a=(double)angle,r;
    __asm__ volatile("fldl %1; fcos; fstpl %0":"=m"(r):"m"(a));
    return (float)r;
}
static void draw_frame() {
    if(!pixels||!screen_w||!screen_h)return;
    const int w=(int)screen_w,h=(int)screen_h;
    const int status_h=h>=240?18:0;
    const int view_h=h-status_h;
    const float dirx=fcos(camera_yaw), diry=fsin(camera_yaw);
    const float plane_x=-diry*0.66f, plane_y=dirx*0.66f;
    const int horizon=view_h/2;
    /* Per-column DDA ray casting gives continuous perspective as the camera turns. */
    for(int x=0;x<w;x++) {
        float camera_x=2.0f*(float)x/(float)w-1.0f;
        float ray_x=dirx+plane_x*camera_x;
        float ray_y=diry+plane_y*camera_x;
        int map_x=(int)player_x,map_y=(int)player_y;
        float delta_x=ray_x==0.0f?1.0e30f:(ray_x<0?-ray_x:ray_x);
        float delta_y=ray_y==0.0f?1.0e30f:(ray_y<0?-ray_y:ray_y);
        delta_x=1.0f/delta_x; delta_y=1.0f/delta_y;
        int step_x,step_y,hit=0,side=0;
        float side_x,side_y;
        if(ray_x<0) { step_x=-1; side_x=(player_x-(float)map_x)*delta_x; }
        else { step_x=1; side_x=((float)map_x+1.0f-player_x)*delta_x; }
        if(ray_y<0) { step_y=-1; side_y=(player_y-(float)map_y)*delta_y; }
        else { step_y=1; side_y=((float)map_y+1.0f-player_y)*delta_y; }
        for(int n=0;n<64&&!hit;n++) {
            if(side_x<side_y) { side_x+=delta_x; map_x+=step_x; side=0; }
            else { side_y+=delta_y; map_y+=step_y; side=1; }
            if(blocked(map_x,map_y)) hit=1;
        }
        float dist=side==0?(side_x-delta_x):(side_y-delta_y);
        if(dist<0.05f)dist=0.05f;
        int line_h=(int)((float)view_h/dist);
        int top=horizon-line_h/2, bottom=horizon+line_h/2;
        if(top<0)top=0;if(bottom>=view_h)bottom=view_h-1;
        /* Ceiling and floor gradients are independent of wall distance. */
        for(int y=0;y<view_h;y++) {
            u32 color;
            if(y<horizon) {
                int shade=27+(horizon-y)*24/(horizon?horizon:1);
                color=rgb(shade,shade+7,shade+17);
            } else {
                int depth=y-horizon;
                int shade=47-depth*22/(horizon?horizon:1);
                if(shade<17)shade=17;
                color=rgb(shade+5,shade,shade-5);
            }
            pixels[(u64)y*pitch+(u32)x]=color;
        }
        if(hit) {
            float wall_x=side==0?player_y+dist*ray_y:player_x+dist*ray_x;
            wall_x-= (float)(int)wall_x;
            int stripe=(int)(wall_x*8.0f);
            int base=side?106:139;
            if(stripe==0||stripe==7)base-=18;
            int shade=base-(int)(dist*9.0f);
            if(shade<28)shade=28;if(shade>180)shade=180;
            u32 wall=side?rgb(shade-17,shade-8,shade-2):rgb(shade,shade-13,shade-25);
            for(int y=top;y<=bottom;y++) {
                int edge=(y-top<2||bottom-y<2);
                pixels[(u64)y*pitch+(u32)x]=edge?rgb(42,43,45):wall;
            }
        }
    }
    /* Reticle and compact HUD are overlaid after the 3D view. */
    rect(w/2-5,horizon,11,1,rgb(235,235,220));
    rect(w/2,horizon-5,1,11,rgb(235,235,220));
    if(status_h) {
        rect(0,view_h,w,status_h,rgb(12,18,26));
        int dir=(int)((camera_yaw+6.28318530718f)*4.0f/6.28318530718f)&3;
        for(int i=0;i<4;i++)rect(10+i*10,view_h+6,6,6,i==dir?rgb(80,220,170):rgb(75,85,95));
    }
    if(won)rect(w/2-80,h/2-18,160,36,rgb(35,150,90));
    Rect dirty={0,0,screen_w,screen_h};
    (void)syscall3(SYS_FLUSH_GRAPHICS,(u64)&dirty,0,0);
}
static void move_player(float dx,float dy) {
    if(won)return;
    /* Axis-separated collision allows sliding along walls instead of sticking. */
    float nx=player_x+dx;
    if(!blocked((int)(nx+(dx>0?0.18f:-0.18f)),(int)player_y))player_x=nx;
    float ny=player_y+dy;
    if(!blocked((int)player_x,(int)(ny+(dy>0?0.18f:-0.18f))))player_y=ny;
    int gx=(int)player_x,gy=(int)player_y;
    if(gx>=0&&gx<MW&&gy>=0&&gy<MH&&maze[gy][gx]=='E')won=1;
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
    /* Query the active display before creating the HWND so fullscreen
       geometry matches the real framebuffer instead of assuming 1280x800. */
    GraphicsBootInfo info{}; info.size=sizeof(info); info.version=1;
    if(syscall3(SYS_GRAPHICS_INFO,(u64)&info,0,0)&&info.surface_va&&info.width&&info.height&&info.pitch>=info.width*4) {
        pixels=(u32*)(uintptr_t)info.surface_va;
        screen_w=info.width; screen_h=info.height; pitch=info.pitch/4;
    }
    int display_w=screen_w?(int)screen_w:1280;
    int display_h=screen_h?(int)screen_h:800;

    /* WS_POPUP is intentional: a game that takes over the display has no
       title bar/border, while USER32 still owns and tracks its HWND. */
    window_handle=create_window(0,"STATIC","SentinelOS - 3D Maze",0x80000000u,
                                0,0,display_w,display_h,0,0,0,0);
    if(!window_handle) {
        print("maze.exe: window creation failed\n");
        (void)syscall3(SYS_TERMINATE_PROCESS,1,0,0); for(;;)__asm__ volatile("pause");
    }
    (void)show_window(window_handle,1);
    /* Hide the pointer while the game captures relative mouse movement. */
    if(show_cursor) {
        for(int i=0;i<8;i++) {
            if(show_cursor(0)<0) break;
        }
    }
    /* Confine pointer to the display while the game owns mouse-look. */
    if(clip_cursor) { ClipRect clip={0,0,display_w,display_h}; (void)clip_cursor(&clip); }
    if(update_window)(void)update_window(window_handle);

    if(!pixels) {
        print("maze.exe: no direct graphics surface; Vulkan loader probe completed, software scene unavailable\n");
    } else {
        draw_frame();
    }
    print("maze.exe: smooth mouse-look and ray-cast software scene active (Vulkan is loader-probed only)\n");
    Message msg{};
    u64 hz=tsc_frequency();
    u64 frame_cycles=hz/60ULL;
    u64 last_frame=read_tsc();
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
            u64 elapsed=now-last_frame;
            last_frame=now;
            float dt=(float)elapsed/(float)hz;
            if(dt<0.0f)dt=0.0f;
            if(dt>0.05f)dt=0.05f;
            u64 keys=syscall3(SYS_GAME_KEY_STATE,0,0,0);
            u32 mouse=(u32)syscall3(SYS_GAME_MOUSE_DELTA,0,0,0);
            int mdx=(int)(short)(mouse&0xffffu);
            /* Continuous yaw: no 90-degree snapping or mouse threshold. */
            camera_yaw+=(float)mdx*0.035f;
            const float tau=6.28318530718f;
            while(camera_yaw<0.0f)camera_yaw+=tau;
            while(camera_yaw>=tau)camera_yaw-=tau;
            if(set_cursor_pos && screen_w && screen_h)
                (void)set_cursor_pos((int)screen_w/2,(int)screen_h/2);
            if(keys&(1u<<4)) goto game_exit;
            float forward=0.0f,strafe=0.0f;
            if(keys&(1u<<0))forward+=1.0f;
            if(keys&(1u<<2))forward-=1.0f;
            if(keys&(1u<<1))strafe-=1.0f;
            if(keys&(1u<<3))strafe+=1.0f;
            if(forward!=0.0f||strafe!=0.0f) {
                float len=1.0f;
                if(forward!=0.0f&&strafe!=0.0f)len=0.70710678f;
                const float speed=2.8f*dt*len;
                float dx=(fcos(camera_yaw)*forward-fsin(camera_yaw)*strafe)*speed;
                float dy=(fsin(camera_yaw)*forward+fcos(camera_yaw)*strafe)*speed;
                move_player(dx,dy);
            }
            /* Redraw at the frame cadence even when standing still, so mouse
               look and animation never depend on a keypress edge. */
            draw_frame();
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
