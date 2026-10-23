/*
 * SentinelOS VirGL GPU triangle test.
 * Freestanding C++ PE: no CRT, exceptions, RTTI, or import libraries.
 * Submits a spinning, perspective-projected 3D triangle through the existing
 * VirGL vertex-buffer and render-target resources. VirGL executes TGSI vertex
 * and fragment shaders and rasterizes the triangle; CPU code animates the
 * triangle's three input vertices. This is not a Vulkan graphics-pipeline test.
 */
#include <stdint.h>
#include "virgl_protocol.h"
#include "../user/soft3d.h"
#include "../user/font8x8.h"

using u64 = unsigned long long;
using u32 = unsigned int;
using i32 = int;
using BOOL = int;
using LPCSTR = const char *;

enum : u64 { SYS_TERMINATE_PROCESS=0, SYS_WRITE_FILE=1, SYS_GET_PROC_ADDRESS=9,
             SYS_GET_INPUT=10, SYS_GET_TICK_COUNT=24, SYS_GRAPHICS_INFO=32,
             SYS_FLUSH_GRAPHICS=33, SYS_YIELD=21, SYS_GAME_KEY_STATE=38, SYS_GAME_MOUSE_DELTA=39, SYS_SUBMIT_3D=34, SYS_PRESENT_LAYER=37 };

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
using VkEnumeratePhysicalDevices_t=int(__attribute__((ms_abi)) *)(void*,u32*,void**);
using VkGetPhysicalDeviceQueueFamilyProperties_t=void(__attribute__((ms_abi)) *)(void*,u32*,void*);
using VkCreateDevice_t=int(__attribute__((ms_abi)) *)(void*,const void*,const void*,void**);
using VkGetDeviceQueue_t=void(__attribute__((ms_abi)) *)(void*,u32,u32,void**);
using VkDestroyDevice_t=void(__attribute__((ms_abi)) *)(void*,const void*);
using VkDestroyInstance_t=void(__attribute__((ms_abi)) *)(void*,const void*);
using VkDeviceWaitIdle_t=int(__attribute__((ms_abi)) *)(void*);
using VkSubmitVirGLStream_t=int(__attribute__((ms_abi)) *)(const u32*,u32);
using VkPresentVirGL_t=int(__attribute__((ms_abi)) *)();
struct VkAppInfoMaze { u32 sType; const void *pNext; const char *pApplicationName; u32 applicationVersion; const char *pEngineName; u32 engineVersion; u32 apiVersion; };
struct VkInstanceInfoMaze { u32 sType; const void *pNext; u32 flags; const VkAppInfoMaze *pApplicationInfo; u32 enabledLayerCount; const char *const *ppEnabledLayerNames; u32 enabledExtensionCount; const char *const *ppEnabledExtensionNames; };
struct VkQueueInfoMaze { u32 sType; const void *pNext; u32 flags; u32 queueFamilyIndex; u32 queueCount; const float *pQueuePriorities; };
struct VkDeviceInfoMaze { u32 sType; const void *pNext; u32 flags; u32 queueCreateInfoCount; const VkQueueInfoMaze *pQueueCreateInfos; u32 enabledLayerCount; const char *const *ppEnabledLayerNames; u32 enabledExtensionCount; const char *const *ppEnabledExtensionNames; const void *pEnabledFeatures; };
struct VkQueueFamilyMaze { u32 queueFlags,queueCount,timestampValidBits; struct { i32 width,height,depth; } minImageTransferGranularity; };
struct VkRuntimeMaze { void *instance,*physical,*device,*queue; VkDestroyDevice_t destroyDevice; VkDestroyInstance_t destroyInstance; VkDeviceWaitIdle_t waitIdle; u32 ready; };
static VkRuntimeMaze vkrt{};
static VkSubmitVirGLStream_t submit_virgl_stream;
static VkPresentVirGL_t present_virgl;
static int gpu_scene_ready=0;
static int gpu_rasterizer_created=0;
static S3D_SURFACE gpu_triangle_surface{};
static int gpu_triangle_surface_ready=0;
static int gpu_triangle_error_reported=0;
static float gpu_triangle_angle=0.0f;

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
struct PresentLayerArgs { u64 pixels; u32 layer,x,y,w,h,width,height; };
static int layer_present_warned=0;
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

/* Publish the completed scanout image to the WM-owned per-process window layer. */
static void present_maze_layer() {
    if(!pixels||!screen_w||!screen_h)return;
    PresentLayerArgs a={(u64)(uintptr_t)pixels,2u,0u,0u,screen_w,screen_h,screen_w,screen_h};
    if(!syscall3(SYS_PRESENT_LAYER,(u64)(uintptr_t)&a,0,0) && !layer_present_warned) {
        print("maze.exe: window-layer presentation rejected; game may remain hidden\n");
        layer_present_warned=1;
    }
}

static void draw_status_text(int x,int y,const char *s,u32 color,int scale) {
    for(;*s;s++,x+=8*scale) {
        unsigned char ch=(unsigned char)*s;
        if(ch>=128)continue;
        for(int row=0;row<8;row++) {
            unsigned char bits=font8x8_basic[ch][row];
            for(int col=0;col<8;col++)
                if(bits&(1u<<col))rect(x+col*scale,y+row*scale,scale,scale,color);
        }
    }
}

/* Diagnostic UI only: the 3D scene remains GPU-only. */
static int gpu_status_screen_drawn=0;
static void draw_gpu_unavailable_screen() {
    if(gpu_status_screen_drawn||!pixels||!screen_w||!screen_h)return;
    const u32 bg=rgb(12,18,31),panel=rgb(23,34,53),cyan=rgb(70,210,238);
    for(u32 y=0;y<screen_h;y++)
        for(u32 x=0;x<screen_w;x++)
            pixels[(u64)y*pitch+x]=bg;
    int pw=(int)screen_w-80,ph=230;
    if(pw>760)pw=760;
    if(pw<320)pw=(int)screen_w-24;
    if(pw<1)pw=(int)screen_w;
    if(ph>(int)screen_h-24)ph=(int)screen_h-24;
    if(ph<80)ph=(int)screen_h;
    int px=((int)screen_w-pw)/2,py=((int)screen_h-ph)/2;
    rect(px,py,pw,ph,panel);
    rect(px,py,pw,5,cyan);
    draw_status_text(px+24,py+26,"GPU RENDERING UNAVAILABLE",rgb(245,248,255),2);
    draw_status_text(px+24,py+82,"VIRGL 3D TARGET NOT ACTIVE",cyan,1);
    draw_status_text(px+24,py+112,"START QEMU WITH HOST OPENGL",rgb(224,229,240),1);
    draw_status_text(px+24,py+142,"VNC MODE WITH VIRGL=0 IS 2D ONLY",rgb(224,229,240),1);
    draw_status_text(px+24,py+184,"NO CPU 3D FALLBACK IS USED",rgb(154,171,194),1);
    present_maze_layer();
    gpu_status_screen_drawn=1;
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
static u64 fps_window_start;
static u32 fps_window_frames;
static void print_u32(u32 value) {
    char digits[11];u32 n=0;
    do { digits[n++]=(char)('0'+value%10u); value/=10u; } while(value&&n<sizeof(digits));
    while(n) { char one[2]={digits[--n],0}; print(one); }
}
static void report_frame_rate() {
    ++fps_window_frames;
    if(fps_window_frames<300u)return;
    u64 now=syscall3(SYS_GET_TICK_COUNT,0,0,0);
    if(!fps_window_start) { fps_window_start=now; fps_window_frames=0; return; }
    u64 elapsed=now-fps_window_start;
    if(elapsed<1000u)return;
    u32 fps=(u32)(((u64)fps_window_frames*1000u)/elapsed);
    print("maze.exe: measured VirGL shader-triangle frame rate = ");print_u32(fps);print(" FPS\n");
    fps_window_start=now;fps_window_frames=0;
}
/* GPU raster path: CPU computes one ray intersection per vertical strip;
 * VirGL/Gallium performs the actual pixel fills in its 3D render target. */
static u32 float_bits(float f) { union { float f; u32 u; } v; v.f=f; return v.u; }
static int gpu_append_scissor_clear(VirglStream *s,u32 x,u32 y,u32 w,u32 h,
                                    float r,float g,float b) {
    if(!w||!h)return 1;
    /* VirGL packs two 16-bit coordinates into each scissor dword. */
    const u32 scissor[3]={0u,(x&0xffffu)|((y&0xffffu)<<16),
                           ((x+w)&0xffffu)|(((y+h)&0xffffu)<<16)};
    const u32 clear[7]={4u,float_bits(r),float_bits(g),float_bits(b),
                        float_bits(1.0f),float_bits(1.0f),0u};
    return virgl_stream_command(s,VIRGL_CCMD_SET_SCISSOR_STATE,VIRGL_OBJECT_NULL,scissor,3) &&
           virgl_stream_command(s,VIRGL_CCMD_CLEAR,VIRGL_OBJECT_NULL,clear,7);
}
/* Batch 8: reject malformed or incomplete GPU frame streams before
 * crossing the kernel boundary. This validates transport packets, not shader
 * execution or proof that the GPU produced the expected pixels. */
static int gpu_frame_stream_ready(const VirglStream *stream) {
    if(!stream || stream->failed || !stream->words || !stream->used_dwords)
        return 0;
    if(!virgl_stream_validate(stream->words,stream->used_dwords))
        return 0;
    u32 commands=virgl_stream_count_commands(stream->words,stream->used_dwords);
    if(commands==UINT32_MAX || commands<5u)
        return 0;
    u32 bind=virgl_stream_find_command(stream->words,stream->used_dwords,
                                       VIRGL_CCMD_BIND_OBJECT);
    u32 framebuffer=virgl_stream_find_command(stream->words,stream->used_dwords,
                                              VIRGL_CCMD_SET_FRAMEBUFFER_STATE);
    u32 clear=virgl_stream_find_command(stream->words,stream->used_dwords,
                                        VIRGL_CCMD_CLEAR);
    if(bind==UINT32_MAX || framebuffer==UINT32_MAX || clear==UINT32_MAX)
        return 0;
    /* A clear command needs seven payload dwords; the framebuffer packet
       needs at least its count/depth header. Reject truncated/wrong packets. */
    if((stream->words[clear]>>16)!=7u ||
       (stream->words[framebuffer]>>16)<2u)
        return 0;
    return 1;
}

static i32 gpu_fixed16(float v) {
    double q=(double)v*65536.0;
    if(q>2147483000.0)q=2147483000.0;
    if(q< -2147483000.0)q= -2147483000.0;
    return (i32)q;
}
static S3D_VERTEX gpu_project_spinning_vertex(float x,float y,float z,
                                               float sx,float cx,float sy,float cy,
                                               u32 color) {
    /* Rotate the 3D vertex around Y, then X; project to Vulkan-style clip
       coordinates. The vertex and fragment shaders still run on VirGL. */
    float rx=x*cy+z*sy;
    float rz= -x*sy+z*cy;
    float ry=y*cx-rz*sx;
    rz=y*sx+rz*cx;
    float camera_z=2.8f-rz;
    if(camera_z<0.35f)camera_z=0.35f;
    float perspective=1.65f/camera_z;
    S3D_VERTEX out{};
    out.x=gpu_fixed16(rx*perspective);
    out.y=gpu_fixed16(ry*perspective);
    out.z=gpu_fixed16(0.45f+rz*0.12f);
    out.color=color;
    return out;
}
static void draw_frame_gpu() {
    if(!gpu_triangle_surface_ready) {
        if(!S3DGetSurface(&gpu_triangle_surface) ||
           gpu_triangle_surface.backend!=2 ||
           gpu_triangle_surface.virgl_buffer_resource_id!=S3D_VIRGL_VERTEX_BUFFER_ID ||
           !gpu_triangle_surface.virgl_buffer ||
           gpu_triangle_surface.virgl_buffer_size<84u) {
            if(!gpu_triangle_error_reported) {
                print("maze.exe: FATAL: VirGL triangle target/vertex buffer unavailable; shader demo cannot run\n");
                gpu_triangle_error_reported=1;
            }
            return;
        }
        gpu_triangle_surface_ready=1;
        print("maze.exe: VirGL GPU triangle target ready; vertex+fragment TGSI shaders enabled\n");
    }
    gpu_triangle_angle+=0.035f;
    const float tau=6.28318530718f;
    while(gpu_triangle_angle>=tau)gpu_triangle_angle-=tau;
    float sy=fsin(gpu_triangle_angle),cy=fcos(gpu_triangle_angle);
    float sx=fsin(gpu_triangle_angle*0.63f),cx=fcos(gpu_triangle_angle*0.63f);
    S3D_VERTEX a=gpu_project_spinning_vertex(-0.9f,-0.68f,0.0f,sx,cx,sy,cy,0xffff6548u);
    S3D_VERTEX b=gpu_project_spinning_vertex( 0.9f,-0.68f,0.0f,sx,cx,sy,cy,0xff45e7ffu);
    S3D_VERTEX c=gpu_project_spinning_vertex( 0.0f, 0.88f,0.0f,sx,cx,sy,cy,0xffa7ff62u);
    if(!S3DDrawTriangleVirGL(&gpu_triangle_surface,&a,&b,&c)) {
        if(!gpu_triangle_error_reported) {
            print("maze.exe: FATAL: VirGL shader triangle submission/presentation failed\n");
            gpu_triangle_error_reported=1;
        }
        return;
    }
    report_frame_rate();
}

static void draw_frame() {
    if(gpu_scene_ready){draw_frame_gpu();return;}
    if(!pixels||!screen_w||!screen_h)return;
    /* Keep the launched fullscreen window visible in a 2D-only VM. This is
       a diagnostic screen, not a CPU-rendered substitute for the 3D scene. */
    draw_gpu_unavailable_screen();
    return;
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
    present_maze_layer();
    report_frame_rate();
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
    /* Bring up a real Vulkan instance, enumerate the software physical device,
       and create a graphics queue. Scene drawing is switched only after a
       pipeline and render target have been created successfully. */
    auto vkCreateInstance=(VkCreateInstance_t)get_proc("vulkan.dll","vkCreateInstance");
    auto vkEnumerateVersion=(VkEnumerateInstanceVersion_t)get_proc("vulkan.dll","vkEnumerateInstanceVersion");
    auto vkGetInstanceProcAddr=(VkGetInstanceProcAddr_t)get_proc("vulkan.dll","vkGetInstanceProcAddr");
    u32 vkVersion=0;
    int vulkan_available=0;
    const char *vk_failure="loader entry points missing";
    if(vkCreateInstance&&vkGetInstanceProcAddr) {
        VkAppInfoMaze app={0,0,"SentinelOS Maze",1,"SentinelOS",1,(1u<<22)};
        VkInstanceInfoMaze ici={1,0,0,&app,0,0,0,0};
        void *instance=0;
        int create_result=vkCreateInstance(&ici,0,&instance);
        if(create_result==0&&instance) {
            vk_failure="instance dispatch functions missing";
            auto vkEnumPhys=(VkEnumeratePhysicalDevices_t)vkGetInstanceProcAddr(instance,"vkEnumeratePhysicalDevices");
            auto vkQueueProps=(VkGetPhysicalDeviceQueueFamilyProperties_t)vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceQueueFamilyProperties");
            auto vkCreateDev=(VkCreateDevice_t)vkGetInstanceProcAddr(instance,"vkCreateDevice");
            auto vkGetQueue=(VkGetDeviceQueue_t)vkGetInstanceProcAddr(instance,"vkGetDeviceQueue");
            vkrt.destroyInstance=(VkDestroyInstance_t)vkGetInstanceProcAddr(instance,"vkDestroyInstance");
            if(vkEnumPhys&&vkQueueProps&&vkCreateDev&&vkGetQueue&&vkrt.destroyInstance) {
                vk_failure="physical-device enumeration failed";
                u32 count=0;
                int enum_result=vkEnumPhys(instance,&count,0);
                if(enum_result==0&&count>0&&count<=8) {
                    void *physical[8]={0}; u32 available=count;
                    vk_failure="physical-device handles unavailable";
                    enum_result=vkEnumPhys(instance,&available,physical);
                    if(enum_result==0&&available>0&&physical[0]) {
                        u32 qcount=0;
                        vk_failure="no graphics queue family";
                        vkQueueProps(physical[0],&qcount,0);
                        VkQueueFamilyMaze qprops[8]={{0}};
                        if(qcount>0&&qcount<=8) {
                            u32 qcap=qcount; vkQueueProps(physical[0],&qcap,qprops);
                            u32 qfamily=0; int found=0;
                            for(u32 qi=0;qi<qcap;qi++) if((qprops[qi].queueFlags&1u)&&qprops[qi].queueCount) {qfamily=qi;found=1;break;}
                            if(found) {
                                float priority=1.0f;
                                VkQueueInfoMaze qci={2,0,0,qfamily,1,&priority};
                                VkDeviceInfoMaze dci={3,0,0,1,&qci,0,0,0,0,0};
                                void *device=0;
                                vk_failure="vkCreateDevice failed";
                                int device_result=vkCreateDev(physical[0],&dci,0,&device);
                                if(device_result==0&&device) {
                                    vkrt.instance=instance;vkrt.physical=physical[0];vkrt.device=device;
                                    vkrt.destroyDevice=(VkDestroyDevice_t)vkGetInstanceProcAddr(instance,"vkDestroyDevice");
                                    vkrt.waitIdle=(VkDeviceWaitIdle_t)vkGetInstanceProcAddr(instance,"vkDeviceWaitIdle");
                                    vk_failure="device queue or required device functions missing";
                                    vkGetQueue(device,qfamily,0,&vkrt.queue);
                                    if(vkrt.queue&&vkrt.destroyDevice&&vkrt.waitIdle) {vkrt.ready=1;vulkan_available=1;}
                                    else { if(vkrt.destroyDevice)vkrt.destroyDevice(device,0);vkrt.device=0; }
                                }
                            }
                        } else vk_failure="invalid graphics queue family count";
                    } else vk_failure="physical-device enumeration failed";
                } else vk_failure="no Vulkan physical device";
            }
            if(!vkrt.ready) {
                if(vkrt.destroyInstance)vkrt.destroyInstance(instance,0);
                vkrt.instance=0;
            }
        } else {
            vk_failure="vkCreateInstance failed";
        }
    }
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
    u64 graphics_info_ok=syscall3(SYS_GRAPHICS_INFO,(u64)&info,0,0);
    if(graphics_info_ok&&info.surface_va&&info.width&&info.height&&info.pitch>=info.width*4) {
        pixels=(u32*)(uintptr_t)info.surface_va;
        screen_w=info.width; screen_h=info.height; pitch=info.pitch/4;
    }
    submit_virgl_stream=(VkSubmitVirGLStream_t)get_proc("vulkan.dll","SentinelVulkanSubmitVirGLStream");
    present_virgl=(VkPresentVirGL_t)get_proc("vulkan.dll","SentinelVulkanPresentVirGL");
    /* GPU-only mode: never silently substitute CPU rasterization. The
       VirGL span renderer submits GPU scissor/clear work and presents the
       GPU target; if the transport is unavailable, report that explicitly. */
    const int virgl_capable=(graphics_info_ok && (info.capabilities&(1u<<4))!=0);
    const int virgl_buffer_ok=(virgl_capable && info.virgl_buffer_va &&
                               info.virgl_buffer_resource_id==S3D_VIRGL_VERTEX_BUFFER_ID &&
                               info.virgl_buffer_size>=84u);
    int virgl_gpu_candidate=virgl_buffer_ok;
    gpu_scene_ready=virgl_gpu_candidate;
    if(gpu_scene_ready) {
        print("maze.exe: GPU-only mode; VirGL shader-driven spinning triangle selected\n");
    } else {
        if(!graphics_info_ok) {
            print("maze.exe: GPU diagnostics: SYS_GRAPHICS_INFO failed; no GPU surface metadata\n");
        } else {
            print("maze.exe: GPU diagnostics: graphics info received; capabilities=");
            print_hex((u64)info.capabilities);
            print(" buffer_va=");print_hex(info.virgl_buffer_va);
            print(" buffer_id=");print_hex((u64)info.virgl_buffer_resource_id);
            print(" buffer_bytes=");print_hex((u64)info.virgl_buffer_size);
            print("\n");
            if(!virgl_capable)
                print("maze.exe: reason: kernel did not initialize a VirGL 3D target (2D/VNC mode or host VirGL unavailable)\n");
            else if(!virgl_buffer_ok)
                print("maze.exe: reason: VirGL is advertised but the shared vertex buffer mapping/size is invalid\n");
        }
    }
    /* Only probe transport after the kernel has positively advertised an
       initialized VirGL context, target, and shared buffer. Do not report a
       skipped probe as a command-submission failure in 2D-only mode. */
    int virgl_submit_ok=0;
    if(virgl_buffer_ok) {
        u32 command_words[1]={0};
        VirglStream stream{};
        virgl_stream_init(&stream,command_words,1);
        if(virgl_stream_command(&stream,VIRGL_CCMD_NOP,VIRGL_OBJECT_NULL,nullptr,0) &&
           virgl_stream_bytes(&stream)==sizeof(u32)) {
            virgl_submit_ok=(syscall3(SYS_SUBMIT_3D,(u64)command_words,sizeof(u32),0)!=0);
        }
        if(!virgl_submit_ok)
            print("maze.exe: reason: VirGL was initialized but its NOP command was rejected\n");
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
        print("maze.exe: FATAL: no direct graphics surface; GPU-only rendering unavailable\n");
    } else {
        draw_frame();
    }
    if(vulkan_available) print(gpu_scene_ready ? "maze.exe: Vulkan device ready; spinning triangle is rendered by VirGL vertex/fragment shaders\n" : "maze.exe: Vulkan device ready, but GPU-only triangle renderer unavailable; no CPU fallback\n");
    else { print("maze.exe: Vulkan runtime probe failed at "); print(vk_failure); print("; independent VirGL triangle path may still run\n"); }
    Message msg{};
    u64 hz=tsc_frequency();
    /* Keep a phase-locked 60 Hz deadline instead of resetting the clock
       to the late wakeup time every frame. This prevents scheduler jitter from
       accumulating into a progressively slower frame cadence. */
    u64 frame_cycles=hz/60ULL;
    if(!frame_cycles)frame_cycles=1;
    u64 last_frame=read_tsc();
    u64 next_frame=last_frame+frame_cycles;
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
        if(now>=next_frame) {
            u64 elapsed=now-last_frame;
            last_frame=now;
            /* Advance from the previous deadline, not from 'now'. If we fell
               far behind, drop missed presentation slots instead of spinning
               through a burst of stale frames. */
            next_frame+=frame_cycles;
            if(now>=next_frame && now-next_frame>=frame_cycles*3ULL)
                next_frame=now+frame_cycles;
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
    if(vkrt.ready) {
        if(vkrt.waitIdle) (void)vkrt.waitIdle(vkrt.device);
        if(vkrt.destroyDevice) vkrt.destroyDevice(vkrt.device,0);
        if(vkrt.destroyInstance) vkrt.destroyInstance(vkrt.instance,0);
        vkrt.ready=0;
    }
    /* Do not leave the desktop pointer clipped/hidden after the game exits. */
    if(clip_cursor) (void)clip_cursor(nullptr);
    if(show_cursor) { for(int i=0;i<8;i++) if(show_cursor(1)>=0) break; }
    (void)syscall3(SYS_TERMINATE_PROCESS,0,0,0);
    for(;;)__asm__ volatile("pause");
}
