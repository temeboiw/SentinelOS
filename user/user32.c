
typedef unsigned int u32;
typedef unsigned int DWORD;
typedef unsigned int UINT;
typedef void *HANDLE;
typedef unsigned short WORD;
typedef unsigned long long u64;
typedef int BOOL;
typedef char *LPSTR;
typedef const char *LPCSTR;
typedef long long i64;

/* Freestanding user DLLs are linked with /nodefaultlib, so provide the
 * C runtime byte-copy primitive locally. Volatile accesses prevent Clang
 * from recognizing this loop and lowering it back into a memcpy call. */
void *memcpy(void *destination, const void *source, unsigned long long count) {
    volatile unsigned char *dst = (volatile unsigned char *)destination;
    const volatile unsigned char *src = (const volatile unsigned char *)source;
    for (unsigned long long i = 0; i < count; ++i) dst[i] = src[i];
    return destination;
}

void *memset(void *destination, int value, unsigned long long count) {
    volatile unsigned char *dst = (volatile unsigned char *)destination;
    for (unsigned long long i = 0; i < count; ++i)
        dst[i] = (unsigned char)value;
    return destination;
}

__attribute__((dllexport)) i64 DefWindowProcA(void *hwnd, unsigned int message, u64 wParam, i64 lParam);
__attribute__((dllexport)) i64 SendMessageA(void *hwnd, unsigned int message, u64 wParam, i64 lParam);
__attribute__((dllexport)) BOOL DestroyWindow(void *hwnd);
__attribute__((dllexport)) int GetWindowTextLengthA(void *hwnd);
__attribute__((dllexport)) BOOL IsWindow(void *hwnd);
static void u32_wide_to_ansi(char *out, unsigned int cap, const unsigned short *in);
static const char *u32_clip_lookup(unsigned int format);

enum {
    SYS_Exit=0, SYS_Write=1, SYS_MapFb=8, SYS_GetInput=10, SYS_GetSystemTime=23,
    SYS_Shutdown=11, SYS_WmReg=15, SYS_WmMin=16,
    SYS_WmUnreg=17, SYS_WmQuery=18, SYS_WmRestore=19,
    SYS_WmRect=20, SYS_CreateProcess=4, SYS_GetTickCount=24, SYS_InitializeGraphics=32, SYS_FlushGraphics=33, SYS_PresentLayer=37, SYS_PresentWindowSurface=40, SYS_NtGetApplicationError=42
};

static u64 sy3(u64 n, u64 a, u64 b, u64 c) {
    u64 r;
    __asm__ volatile(
        "mov %[n], %%rax\n"
        "mov %[a], %%rdi\n"
        "mov %[b], %%rsi\n"
        "mov %[c], %%rdx\n"
        "int $0x80\n"
        "mov %%rax, %[r]"
        : [r]"=r"(r)
        : [n]"r"(n), [a]"r"(a), [b]"r"(b), [c]"r"(c)
        : "rax","rdi","rsi","rdx","memory","cc");
    return r;
}

/* Nonstandard SentinelOS extension for apps that render into a window-local
 * ARGB surface. Dirty coordinates are local to the surface, not screen-space.
 * The kernel translates them using the registered window rectangle. */
typedef struct {
    u64 pixels;
    u32 x, y, w, h, width, height;
} U32_WINDOW_SURFACE_ARGS;

__attribute__((dllexport))
BOOL PresentWindowSurface(const u32 *pixels, u32 width, u32 height,
                          u32 dirty_x, u32 dirty_y,
                          u32 dirty_w, u32 dirty_h) {
    if (!pixels || !width || !height || width > 1920u || height > 1080u ||
        dirty_x >= width || dirty_y >= height || !dirty_w || !dirty_h)
        return 0;
    U32_WINDOW_SURFACE_ARGS args = {
        (u64)pixels, dirty_x, dirty_y, dirty_w, dirty_h, width, height
    };
    return sy3(SYS_PresentWindowSurface, (u64)&args, 0, 0) != 0;
}

typedef struct {
    WORD year, month, day_of_week, day, hour, minute, second, milliseconds;
} U32_SYSTEM_TIME;

static int u32_get_system_time(U32_SYSTEM_TIME *out) {
    return sy3(SYS_GetSystemTime, (u64)out, 0, 0) == 0;
}

typedef struct { u64 va; u32 w, h, pitch; } FBInfo;
typedef struct {
    u32 size, version, backend, capabilities;
    u32 width, height, pitch;
    u32 pci_bus, pci_device, pci_function;
    unsigned short vendor_id, device_id;
    unsigned char pci_class, pci_subclass, pci_prog_if, initialized;
    u64 surface_va;
    u64 virgl_buffer_va;
    u32 virgl_buffer_resource_id, virgl_buffer_size;
} GfxBootInfo;
typedef struct { u32 x, y, w, h; } GfxRect;
typedef struct { int type, x, y, btn; char key; } InputEvent;
typedef struct { int left, top, right, bottom; } U32_RECT;
typedef struct {
    u32 hwnd, message;
    u64 wParam;
    i64 lParam;
    u32 time;
    int pt_x, pt_y;
} MSG;
typedef struct {
    int x, y, w, h;
    char title[64];
    int visible, minimized;
} WINDOW;

static FBInfo g_fb;
static u32 *g_front, *g_scanout, g_pitch, g_fw, g_fh, g_scan_pitch;
static int g_gpu_surface;
/* Z-ordered surfaces: cached background, desktop, window, taskbar, cursor. */
enum { U32_LAYER_BACKGROUND=0, U32_LAYER_DESKTOP=10, U32_LAYER_WINDOW=20, U32_LAYER_TASKBAR=30, U32_LAYER_CURSOR=100 };
#define U32_LAYER_MAX_PIXELS (1920u * 1080u)
static u32 u32_desktop_layer[U32_LAYER_MAX_PIXELS];
static u32 u32_window_layer[U32_LAYER_MAX_PIXELS];
static u32 u32_taskbar_layer[U32_LAYER_MAX_PIXELS];

/* Automatic layer stack. Each frame registers the active surfaces, then the
 * compositor sorts them by z-order instead of relying on hard-coded blend
 * statements. Dirty rectangles are shared by every registered surface. */
typedef struct { u32 *pixels; int z; int visible; const char *name; } U32_LAYER_SURFACE;
#define U32_LAYER_SURFACE_COUNT 3
static U32_LAYER_SURFACE u32_layer_stack[U32_LAYER_SURFACE_COUNT];
static unsigned int u32_layer_stack_count;
static void u32_layers_rebuild(void) {
    u32_layer_stack_count=0;
    u32_layer_stack[u32_layer_stack_count++]=(U32_LAYER_SURFACE){u32_desktop_layer,U32_LAYER_DESKTOP,1,"desktop"};
    u32_layer_stack[u32_layer_stack_count++]=(U32_LAYER_SURFACE){u32_window_layer,U32_LAYER_WINDOW,1,"window"};
    u32_layer_stack[u32_layer_stack_count++]=(U32_LAYER_SURFACE){u32_taskbar_layer,U32_LAYER_TASKBAR,1,"taskbar"};
    for(unsigned int i=1;i<u32_layer_stack_count;i++) {
        U32_LAYER_SURFACE item=u32_layer_stack[i];
        unsigned int j=i;
        while(j>0 && u32_layer_stack[j-1].z>item.z) {
            u32_layer_stack[j]=u32_layer_stack[j-1];
            --j;
        }
        u32_layer_stack[j]=item;
    }
}
/* Optional compositor clip used while moving a window. */
static int u32_render_clip;
/* Bypass ordinary per-window clipping for drag frames so old window bounds
 * are cleared and every child control is rebuilt at its new screen X/Y. */
static int u32_force_full_compose;
static int u32_clip_x0, u32_clip_y0, u32_clip_x1, u32_clip_y1;
/* Cached static Aero wallpaper. Dynamic window/taskbar content is composed over it. */
#define U32_AERO_CACHE_W 1920u
#define U32_AERO_CACHE_H 1080u
static u32 u32_aero_bg_cache[U32_AERO_CACHE_W * U32_AERO_CACHE_H];
static u32 u32_aero_bg_w, u32_aero_bg_h;
static int u32_shared_background_published;
static int g_mx = 640, g_my = 400;
typedef struct { int x,y; unsigned int time; unsigned long long extra; } U32_MOUSEMOVEPOINT;
static U32_MOUSEMOVEPOINT u32_mouse_history[32];
static unsigned int u32_mouse_history_head,u32_mouse_history_count,u32_mouse_time;
typedef struct { unsigned short usagePage,usage; DWORD flags; void *hwndTarget; } U32_RAWINPUTDEVICE;
static U32_RAWINPUTDEVICE u32_raw_devices[16];
typedef struct { DWORD pointerType,pointerId,frameId,pointerFlags; void *sourceDevice,*hwndTarget; struct { int x,y; } ptPixelLocation,ptHimetricLocation,ptPixelLocationRaw,ptHimetricLocationRaw; DWORD dwTime,historyCount; int InputData; DWORD dwKeyStates; unsigned long long PerformanceCount; DWORD ButtonChangeType; } U32_POINTER_INFO;

static void u32_mouse_record(void){unsigned int at=(u32_mouse_history_head+u32_mouse_history_count)%32;if(u32_mouse_history_count==32){u32_mouse_history_head=(u32_mouse_history_head+1)%32;at=(u32_mouse_history_head+u32_mouse_history_count-1)%32;}else u32_mouse_history_count++;u32_mouse_history[at].x=g_mx;u32_mouse_history[at].y=g_my;u32_mouse_history[at].time=++u32_mouse_time;u32_mouse_history[at].extra=0;}

static int g_menu, g_drag, g_drag_ox, g_drag_oy;
static int g_start_hover=-1;
static int g_start_scale_active, g_start_scale_percent=100, g_start_scale_x, g_start_scale_y;
/* A non-desktop process can receive clicks over the shared taskbar. Publish
 * its freshly redrawn taskbar layer only when that process changes taskbar UI;
 * ordinary window repaints must never overwrite the desktop's taskbar/menu. */
static int u32_taskbar_dirty;
/* Window geometry is committed only when the title-bar grab is released. */
static int g_drag_pending_x, g_drag_pending_y, g_drag_moved;
/* Suppress the per-process welcome card while exposing desktop after close. */
static int u32_suppress_desktop_card;
static int g_search_focus;
static char g_search_text[32];
static int g_search_len;
static int cur_on, cur_ox=640, cur_oy=400;
static int u32_show_cursor_count;
/* SetCursor(NULL) explicitly hides the pointer until a non-NULL cursor is set. */
static int u32_cursor_explicit_hidden;
static void u32_repaint_scene_rect(int x0, int y0, int x1, int y1);
static void compose_region(int x0, int y0, int x1, int y1);
static void u32_composite_layers(int x0, int y0, int x1, int y1);
static int u32_is_desktop_window(void);
static void u32_clear_layer(u32 *layer);
static WINDOW g_wnd;
/* Kernel window registration is deferred until ShowWindow for initially hidden HWNDs. */
static int u32_wnd_registered;
static int u32_normal_x, u32_normal_y, u32_normal_w, u32_normal_h, u32_normal_valid;
/* USER32 child HWNDs. Coordinates are client-relative, so moving the parent
 * automatically moves every child without stale screen-space rectangles. */
typedef struct {
    int used, visible, enabled, pressed;
    void *parent;
    unsigned int id, style, exstyle;
    int x, y, w, h;
    char cls[24], text[96];
} U32_CHILDWND;
static U32_CHILDWND u32_child_windows[64];
static int u32_class_name_equal(const char *a,const char *b);
static U32_CHILDWND *u32_child_from_hwnd(void *hwnd) {
    unsigned long long v=(unsigned long long)hwnd;
    if(v<0x1000u || v>=0x1040u)return 0;
    U32_CHILDWND *c=&u32_child_windows[v-0x1000u];
    return c->used?c:0;
}
static void *u32_child_create(LPCSTR cls,LPCSTR title,u32 ex,u32 style,int x,int y,int w,int h,void *parent,void *menu) {
    int slot=-1;
    for(int i=0;i<64;i++)if(!u32_child_windows[i].used){slot=i;break;}
    if(slot<0)return 0;
    U32_CHILDWND *c=&u32_child_windows[slot];
    c->used=1;c->visible=(style&0x10000000u)!=0;c->enabled=(style&0x08000000u)==0;c->pressed=0;
    c->parent=parent;c->id=(unsigned int)(unsigned long long)menu;c->style=style;c->exstyle=ex;
    c->x=x;c->y=y;c->w=w;c->h=h;
    unsigned int i=0;while(cls&&cls[i]&&i+1<sizeof(c->cls)){c->cls[i]=cls[i];i++;}c->cls[i]=0;
    i=0;while(title&&title[i]&&i+1<sizeof(c->text)){c->text[i]=title[i];i++;}c->text[i]=0;
    return (void *)(unsigned long long)(0x1000u+(unsigned int)slot);
}
static void u32_child_destroy_all(void *parent) {
    for(int i=0;i<64;i++)if(u32_child_windows[i].used&&u32_child_windows[i].parent==parent)u32_child_windows[i].used=0;
}
static int u32_damage_intersects(int x0, int y0, int x1, int y1);
static void u32_draw_child_windows(void);
static MSG u32_msg_queue[32];
static unsigned int u32_msg_head, u32_msg_count;
static int u32_msg_time;
static int u32_last_msg_time;
typedef struct { void *hwnd; unsigned long long id; unsigned int interval; unsigned long long next_due; void *callback; int used; } U32_TIMER;
static U32_TIMER u32_timers[16];
static unsigned long long u32_tick_now(void) { u64 t=sy3(SYS_GetTickCount,0,0,0); return t==(u64)-1?0:t; }
static u32 u32_wnd_style, u32_wnd_exstyle, u32_wnd_id;
static void *u32_wnd_proc, *u32_wnd_userdata, *u32_wnd_menu, *u32_wnd_parent;
static unsigned long long u32_context_help_id;
static long u32_class_style; static void *u32_class_proc;
static char u32_wnd_class_name[96];
static long u32_class_extra[2];
static int u32_scroll_min[2],u32_scroll_max[2],u32_scroll_page[2],u32_scroll_pos[2],u32_scroll_visible[2];
static unsigned int u32_double_click_time=500;
static unsigned int u32_text_align;
static int u32_in_send_message;
static void *u32_wnd_region;
typedef struct { void *hwnd; int id; unsigned int mod,key; int used; } U32_HOTKEY;
static U32_HOTKEY u32_hotkeys[16];
typedef struct { unsigned char fVirt; unsigned short key,cmd; } U32_ACCEL;
typedef struct { const U32_ACCEL *entries; int count,used; } U32_ACCELTABLE;
static U32_ACCELTABLE u32_accel_tables[16];
static long long u32_message_extra_info;
static int u32_caret_x,u32_caret_y,u32_caret_w=1,u32_caret_h=16,u32_caret_created,u32_caret_visible=1;
static unsigned char u32_key_state[256]; static int u32_process_dpi_awareness=1; static void *u32_thread_dpi_context=(void *)-2; static DWORD u32_display_affinity,u32_layered_color,u32_layered_flags; static unsigned char u32_layered_alpha; static void *u32_current_cursor=(void *)0x337F00u; static DWORD u32_last_input_time; static int u32_redraw_enabled=1; static unsigned char u32_composition_values[16][64]; static unsigned int u32_composition_sizes[16];
typedef struct { void *hwnd; int setting; DWORD flags,size; unsigned char data[64]; int used; } U32_FEEDBACK_SETTING;
static U32_FEEDBACK_SETTING u32_feedback_settings[16];
typedef struct { void *parent; unsigned int id,value; int checked,used; char text[128]; } U32_DLGCTRL;
static U32_DLGCTRL u32_dlgctrl[32];
static U32_DLGCTRL *u32_dlgctrl_get(void *parent,unsigned int id,int create){int free_slot=-1;for(int i=0;i<32;i++){if(u32_dlgctrl[i].used&&u32_dlgctrl[i].parent==parent&&u32_dlgctrl[i].id==id)return &u32_dlgctrl[i];if(!u32_dlgctrl[i].used&&free_slot<0)free_slot=i;}if(create&&free_slot>=0){U32_DLGCTRL *c=&u32_dlgctrl[free_slot];c->used=1;c->parent=parent;c->id=id;c->value=0;c->checked=0;c->text[0]=0;return c;}return 0;}
/* Renderer telemetry for Phase 1. These counters measure actual USER32
 * composition work (not monitor refresh/vblank); they let Task Manager and
 * graphics tests distinguish redraw pressure from presentation limits. */
typedef struct {
    u64 compose_calls;
    u64 full_composes;
    u64 clipped_composes;
    u64 dirty_pixels;
    u64 gpu_flush_calls;
    u64 last_compose_cycles;
    u64 max_compose_cycles;
    u64 compose_fps;
    u64 sample_tick_ms;
    u64 sample_frames;
} U32_RENDER_STATS;
static U32_RENDER_STATS u32_render_stats;
static u64 u32_compose_sample_frames;
/* DrawTaskbar is a common clock/menu update path. Mark it so an already
 * published desktop can update only the taskbar layer instead of resubmitting
 * unchanged wallpaper, desktop icons/card, and window pixels. */
static int u32_taskbar_only_compose;
static int u32_taskbar_menu_was_open;
static u64 u32_rdtsc(void) {
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}
__attribute__((dllexport)) BOOL GetSentinelRenderStats(U32_RENDER_STATS *out) {
    if (!out) return 0;
    *out = u32_render_stats;
    return 1;
}

static unsigned int u32_caret_blink=530;
static int u32_clip_left,u32_clip_top,u32_clip_right,u32_clip_bottom,u32_clip_active;
static struct {void *hwnd;char name[48];void *data;int used;} u32_props[32];
static int u32_clipboard_open; static unsigned int u32_clipboard_format,u32_clipboard_seq; static void *u32_clipboard_data,*u32_clipboard_owner;
static char g_lines[40][80];
static int g_nlines, g_tb_n, g_tb_pids[8];

static BOOL u32_msg_push(const MSG *msg) {
    if (!msg || u32_msg_count >= 32) return 0;
    unsigned int tail = (u32_msg_head + u32_msg_count) % 32;
    u32_msg_queue[tail] = *msg;
    if (!u32_msg_queue[tail].time) u32_msg_queue[tail].time = (u32)++u32_msg_time;
    u32_msg_count++;
    return 1;
}
static BOOL u32_msg_matches(const MSG *m,void *hwnd,u32 min,u32 max) {
    if(m->message==0x0012u)return 1;
    if(hwnd&&m->hwnd!=(u32)(u64)hwnd)return 0;
    if(min&&m->message<min)return 0;
    if(max&&m->message>max)return 0;
    return 1;
}
static BOOL u32_msg_take_filtered(MSG *msg,void *hwnd,u32 min,u32 max,int remove) {
    for(unsigned int off=0;off<u32_msg_count;off++) {
        unsigned int at=(u32_msg_head+off)%32;MSG *candidate=&u32_msg_queue[at];
        if(!u32_msg_matches(candidate,hwnd,min,max))continue;
        if(msg)*msg=*candidate;u32_last_msg_time=(int)candidate->time;
        if(remove) {
            for(unsigned int j=off;j+1<u32_msg_count;j++)u32_msg_queue[(u32_msg_head+j)%32]=u32_msg_queue[(u32_msg_head+j+1)%32];
            u32_msg_count--;
        }
        return 1;
    }
    return 0;
}
static void u32_pump_timers(void) {
    unsigned long long now=u32_tick_now();
    if(!now)return;
    for(int i=0;i<16;i++) {
        U32_TIMER *t=&u32_timers[i];
        if(!t->used||now<t->next_due)continue;
        MSG m={0};m.hwnd=(u32)(u64)t->hwnd;m.message=0x0113u;m.wParam=t->id;m.lParam=(i64)t->callback;
        m.time=(u32)now;m.pt_x=g_mx;m.pt_y=g_my;
        if(u32_msg_push(&m)) {
            unsigned long long missed=(now-t->next_due)/t->interval;
            t->next_due+=(missed+1ULL)*t->interval;
        } else t->next_due=now+t->interval;
    }
}

/* Frutiger Aero palette: sky blue glass, aquatic cyan, white highlights. */
#define COL_BG       0xFF101318
#define COL_PANEL    0xFF1B2028
#define COL_TITLE    0xFF252A33
#define COL_ACCENT   0xFFFF3B30
#define COL_ACCENT2  0xFF3DDC84
#define COL_WARN     0xFFFFB454
#define COL_DANGER   0xFFFF766E
#define COL_TEXT     0xFFF1F3F6
#define COL_MUTED    0xFF9AA3B1
#define COL_TASK     0xFF252A33
#define COL_TASKLINE 0xFF454B56
#define COL_WHITE    0xFFF1F3F6
#define COL_ICON     0xFFFF3B30

#include "font8x8.h"

static void putpx(int x, int y, u32 col) {
    if (!g_front || x < 0 || y < 0 ||
        (u32)x >= g_fw || (u32)y >= g_fh)
        return;
    if (u32_render_clip &&
        (x < u32_clip_x0 || x >= u32_clip_x1 ||
         y < u32_clip_y0 || y >= u32_clip_y1))
        return;
    g_front[y * g_pitch + x] = col;
}

static void fill(int x, int y, int w, int h, u32 col) {
    if (w <= 0 || h <= 0) return;
    if(g_start_scale_active) {
        int s=g_start_scale_percent;
        x=g_start_scale_x+((x-g_start_scale_x)*s+50)/100;
        y=g_start_scale_y+((y-g_start_scale_y)*s+50)/100;
        w=(w*s+50)/100;h=(h*s+50)/100;
        if(w<1)w=1;if(h<1)h=1;
    }
    int x1=x+w, y1=y+h;
    if (u32_render_clip) {
        if (x < u32_clip_x0) x=u32_clip_x0;
        if (y < u32_clip_y0) y=u32_clip_y0;
        if (x1 > u32_clip_x1) x1=u32_clip_x1;
        if (y1 > u32_clip_y1) y1=u32_clip_y1;
    }
    if (x < 0) x=0;
    if (y < 0) y=0;
    if (x1 > (int)g_fw) x1=(int)g_fw;
    if (y1 > (int)g_fh) y1=(int)g_fh;
    if (x1 <= x || y1 <= y) return;
    for (int yy=y; yy<y1; ++yy)
        for (int xx=x; xx<x1; ++xx)
            g_front[yy*g_pitch+xx]=col;
}

static void panel(int x, int y, int w, int h, u32 f, u32 b) {
    fill(x, y, w, h, f);
    fill(x, y, w, 1, b);
    fill(x, y + h - 1, w, 1, b);
    fill(x, y, 1, h, b);
    fill(x + w - 1, y, 1, h, b);
}

static u32 color_mix(u32 a, u32 b, unsigned int t) {
    unsigned int r=(((a>>16)&255)*(255-t)+((b>>16)&255)*t)/255;
    unsigned int g=(((a>>8)&255)*(255-t)+((b>>8)&255)*t)/255;
    unsigned int bl=((a&255)*(255-t)+(b&255)*t)/255;
    return 0xFF000000u|(r<<16)|(g<<8)|bl;
}
static void gradient_v(int x,int y,int w,int h,u32 top,u32 bottom) {
    if(w<=0||h<=0)return;
    /* Preserve the original gradient coordinate while skipping rows that
     * cannot affect a dirty rectangle. fill() clips horizontally. */
    int first=0,last=h;
    if(u32_render_clip) {
        if(y<u32_clip_y0)first=u32_clip_y0-y;
        if(y+h>u32_clip_y1)last=u32_clip_y1-y;
        if(first<0)first=0;if(last>h)last=h;
    }
    for(int yy=first;yy<last;yy++) {
        u32 c=color_mix(top,bottom,(unsigned int)(yy*255/(h>1?h-1:1)));
        fill(x,y+yy,w,1,c);
    }
}
static void round_blend_pixel(int x,int y,u32 c,unsigned int a) {
    if(!a||!g_front||x<0||y<0||(u32)x>=g_fw||(u32)y>=g_fh)return;
    if(u32_render_clip&&(x<u32_clip_x0||x>=u32_clip_x1||y<u32_clip_y0||y>=u32_clip_y1))return;
    u32 *p=&g_front[y*g_pitch+x],old=*p;
    if(a>=255u){*p=c;return;}
    unsigned int r=(((c>>16)&255u)*a+((old>>16)&255u)*(255u-a))/255u;
    unsigned int g=(((c>>8)&255u)*a+((old>>8)&255u)*(255u-a))/255u;
    unsigned int b=((c&255u)*a+(old&255u)*(255u-a))/255u;
    *p=0xFF000000u|(r<<16)|(g<<8)|b;
}
/* Antialiased circular corners: 4x4 coverage is evaluated only in the corner
 * quadrants; the centre stays a fast horizontal fill. No beveled/diagonal cuts. */
static void round_fill(int x,int y,int w,int h,int r,u32 c) {
    if(w<=0||h<=0)return;
    if(r>w/2)r=w/2;if(r>h/2)r=h/2;if(r<0)r=0;
    int first=0,last=h;
    if(u32_render_clip) {
        if(y<u32_clip_y0)first=u32_clip_y0-y;
        if(y+h>u32_clip_y1)last=u32_clip_y1-y;
        if(first<0)first=0;if(last>h)last=h;
    }
    if(r==0){fill(x,y+first,w,last-first,c);return;}
    for(int yy=first;yy<last;yy++) {
        int corner=(yy<r||yy>=h-r);
        if(!corner){fill(x,y+yy,w,1,c);continue;}
        int cy=yy<r?r:h-r;
        fill(x+r,y+yy,w-2*r,1,c);
        for(int side=0;side<2;side++)for(int xx=0;xx<r;xx++) {
            int px=side?x+w-r+xx:x+xx;
            int cx=side?w-r:r;
            unsigned int hits=0;
            for(int sy=0;sy<4;sy++)for(int sx=0;sx<4;sx++) {
                int dx8=(px-x)*8+sx*2+1-cx*8;
                int dy8=yy*8+sy*2+1-cy*8;
                if((long long)dx8*dx8+(long long)dy8*dy8<=(long long)r*r*64)hits++;
            }
            if(hits)round_blend_pixel(px,y+yy,c,hits*255u/16u);
        }
    }
}
static int u32_isqrt(u64 n) {
    if(!n)return 0;
    u64 x=n, y=(x+1u)/2u;
    while(y<x){x=y;y=(x+n/x)/2u;}
    return (int)x;
}
static void u32_ellipse_fill(int cx,int cy,int rx,int ry,u32 color) {
    if(rx<=0||ry<=0)return;
    u64 rx2=(u64)rx*(u64)rx, ry2=(u64)ry*(u64)ry;
    for(int dy=-ry;dy<=ry;dy++) {
        u64 ay=(u64)(dy<0?-dy:dy);
        if(ay>(u64)ry)continue;
        u64 rem=ry2-ay*ay;
        int span=u32_isqrt((rx2*rem)/ry2);
        fill(cx-span,cy+dy,span*2+1,1,color);
    }
}
static void aero_desktop(void) {
    int h=(int)g_fh,w=(int)g_fw;
    int first_x=0,last_x=w;
    if(u32_render_clip) {
        if(first_x<u32_clip_x0)first_x=u32_clip_x0;
        if(last_x>u32_clip_x1)last_x=u32_clip_x1;
        if(first_x<0)first_x=0;if(last_x>w)last_x=w;
    }
    /* Sentinel industrial desktop: charcoal gradient, fine technical grid,
       and a restrained red signal glow. Render only the damaged region. */
    for(int y=0;y<h;y++) {
        if(u32_render_clip&&(y<u32_clip_y0||y>=u32_clip_y1))continue;
        u32 c=color_mix(0xFF202630,0xFF0B0D11,(unsigned int)(y*110/(h>1?h-1:1)));
        fill(first_x,y,last_x-first_x,1,c);
    }
    int cx=w/2,cy=h/3;
    for(int x=first_x;x<last_x;x++) {
        int dx=x-cx;if(dx<0)dx=-dx;
        if(dx<w/3) {
            u32 strength=(u32)(48-(dx*48/(w/3+1)));
            if(strength) {
                for(int y=0;y<h;y++) {
                    if(u32_render_clip&&(y<u32_clip_y0||y>=u32_clip_y1))continue;
                    if(y>cy-h/3&&y<cy+h/2) {
                        u32 *p=&g_front[(u64)y*g_pitch+(u32)x];
                        *p=color_mix(*p,0xFF35191D,strength);
                    }
                }
            }
        }
        if(x%48==0)fill(x,0,1,h,0xFF29303A);
    }
    for(int y=0;y<h;y++) {
        if(y%48==0)fill(first_x,y,last_x-first_x,1,0xFF29303A);
        if(y%4==0)fill(first_x,y,last_x-first_x,1,0xFF15191F);
    }
    /* Registration marks echo the website's red technical corner details. */
    fill(28,28,42,3,0xFFFF3B30);
    fill(28,31,3,26,0xFFFF3B30);
    if(w>80) {
        fill(w-70,28,42,3,0xFFFF3B30);
        fill(w-31,28,3,26,0xFFFF3B30);
    }
}

#include "font_ttf.h"

static void draw_char(int x, int y, char ch, u32 col) {
    unsigned char code = (unsigned char)ch;
    static const unsigned char latin1[6][8] = {
        /* ä, ö, å, Ä, Ö, Å: 8x8 Latin-1 glyphs */
        { 0x00,0x24,0x00,0x1C,0x02,0x1E,0x22,0x1D },
        { 0x00,0x24,0x00,0x1C,0x22,0x22,0x22,0x1C },
        { 0x08,0x14,0x08,0x1C,0x22,0x3E,0x22,0x22 },
        { 0x24,0x00,0x1C,0x22,0x22,0x3E,0x22,0x22 },
        { 0x24,0x00,0x1C,0x22,0x22,0x22,0x22,0x1C },
        { 0x08,0x14,0x08,0x1C,0x22,0x22,0x3E,0x22 }
    };
    const unsigned char *glyph;
    if (code == 0xE4) glyph = latin1[0];
    else if (code == 0xF6) glyph = latin1[1];
    else if (code == 0xE5) glyph = latin1[2];
    else if (code == 0xC4) glyph = latin1[3];
    else if (code == 0xD6) glyph = latin1[4];
    else if (code == 0xC5) glyph = latin1[5];
    else if (code < 128) glyph = font8x8_basic[code];
    else glyph = font8x8_basic['?'];

    for (int row = 0; row < 8; row++) {
        unsigned char bits = glyph[row];
        for (int colx = 0; colx < 8; colx++) {
            if (bits & (1u << colx))
                putpx(x + colx, y + row, col);
        }
    }
}

static void draw_text(int x, int y, const char *s, u32 col) {
    if (!s) return;
    if(g_start_scale_active) {
        int sc=g_start_scale_percent;
        x=g_start_scale_x+((x-g_start_scale_x)*sc+50)/100;
        y=g_start_scale_y+((y-g_start_scale_y)*sc+50)/100;
    }
    for (; *s; s++) {
        int adv = u32_ttf_font.valid
            ? u32_ttf_draw(&u32_ttf_font,x,y+13,(unsigned char)*s,14,col)
            : -1;
        if (adv < 0) { draw_char(x,y,*s,col); x += 8; }
        else x += adv > 0 ? adv : 1;
    }
}

/* Save-under for fast cursor motion. cur_under holds the framebuffer pixels
 * the cursor covered; cur_paint holds what the cursor wrote (0 = untouched).
 * On a plain mouse move the old cursor is erased by restoring only pixels that
 * still contain the cursor's own colour, so content that another process
 * repainted underneath is never overwritten with stale pixels. */
static const char u32_cursor_shape[16][17] = {
    "#...............",
    "##..............",
    "#w+#............",
    "#ww#............",
    "#www#...........",
    "#wwww#..........",
    "#wwwww#.........",
    "#wwwwww#........",
    "#wwwwwww#.......",
    "#wwwwwwww#......",
    "#wwww#####......",
    "#ww#ww#.........",
    "#w#.#ww#........",
    "##...#ww#.......",
    "#.....#ww#......",
    "......##........"
};

static u32 cur_under[16*16];
static u32 cur_paint[16*16];

static void ensure_fb(void);
static void compose(void);
static void u32_flush_graphics(int x, int y, int w, int h);
/* Cursor and scene must use the same render target. With VirtIO-GPU,
 * g_scanout is the mapped backing surface; the firmware framebuffer is not
 * the active scanout and writing the cursor there makes the pointer vanish. */
static u32 *cursor_target(void) { return g_scanout; }
static u32 cursor_target_pitch(void) { return g_scan_pitch; }
static u32 cursor_target_width(void) { return g_fw; }
static u32 cursor_target_height(void) { return g_fh; }

static void cursor_restore_fast(void) {
    u32 *target=cursor_target();
    u32 pitch=cursor_target_pitch(), width=cursor_target_width(), height=cursor_target_height();
    if (!cur_on || !target || !pitch) return;
    for (int j=0;j<16;j++) for (int i=0;i<16;i++) {
        u32 pv=cur_paint[j*16+i];
        int yy=cur_oy+j, xx=cur_ox+i;
        if (!pv || yy<0 || (u32)yy>=height || xx<0 || (u32)xx>=width) continue;
        if (target[(u32)yy*pitch+(u32)xx]==pv)
            target[(u32)yy*pitch+(u32)xx]=cur_under[j*16+i];
    }
    cur_on=0;
}

/* Erase the pointer footprint from the authoritative scene whenever the
 * pointer is hidden. Restoring saved-under pixels alone can resurrect stale
 * window/menu pixels after another process has repainted the scanout. */
static void cursor_clear_footprint(void) {
    if (!cur_on || !g_scanout) return;
    int x0=cur_ox, y0=cur_oy;
    cursor_restore_fast();
    compose_region(x0,y0,x0+16,y0+16);
}

static void cursor_restore(void) {
    if (!cur_on || !g_scanout) return;

    /*
     * A non-desktop USER32 process does not own the shared Z-ordered scene.
     * u32_repaint_scene_rect() would submit this process's private window
     * layer as shared layer 2, which can overwrite the desktop compositor's
     * authoritative pixels after this window has closed. In that case remove
     * only the cursor pixels we actually painted, using the underlay captured
     * after the last scene presentation. The desktop owner can safely ask the
     * kernel to reconstruct the cursor footprint from its persistent layers.
     */
    if (g_gpu_surface && !u32_is_desktop_window()) {
        cursor_restore_fast();
        return;
    }

    u32_repaint_scene_rect(cur_ox, cur_oy, cur_ox + 16, cur_oy + 16);
    /*
     * Recomposition replaces the cursor footprint with clean scene pixels.
     * Mark the saved-under buffer invalid immediately: keeping cur_on set
     * makes the next paint restore stale pixels captured before composition,
     * which produces trails when the compositor changes beneath the pointer.
     */
    cur_on = 0;
    for (int i=0;i<16*16;i++) cur_paint[i]=0;
}

/* Select a stock cursor from geometry. The arrow remains the original
 * tested bitmap; other shapes are rasterized into the same 16x16 save-under
 * buffer so cursor movement keeps the existing no-trail path. */
static unsigned int u32_hover_cursor_id = 32512u; /* IDC_ARROW */
static unsigned int u32_cursor_effective_id(void) {
    unsigned long long raw=(unsigned long long)u32_current_cursor;
    unsigned int id=(unsigned int)(raw & 0xFFFFu);
    if ((raw & 0xFFFF0000ULL)==0x00330000ULL && id!=32512u)
        return id; /* an application's explicit SetCursor wins */
    return u32_hover_cursor_id;
}
static char u32_cursor_pixel(unsigned int id,int x,int y) {
    if(id==32512u) return u32_cursor_shape[y][x];
    if(id==32513u) { /* IDC_IBEAM */
        if(x==7||x==8||y==2||y==3||y==12||y==13) return '#';
        if((x==7||x==8)&&(y>=4&&y<=11)) return 'w';
        if(y==3&&(x>=5&&x<=10))return 'w';
        if(y==12&&(x>=5&&x<=10))return 'w';
        return '.';
    }
    if(id==32514u || id==32650u) { /* wait / app-starting hourglass */
        if(y==1||y==2||y==13||y==14) return (x>=3&&x<=12)?'#':'.';
        if(y>=3&&y<=12) {
            int d=y<8?y-2:13-y;
            if(x>=3+d&&x<=12-d)return (x==3+d||x==12-d)?'#':'w';
        }
        return '.';
    }
    if(id==32515u) { /* cross */
        if(x==7||x==8||y==7||y==8)return (x==7||y==7)?'#':'w';
        return '.';
    }
    if(id==32516u) { /* up arrow */
        if(y<=7&&x>=7-y/2&&x<=8+y/2)return (x==7-y/2||x==8+y/2)?'#':'w';
        if(y>=7&&y<=14&&x>=6&&x<=9)return (x==6||x==9||y==14)?'#':'w';
        return '.';
    }
    if(id==32644u || id==32645u || id==32646u ||
       id==32642u || id==32643u) {
        int on=0, edge=0;
        if(id==32644u) { /* horizontal resize */
            on=(y==7||y==8||((x<=5||x>=10)&&y>=4&&y<=11&&
                ((x<=5)?(x+y>=7&&x+y<=10):(x-y>=2&&x-y<=5))));
            edge=(y==7||y==8);
        } else if(id==32645u) { /* vertical resize */
            on=(x==7||x==8||((y<=5||y>=10)&&x>=4&&x<=11&&
                ((y<=5)?(x+y>=7&&x+y<=10):(x-y>=2&&x-y<=5))));
            edge=(x==7||x==8);
        } else if(id==32646u) { /* four-way move */
            on=(x==7||x==8||y==7||y==8||
                (x>=4&&x<=11&&y>=4&&y<=11&&
                 ((x==y)||(x+y==15))));
            edge=(x==7||x==8||y==7||y==8);
        } else if(id==32642u) { /* NW-SE resize */
            on=(x==y||x==y+1||x==y-1||
                (x<=5&&y<=5&&x+y>=4&&x+y<=7)||
                (x>=10&&y>=10&&x+y>=23&&x+y<=26));
            edge=(x==y||x==y+1||x==y-1);
        } else { /* NE-SW resize */
            on=(x+y==15||x+y==14||x+y==16||
                (x>=10&&y<=5&&x-y>=5&&x-y<=8)||
                (x<=5&&y>=10&&y-x>=5&&y-x<=8));
            edge=(x+y==15||x+y==14||x+y==16);
        }
        if(on)return edge?'#':'w';
        return '.';
    }
    if(id==32648u) { /* forbidden */
        int dx=x-7,dy=y-7;
        int d=dx*dx+dy*dy;
        if(d>=25&&d<=64)return '#';
        if(dx>=-4&&dx<=4&&dy>=-4&&dy<=4&&
           (dx-dy<=1&&dx-dy>=-1))return 'w';
        return '.';
    }
    if(id==32649u) { /* hand / clickable */
        /* index finger */
        if((x>=6&&x<=8&&y>=1&&y<=8) ||
           (x>=4&&x<=10&&y>=7&&y<=11) ||
           (x>=3&&x<=11&&y>=10&&y<=13) ||
           (x>=5&&x<=10&&y==14) ||
           (x>=3&&x<=5&&y>=8&&y<=11) ||
           (x>=9&&x<=11&&y>=8&&y<=10)) {
            if(x==4||x==10||y==13||y==14|| (x==6&&y==1))return '#';
            return 'w';
        }
        return '.';
    }
    if(id==32651u) { /* help cursor: arrow with question mark */
        if(x<=5&&y<=10&&x+y>=3&&x+y<=7)return '#';
        if((x==9&&y>=3&&y<=5)||(y==3&&x>=9&&x<=12)||
           (x==12&&y>=4&&y<=6)||(y==7&&x>=10&&x<=12)||
           (x==10&&y==8)||(x==10&&y==10))return '#';
        return '.';
    }
    return u32_cursor_shape[y][x];
}
/* Hover targeting is deliberately conservative: only standard controls and
 * window resize/title areas change the pointer; desktop painting is untouched. */
static unsigned int u32_auto_cursor_at(int x,int y) {
    int h=(int)g_fh,w=(int)g_fw;
    if(x<0||y<0||x>=w||y>=h)return 32512u;
    if(g_wnd.visible&&!g_wnd.minimized) {
        int wx=g_wnd.x,wy=g_wnd.y,ww=g_wnd.w,wh=g_wnd.h;
        if(x>=wx-4&&x<wx+ww+4&&y>=wy-4&&y<wy+wh+4) {
            int left=x<wx+4,right=x>=wx+ww-4;
            int top=y<wy+4,bottom=y>=wy+wh-4;
            if((left&&top)||(right&&bottom))return 32642u;
            if((right&&top)||(left&&bottom))return 32643u;
            if(left||right)return 32644u;
            if(top||bottom)return 32645u;
            for(int i=0;i<64;i++) {
                U32_CHILDWND *c=&u32_child_windows[i];
                if(!c->used||!c->visible||c->parent!=(void *)1)continue;
                if(x>=wx+c->x&&x<wx+c->x+c->w&&
                   y>=wy+c->y&&y<wy+c->y+c->h) {
                    if(!c->enabled)return 32648u;
                    if(c->id==0xF020u||c->id==0xF060u||
                       u32_class_name_equal(c->cls,"BUTTON"))return 32649u;
                    if(u32_class_name_equal(c->cls,"EDIT"))return 32513u;
                }
            }
            if(y<wy+32&&x<wx+ww-60)return 32646u;
        }
    }
    /* Keep the arrow visible above the Start surface. The menu is scaled
     * to the current display, so use the exact same scaled hit rectangles
     * as painting/click dispatch instead of fixed 1024x768 coordinates. */
    if(g_menu) return 32512u;
    if(y>=h-44)return 32649u;
    return 32512u;
}

static void cursor_save_draw(int x, int y) {
    /* ShowCursor(FALSE) hides the pointer for this process. Restore any
     * previously painted pixels before returning so no cursor trail remains. */
    if (u32_show_cursor_count < 0 || u32_cursor_explicit_hidden) {
        cursor_clear_footprint();
        cur_on=0;
        return;
    }
    /* CPU-rendered arrow on the actual framebuffer. Transparent pixels do
     * not touch the scene; cursor pixels never enter the GPU layer surface. */
    u32 *target=cursor_target();
    u32 pitch=cursor_target_pitch(), width=cursor_target_width(), height=cursor_target_height();
    if (!target || !pitch || !width || !height) return;
    if (cur_on) cursor_restore_fast();
    cur_ox=x; cur_oy=y;
    for (int j=0;j<16;j++) for (int i=0;i<16;i++) {
        int yy=y+j, xx=x+i, idx=j*16+i;
        char p=u32_cursor_pixel(u32_cursor_effective_id(),i,j);
        u32 val=0;
        cur_paint[idx]=0;
        if (yy<0 || (u32)yy>=height || xx<0 || (u32)xx>=width) continue;
        if (p=='#') val=0xFF000000u;
        else if (p=='w') val=COL_WHITE;
        else if (p=='+') val=COL_ACCENT;
        if (!val) continue;
        cur_under[idx]=target[(u32)yy*pitch+(u32)xx];
        cur_paint[idx]=val;
        target[(u32)yy*pitch+(u32)xx]=val;
    }
    cur_on=1;
}

/* Cheap cursor move: no scene recomposition, so the cursor is never absent
 * for longer than a 16x16 pixel copy while the mouse is moving. */
static void cursor_repaint_transition(int old_x,int old_y,int x,int y) {
    ensure_fb();
    if(!g_scanout)return;
    /* Mouse input may be serviced by the foreground USER32 process after
     * the Explorer shell starts. Keep pointer movement independent of the
     * desktop window title/identity: the input path must always be able to
     * restore the old footprint and paint the current system cursor. Full
     * compose() remains desktop-only for cursor painting to avoid duplicates. */

    /* Repaint the old cursor footprint from the scene, rather than restoring
     * saved-under pixels: other windows/processes may have updated that area
     * while the cursor was there. Then draw the cursor exactly once at its
     * new position and flush that footprint. */
    int rendered_x=cur_ox, rendered_y=cur_oy;
    if (cur_on) {
        if (g_gpu_surface && u32_is_desktop_window()) {
            u32_repaint_scene_rect(rendered_x,rendered_y,rendered_x+16,rendered_y+16);
            cur_on=0;
            for(int i=0;i<16*16;i++)cur_paint[i]=0;
        } else cursor_restore_fast();
    }
    cur_ox=x; cur_oy=y;
    cursor_save_draw(x,y);
    /* Present old and new cursor footprints together. Two separate flushes
       expose a one-frame gap and make the pointer flicker under rapid motion. */
    int x0=rendered_x<x?rendered_x:x, y0=rendered_y<y?rendered_y:y;
    int x1=(rendered_x>x?rendered_x:x)+16, y1=(rendered_y>y?rendered_y:y)+16;
    if(x0<0)x0=0;if(y0<0)y0=0;
    if(x1>(int)g_fw)x1=(int)g_fw;if(y1>(int)g_fh)y1=(int)g_fh;
    if(x1>x0&&y1>y0)u32_flush_graphics(x0,y0,x1-x0,y1-y0);
    (void)old_x; (void)old_y;
}

static void ensure_fb(void) {
    if (g_scanout) return;
    if (!sy3(SYS_MapFb, (u64)&g_fb, 0, 0) || !g_fb.va) return;

    g_scanout = (u32 *)g_fb.va;
    g_fw = g_fb.w; g_fh = g_fb.h;
    g_scan_pitch = g_fb.pitch / 4u;
    g_gpu_surface = 0;

    /* Match sentlogon.exe: USER32 must render into the active GPU backing
     * surface when VirtIO-GPU is selected, not the inactive firmware buffer. */
    GfxBootInfo gfx = {0};
    if (sy3(SYS_InitializeGraphics, (u64)&gfx, 0, 0) &&
        gfx.backend == 2 && gfx.surface_va &&
        gfx.width && gfx.height && gfx.pitch >= gfx.width * 4u) {
        g_scanout = (u32 *)(u64)gfx.surface_va;
        g_fw = gfx.width;
        g_fh = gfx.height;
        g_scan_pitch = gfx.pitch / 4u;
        g_gpu_surface = 1;
    }

    g_front = g_scanout;
    g_pitch = g_scan_pitch;
    u32_ttf_load();
}

/* Flush raw rectangles without re-entering cursor drawing. */
static void u32_flush_raw(int x, int y, int w, int h) {
    if (!g_gpu_surface || w <= 0 || h <= 0) return;
    int x0=x<0?0:x, y0=y<0?0:y, x1=x+w, y1=y+h;
    if (x1>(int)g_fw) x1=(int)g_fw;
    if (y1>(int)g_fh) y1=(int)g_fh;
    if (x1<=x0 || y1<=y0) return;
    GfxRect rect={(u32)x0,(u32)y0,(u32)(x1-x0),(u32)(y1-y0)};
    (void)sy3(SYS_FlushGraphics,(u64)&rect,0,0);
    u32_render_stats.gpu_flush_calls++;
}

static void u32_flush_graphics(int x, int y, int w, int h) {
    /* Flush only scene pixels here. Drawing the cursor from inside a generic
     * flush used to save cursor pixels as their own background, then save
     * them again on the next flush; once the compositor changed even one
     * pixel, the equality-based restore left a growing rectangular trail.
     * Cursor movement and cursor repaint are handled explicitly below. */
    u32_flush_raw(x,y,w,h);
}

static void u32_clear_layer(u32 *layer) {
    if (!layer || g_fw>1920u || g_fh>1080u) return;
    u32 x0=0,y0=0,x1=g_fw,y1=g_fh;
    if (u32_render_clip) {
        if (u32_clip_x0>0)x0=(u32)u32_clip_x0;
        if (u32_clip_y0>0)y0=(u32)u32_clip_y0;
        if (u32_clip_x1>=0 && (u32)u32_clip_x1<x1)x1=(u32)u32_clip_x1;
        if (u32_clip_y1>=0 && (u32)u32_clip_y1<y1)y1=(u32)u32_clip_y1;
    }
    if(x0>x1)x0=x1; if(y0>y1)y0=y1;
    for(u32 y=y0;y<y1;y++) {
        /* Clear each damaged row sequentially rather than recomputing
         * y*width+x for every pixel. Keep the clip bounds unchanged. */
        u32 *p=&layer[y*g_fw+x0];
        for(u32 x=x0;x<x1;x++)*p++=0;
    }
}

static u32 u32_alpha_over(u32 dst,u32 src) {
    u32 a=src>>24;
    if(!a)return dst; if(a==255u)return src;
    u32 ia=255u-a;
    u32 r=(((src>>16)&255u)*a+((dst>>16)&255u)*ia)/255u;
    u32 g=(((src>>8)&255u)*a+((dst>>8)&255u)*ia)/255u;
    u32 b=((src&255u)*a+(dst&255u)*ia)/255u;
    return 0xFF000000u|(r<<16)|(g<<8)|b;
}

/* BACKGROUND < registered Z-ordered surfaces < CURSOR. */
static void u32_composite_layers(int x0,int y0,int x1,int y1) {
    if(!g_scanout||g_fw>1920u||g_fh>1080u)return;
    if(x0<0)x0=0;if(y0<0)y0=0;
    if(x1>(int)g_fw)x1=(int)g_fw;if(y1>(int)g_fh)y1=(int)g_fh;
    if(x0>=x1||y0>=y1)return;

    u32_layers_rebuild();
    int desktop_owner=u32_is_desktop_window();
    /* Filter the stable layer descriptors once per rectangle, not once per
     * pixel. This keeps visibility/pointer checks out of the hot inner loop. */
    u32 *visible_layers[U32_LAYER_SURFACE_COUNT];
    unsigned int visible_layer_count=0;
    if(desktop_owner) {
        for(unsigned int li=0;li<u32_layer_stack_count;li++) {
            U32_LAYER_SURFACE *layer=&u32_layer_stack[li];
            if(layer->visible&&layer->pixels)
                visible_layers[visible_layer_count++]=layer->pixels;
        }
    }
    for(int y=y0;y<y1;y++) {
        /* Advance row pointers once; avoid repeated stride multiplication in
         * the inner pixel loop, which is the compositor's hottest path. */
        u32 at=(u32)y*g_fw+(u32)x0;
        u32 *dst=&g_scanout[(u32)y*g_scan_pitch+(u32)x0];
        u32 *window_row=&u32_window_layer[at];
        u32 *background_row=(u32_aero_bg_w==g_fw&&u32_aero_bg_h==g_fh)
            ? &u32_aero_bg_cache[at] : 0;
        u32 *layer_row[U32_LAYER_SURFACE_COUNT];
        for(unsigned int li=0;li<visible_layer_count;li++)
            layer_row[li]=&visible_layers[li][at];
        for(int x=x0;x<x1;x++,dst++,at++,window_row++) {
            /*
             * Only the desktop owner may rebuild pixels from its wallpaper
             * and desktop surfaces. Other processes preserve shared scanout
             * and blend only their own window surface.
             */
            u32 c;
            if(desktop_owner) {
                c=background_row?*background_row++:0xFF000000u;
                for(unsigned int li=0;li<visible_layer_count;li++)
                    c=u32_alpha_over(c,*layer_row[li]++);
            } else {
                c=*dst;
                c=u32_alpha_over(c,*window_row);
            }
            *dst=c;
        }
    }
    u32_flush_graphics(x0,y0,x1-x0,y1-y0);
}

static void u32_repaint_scene_rect(int x0,int y0,int x1,int y1) {
    if(!g_scanout)return;
    if(x0<0)x0=0;if(y0<0)y0=0;
    if(x1>(int)g_fw)x1=(int)g_fw;if(y1>(int)g_fh)y1=(int)g_fh;
    if(x0>=x1||y0>=y1)return;
    if(g_gpu_surface) {
        /* The kernel owns the shared GPU scanout. Re-submit only the already
         * rendered layer covering the cursor's old footprint; the kernel then
         * reconstructs the underlay from its persistent Z-ordered buffers.
         * Never restore wallpaper directly over a window or taskbar. */
        typedef struct { u64 pixels; u32 layer,x,y,w,h,width,height; } U32_PRESENT_LAYER;
        int desktop_owner=u32_is_desktop_window();
        U32_PRESENT_LAYER a;
        a.x=(u32)x0;a.y=(u32)y0;a.w=(u32)(x1-x0);a.h=(u32)(y1-y0);
        a.width=g_fw;a.height=g_fh;
        if (desktop_owner) {
            /*
             * Rebuild the old cursor rectangle in the same Z order as the
             * real scene. Presenting only the taskbar layer here painted
             * taskbar pixels over windows and left stale cursor/save-under
             * pixels behind when the pointer crossed layer boundaries.
             */
            a.layer=1;a.pixels=(u64)u32_desktop_layer;
            (void)sy3(SYS_PresentLayer,(u64)&a,0,0);
            a.layer=2;a.pixels=(u64)u32_window_layer;
            (void)sy3(SYS_PresentLayer,(u64)&a,0,0);
            a.layer=3;a.pixels=(u64)u32_taskbar_layer;
            (void)sy3(SYS_PresentLayer,(u64)&a,0,0);
        } else {
            a.layer=2;a.pixels=(u64)u32_window_layer;
            (void)sy3(SYS_PresentLayer,(u64)&a,0,0);
        }
        return;
    }
    if(u32_aero_bg_w!=g_fw||u32_aero_bg_h!=g_fh)return;
    for(int y=y0;y<y1;y++) {
        u32 *dst=&g_scanout[(u32)y*g_scan_pitch+(u32)x0];
        const u32 *src=&u32_aero_bg_cache[(u32)y*g_fw+(u32)x0];
        for(int x=x0;x<x1;x++)*dst++=*src++;
    }
    u32_composite_layers(x0,y0,x1,y1);
}

static int hit(int x, int y, int x0, int y0, int x1, int y1) {    return x >= x0 && x < x1 && y >= y0 && y < y1;
}

/* Shared Start-menu geometry for drawing, pointer hover, and hit testing. */
typedef struct {
    int x, y, scale, width, height;
} U32_START_LAYOUT;

static U32_START_LAYOUT u32_start_layout(void) {
    U32_START_LAYOUT l;
    int w=(int)g_fw, h=(int)g_fh;
    int ty=h-48;
    int sx=w*100/1024, sy=h*100/768;
    int sc=sx<sy?sx:sy;
    if(sc>100)sc=100;
    if(sc<62)sc=62;
    l.x=8;
    l.y=ty-(360*sc+50)/100-4;
    if(l.y<4)l.y=4;
    l.scale=sc;
    l.width=300;
    l.height=360;
    return l;
}

static int u32_start_hover_at(int x, int y) {
    U32_START_LAYOUT l=u32_start_layout();
    int sc=l.scale;
    x=l.x+((x-l.x)*100)/sc;
    y=l.y+((y-l.y)*100)/sc;
    if(hit(x,y,l.x+12,l.y+60,l.x+l.width-12,l.y+92)) return 0;
    if(hit(x,y,l.x+12,l.y+116,l.x+144,l.y+164)) return 1;
    if(hit(x,y,l.x+154,l.y+116,l.x+288,l.y+164)) return 2;
    if(hit(x,y,l.x+12,l.y+170,l.x+144,l.y+218)) return 3;
    if(hit(x,y,l.x+12,l.y+258,l.x+144,l.y+298)) return 4;
    if(hit(x,y,l.x+154,l.y+258,l.x+288,l.y+298)) return 5;
    if(hit(x,y,l.x+12,l.y+310,l.x+288,l.y+356)) return 6;
    return -1;
}

static int u32_menu_text_is(const char *wanted) {
    const char *a=g_search_text,*b=wanted;
    while(*a&&*b) {
        char ca=*a++,cb=*b++;
        if(ca>='A'&&ca<='Z')ca=(char)(ca+'a'-'A');
        if(cb>='A'&&cb<='Z')cb=(char)(cb+'a'-'A');
        if(ca!=cb)return 0;
    }
    return *a==0&&*b==0;
}

static void u32_menu_launch(const char *name) {
    u64 result=sy3(SYS_CreateProcess,(u64)name,0,0);
    if(!result||result==(u64)-1) {
        static const char error[]="Start menu: application launch failed\\n";
        sy3(SYS_Write,1,(u64)error,sizeof(error)-1);
    }
}

static void submit_search(void) {
    g_search_text[g_search_len]=0;
    static char cmd_name[]="cmd.exe";
    static char taskmgr_name[]="taskmgr.exe";
    static char files_name[]="fileexplorer.exe";
    g_menu=0;g_search_focus=0;
    if(u32_menu_text_is("cmd")||u32_menu_text_is("cmd.exe"))
        u32_menu_launch(cmd_name);
    else if(u32_menu_text_is("taskmgr")||u32_menu_text_is("taskmgr.exe")||
            u32_menu_text_is("task manager"))
        u32_menu_launch(taskmgr_name);
    else if(u32_menu_text_is("file explorer")||u32_menu_text_is("explorer")||
            u32_menu_text_is("fileexplorer.exe"))
        u32_menu_launch(files_name);
    else if(g_search_len>0) {
        static const char message[]="Start menu: no match; try cmd, taskmgr, or file explorer\\n";
        sy3(SYS_Write,1,(u64)message,sizeof(message)-1);
    }
}

/*
 * Draw the taskbar and Start menu.
 */
/* Convert the UTC-backed kernel clock to Europe/Helsinki. Finland uses
 * EET (UTC+2) in winter and EEST (UTC+3) between the last Sundays of March
 * and October; the DST transition instants are 01:00 UTC. */
static int u32_weekday(int y,int m,int d) {
    static const int offsets[12]={0,3,2,5,0,3,5,1,4,6,2,4};
    if(m<3)y--;
    return (y+y/4-y/100+y/400+offsets[m-1]+d)%7;
}
static int u32_month_days(int y,int m) {
    static const int days[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(m==2 && ((y%4==0 && y%100!=0)||y%400==0))return 29;
    return (m>=1&&m<=12)?days[m-1]:30;
}
static void u32_helsinki_time(U32_SYSTEM_TIME *t) {
    if(!t || t->month<1 || t->month>12 || t->day<1 || t->day>31)return;
    int march_last=31-u32_weekday(t->year,3,31);
    int october_last=31-u32_weekday(t->year,10,31);
    int summer=0;
    if(t->month>3 && t->month<10)summer=1;
    else if(t->month==3 && (t->day>march_last ||
            (t->day==march_last && t->hour>=1)))summer=1;
    else if(t->month==10 && (t->day<october_last ||
            (t->day==october_last && t->hour<1)))summer=1;
    t->hour=(unsigned short)(t->hour+(summer?3:2));
    if(t->hour>=24) {
        t->hour=(unsigned short)(t->hour-24);
        if(++t->day>u32_month_days(t->year,t->month)) {
            t->day=1;
            if(++t->month>12){t->month=1;++t->year;}
        }
        t->day_of_week=(unsigned short)u32_weekday(t->year,t->month,t->day);
    }
}

static void draw_taskbar(void) {
    int h = (int)g_fh;
    int w = (int)g_fw;
    int ty = h - 48;
    if (ty < 0) ty = 0;

    /* Rounded smoked-glass taskbar with restrained Sentinel red signal accents. */
    fill(0, ty-3, w, 3, 0x553B9CC6);
    round_fill(0, ty, w, h-ty, 14, 0xD91A1E25);
    round_fill(0, ty+14, w, h-ty-14, 10, 0xD91A1E25);
    gradient_v(0,ty+1,w,13,0xD91A1E25,0xCC222731);
    fill(10, ty+1, w-20, 1, 0xFFE9ECF2);
    fill(0, ty+2, w, 1, 0xAAFF3B30);

    /* Rounded Sentinel red Start tile. */
    round_fill(5,ty+5,68,38,12,0xB8080A0D);
    round_fill(5,ty+4,68,37,12,0xFFFF3B30);
    gradient_v(7,ty+6,64,17,0xFFFF766E,0xFFDB3028);
    fill(7,ty+22,64,17,0xFFFF3B30);
    fill(14,ty+14,8,7,0xFFE9ECF2); fill(24,ty+13,8,8,0xFFE9ECF2);
    fill(14,ty+23,8,7,0xFFE9ECF2); fill(24,ty+23,8,8,0xFFE9ECF2);

    /* Subtle red glass separator, consistent with Sentinel chrome. */
    fill(78,ty+9,1,h-ty-18,0x66FF3B30);

    g_tb_n = 0;
    int ax = 54;
    int right_limit = w - 108;
    for (int i = 0; i < 8; i++) {
        char name[40];
        int minv = 0;
        u64 pid = sy3(SYS_WmQuery, (u64)i, (u64)name, (u64)&minv);
        if (!pid) break;
        g_tb_pids[g_tb_n++] = (int)pid;

        int bw = 110;
        if (ax + bw > right_limit) break;
        round_fill(ax,ty+4,bw,h-ty-8,10,minv?0x88464D58:0xCC282E38);
        round_fill(ax+1,ty+5,bw-2,h-ty-10,9,minv?0x88464D58:0xDD20252D);
        if (!minv) {
            gradient_v(ax+3,ty+5,bw-6,9,0xFFE9ECF2,0x88FF766E);
            fill(ax+7,ty+3,bw-14,2,0xFFFF3B30);
        }
        draw_text(ax + 9, ty + 19, name[0] ? name : "App", 0xFFE9ECF2);
        ax += bw + 2;
    }

    /* Clock and date come from the RTC-backed kernel system-time service. */
    round_fill(w > 104 ? w - 104 : 0, ty + 5, w > 104 ? 99 : w, h - ty - 10, 10, 0x88323843);
    fill(w > 104 ? w-100 : 1,ty+7,w>104?94:w-2,1,0x66FF3B30);
    if (w >= 108) {
        U32_SYSTEM_TIME now;
        char clock_text[6] = {'-', '-', ':', '-', '-', 0};
        char date_text[11] = {'-', '-', '-', '-', '-', '-', '-', '-', '-', '-', 0};
        if (u32_get_system_time(&now)) {
            u32_helsinki_time(&now);
            clock_text[0] = (char)('0' + (now.hour / 10) % 10);
            clock_text[1] = (char)('0' + now.hour % 10);
            clock_text[3] = (char)('0' + (now.minute / 10) % 10);
            clock_text[4] = (char)('0' + now.minute % 10);
            date_text[0] = (char)('0' + (now.year / 1000) % 10);
            date_text[1] = (char)('0' + (now.year / 100) % 10);
            date_text[2] = (char)('0' + (now.year / 10) % 10);
            date_text[3] = (char)('0' + now.year % 10);
            date_text[4] = '-';
            date_text[5] = (char)('0' + (now.month / 10) % 10);
            date_text[6] = (char)('0' + now.month % 10);
            date_text[7] = '-';
            date_text[8] = (char)('0' + (now.day / 10) % 10);
            date_text[9] = (char)('0' + now.day % 10);
        }
        draw_text(w - 40, ty + 10, clock_text, 0xFFE9ECF2);
        draw_text(w - 68, ty + 29, date_text, 0xFFB7BEC9);
    }

    if (g_menu) {
        int mw=300,mh=360,mx=8;
        int ssx=(int)g_fw*100/1024,ssy=(int)g_fh*100/768;
        g_start_scale_percent=ssx<ssy?ssx:ssy;
        if(g_start_scale_percent>100)g_start_scale_percent=100;
        if(g_start_scale_percent<62)g_start_scale_percent=62;
        int my=ty-(mh*g_start_scale_percent+50)/100-4;if(my<4)my=4;
        g_start_scale_x=mx;g_start_scale_y=my;g_start_scale_active=1;
        /* Keep all menu geometry proportional to the available screen. */
        /* Start surface uses the same rounded charcoal panels and signal-red accents as the website. */
        round_fill(mx+4,my+5,mw,mh,12,0xB8080A0D);
        round_fill(mx,my,mw,mh,11,0xFF454B56);
        round_fill(mx+1,my+1,mw-2,mh-2,16,0xFF1B2028);
        fill(mx+2,my+2,mw-4,1,0xFFE9ECF2);
        fill(mx,my,4,mh,0xFFFF3B30);
        gradient_v(mx+5,my+1,mw-6,54,0xFFE9ECF2,0xFF242A33);
        fill(mx+12,my+12,30,30,0xFFFF3B30);
        fill(mx+18,my+18,7,7,0xFFE9ECF2);fill(mx+27,my+18,7,7,0xFFE9ECF2);
        fill(mx+18,my+27,7,7,0xFFE9ECF2);fill(mx+27,my+27,7,7,0xFFE9ECF2);
        draw_text(mx+52,my+16,"SENTINELOS",0xFFF1F3F6);
        draw_text(mx+52,my+32,"YOUR WORKSPACE",0xFF9AA3B1);
        panel(mx+12,my+60,mw-24,32,0xFF252A33,g_search_focus?0xFFFF3B30:0xFF414854);
        round_fill(mx+22,my+69,9,9,5,0xFF9AA3B1);
        round_fill(mx+24,my+71,5,5,3,0xFF252A33);
        fill(mx+30,my+78,5,2,0xFF9AA3B1);
        if(g_search_len>0) {
            g_search_text[g_search_len]=0;
            draw_text(mx+42,my+72,g_search_text,COL_TEXT);
        } else draw_text(mx+42,my+72,"Search...",0xFF9AA3B1);
        draw_text(mx+16,my+105,"PINNED",0xFFF1F3F6);
        draw_text(mx+mw-66,my+105,"APPS",0xFFFF766E);
        round_fill(mx+12,my+116,132,48,7,g_start_hover==1?0xFF422326:0xFF292E37);
        if(g_start_hover==1)fill(mx+12,my+116,132,2,0xFFFF3B30);
        round_fill(mx+18,my+125,25,25,4,0xFFFF3B30);
        fill(mx+23,my+131,15,3,0xFFE9ECF2);fill(mx+23,my+138,15,3,0xFFE9ECF2);
        draw_text(mx+50,my+135,"Files",0xFFF1F3F6);
        round_fill(mx+154,my+116,134,48,7,g_start_hover==2?0xFF422326:0xFF292E37);
        if(g_start_hover==2)fill(mx+154,my+116,134,2,0xFFFF3B30);
        round_fill(mx+160,my+125,25,25,4,0xFF3DDC84);
        fill(mx+165,my+130,15,2,0xFFE9ECF2);fill(mx+165,my+136,15,2,0xFFE9ECF2);fill(mx+165,my+142,15,2,0xFFE9ECF2);
        draw_text(mx+192,my+135,"Task Mgr",0xFFF1F3F6);
        round_fill(mx+12,my+170,132,48,7,g_start_hover==3?0xFF422326:0xFF292E37);
        if(g_start_hover==3)fill(mx+12,my+170,132,2,0xFFFF3B30);
        round_fill(mx+18,my+179,25,25,4,0xFF454B56);
        draw_text(mx+24,my+188,"C:",0xFFF1F3F6);
        draw_text(mx+50,my+189,"Command",0xFFF1F3F6);
        draw_text(mx+16,my+236,"POWER OPTIONS",0xFF9AA3B1);
        fill(mx+12,my+248,mw-24,1,0xFF414854);
        round_fill(mx+12,my+258,132,40,7,g_start_hover==4?0xFF422326:0xFF292E37);
        if(g_start_hover==4)fill(mx+12,my+258,132,2,0xFFFF3B30);
        draw_text(mx+24,my+274,"Shut down",0xFFFF766E);
        round_fill(mx+154,my+258,134,40,7,g_start_hover==5?0xFF422326:0xFF292E37);
        if(g_start_hover==5)fill(mx+154,my+258,134,2,0xFFFF3B30);
        draw_text(mx+166,my+274,"Restart",0xFFFFB454);
        fill(mx+12,my+310,mw-24,1,0xFF414854);
        if(g_start_hover==6)round_fill(mx+10,my+316,mw-20,24,4,0xFF422326);
        draw_text(mx+16,my+329,"Settings / Help",0xFFF1F3F6);
        g_start_scale_active=0;
    }
}

static int u32_is_desktop_window(void) {
    static const char desktop[] = "Desktop";
    int i=0;
    while (desktop[i] && g_wnd.title[i] == desktop[i]) i++;
    return desktop[i] == 0 && g_wnd.title[i] == 0;
}

static void u32_draw_child_windows(void) {
    for(int i=0;i<64;i++) {
        U32_CHILDWND *c=&u32_child_windows[i];
        if(!c->used||!c->visible||c->parent!=(void *)1||g_wnd.minimized)continue;
        int x=g_wnd.x+c->x,y=g_wnd.y+c->y,w=c->w,h=c->h;
        /* Child controls are retained in their layer. A dirty repaint outside
         * a control's bounds only needs to clear/recompose the damaged pixels;
         * rasterizing that control again would add work without changing output. */
        if (!u32_damage_intersects(x-1,y-1,x+w+1,y+h+1)) continue;
        if(c->id==0xF020u) {
            round_fill(x,y,w,h,5,0xFF2B83A8);
            gradient_v(x+1,y+1,w-2,11,0xFFB9F4FC,0xFF55B7D9);
            fill(x+6,y+11,w-12,3,0xFF174F6A);
        } else if(c->id==0xF060u) {
            round_fill(x,y,w,h,5,0xFFB74F69);
            gradient_v(x+1,y+1,w-2,10,0xFFFFD4DD,0xFFE56D83);
            fill(x+7,y+7,2,10,0xFFFFFFFF);fill(x+15,y+7,2,10,0xFFFFFFFF);
            fill(x+7,y+7,10,2,0xFFFFFFFF);fill(x+7,y+15,10,2,0xFFFFFFFF);
        } else {
            round_fill(x,y,w,h,4,0xFF559EBB);
            round_fill(x+1,y+1,w-2,h-2,3,c->pressed?0xFFD5EDF5:0xFFF5FCFF);
            draw_text(x+8,y+(h>18?(h-16)/2:1),c->text,0xFF164E68);
        }
    }
}

static int u32_damage_intersects(int x0, int y0, int x1, int y1) {
    if (!u32_render_clip) return 1;
    return x1 > u32_clip_x0 && x0 < u32_clip_x1 &&
           y1 > u32_clip_y0 && y0 < u32_clip_y1;
}

static void compose(void) {
    if(!u32_redraw_enabled)return;
    ensure_fb();
    if (!g_front) return;
    u64 compose_start_cycles=u32_rdtsc();
    u32_render_stats.compose_calls++;
    if (u32_render_clip) u32_render_stats.clipped_composes++;
    else u32_render_stats.full_composes++;
    u64 sample_now=u32_tick_now();
    if (sample_now && !u32_render_stats.sample_tick_ms)
        u32_render_stats.sample_tick_ms=sample_now;
    if (sample_now && u32_render_stats.sample_tick_ms &&
        sample_now-u32_render_stats.sample_tick_ms>=1000ULL) {
        u32_render_stats.compose_fps=u32_compose_sample_frames;
        u32_compose_sample_frames=0;
        u32_render_stats.sample_tick_ms=sample_now;
    }
    u32_compose_sample_frames++;

    /* A shared-scanout repaint must remove the previous CPU cursor before
     * any window layer is submitted. Otherwise a partial kernel composition
     * can leave the old cursor in scanout, and the next cursor_save_draw()
     * saves cursor pixels as its own underlay. Rebuild that footprint through
     * the kernel's persistent Z-ordered surfaces first. */
    /* The cursor belongs to the desktop compositor, not each process's
     * private USER32 instance. An app repaint must never restore or repaint
     * the shared screen-space pointer using that app's private layer state. */
    if (g_gpu_surface && cur_on && u32_is_desktop_window())
        cursor_restore();

    /* A forced drag redraw must also discard any clip inherited from an
     * earlier paint. Otherwise the parent can move vertically while its child
     * buttons are still clipped to the old Y range. Restore the caller's clip
     * after the complete frame has been composed. */
    int force_saved_clip=0, force_saved_x0=u32_clip_x0, force_saved_y0=u32_clip_y0;
    int force_saved_x1=u32_clip_x1, force_saved_y1=u32_clip_y1;
    if(u32_force_full_compose) {
        force_saved_clip=u32_render_clip;
        u32_render_clip=0;
    }

    /* Do not erase the cursor before painting. Keep it over the scene while
     * layers are recomposed; cursor_save_draw() refreshes its saved-under
     * pixels after the final frame is ready. */
    /* The layer descriptors and their backing arrays are process-local USER32
     * globals, but every process maps the same scanout. If an ordinary app
     * composites its private desktop/window layers, it replaces pixels owned
     * by Explorer and other apps with its own stale copy. Only the desktop
     * owner may run the full-screen layered compositor; app paints are clipped
     * to that app's window and go straight to scanout. */
    int layered = g_fw<=1920u && g_fh<=1080u;
    int layer_x0=0,layer_y0=0,layer_x1=(int)g_fw,layer_y1=(int)g_fh;

    /* Ordinary app redraws are clipped to their own window. The desktop
     * process owns full-screen composition; explicit dirty clips are kept. */
    int auto_clip = 0;
    int saved_x0=u32_clip_x0, saved_y0=u32_clip_y0;
    int saved_x1=u32_clip_x1, saved_y1=u32_clip_y1;
    if (!u32_render_clip && !u32_force_full_compose && g_wnd.visible && !g_wnd.minimized &&
        g_wnd.w > 2 && !u32_is_desktop_window()) {
        u32_render_clip=1;
        u32_clip_x0=g_wnd.x-6; u32_clip_y0=g_wnd.y-6;
        u32_clip_x1=g_wnd.x+g_wnd.w+10;
        u32_clip_y1=g_wnd.y+g_wnd.h+14;
        auto_clip=1;
    } else if (!u32_render_clip && !u32_force_full_compose && u32_is_desktop_window() &&
               u32_aero_bg_w == g_fw && u32_aero_bg_h == g_fh) {
        /* When a desktop-owned app window is visible, its content updates
         * must not repaint the whole lower desktop. Rebuild only that window's
         * bounds; the taskbar outside this clip is persistent and unchanged. */
        u32_render_clip=1;
        if (g_wnd.visible && !g_wnd.minimized && g_wnd.w>2 && g_wnd.h>2) {
            u32_clip_x0=g_wnd.x-6; u32_clip_y0=g_wnd.y-6;
            u32_clip_x1=g_wnd.x+g_wnd.w+10;
            u32_clip_y1=g_wnd.y+g_wnd.h+14;
        } else {
            /* The welcome/search card can change while no window is open.
             * Keep the existing safe desktop-region fallback for that case. */
            u32_clip_x0=0; u32_clip_y0=60;
            u32_clip_x1=(int)g_fw; u32_clip_y1=(int)g_fh;
        }
        auto_clip=1;
    }

    if (g_wnd.visible) {
        for (int i = 0; i < 8; i++) {
            char name[40];
            int minv = 0;

            u64 pid = sy3(
                SYS_WmQuery, (u64)i, (u64)name, (u64)&minv
            );            if (!pid) break;

            int same = 1;

            for (int k = 0; name[k] || g_wnd.title[k]; k++) {
                if (name[k] != g_wnd.title[k]) {
                    same = 0;
                    break;
                }
            }

            if (same) {
                g_wnd.minimized = minv;
                break;
            }
        }
    }

    /* Stage 1: generate the expensive gradient/water wallpaper only when
     * the framebuffer dimensions change. Later compositions restore its
     * cached pixels before drawing dynamic UI on top. Oversized modes retain
     * the original rendering path rather than writing past the cache. */
    if (g_fw > 0 && g_fh > 0 &&
        g_fw <= U32_AERO_CACHE_W && g_fh <= U32_AERO_CACHE_H) {
        if (u32_aero_bg_w != g_fw || u32_aero_bg_h != g_fh) {
            /* Cache creation must always render the entire wallpaper, even
             * when the caller requested a small dirty-region repaint. */
            /* Build this process' wallpaper cache off-screen: starting an app
             * must not repaint the live desktop with its private cache. */
            u32 *saved_front = g_front;
            u32 saved_pitch = g_pitch;
            int was_clipped = u32_render_clip;
            g_front = u32_aero_bg_cache;
            g_pitch = g_fw;
            u32_render_clip = 0;
            aero_desktop();
            g_front = saved_front;
            g_pitch = saved_pitch;
            u32_aero_bg_w = g_fw;
            u32_aero_bg_h = g_fh;
            u32_render_clip = was_clipped;

            /* The desktop's first composition must replace the login screen
             * with the real desktop wallpaper. App processes build private
             * caches too, but must never blast their cache over the desktop. */
            if (u32_is_desktop_window()) {
                for (u32 y=0; y<g_fh; ++y)
                    for (u32 x=0; x<g_fw; ++x)
                        g_front[y*g_pitch+x] = u32_aero_bg_cache[y*g_fw+x];
            }
        } else {
            u32 x0=0,y0=0,x1=g_fw,y1=g_fh;
            if (u32_render_clip) {
                x0=(u32)u32_clip_x0; y0=(u32)u32_clip_y0;
                x1=(u32)u32_clip_x1; y1=(u32)u32_clip_y1;
                if (x0>g_fw)x0=g_fw;if(y0>g_fh)y0=g_fh;
                if (x1>g_fw)x1=g_fw;if(y1>g_fh)y1=g_fh;
            }
            /* Layered mode composites over the cached wallpaper in one pass
             * at the end (u32_composite_layers); writing wallpaper into the
             * live framebuffer here made every repaint visibly blank the
             * region until the slow layer rendering finished. */
            if (!layered) {
                for (u32 y=y0; y<y1; ++y)
                    for (u32 x=x0; x<x1; ++x)
                        g_front[y * g_pitch + x] = u32_aero_bg_cache[y * g_fw + x];
            }
        }
    } else {
        aero_desktop();
    }

    if(layered){g_front=u32_desktop_layer;g_pitch=g_fw;u32_clear_layer(u32_desktop_layer);}
    /* Skip fixed desktop artwork entirely when a dirty repaint cannot touch it. */
    if (u32_damage_intersects(12,10,194,55)) {
        /* Floating glass identity plaque. */
        round_fill(12,10,182,45,9,0xAFFFFFFF);
        round_fill(14,12,178,41,8,0xEAFBFFFF);
        fill(22,13,162,1,0xFFFFFFFF);
        draw_text(24, 17, "SentinelOS", 0xFF1C6587);
        draw_text(24, 33, "A calmer desktop", 0xFF477F95);
    }

    if (u32_damage_intersects(31,72,122,147)) {
        int ix = 36, iy = 72, s = 48;

        round_fill(ix + 5, iy + 7, s, s, 10, 0x66324D67);
        round_fill(ix, iy, s, s, 10, 0xFF0B78A8);
        gradient_v(ix+2,iy+2,s-4,20,0xFFB9F7FF,0xFF42B7DF);
        round_fill(ix+9,iy+24,s-18,6,3,0xFFFFFFFF);
        round_fill(ix+9,iy+34,s-18,5,2,0xFFD5F8FF);
        fill(ix+9,iy+43,s-22,3,0xFF8AE5F6);
        round_fill(ix-5,iy+s+7,86,20,5,0xBFFFFFFF);
        draw_text(ix-1, iy+s+10, "Explorer", 0xFF164B68);
    }

    /* Desktop welcome/search card, populated by explorer.exe through TextOutA. */
    if (!g_wnd.visible && !u32_suppress_desktop_card &&
        u32_damage_intersects(132,72,526,230)) {
        int cx=132, cy=72, cw=390, ch=158;
        round_fill(cx+4,cy+7,cw,ch,13,0x55314F62);
        round_fill(cx,cy,cw,ch,13,0xBFFFFFFF);
        round_fill(cx+2,cy+2,cw-4,ch-4,11,0xEAF9FDFF);
        gradient_v(cx+3,cy+3,cw-6,35,0xFFFFFFFF,0xFFC4F1FA);
        fill(cx+8,cy+3,cw-16,1,0xFFFFFFFF);
        draw_text(cx+18,cy+11,g_lines[4][0]?g_lines[4]:"Welcome to SentinelOS",0xFF145777);
        draw_text(cx+18,cy+35,g_lines[5][0]?g_lines[5]:"A fresh breeze for your desktop",0xFF4E8297);
        draw_text(cx+18,cy+68,g_lines[7][0]?g_lines[7]:"Search apps below: type cmd and press Enter",0xFF245E78);
        round_fill(cx+14,cy+99,cw-28,38,8,0xFFB2DDE9);
        round_fill(cx+16,cy+101,cw-32,34,7,0xFFF9FEFF);
        fill(cx+18,cy+102,cw-36,1,0xFFFFFFFF);
        draw_text(cx+27,cy+111,g_lines[8][0]?g_lines[8]:"Search: ",0xFF276A84);
    }

    if(layered){g_front=u32_window_layer;g_pitch=g_fw;u32_clear_layer(u32_window_layer);}
    if (g_wnd.visible && !g_wnd.minimized) {
        int x = g_wnd.x, y = g_wnd.y;
        int w = g_wnd.w, h = g_wnd.h;

        /* Borderless popup/fullscreen windows must not inherit SentinelOS
         * Aero chrome. Keep them as managed windows; do not destroy the HWND. */
        /* Only WS_POPUP explicitly requests borderless rendering.
         * Missing WS_CAPTION alone is common for ordinary tool/dialog
         * windows and must not remove their Aero frame. */
        int u32_borderless = (u32_wnd_style & 0x80000000u) != 0;
        if (!u32_borderless) {
            /* Soft shadow, rounded charcoal frame, Sentinel red caption accents. */
            round_fill(x+5,y+8,w,h,18,0xB8080A0D);
            round_fill(x,y,w,h,16,0xFF454B56);
            round_fill(x+1,y+1,w-2,h-2,15,0xFF1B2028);
            round_fill(x+2,y+2,w-4,42,14,0xFF252A33);
            fill(x+2,y+24,w-4,18,0xFF252A33);
            gradient_v(x+3,y+3,w-6,13,0xFF343A45,0xFF252A33);
            fill(x+4,y+3,w-8,1,0xFFE9ECF2);
            fill(x+3,y+43,w-6,1,0xFFE9ECF2);
            draw_text(x + 15, y + 14, g_wnd.title, 0xFFF1F3F6);
            /* Render the standard caption controls as part of the retained
             * window frame, independent of child-HWND clipping or app menus. */
            if(w>=112) {
                int bx=x+w-56, by=y+5;
                round_fill(bx,by,23,23,8,0xFF303641);
                gradient_v(bx+1,by+1,21,10,0xFF515966,0xFF303641);
                fill(bx+6,by+15,11,2,0xFFE9ECF2);
                bx=x+w-29;
                round_fill(bx,by,23,23,8,0xFFFF3B30);
                gradient_v(bx+1,by+1,21,10,0xFFFF766E,0xFFDB3028);
                fill(bx+6,by+6,2,11,0xFFE9ECF2);
                fill(bx+15,by+6,2,11,0xFFE9ECF2);
                fill(bx+6,by+6,11,2,0xFFE9ECF2);
                fill(bx+6,by+15,11,2,0xFFE9ECF2);
            }
            round_fill(x+5,y+47,w-10,h-53,12,0xFF242A33);
            round_fill(x+9,y+51,w-18,h-57,10,0xFF1B2028);
        } else {
            /* No title bar, border, or rounded inset in popup/borderless mode. */
            fill(x,y,w,h,0xFF000000);
        }

        if (!u32_borderless) u32_draw_child_windows();

        for (int i = 0; i < g_nlines; i++) {
            const char *s = g_lines[i];
            int tx = x + 14;
            int ty = y + 44 + i * 16;
            u32 ic = 0;

            /* Most dirty rectangles only touch a few text rows. Avoid scanning
             * and decoding every line when its glyphs cannot reach this region. */
            if (u32_render_clip &&
                (ty + 12 <= u32_clip_y0 || ty >= u32_clip_y1))
                continue;

            if (s[0] == '[' && s[1] == 'D' && s[2] == 'I')
                ic = COL_WARN;
            else if (s[0] == '[' && s[1] == 'E')
                ic = COL_ACCENT2;            else if (s[0] == '[' && s[1] == 'D' && s[2] == 'L')
                ic = COL_ACCENT;
            else if (s[0] == '[')
                ic = COL_MUTED;

            if (ic) {
                fill(tx, ty, 12, 12, ic);
                fill(tx + 2, ty + 2, 8, 3, COL_WHITE);
                tx += 18;

                while (*s && *s != ' ') s++;
                if (*s == ' ') s++;
            }

            draw_text(tx, ty + 2, s, COL_TEXT);
        }
    }

    if(layered){g_front=u32_taskbar_layer;g_pitch=g_fw;u32_clear_layer(u32_taskbar_layer);}
    /* Window text/animation updates above the taskbar should not rasterize
     * the entire Start/taskbar UI on every composition. Draw it only for a
     * full frame or when the dirty rectangle actually reaches the taskbar.
     * The layer is still cleared inside the damaged rectangle, so transparent
     * taskbar-layer pixels remain correct for windows overlapping its edge. */
    int taskbar_top=(int)g_fh-48;
    if(taskbar_top<0)taskbar_top=0;
    int taskbar_in_damage=!u32_render_clip ||
        (u32_clip_y1>taskbar_top && u32_clip_y0<(int)g_fh &&
         u32_clip_x1>0 && u32_clip_x0<(int)g_fw);
    if(taskbar_in_damage) draw_taskbar();
    if(u32_render_clip){layer_x0=u32_clip_x0;layer_y0=u32_clip_y0;layer_x1=u32_clip_x1;layer_y1=u32_clip_y1;}
    if(layered){g_front=g_scanout;g_pitch=g_scan_pitch;if(!g_gpu_surface)u32_composite_layers(layer_x0,layer_y0,layer_x1,layer_y1);}
    if(layered&&g_gpu_surface) {
        typedef struct { u64 pixels; u32 layer,x,y,w,h,width,height; } U32_PRESENT_LAYER;
        int px0=layer_x0,py0=layer_y0,px1=layer_x1,py1=layer_y1;
        if(px0<0)px0=0;if(py0<0)py0=0;
        if(px1>(int)g_fw)px1=(int)g_fw;if(py1>(int)g_fh)py1=(int)g_fh;
        if(px1>px0&&py1>py0) {
            U32_PRESENT_LAYER a;
            a.x=(u32)px0;a.y=(u32)py0;a.w=(u32)(px1-px0);a.h=(u32)(py1-py0);a.width=g_fw;a.height=g_fh;
            if(u32_is_desktop_window()) {
                /* A taskbar-only pass is safe only after the kernel has the
                 * persistent background. On the first frame, publish every
                 * layer so boot/login pixels cannot be left underneath. */
                int taskbar_only_present =
                    u32_taskbar_only_compose && u32_shared_background_published;
                if(!u32_shared_background_published) {
                    a.layer=0;a.pixels=(u64)u32_aero_bg_cache;
                    a.x=0;a.y=0;a.w=g_fw;a.h=g_fh;
                    u32_shared_background_published=(int)sy3(SYS_PresentLayer,(u64)&a,0,0);
                    a.x=(u32)px0;a.y=(u32)py0;a.w=(u32)(px1-px0);a.h=(u32)(py1-py0);
                }
                /* Wallpaper is immutable after its first full publish. Do not
                 * copy the same background buffer on every ordinary repaint.
                 * Clock and Start-menu updates touch only the taskbar surface. */
                if(!taskbar_only_present) {
                    a.layer=1;a.pixels=(u64)u32_desktop_layer;(void)sy3(SYS_PresentLayer,(u64)&a,0,0);
                    a.layer=2;a.pixels=(u64)u32_window_layer;(void)sy3(SYS_PresentLayer,(u64)&a,0,0);
                }
                a.layer=3;a.pixels=(u64)u32_taskbar_layer;
                (void)sy3(SYS_PresentLayer,(u64)&a,0,0);
                u32_taskbar_dirty=0;
            } else {
                a.layer=2;a.pixels=(u64)u32_window_layer;(void)sy3(SYS_PresentLayer,(u64)&a,0,0);
                /* A click may be delivered to an application USER32 instance
                 * rather than Desktop. Publish its Start-menu/taskbar change
                 * explicitly; ordinary app paints must not overwrite layer 3. */
                if(u32_taskbar_dirty) {
                    a.layer=3;a.pixels=(u64)u32_taskbar_layer;
                    if(sy3(SYS_PresentLayer,(u64)&a,0,0))
                        u32_taskbar_dirty=0;
                }
            }
            /* Cursor is rendered exactly once below, after every layer is
             * submitted. Drawing it here as well caused duplicate save-under
             * cycles and was a source of stale pixels/trails. */
        }
    }

    if (auto_clip) {
        u32_render_clip=0;
        u32_clip_x0=saved_x0; u32_clip_y0=saved_y0;
        u32_clip_x1=saved_x1; u32_clip_y1=saved_y1;
    }
    /* Keep the cursor as the final screen-space overlay, after every window,
     * taskbar, Start-menu and partial repaint. Re-saving is safe when the
     * layered path already painted it: cursor_save_draw first restores the
     * previous footprint and then captures the newly composed underlay. */
    g_front=g_scanout; g_pitch=g_scan_pitch;
    u32_render_clip=0;
    /* Do not overwrite cur_ox/cur_oy here: they identify the old cursor's
     * saved-under rectangle. cursor_save_draw restores that rectangle first,
     * then records the new pointer position and paints the final overlay. */
    /* On the shared GPU scanout only the desktop owner may draw the
     * system cursor. App-local compose() calls otherwise leave stale copies
     * of the pointer behind when their window contents are repainted. */
    if (!g_gpu_surface || u32_is_desktop_window()) {
        cursor_save_draw(g_mx,g_my);
        if(g_scanout && g_scan_pitch && g_fw && g_fh)
            u32_flush_raw(g_mx,g_my,16,16);
    }
    if(u32_force_full_compose) {
        u32_render_clip=force_saved_clip;
        u32_clip_x0=force_saved_x0; u32_clip_y0=force_saved_y0;
        u32_clip_x1=force_saved_x1; u32_clip_y1=force_saved_y1;
    }
    u64 compose_end_cycles=u32_rdtsc();
    u32_render_stats.last_compose_cycles=compose_end_cycles-compose_start_cycles;
    if (u32_render_stats.last_compose_cycles>u32_render_stats.max_compose_cycles)
        u32_render_stats.max_compose_cycles=u32_render_stats.last_compose_cycles;
    u64 dx0=layer_x0<0?0:(u64)layer_x0, dy0=layer_y0<0?0:(u64)layer_y0;
    u64 dx1=layer_x1>(int)g_fw?g_fw:(u64)layer_x1;
    u64 dy1=layer_y1>(int)g_fh?g_fh:(u64)layer_y1;
    if (dx1>dx0 && dy1>dy0)
        u32_render_stats.dirty_pixels+=(dx1-dx0)*(dy1-dy0);
}

/* Recompose only a damaged screen rectangle. The static Aero wallpaper is
 * restored from cache in this region, not regenerated across the framebuffer. */
static void compose_region(int x0, int y0, int x1, int y1) {
    int was_clipped = u32_render_clip;
    int ox0 = u32_clip_x0, oy0 = u32_clip_y0;
    int ox1 = u32_clip_x1, oy1 = u32_clip_y1;
    if (was_clipped) {
        if (ox0 < x0) x0 = ox0;
        if (oy0 < y0) y0 = oy0;
        if (ox1 > x1) x1 = ox1;
        if (oy1 > y1) y1 = oy1;
    }
    /* Normalize damage before entering the compositor. Negative or oversized
     * rectangles are common at screen edges during window drags; clipping once
     * here avoids carrying invalid bounds through every drawing primitive. */
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)g_fw) x1 = (int)g_fw;
    if (y1 > (int)g_fh) y1 = (int)g_fh;
    if (x1 <= x0 || y1 <= y0) return;
    u32_render_clip = 1;
    u32_clip_x0 = x0; u32_clip_y0 = y0;
    u32_clip_x1 = x1; u32_clip_y1 = y1;
    compose();
    u32_render_clip = was_clipped;
    u32_clip_x0 = ox0; u32_clip_y0 = oy0;
    u32_clip_x1 = ox1; u32_clip_y1 = oy1;
}

static void show_shutdown_screen(int reboot) {
    ensure_fb();
    if (!g_front || !g_fw || !g_fh) return;
    cursor_restore();
    gradient_v(0,0,(int)g_fw,(int)g_fh,0xFF071725,0xFF123F5C);
    int cx=(int)g_fw/2, cy=(int)g_fh/2;
    round_fill(cx-190,cy-72,380,144,14,0xE8193448);
    round_fill(cx-188,cy-70,376,140,12,0xFF102D42);
    fill(cx-174,cy-52,348,1,0xFF4BA6C7);
    draw_text(cx-142,cy-22,reboot?"Restarting SentinelOS":"Shutting down SentinelOS",0xFFFFFFFF);
    draw_text(cx-142,cy+12,reboot?"Your system is restarting...":"Your system is shutting down...",0xFF9CD9EB);

    /*
     * This screen is drawn directly into the shared scanout, bypassing the
     * layered compositor. On VirtIO-GPU those CPU writes are not visible
     * until the dirty rectangle is transferred and flushed to the host.
     * Flush the complete screen before starting the five-second hold.
     */
    u32_flush_raw(0,0,(int)g_fw,(int)g_fh);

    /* Keep the shutdown screen visible before handing control to firmware. */
    u64 start=sy3(SYS_GetTickCount,0,0,0);
    if (start!=(u64)-1) {
        for (;;) {
            u64 now=sy3(SYS_GetTickCount,0,0,0);
            if (now!=(u64)-1 && now>=start && now-start>=5000) break;
            __asm__ volatile("pause");
        }
    } else {
        for (volatile u64 i=0;i<100000000ULL;i++) __asm__ volatile("pause");
    }
}

static void on_click(int x, int y) {
    int h = (int)g_fh;
    int ty = h - 44;

    if (g_wnd.visible && !g_wnd.minimized) {
        int wx = g_wnd.x, wy = g_wnd.y;
        int ww = g_wnd.w;
        if(!(u32_wnd_style&0x80000000u) && ww>=112 &&
           y>=wy+5 && y<wy+28) {
            if(x>=wx+ww-29 && x<wx+ww-5) {
                SendMessageA((void *)1,0x0010u,0,0); /* WM_CLOSE */
                DestroyWindow((void *)1);
                return;
            }
            if(x>=wx+ww-56 && x<wx+ww-32) {
                g_wnd.minimized=1;
                (void)sy3(SYS_WmMin,1,0,0);
                compose();
                return;
            }
        }
        U32_CHILDWND *clicked_child=0;
        for(int ci=0;ci<64;ci++) {
            U32_CHILDWND *cc=&u32_child_windows[ci];
            if(!cc->used||!cc->visible||!cc->enabled||cc->parent!=(void *)1)continue;
            if(hit(x,y,wx+cc->x,wy+cc->y,wx+cc->x+cc->w,wy+cc->y+cc->h)){clicked_child=cc;break;}
        }

        if (clicked_child && clicked_child->id==0xF060u) {
            /*
             * Closing a window is not the same as terminating its process.
             * Give the application its normal WM_CLOSE notification first,
             * then enforce the close if the app does not destroy the window.
             * This makes the title-bar X work for every USER32 window and
             * avoids killing the scheduler's current process from input code.
             */
            SendMessageA((void *)1, 0x0010u, 0, 0); /* WM_CLOSE */

            /*
             * A window procedure may handle WM_CLOSE without calling
             * DefWindowProc, or may mark the window hidden without removing
             * its registered window. Always run USER32's final teardown
             * after the notification so the X button cannot leave a stale
             * window in the system window list.
             */
            DestroyWindow((void *)1);
            return;
        }

        if (clicked_child && clicked_child->id==0xF020u) {
            g_wnd.minimized = 1;
            sy3(SYS_WmMin, 1, 0, 0);
            compose();
            return;
        }

        if(clicked_child) {
            SendMessageA((void *)1,0x0111u,clicked_child->id,
                (i64)(unsigned long long)(0x1000u+(unsigned int)(clicked_child-u32_child_windows)));
            return;
        }

        if (!clicked_child && hit(x, y, wx, wy, wx + ww - 60, wy + 32)) {
            g_drag = 1;
            g_drag_ox = x - wx;
            g_drag_oy = y - wy;
            g_drag_pending_x = wx;
            g_drag_pending_y = wy;
            g_drag_moved = 0;
            return;
        }
    }

    if (y >= ty) {
        int ax=90;
        for(int i=0;i<g_tb_n;i++) {
            int bw=110;
            if(hit(x,y,ax,ty+6,ax+bw,ty+38)) {
                sy3(SYS_WmRestore,(u64)g_tb_pids[i],0,0);
                if(g_wnd.visible) {
                    g_wnd.minimized=0;
                    sy3(SYS_WmMin,0,0,0);
                }
                compose();return;
            }
            ax+=bw+6;
        }
        if(hit(x,y,8,ty+6,78,ty+38)) {
            U32_START_LAYOUT menu_layout=u32_start_layout();
            int damage_y=g_menu?menu_layout.y:(ty-368);
            u32_taskbar_dirty=1;
            g_menu=!g_menu;g_search_focus=0;g_start_hover=-1;
            if(g_menu) {
                g_search_len=0;g_search_text[0]=0;
                menu_layout=u32_start_layout();
                damage_y=menu_layout.y;
            }
            if(damage_y<0)damage_y=0;
            compose_region(0,damage_y,(int)g_fw,(int)g_fh);
            return;
        }
    }

    if(g_menu) {
        int mw=300,mh=360,mx=8;
        int ssx=(int)g_fw*100/1024,ssy=(int)g_fh*100/768;
        int sc=ssx<ssy?ssx:ssy;if(sc>100)sc=100;if(sc<62)sc=62;
        int my=ty-(mh*sc+50)/100-4;if(my<4)my=4;
        x=mx+((x-mx)*100)/sc;y=my+((y-my)*100)/sc;
        if(hit(x,y,mx+12,my+60,mx+mw-12,my+92)) {
            g_search_focus=1;
            u32_taskbar_dirty=1;compose_region(0,my,(int)g_fw,(int)g_fh);return;
        }
        if(hit(x,y,mx+12,my+116,mx+144,my+164)) {
            static char files_name[]="fileexplorer.exe";
            g_menu=0;g_search_focus=0;u32_taskbar_dirty=1;
            compose_region(0,my,(int)g_fw,(int)g_fh);u32_menu_launch(files_name);return;
        }
        if(hit(x,y,mx+154,my+116,mx+288,my+164)) {
            static char taskmgr_name[]="taskmgr.exe";
            g_menu=0;g_search_focus=0;u32_taskbar_dirty=1;
            compose_region(0,my,(int)g_fw,(int)g_fh);u32_menu_launch(taskmgr_name);return;
        }
        if(hit(x,y,mx+12,my+170,mx+144,my+218)) {
            static char cmd_name[]="cmd.exe";
            g_menu=0;g_search_focus=0;u32_taskbar_dirty=1;
            compose_region(0,my,(int)g_fw,(int)g_fh);u32_menu_launch(cmd_name);return;
        }
        if(hit(x,y,mx+12,my+258,mx+144,my+298)) {
            g_menu=0;g_search_focus=0;u32_taskbar_dirty=1;
            compose_region(0,my,(int)g_fw,(int)g_fh);
            show_shutdown_screen(0);sy3(SYS_Shutdown,0,0,0);return;
        }
        if(hit(x,y,mx+154,my+258,mx+288,my+298)) {
            g_menu=0;g_search_focus=0;u32_taskbar_dirty=1;
            compose_region(0,my,(int)g_fw,(int)g_fh);
            show_shutdown_screen(1);sy3(SYS_Shutdown,1,0,0);return;
        }
        if(hit(x,y,mx+12,my+310,mx+288,my+356)) {
            g_search_focus=0;g_search_len=0;g_search_text[0]=0;
            static const char info[]="SentinelOS Settings: environment and display controls are being added.\n";
            sy3(SYS_Write,1,(u64)info,sizeof(info)-1);return;
        }
        g_menu=0;g_search_focus=0;g_start_hover=-1;u32_taskbar_dirty=1;
        compose_region(0,my,(int)g_fw,(int)g_fh);return;
    }
    g_menu=0;
}

__attribute__((dllexport))
void *CreateWindowExA(
    u32 ex, LPCSTR cls, LPCSTR title, u32 style,
    int x, int y, int w, int h,
    void *parent, void *menu, void *inst, void *param
) {
    (void)inst; (void)param;
    /* Child controls are genuine USER32 HWNDs, not screen-space decorations. */
    if(parent && parent==(void *)1 && cls &&
       (u32_class_name_equal(cls,"BUTTON") || u32_class_name_equal(cls,"STATIC") ||
        u32_class_name_equal(cls,"EDIT") || u32_class_name_equal(cls,"SCROLLBAR"))) {
        return u32_child_create(cls,title,ex,style,x,y,w,h,parent,menu);
    }
    u32_child_destroy_all((void *)1);
    /* A process has one top-level HWND in the current compact USER32 model,
     * but it must still use the registered class procedure. Clearing this to
     * NULL made CreateWindowEx appear to succeed while silently skipping the
     * application's WM_NCCREATE/WM_CREATE initialization (many games then
     * never initialized their renderer or first frame). */
    u32_wnd_registered=0;
    { unsigned int ci=0; while(cls && (u64)cls>0xFFFFu && cls[ci] && ci+1<sizeof(u32_wnd_class_name)){u32_wnd_class_name[ci]=cls[ci];ci++;} u32_wnd_class_name[ci]=0; }
    u32_wnd_style=style; u32_wnd_exstyle=ex; u32_wnd_id=0;
    u32_wnd_proc=u32_class_proc; u32_wnd_userdata=0; u32_wnd_menu=menu; u32_wnd_parent=parent;
    u32_context_help_id=0;

    ensure_fb();
    if (!g_front || !g_fw || !g_fh) return 0;

    /* Resolve CW_USEDEFAULT and reject unusable geometry before registering
     * the window with the kernel manager. Clamp normal windows to the active
     * display while leaving explicit popup/full-screen geometry intact. */
    if ((unsigned int)x==0x80000000u) x=40;
    if ((unsigned int)y==0x80000000u) y=40;
    if ((unsigned int)w==0x80000000u || w<=0) w=640;
    if ((unsigned int)h==0x80000000u || h<=0) h=400;
    if (w<80) w=80;
    if (h<60) h=60;
    if (w>(int)g_fw) w=(int)g_fw;
    if (h>(int)g_fh) h=(int)g_fh;
    if (x<0) x=0;
    if (y<0) y=0;
    if (x+w>(int)g_fw) x=(int)g_fw-w;
    if (y+h>(int)g_fh) y=(int)g_fh-h;

    g_wnd.x=x; g_wnd.y=y; g_wnd.w=w; g_wnd.h=h;
    g_wnd.visible=(style&0x10000000u)!=0;
    if (!(style&0x10000000u) && (style&0x80000000u)==0)
        g_wnd.visible=0;
    g_wnd.minimized=0;

    int i=0;
    if(title)for(;title[i]&&i<63;i++)g_wnd.title[i]=title[i];
    g_wnd.title[i]=0;
    g_nlines=0; g_menu=0;  g_search_focus=0; g_search_len=0; g_drag=0;

    /* Send the mandatory creation notifications before publishing a visible
     * window. Applications can allocate their renderer/resources in WM_CREATE.
     * WM_NCCREATE returning FALSE aborts creation; WM_CREATE returning -1 does
     * likewise, matching the Win32 contract. */
    typedef i64 (*U32_CREATEPROC)(void *,u32,u64,i64);
    typedef struct {
        void *create_param,*instance,*menu,*parent;
        int cy,cx,y,x;
        long style;
        LPCSTR name,cls;
        u32 exstyle;
    } U32_CREATESTRUCTA;
    U32_CREATESTRUCTA create_info={
        param,inst,menu,parent,h,w,y,x,(long)style,
        title?title:"",cls?cls:"",ex
    };
    U32_CREATEPROC create_proc=(U32_CREATEPROC)u32_wnd_proc;
    if(create_proc) {
        if(!create_proc((void *)1,0x0081u,0,(i64)&create_info)) {
            u32_child_destroy_all((void *)1);
            g_wnd.visible=0;
            return 0;
        }
        if(create_proc((void *)1,0x0001u,0,(i64)&create_info)==-1) {
            u32_child_destroy_all((void *)1);
            g_wnd.visible=0;
            return 0;
        }
    }

    if (g_wnd.visible) {
        (void)sy3(SYS_WmReg,0,(u64)g_wnd.title,0);
        u32_wnd_registered=1;
        int rect[4]={g_wnd.x,g_wnd.y,g_wnd.w,g_wnd.h};
        (void)sy3(SYS_WmRect,(u64)rect,0,0);
        /* Caption controls are painted and hit-tested by USER32 itself. */
        if(create_proc) {
            (void)create_proc((void *)1,0x0018u,1,0); /* WM_SHOWWINDOW */
            (void)create_proc((void *)1,0x0005u,0,((i64)(u32)h<<16)|(u32)w); /* WM_SIZE */
            (void)create_proc((void *)1,0x0003u,0,((i64)(u32)y<<16)|(u32)x); /* WM_MOVE */
        }
    }

    compose();
    return (void *)1;
}

__attribute__((dllexport))
BOOL ShowWindow(void *hwnd, int cmd) {
    U32_CHILDWND *child=u32_child_from_hwnd(hwnd);
    if(child){BOOL was=child->visible;child->visible=(cmd!=0);compose();return was;}
    if(hwnd!=(void*)1 || g_wnd.w<=2)return 0;
    BOOL was=g_wnd.visible&&!g_wnd.minimized;
    /* Explorer's Desktop HWND is a shell/composition owner, not a normal
     * visible top-level window. Keep it out of the window chrome and window
     * manager while still allowing its text/taskbar calls to compose the shell. */
    if(u32_is_desktop_window()) {
        g_wnd.visible=0;
        g_wnd.minimized=0;
        u32_child_destroy_all((void *)1);
        compose();
        return was;
    }
    if(cmd==0 || cmd==6 || cmd==2) {
        g_wnd.visible=1;g_wnd.minimized=1;
        (void)sy3(SYS_WmMin,1,0,0);
    } else {
        g_wnd.visible=1;g_wnd.minimized=0;
        /* CreateWindowEx may have started hidden (style lacks WS_VISIBLE).
         * Publish it to the kernel window manager on its first show, otherwise
         * the local USER32 frame exists but no global window layer is allocated. */
        if(!u32_wnd_registered) {
            (void)sy3(SYS_WmReg,0,(u64)g_wnd.title,0);
            u32_wnd_registered=1;
            int r[4]={g_wnd.x,g_wnd.y,g_wnd.w,g_wnd.h};
            (void)sy3(SYS_WmRect,(u64)r,0,0);
        }
        if(cmd==3) {
            if(!u32_normal_valid){u32_normal_x=g_wnd.x;u32_normal_y=g_wnd.y;u32_normal_w=g_wnd.w;u32_normal_h=g_wnd.h;u32_normal_valid=1;}
            ensure_fb();g_wnd.x=0;g_wnd.y=0;g_wnd.w=(int)g_fw;g_wnd.h=(int)g_fh;
            int r[4]={g_wnd.x,g_wnd.y,g_wnd.w,g_wnd.h};(void)sy3(SYS_WmRect,(u64)r,0,0);
        } else if(cmd==9 && u32_normal_valid) {
            g_wnd.x=u32_normal_x;g_wnd.y=u32_normal_y;g_wnd.w=u32_normal_w;g_wnd.h=u32_normal_h;
            int r[4]={g_wnd.x,g_wnd.y,g_wnd.w,g_wnd.h};(void)sy3(SYS_WmRect,(u64)r,0,0);
        }
        (void)sy3(SYS_WmMin,0,0,0);
    }
    compose();
    return was;
}

__attribute__((dllexport))
BOOL UpdateWindow(void *hwnd) {
    (void)hwnd;
    compose();
    return 1;
}

__attribute__((dllexport))
BOOL TextOutA(void *hdc, int x, int y, LPCSTR str, int len) {
    (void)hdc;
    if (!str) return 0;

    if (len < 0) {
        len = 0;
        while (str[len]) len++;
    }

    /*
     * SentinelOS's retained text-window backend stores rows rather than a
     * device context. Treat x as a character-column offset in that backend:
     * this preserves the existing row-based app contract and allows controls
     * such as Explorer's context menu to draw at a real horizontal offset.
     */
    if (y >= 0 && y < 40 && x < 79 && len > 0) {
        int col = x < 0 ? 0 : x;
        int source = x < 0 ? -x : 0;
        if (source > len) source = len;
        int n = len - source;
        if (n > 79 - col) n = 79 - col;
        for (int i = 0; i < n; i++)
            g_lines[y][col + i] = str[source + i];
        int end = col + n;
        if (end < 79) g_lines[y][end] = 0;
        else g_lines[y][79] = 0;
        if (y >= g_nlines) g_nlines = y + 1;
    }
    return 1;
}

static U32_RECT u32_invalid_rect;
static int u32_update_pending;
static BOOL u32_update_erase;
__attribute__((dllexport))
BOOL InvalidateRect(void *hwnd, void *rc, BOOL erase) {
    int desktop_window=u32_is_desktop_window();
    if(hwnd&&!IsWindow(hwnd)&&!desktop_window)return 0;
    U32_RECT r;
    if(rc)r=*(const U32_RECT*)rc;
    else {r.left=0;r.top=0;r.right=(g_wnd.visible&&!desktop_window)?g_wnd.w:(int)g_fw;r.bottom=(g_wnd.visible&&!desktop_window)?g_wnd.h:(int)g_fh;}
    if(r.left<0)r.left=0;if(r.top<0)r.top=0;
    int maxw=(g_wnd.visible&&!desktop_window)?g_wnd.w:(int)g_fw;
    int maxh=(g_wnd.visible&&!desktop_window)?g_wnd.h:(int)g_fh;
    if(r.right>maxw)r.right=maxw;if(r.bottom>maxh)r.bottom=maxh;
    if(r.right<=r.left||r.bottom<=r.top)return 1;
    if(!u32_update_pending)u32_invalid_rect=r;
    else {if(r.left<u32_invalid_rect.left)u32_invalid_rect.left=r.left;if(r.top<u32_invalid_rect.top)u32_invalid_rect.top=r.top;if(r.right>u32_invalid_rect.right)u32_invalid_rect.right=r.right;if(r.bottom>u32_invalid_rect.bottom)u32_invalid_rect.bottom=r.bottom;}
    u32_update_pending=1;if(erase)u32_update_erase=1;

    /*
     * Windows-style damage repaint: translate the invalid client rectangle
     * to screen coordinates, then compose only that region. Include both
     * cursor positions because the cursor is saved directly in the FB.
     */
    {
        int ox = (g_wnd.visible && hwnd) ? g_wnd.x : 0;
        int oy = (g_wnd.visible && hwnd) ? g_wnd.y : 0;
        int x0 = r.left + ox, y0 = r.top + oy;
        int x1 = r.right + ox, y1 = r.bottom + oy;
        /*
         * Cursor save/restore is handled directly by compose(). Do not union
         * its old and new positions into this content dirty rectangle: doing
         * so repaints every pixel between a fast mouse move and produces a
         * visible horizontal streak through text.
         */
        int was_clipped = u32_render_clip;
        int ox0 = u32_clip_x0, oy0 = u32_clip_y0;
        int ox1 = u32_clip_x1, oy1 = u32_clip_y1;

        if (was_clipped) {
            if (ox0 < x0) x0 = ox0;
            if (oy0 < y0) y0 = oy0;
            if (ox1 > x1) x1 = ox1;
            if (oy1 > y1) y1 = oy1;
        }

        u32_render_clip = 1;
        u32_clip_x0 = x0; u32_clip_y0 = y0;
        u32_clip_x1 = x1; u32_clip_y1 = y1;
        /* Consume this damage batch before composing. Leaving it pending
         * unions every future invalidation with stale rectangles. */
        u32_update_pending = 0;
        u32_update_erase = 0;
        compose();
        u32_render_clip = was_clipped;
        u32_clip_x0 = ox0; u32_clip_y0 = oy0;
        u32_clip_x1 = ox1; u32_clip_y1 = oy1;
    }
    return 1;
}

typedef struct {
    u32 pid;
    u32 reserved;
    u64 vector, rip, error, cr2;
    char image_name[64];
    char exception_name[32];
} U32_APP_ERROR;
__attribute__((dllexport)) int MessageBoxA(void *, LPCSTR, LPCSTR, u32);
static u64 u32_last_app_error_poll_tick;
static int u32_poll_application_error(void) {
    /* GetMessageA is a polling loop in this OS. Rate-limit the kernel query
     * to the clock's 1-second resolution instead of issuing a syscall on
     * every empty input iteration. */
    u64 now = u32_tick_now();
    if (now && now == u32_last_app_error_poll_tick) return 0;
    if (now) u32_last_app_error_poll_tick = now;
    U32_APP_ERROR event = {0};
    if (!sy3(SYS_NtGetApplicationError, (u64)&event, 0, 0)) return 0;
    char body[256] = {0};
    unsigned int n = 0;
    const char *s = event.image_name;
    while (*s && n < sizeof(body)-1) body[n++] = *s++;
    const char *tail = " was closed after an unrecoverable CPU exception.\n\nException: ";
    while (*tail && n < sizeof(body)-1) body[n++] = *tail++;
    s = event.exception_name;
    while (*s && n < sizeof(body)-1) body[n++] = *s++;
    const char *pid_text = "\nProcess ID: ";
    while (*pid_text && n < sizeof(body)-1) body[n++] = *pid_text++;
    char digits[10]; unsigned int count = 0, pid = event.pid;
    do { digits[count++] = (char)('0' + pid % 10u); pid /= 10u; } while (pid && count < sizeof(digits));
    while (count && n < sizeof(body)-1) body[n++] = digits[--count];
    const char *rip_text = "\nInstruction address: 0x";
    while (*rip_text && n < sizeof(body)-1) body[n++] = *rip_text++;
    static const char hex[] = "0123456789ABCDEF";
    for (int shift = 60; shift >= 0 && n < sizeof(body)-1; shift -= 4)
        body[n++] = hex[(event.rip >> shift) & 15u];
    body[n] = 0;

    /* Mirror the exact dialog text to the serial console so launch/crash
       reports can be copied from the emulator log even if the UI is stuck. */
    static const char serial_begin[] = "\n========== APPLICATION ERROR DIALOG BEGIN ==========\n";
    static const char serial_end[] = "\n========== APPLICATION ERROR DIALOG END ============\n";
    (void)sy3(SYS_Write, 1, (u64)serial_begin, sizeof(serial_begin) - 1);
    (void)sy3(SYS_Write, 1, (u64)body, n);
    (void)sy3(SYS_Write, 1, (u64)"\n", 1);
    (void)sy3(SYS_Write, 1, (u64)serial_end, sizeof(serial_end) - 1);

    (void)MessageBoxA(0, body, "Application Error", 0x10u);
    return 1;
}

__attribute__((dllexport))
BOOL GetMessageA(MSG *msg, void *hwnd, u32 min, u32 max) {
    if (!msg) return 0;
    if (!g_front) ensure_fb();

    for (;;) {
        u32_pump_timers();
        (void)u32_poll_application_error();
        if (u32_msg_take_filtered(msg,hwnd,min,max,1)) return msg->message == 0x0012 ? 0 : 1;
        InputEvent ev = {0};

        if (sy3(SYS_GetInput, (u64)&ev, 0, 0)) {
            u32_last_input_time=(DWORD)u32_tick_now();
            if (ev.type == 1) {
                char key = ev.key;

                /*
                 * Handle keyboard input while the Start menu
                 * search box has focus.
                 */
                if (g_menu && g_search_focus) {
                    if(key==27){g_menu=0;g_search_focus=0;g_start_hover=-1;compose();continue;}
                    if (key == '\b') {
                        if (g_search_len > 0) {
                            g_search_len--;
                            g_search_text[g_search_len] = 0;
                        }


                        compose_region(0, ((int)g_fh-48-360-4)>4?((int)g_fh-48-360-4):4, (int)g_fw, (int)g_fh);
                        continue;
                    }

                    if (key == '\n' || key == '\r') {
                        submit_search();
                        compose();
                        continue;
                    }

                    if (key >= 32 && key <= 126 &&
                        g_search_len < 30) {
                        g_search_text[g_search_len++] = key;
                        g_search_text[g_search_len] = 0;

                        compose_region(0, ((int)g_fh-48-360-4)>4?((int)g_fh-48-360-4):4, (int)g_fw, (int)g_fh);
                    }

                    continue;
                }

                msg->hwnd=g_wnd.visible?1u:0u;msg->message=0x0100u;
                msg->wParam=(u64)(unsigned char)ev.key;msg->lParam=0;msg->time=(u32)u32_tick_now();
                msg->pt_x=g_mx;msg->pt_y=g_my;
                (void)u32_msg_push(msg);continue;
            }

            if (ev.type == 2 || ev.type == 3) {
                /* Preserve the old cursor rectangle even if this event
                 * closes a window or triggers composition before repaint. */
                int old_mx=g_mx, old_my=g_my;
                g_mx = ev.x;
                g_my = ev.y;
                u32_mouse_record();
                if(g_menu) {
                    int hv=u32_start_hover_at(g_mx,g_my);
                    if(hv!=g_start_hover) {
                        g_start_hover=hv;
                        U32_START_LAYOUT sl=u32_start_layout();
                        compose_region(0,sl.y,(int)g_fw,(int)g_fh);
                    }
                }
                u32_hover_cursor_id=u32_auto_cursor_at(g_mx,g_my);

                msg->hwnd=g_wnd.visible?1u:0u;msg->message=(ev.type==3)?0x0201u:0x0200u;
                msg->wParam=(u64)(unsigned int)ev.btn;msg->lParam=0;msg->time=(u32)u32_tick_now();
                msg->pt_x=ev.x;msg->pt_y=ev.y;

                /*
                 * A type-3 event is the button-down edge; type-2 events
                 * carry motion and also report button release. Only move
                 * during motion with the left button still held. If release
                 * left g_drag set, the next unrelated click used to reuse
                 * the old drag offset and teleport the window under the
                 * pointer before hit-testing that click.
                 */
                if (ev.type == 2 && g_drag && (ev.btn & 1)) {
                    int nx = ev.x - g_drag_ox;
                    int ny = ev.y - g_drag_oy;
                    if (nx < 0) nx = 0;
                    if (ny < 0) ny = 0;
                    if (nx != g_drag_pending_x || ny != g_drag_pending_y)
                        g_drag_moved = 1;
                    /* Commit both axes during the drag, then repaint only the
                     * union of the old and new window bounds. Child HWNDs use
                     * client-relative coordinates, so USER32 redraws them at
                     * the new screen X and Y in the same frame as their parent. */
                    if (nx != g_wnd.x || ny != g_wnd.y) {
                        int old_x = g_wnd.x, old_y = g_wnd.y;
                        g_wnd.x = nx;
                        g_wnd.y = ny;
                        g_drag_pending_x = nx;
                        g_drag_pending_y = ny;
                        int rect[4] = { nx, ny, g_wnd.w, g_wnd.h };
                        sy3(SYS_WmRect, (u64)rect, 0, 0);
                        /* Repaint only the union of the old and new window
                         * bounds. The persistent desktop/background layers are
                         * restored under the old position, and child controls are
                         * redrawn with the parent at its new position. This avoids
                         * rebuilding and presenting the entire screen per mouse move. */
                        int x0=old_x<nx?old_x:nx;
                        int y0=old_y<ny?old_y:ny;
                        int x1=(old_x+g_wnd.w+8)>(nx+g_wnd.w+8)
                            ?old_x+g_wnd.w+8:nx+g_wnd.w+8;
                        int y1=(old_y+g_wnd.h+12)>(ny+g_wnd.h+12)
                            ?old_y+g_wnd.h+12:ny+g_wnd.h+12;
                        compose_region(x0,y0,x1,y1);
                    }
                }

                if (ev.type == 3) {
                    /* Button-down starts a fresh hit-test/drag operation. */
                    g_drag = 0;
                    on_click(ev.x, ev.y);
                } else {
                    /* Type 2 also reports button release. End the drag on
                     * that transition, not on the next button-down. */
                    if (!(ev.btn & 1)) {
                        if (g_drag && g_drag_moved &&
                            (g_drag_pending_x != g_wnd.x ||
                             g_drag_pending_y != g_wnd.y)) {
                            int old_x=g_wnd.x, old_y=g_wnd.y;
                            g_wnd.x=g_drag_pending_x;
                            g_wnd.y=g_drag_pending_y;
                            int rect[4]={g_wnd.x,g_wnd.y,g_wnd.w,g_wnd.h};
                            sy3(SYS_WmRect,(u64)rect,0,0);
                            /* One committed redraw after release: old bounds
                             * are restored and the complete window, including
                             * every button/control, is painted at its new spot. */
                            int x0=old_x<g_wnd.x?old_x:g_wnd.x;
                            int y0=old_y<g_wnd.y?old_y:g_wnd.y;
                            int x1=(old_x+g_wnd.w+8)>(g_wnd.x+g_wnd.w+8)
                                ?old_x+g_wnd.w+8:g_wnd.x+g_wnd.w+8;
                            int y1=(old_y+g_wnd.h+12)>(g_wnd.y+g_wnd.h+12)
                                ?old_y+g_wnd.h+12:g_wnd.y+g_wnd.h+12;
                            g_drag=0;
                            g_drag_moved=0;
                            compose_region(x0,y0,x1,y1);
                        } else {
                            g_drag=0;
                            g_drag_moved=0;
                        }
                    }
                }

                /* Clear the old cursor footprint after click handlers and drag
                 * composition finish, then draw it at the new position. */
                cursor_repaint_transition(old_mx,old_my,g_mx,g_my);
                (void)u32_msg_push(msg);
                continue;
            }
        }

        __asm__ volatile("pause");
    }
}

__attribute__((dllexport))
BOOL TranslateMessage(const MSG *m) {
    if (!m || m->message != 0x0100 || m->wParam < 32 || m->wParam > 126) return 0;
    MSG ch = *m;
    ch.message = 0x0102; /* WM_CHAR */
    ch.wParam = m->wParam;
    return u32_msg_push(&ch);
}

__attribute__((dllexport))
i64 DispatchMessageA(const MSG *m) {
    if (!m || m->message == 0x0012) return 0;
    if(m->message==0x0113u && m->lParam) {
        typedef void (*U32_TIMERPROC)(void *,unsigned int,unsigned long long,unsigned long);
        ((U32_TIMERPROC)(u64)m->lParam)((void *)(u64)m->hwnd,m->message,m->wParam,m->time);
        return 0;
    }
    if (!u32_wnd_proc)
        return (i64)DefWindowProcA((void *)(u64)m->hwnd, m->message, m->wParam, m->lParam);
    typedef i64 (*U32_WNDPROC)(void *, unsigned int, u64, i64);
    U32_WNDPROC proc = (U32_WNDPROC)u32_wnd_proc;
    return proc((void *)(u64)m->hwnd, m->message, m->wParam, m->lParam);
}

/* Modal message box: a real captioned window with an OK button. */
__attribute__((dllexport))
int MessageBoxA(void *hwnd, LPCSTR text, LPCSTR caption, u32 type) {
    (void)hwnd;
    /* MB_YESNO is used by the MSI reader before writing extracted output. */
    int yesno = ((type & 0x0Fu) == 0x04u);
    WINDOW old_wnd = g_wnd;
    char old_lines[40][80];
    int old_nlines = g_nlines, old_menu = g_menu, old_search = g_search_focus;
    for (int i=0;i<40;i++) for(int j=0;j<80;j++) old_lines[i][j]=g_lines[i][j];

    ensure_fb();
    if (!g_front || !g_fw || !g_fh) {
        const char *s=text?text:"Application error"; u64 n=0;
        while(s[n])n++;
        (void)sy3(SYS_Write,1,(u64)s,n);
        (void)sy3(SYS_Write,1,(u64)"\n",1);
        return 1;
    }

    (void)sy3(SYS_WmUnreg,0,0,0);
    g_wnd.x=((int)g_fw-440)/2; g_wnd.y=((int)g_fh-210)/2;
    if(g_wnd.x<8)g_wnd.x=8; if(g_wnd.y<8)g_wnd.y=8;
    g_wnd.w=440; g_wnd.h=210; g_wnd.visible=1; g_wnd.minimized=0;
    const char *cap=(caption&&caption[0])?caption:"SentinelOS";
    int ti=0; while(cap[ti]&&ti<62){g_wnd.title[ti]=cap[ti];ti++;}
    g_wnd.title[ti]=0;
    for(int i=0;i<40;i++)g_lines[i][0]=0;
    g_nlines=0; g_menu=0; g_search_focus=0;

    const char *src=text?text:"";
    while(*src&&g_nlines<7) {
        char line[80]; int n=0,last=-1;
        while(src[n]&&src[n]!='\n'&&n<48){line[n]=src[n];if(src[n]==' ')last=n;n++;}
        int take=n;
        if(src[n]&&src[n]!='\n'&&last>0)take=last;
        while(take>0&&line[take-1]==' ')take--;
        line[take]=0;
        int j=0;while(line[j]&&j<78){g_lines[g_nlines][j]=line[j];j++;}
        g_lines[g_nlines][j]=0;g_nlines++;
        src+=take;while(*src==' ')src++;if(*src=='\n')src++;
        if(take==0&&*src)src++;
    }
    int rect[4]={g_wnd.x,g_wnd.y,g_wnd.w,g_wnd.h};
    (void)sy3(SYS_WmReg,0,(u64)g_wnd.title,0);
    (void)sy3(SYS_WmRect,(u64)rect,0,0);

    int done=0;
    int result=1;
    while(!done) {
        /* Remove the previous cursor before repainting the modal scene.
         * Keep cur_on/save-under valid until cursor_restore_fast() restores
         * exactly the pixels beneath that cursor. Clearing cur_on first
         * leaves the old cursor pixels in scanout, and the next cursor draw
         * then saves those cursor pixels as its underlay, producing a trail. */
        if(cur_on) cursor_restore_fast();
        compose();
        int by=g_wnd.y+g_wnd.h-42;
        int bx=yesno?g_wnd.x+g_wnd.w-190:g_wnd.x+g_wnd.w-100;
        round_fill(bx,by,78,28,10,0xFFFF3B30);
        round_fill(bx+2,by+2,74,24,8,0xFF292E37);
        draw_text(bx+(yesno?25:26),by+9,yesno?"Yes":"OK",0xFFF1F3F6);
        int nx=g_wnd.x+g_wnd.w-100;
        if(yesno) {
            round_fill(nx,by,78,28,10,0xFFFF3B30);
            round_fill(nx+2,by+2,74,24,8,0xFF292E37);
            draw_text(nx+24,by+9,"No",0xFFF1F3F6);
        }

        /* The modal title is not "Desktop", so compose() does not paint the
         * pointer. Repaint it as the final overlay after the dialog is ready. */
        cur_ox=g_mx; cur_oy=g_my;
        cursor_save_draw(g_mx,g_my);
        if(g_scanout && g_scan_pitch && g_fw && g_fh)
            u32_flush_raw(g_mx,g_my,16,16);

        InputEvent ev={0};
        if(!sy3(SYS_GetInput,(u64)&ev,0,0))continue;
        if(ev.type==1) {
            if(yesno && (ev.key=='y'||ev.key=='Y')) { result=6; done=1; }
            else if(yesno && (ev.key=='n'||ev.key=='N'||ev.key==27)) { result=7; done=1; }
            else if(ev.key==27||ev.key=='\r'||ev.key=='\n'||ev.key==' ')done=1;
        } else if(ev.type==2||ev.type==3) {
            g_mx=ev.x;g_my=ev.y;u32_mouse_record();
            if(ev.type==3 && hit(ev.x,ev.y,bx,by,bx+78,by+28)) {
                result=yesno?6:1; done=1;
            } else if(ev.type==3 && yesno && hit(ev.x,ev.y,nx,by,nx+78,by+28)) {
                result=7; done=1;
            } else if(ev.type==3 &&
              hit(ev.x,ev.y,g_wnd.x+g_wnd.w-30,g_wnd.y+2,
                           g_wnd.x+g_wnd.w-2,g_wnd.y+30)) {
                result=yesno?7:1; done=1;
            }
        }
    }

    (void)sy3(SYS_WmUnreg,0,0,0);
    g_wnd=old_wnd;
    for(int i=0;i<40;i++)for(int j=0;j<80;j++)g_lines[i][j]=old_lines[i][j];
    g_nlines=old_nlines;g_menu=old_menu;g_search_focus=old_search;
    if(g_wnd.visible&&!g_wnd.minimized) {
        rect[0]=g_wnd.x;rect[1]=g_wnd.y;rect[2]=g_wnd.w;rect[3]=g_wnd.h;
        (void)sy3(SYS_WmReg,0,(u64)g_wnd.title,0);
        (void)sy3(SYS_WmRect,(u64)rect,0,0);
    }
    compose();
    return result;
}

__attribute__((dllexport))
BOOL DestroyWindow(void *hwnd) {
    if(u32_child_from_hwnd(hwnd)) {
        u32_child_from_hwnd(hwnd)->used=0;
        compose();
        return 1;
    }
    if(hwnd!=(void *)1 && hwnd) return 0;

    /* Save the old frame before hiding it so every pixel it covered is
     * repainted from the desktop layers, including the title bar and shadow. */
    int had_window = g_wnd.visible && g_wnd.w > 2 && g_wnd.h > 2;
    sy3(SYS_WmUnreg, 0, 0, 0);
    g_wnd.visible = 0;
    g_wnd.minimized = 0;

    if (had_window) {
        /*
         * Window teardown is the one transition where the cursor can move
         * from a window's pixels onto the desktop while its saved-under
         * buffer still refers to the old scanout. A clipped repaint can
         * refresh the window rectangle but leave the cursor underlay stale;
         * the next mouse move then restores those stale pixels as a trail.
         * Recompose the full scene on close so all Z-ordered layers and the
         * cursor underlay are rebuilt from the same frame. Keep the desktop
         * welcome card suppressed, as before, while the application closes.
         */
        u32_suppress_desktop_card = 1;
        compose();
        u32_suppress_desktop_card = 0;
    } else {
        compose();
    }

    return 1;
}

__attribute__((dllexport))
BOOL PostQuitMessage(int code) {    MSG m = {0};    m.message = 0x0012; /* WM_QUIT */
    m.wParam = (u64)(unsigned int)code;
    m.pt_x = g_mx;
    m.pt_y = g_my;
    return u32_msg_push(&m);
}

__attribute__((dllexport))
BOOL ExitWindowsEx(u32 flags, u32 reason) {
    (void)reason;
    sy3(SYS_Shutdown, (flags & 2) ? 1 : 0, 0, 0);
    return 1;
}

__attribute__((dllexport))
void DrawTaskbar(void) {
    /* Clock refreshes and Start-menu toggles are frequent, small updates.
     * Clip to the taskbar plus the old/new menu footprint; the taskbar layer
     * is presented alone when the shared desktop background is established. */
    if(!u32_is_desktop_window() || !g_fw || !g_fh ||
       u32_force_full_compose || u32_render_clip ||
       (g_gpu_surface && !u32_shared_background_published) ||
       u32_aero_bg_w!=g_fw || u32_aero_bg_h!=g_fh) {
        compose();
        return;
    }

    int old_clip=u32_render_clip;
    int old_x0=u32_clip_x0, old_y0=u32_clip_y0;
    int old_x1=u32_clip_x1, old_y1=u32_clip_y1;
    int h=(int)g_fh, ty=h-48;
    if(ty<0)ty=0;
    int y0=ty;
    if(g_menu || u32_taskbar_menu_was_open) {
        int menu_y=ty-364;
        if(menu_y<0)menu_y=0;
        if(menu_y<y0)y0=menu_y;
    }

    u32_render_clip=1;
    u32_clip_x0=0; u32_clip_y0=y0;
    u32_clip_x1=(int)g_fw; u32_clip_y1=h;
    u32_taskbar_only_compose=1;
    compose();
    u32_taskbar_only_compose=0;
    u32_taskbar_menu_was_open=g_menu!=0;
    u32_render_clip=old_clip;
    u32_clip_x0=old_x0; u32_clip_y0=old_y0;
    u32_clip_x1=old_x1; u32_clip_y1=old_y1;
}

/* Batch startup UI mutations into one final framebuffer composition. */
__attribute__((dllexport)) void BeginRedrawBatch(void) { u32_redraw_enabled=0; }
__attribute__((dllexport)) void EndRedrawBatch(void) { u32_redraw_enabled=1; compose(); }

/* -------------------------------------------------------------------------
 * Additional USER32-compatible exports.
 * Unsupported features return conservative defaults. This is an API surface
 * expansion, not a claim of full Windows message/window-manager semantics.
 * ------------------------------------------------------------------------- */
typedef unsigned long long U32_WPARAM;
typedef long long U32_LPARAM;
typedef long long U32_LRESULT;
typedef unsigned int U32_UINT;
typedef unsigned short U32_WCHAR;

typedef struct { unsigned int cbSize,fMask; int nMin,nMax; unsigned int nPage; int nPos,nTrackPos; } U32_SCROLLINFO;
typedef struct { int x, y; } U32_POINT;
typedef struct { unsigned int length, flags, showCmd; U32_POINT ptMinPosition, ptMaxPosition; U32_RECT rcNormalPosition; } U32_WINDOWPLACEMENT;
typedef struct { u32 cbSize; U32_RECT rcWindow, rcClient; u32 dwStyle, dwExStyle, dwWindowStatus, cxWindowBorders, cyWindowBorders; unsigned short atomWindowType, wCreatorVersion; } U32_WINDOWINFO;
#define U32_MIIM_STATE 0x00000001u
#define U32_MIIM_ID 0x00000002u
#define U32_MIIM_SUBMENU 0x00000004u
#define U32_MIIM_CHECKMARKS 0x00000008u
#define U32_MIIM_DATA 0x00000020u
#define U32_MIIM_STRING 0x00000040u
#define U32_MIIM_BITMAP 0x00000080u
#define U32_MIIM_FTYPE 0x00000100u
#define U32_MFS_STATE_MASK 0x0000018Bu
#define U32_MFT_TYPE_MASK 0x00006B64u
typedef struct { unsigned int flags,state,type; unsigned long long id,data; void *submenu; void *bmp_checked,*bmp_unchecked,*bmp_item; char text[80]; } U32_MENUITEM;
typedef struct { int used, count, default_pos; unsigned int style,max_height,help_id; void *background; unsigned long long menu_data; U32_MENUITEM items[16]; } U32_MENUDATA;
static U32_MENUDATA u32_menus[16];
static void *u32_system_menu;
static U32_MENUDATA *u32_menu_data(void *handle) {
    unsigned long long v=(unsigned long long)handle;
    if (v<0x100 || v>=0x110) return 0;
    U32_MENUDATA *m=&u32_menus[v-0x100];
    return m->used ? m : 0;
}
static int u32_menu_index(U32_MENUDATA *m, unsigned long long item, int bypos) {
    if (!m) return -1;
    if (bypos) return item<(unsigned long long)m->count ? (int)item : -1;
    for (int i=0;i<m->count;i++) if (m->items[i].id==item) return i;
    return -1;
}
static void u32_menu_text(char *out, unsigned int cap, LPCSTR text) {
    unsigned int i=0; if (!out || !cap) return;
    if (text) while (text[i] && i+1<cap) { out[i]=text[i]; i++; }
    out[i]=0;
}
static int u32_menu_insert(void *handle, unsigned int position, unsigned int flags, unsigned long long id, LPCSTR text) {
    U32_MENUDATA *m=u32_menu_data(handle); if (!m || m->count>=16) return 0;
    int bypos=(flags&0x0400u)!=0;
    int at=position==0xFFFFFFFFu ? m->count :
           (bypos ? (position<= (unsigned int)m->count ? (int)position : -1)
                  : u32_menu_index(m,position,0));
    if (at<0 || at>m->count) return 0;
    for (int i=m->count;i>at;i--) m->items[i]=m->items[i-1];
    U32_MENUITEM *it=&m->items[at]; it->flags=flags;it->state=flags&U32_MFS_STATE_MASK;it->type=flags&U32_MFT_TYPE_MASK;it->id=id; it->data=0;it->bmp_checked=it->bmp_unchecked=it->bmp_item=0;it->submenu=(flags&0x0010u)?(void *)id:0;
    u32_menu_text(it->text,sizeof(it->text),text); m->count++;
    if (m->default_pos>=at) m->default_pos++;
    return 1;
}
static int u32_menu_remove(void *handle, unsigned int item, unsigned int flags, int destroy_submenu) {
    U32_MENUDATA *m=u32_menu_data(handle); int idx=u32_menu_index(m,item,(flags&0x0400u)!=0);
    if (idx<0) return 0;
    void *sub=m->items[idx].submenu;
    for (int i=idx;i+1<m->count;i++) m->items[i]=m->items[i+1];
    m->count--; if (m->default_pos==idx) m->default_pos=-1; else if (m->default_pos>idx) m->default_pos--;
    if (destroy_submenu && sub) { U32_MENUDATA *sm=u32_menu_data(sub); if (sm) sm->used=0; }
    return 1;
}
typedef struct { void *hdc; int erase; U32_RECT paint; int restore, inc_update; unsigned char reserved[32]; } U32_PAINTSTRUCT;
typedef struct { unsigned int style; void *proc; int cls_extra, wnd_extra; void *instance, *icon, *cursor, *background; LPCSTR menu_name, class_name; } U32_WNDCLASSA;
static void *u32_capture;
static void *u32_focus;
static void *u32_active;

__attribute__((dllexport)) BOOL GetClientRect(void *hwnd, U32_RECT *r) {
    U32_CHILDWND *child=u32_child_from_hwnd(hwnd);
    if(child){if(!r)return 0;r->left=0;r->top=0;r->right=child->w;r->bottom=child->h;return 1;}
    if(hwnd!=(void *)1 || !r || !g_wnd.visible) return 0;
    r->left=0; r->top=0; r->right=g_wnd.w; r->bottom=g_wnd.h-32; return 1;
}
__attribute__((dllexport)) BOOL MoveWindow(void *hwnd, int x, int y, int w, int h, BOOL repaint) {
    U32_CHILDWND *child=u32_child_from_hwnd(hwnd);
    if(child){if(w<1||h<1)return 0;child->x=x;child->y=y;child->w=w;child->h=h;if(repaint)compose();return 1;}
    if(hwnd!=(void *)1 || !g_wnd.visible || w<1 || h<1) return 0;
    g_wnd.x=x; g_wnd.y=y; g_wnd.w=w; g_wnd.h=h;
    { int r[4]={x,y,w,h}; sy3(SYS_WmRect,(u64)r,0,0); }
    if (repaint) compose(); return 1;
}
__attribute__((dllexport)) BOOL SetWindowPos(void *hwnd, void *after, int x, int y, int cx, int cy, unsigned int flags) {
    (void)after;
    U32_CHILDWND *child=u32_child_from_hwnd(hwnd);
    int oldx,oldy,oldw,oldh;
    if(child){oldx=child->x;oldy=child->y;oldw=child->w;oldh=child->h;}
    else {if(hwnd!=(void*)1||!g_wnd.visible)return 0;oldx=g_wnd.x;oldy=g_wnd.y;oldw=g_wnd.w;oldh=g_wnd.h;}
    if(flags&0x0002u){x=oldx;y=oldy;}
    if(flags&0x0001u){cx=oldw;cy=oldh;}
    if(cx<1||cy<1)return 0;
    if(x==oldx&&y==oldy&&cx==oldw&&cy==oldh)return 1;
    return MoveWindow(hwnd,x,y,cx,cy,!(flags&0x0008u));
}
__attribute__((dllexport)) BOOL GetWindowRect(void *hwnd, U32_RECT *r) {
    U32_CHILDWND *child=u32_child_from_hwnd(hwnd);
    if(child){if(!r||child->parent!=(void *)1)return 0;r->left=g_wnd.x+child->x;r->top=g_wnd.y+child->y;r->right=r->left+child->w;r->bottom=r->top+child->h;return 1;}
    if(hwnd!=(void *)1 || !r || !g_wnd.visible) return 0;
    r->left=g_wnd.x; r->top=g_wnd.y; r->right=g_wnd.x+g_wnd.w; r->bottom=g_wnd.y+g_wnd.h; return 1;
}
__attribute__((dllexport)) BOOL GetWindowPlacement(void *hwnd, U32_WINDOWPLACEMENT *p) {
    if(hwnd!=(void*)1||!p||p->length<sizeof(U32_WINDOWPLACEMENT)||!IsWindow(hwnd))return 0;
    p->flags=0;p->showCmd=g_wnd.minimized?2u:(u32_normal_valid&&g_wnd.x==0&&g_wnd.y==0&&g_wnd.w==(int)g_fw&&g_wnd.h==(int)g_fh?3u:1u);
    p->ptMinPosition.x=p->ptMinPosition.y=-1;p->ptMaxPosition.x=p->ptMaxPosition.y=0;
    p->rcNormalPosition.left=u32_normal_valid?u32_normal_x:g_wnd.x;
    p->rcNormalPosition.top=u32_normal_valid?u32_normal_y:g_wnd.y;
    p->rcNormalPosition.right=(u32_normal_valid?u32_normal_x:g_wnd.x)+(u32_normal_valid?u32_normal_w:g_wnd.w);
    p->rcNormalPosition.bottom=(u32_normal_valid?u32_normal_y:g_wnd.y)+(u32_normal_valid?u32_normal_h:g_wnd.h);
    return 1;
}
__attribute__((dllexport)) BOOL SetWindowPlacement(void *hwnd, const U32_WINDOWPLACEMENT *p) {
    if(hwnd!=(void*)1||!p||p->length<sizeof(U32_WINDOWPLACEMENT)||!IsWindow(hwnd))return 0;
    if(p->rcNormalPosition.right<=p->rcNormalPosition.left||p->rcNormalPosition.bottom<=p->rcNormalPosition.top)return 0;
    u32_normal_x=p->rcNormalPosition.left;u32_normal_y=p->rcNormalPosition.top;
    u32_normal_w=p->rcNormalPosition.right-p->rcNormalPosition.left;
    u32_normal_h=p->rcNormalPosition.bottom-p->rcNormalPosition.top;u32_normal_valid=1;
    if(p->showCmd==2u||p->showCmd==6u) return ShowWindow(hwnd,2);
    if(p->showCmd==3u) return ShowWindow(hwnd,3);
    return ShowWindow(hwnd,9);
}
__attribute__((dllexport)) BOOL IsWindow(void *hwnd) { return (hwnd==(void *)1 && g_wnd.visible) || u32_child_from_hwnd(hwnd)!=0; }
__attribute__((dllexport)) BOOL IsWindowVisible(void *hwnd) { return IsWindow(hwnd) && !g_wnd.minimized; }
__attribute__((dllexport)) BOOL EnableWindow(void *hwnd, BOOL enable) {U32_CHILDWND *child=u32_child_from_hwnd(hwnd);if(child){BOOL was=child->enabled;child->enabled=enable;compose();return was;}if(!IsWindow(hwnd))return 0;BOOL was_disabled=(u32_wnd_style&0x08000000u)!=0;if(enable)u32_wnd_style&=~0x08000000u;else u32_wnd_style|=0x08000000u;if(was_disabled!=!enable)compose();return was_disabled;}
__attribute__((dllexport)) BOOL IsIconic(void *hwnd) { (void)hwnd; return g_wnd.visible && g_wnd.minimized; }
__attribute__((dllexport)) void *GetForegroundWindow(void) { return g_wnd.visible && !g_wnd.minimized ? (void *)1 : (void *)0; }
__attribute__((dllexport)) BOOL SetForegroundWindow(void *hwnd) { if (!IsWindow(hwnd)) return 0; u32_active=hwnd; return 1; }
__attribute__((dllexport)) void *GetActiveWindow(void) { return u32_active ? u32_active : GetForegroundWindow(); }
__attribute__((dllexport)) void *SetActiveWindow(void *hwnd) { void *old=GetActiveWindow(); u32_active=hwnd; return old; }
__attribute__((dllexport)) int GetWindowTextA(void *hwnd, LPSTR out, int cap) {
    int i=0;
    U32_CHILDWND *child=u32_child_from_hwnd(hwnd);
    if(child){if(!out||cap<=0)return 0;while(child->text[i]&&i+1<cap){out[i]=child->text[i];i++;}out[i]=0;return i;}
    if (!out || cap<=0 || !g_wnd.visible) return 0;
    while (g_wnd.title[i] && i+1<cap) { out[i]=g_wnd.title[i]; i++; } out[i]=0; return i;
}
__attribute__((dllexport)) BOOL SetWindowTextA(void *hwnd, LPCSTR title) {
    int i=0; U32_CHILDWND *child=u32_child_from_hwnd(hwnd);
    if(child){if(!title)return 0;while(title[i]&&i+1<sizeof(child->text)){child->text[i]=title[i];i++;}child->text[i]=0;compose();return 1;}
    (void)hwnd; if (!title || !g_wnd.visible) return 0;
    while (title[i] && i<63) { g_wnd.title[i]=title[i]; i++; } g_wnd.title[i]=0; compose(); return 1;
}
__attribute__((dllexport)) int GetClassNameA(void *hwnd, LPSTR out, int cap) {
    int i=0;
    if (!out || cap<=0 || !IsWindow(hwnd)) return 0;
    U32_CHILDWND *child=u32_child_from_hwnd(hwnd);
    const char *s=child?child->cls:u32_wnd_class_name;
    if(!s[0])s="SentinelWindow";
    while (s[i] && i+1<cap) { out[i]=s[i]; i++; }
    out[i]=0;
    return i;
}
typedef struct { int used; unsigned short atom; U32_WNDCLASSA wc; char name[96], menu[96]; } U32_CLASSREG;
static U32_CLASSREG u32_classes[16];
static int u32_class_name_equal(const char *a,const char *b){if(!a||!b)return 0;while(*a&&*b){char x=*a,y=*b;if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return 0;a++;b++;}return *a==0&&*b==0;}
static U32_CLASSREG *u32_class_find(const char *name){if(!name)return 0;for(int i=0;i<16;i++)if(u32_classes[i].used&&u32_class_name_equal(u32_classes[i].name,name))return &u32_classes[i];return 0;}
__attribute__((dllexport)) unsigned short RegisterClassA(const U32_WNDCLASSA *wc) {
    if(!wc||!wc->class_name||!wc->class_name[0])return 0;
    if(u32_class_find(wc->class_name))return 0;
    int slot=-1;for(int i=0;i<16;i++)if(!u32_classes[i].used){slot=i;break;}if(slot<0)return 0;
    U32_CLASSREG *c=&u32_classes[slot];c->used=1;c->atom=(unsigned short)(0xC000+slot);c->wc=*wc;
    unsigned int n=0;while(wc->class_name[n]&&n+1<sizeof(c->name)){c->name[n]=wc->class_name[n];n++;}c->name[n]=0;c->wc.class_name=c->name;
    if(wc->menu_name){n=0;while(wc->menu_name[n]&&n+1<sizeof(c->menu)){c->menu[n]=wc->menu_name[n];n++;}c->menu[n]=0;c->wc.menu_name=c->menu;}else c->wc.menu_name=0;
    u32_class_style=(long)wc->style;u32_class_proc=wc->proc;u32_class_extra[0]=wc->cls_extra;u32_class_extra[1]=wc->wnd_extra;
    return c->atom;
}
__attribute__((dllexport)) BOOL UnregisterClassA(LPCSTR name, void *instance) {
    (void)instance;U32_CLASSREG *c=u32_class_find(name);if(!c)return 0;c->used=0;c->name[0]=0;c->menu[0]=0;return 1;
}
__attribute__((dllexport)) U32_LRESULT DefWindowProcA(void *hwnd, U32_UINT msg, U32_WPARAM wp, U32_LPARAM lp) {
    switch(msg){
        case 0x000Cu: return SetWindowTextA(hwnd,(LPCSTR)lp);
        case 0x000Du: return GetWindowTextA(hwnd,(LPSTR)lp,(int)wp);
        case 0x000Eu: return GetWindowTextLengthA(hwnd);
        case 0x0010u: return DestroyWindow(hwnd);
        case 0x0014u: return 1;
        case 0x001Fu: return 1;
        case 0x0081u: return 1; /* WM_NCCREATE succeeds by default. */
        case 0x0084u: return 1;
        case 0x0085u: compose(); return 0;
        case 0x0018u: return IsWindow(hwnd);
        case 0x0020u: return 1;
        case 0x0319u: return 0;
        default: return 0;
    }
}
__attribute__((dllexport)) U32_LRESULT SendMessageA(void *hwnd, U32_UINT msg, U32_WPARAM wp, U32_LPARAM lp) {
    U32_CHILDWND *child=u32_child_from_hwnd(hwnd);
    if(child) {
        if(msg==0x000Cu) {unsigned int i=0;const char *s=(const char *)lp;while(s&&s[i]&&i+1<sizeof(child->text)){child->text[i]=s[i];i++;}child->text[i]=0;compose();return 1;}
        if(msg==0x00F5u) {if(child->parent==(void *)1)SendMessageA(child->parent,0x0111u,child->id,0);return 0;}
        return DefWindowProcA(hwnd,msg,wp,lp);
    }
    if(!IsWindow(hwnd))return 0;
    if(u32_wnd_proc){
        U32_LRESULT (*proc)(void *,U32_UINT,U32_WPARAM,U32_LPARAM)=(U32_LRESULT (*)(void *,U32_UINT,U32_WPARAM,U32_LPARAM))u32_wnd_proc;
        int was=u32_in_send_message;u32_in_send_message=1;
        U32_LRESULT result=proc(hwnd,msg,wp,lp);
        u32_in_send_message=was;
        return result;
    }
    if (msg==0x000Cu) return SetWindowTextA(hwnd,(LPCSTR)lp);
    if (msg==0x000Du) return GetWindowTextA(hwnd,(LPSTR)lp,(int)wp);
    if (msg==0x0010u) { g_wnd.visible=0; sy3(SYS_WmUnreg,0,0,0); compose(); return 1; }
    if (msg==0x000Fu) { compose(); return 1; }
    return DefWindowProcA(hwnd,msg,wp,lp);
}
__attribute__((dllexport)) BOOL PostMessageA(void *hwnd, U32_UINT msg, U32_WPARAM wp, U32_LPARAM lp) {
    MSG m = {0}; m.hwnd = (u32)(u64)hwnd; m.message = msg; m.wParam = wp; m.lParam = lp;
    m.pt_x = g_mx; m.pt_y = g_my;
    return u32_msg_push(&m);
}
__attribute__((dllexport)) BOOL PeekMessageA(MSG *msg, void *hwnd, U32_UINT min, U32_UINT max, U32_UINT remove) {
    if (!msg) return 0;
    u32_pump_timers();
    return u32_msg_take_filtered(msg,hwnd,min,max,(remove&1u)!=0);
}
__attribute__((dllexport)) short GetKeyState(int key){if(key<0||key>255)return 0;return (short)u32_key_state[key];}
__attribute__((dllexport)) short GetAsyncKeyState(int key){if(key<0||key>255)return 0;return (short)u32_key_state[key];}
__attribute__((dllexport)) BOOL GetCursorPos(U32_POINT *p) {
    if (!p) return 0; p->x=g_mx; p->y=g_my; return 1;
}
__attribute__((dllexport)) BOOL SetCursorPos(int x, int y) {
    g_mx=x; g_my=y; u32_mouse_record(); compose(); return 1;
}
__attribute__((dllexport)) int ShowCursor(BOOL show) {
    /* Win32 semantics: every call adjusts the display count by exactly one.
     * A negative count hides the pointer; zero or above shows it. */
    if (show) { if(u32_show_cursor_count<32767)u32_show_cursor_count++; }
    else { if(u32_show_cursor_count>-32768)u32_show_cursor_count--; }
    if(u32_show_cursor_count<0 || u32_cursor_explicit_hidden) {
        cursor_clear_footprint();
        cur_on=0;
        if(g_scanout) u32_flush_raw(cur_ox,cur_oy,16,16);
    } else {
        cursor_repaint_transition(g_mx,g_my,g_mx,g_my);
    }
    return u32_show_cursor_count;
}
__attribute__((dllexport)) void *SetCapture(void *hwnd) {
    void *old=u32_capture; u32_capture=hwnd; return old;
}
__attribute__((dllexport)) BOOL ReleaseCapture(void) { u32_capture=0; return 1; }
__attribute__((dllexport)) void *GetCapture(void) { return u32_capture; }
__attribute__((dllexport)) void *GetDesktopWindow(void) { return (void *)2; }
__attribute__((dllexport)) void *GetDC(void *hwnd) { (void)hwnd; ensure_fb(); return g_front ? (void *)1 : (void *)0; }
__attribute__((dllexport)) int ReleaseDC(void *hwnd, void *dc) { (void)hwnd; (void)dc; return 1; }
__attribute__((dllexport)) void *BeginPaint(void *hwnd, U32_PAINTSTRUCT *ps) {
    if(!IsWindow(hwnd)||!ps)return 0;ps->hdc=GetDC(hwnd);ps->erase=u32_update_erase;
    if(u32_update_pending)ps->paint=u32_invalid_rect;else{ps->paint.left=0;ps->paint.top=0;ps->paint.right=g_wnd.w;ps->paint.bottom=g_wnd.h-32;}
    ps->restore=0;ps->inc_update=0;u32_update_pending=0;u32_update_erase=0;return ps->hdc;
}
__attribute__((dllexport)) BOOL EndPaint(void *hwnd, const U32_PAINTSTRUCT *ps) {
    (void)hwnd; (void)ps; compose(); return 1;
}
__attribute__((dllexport)) long GetWindowLongA(void *hwnd, int index) {
    if (!IsWindow(hwnd)) return 0;
    if (index==-16) return (long)u32_wnd_style;
    if (index==-20) return (long)u32_wnd_exstyle;
    if (index==-12) return (long)u32_wnd_id;
    if (index==-4) return (long)(unsigned long long)u32_wnd_proc;
    if (index==-21) return (long)(unsigned long long)u32_wnd_userdata;
    return 0;
}
__attribute__((dllexport)) long SetWindowLongA(void *hwnd, int index, long value) {
    if (!IsWindow(hwnd)) return 0;
    long old=GetWindowLongA(hwnd,index);
    if (index==-16) u32_wnd_style=(u32)value;
    else if (index==-20) u32_wnd_exstyle=(u32)value;
    else if (index==-12) u32_wnd_id=(u32)value;
    else if (index==-4) u32_wnd_proc=(void *)(unsigned long long)(unsigned long)value;
    else if (index==-21) u32_wnd_userdata=(void *)(unsigned long long)(unsigned long)value;
    else return 0;
    compose(); return old;
}
__attribute__((dllexport)) int GetSystemMetrics(int index) {
    ensure_fb();
    if (index==0) return (int)g_fw;
    if (index==1) return (int)g_fh;
    if (index==2) return 44;
    return 0;
}
__attribute__((dllexport)) unsigned long long SetTimer(void *hwnd, unsigned long long id, unsigned int ms, void *callback) {
    if(hwnd&&!IsWindow(hwnd))return 0;
    if(ms==0)ms=1;
    int slot=-1;
    if(id)for(int i=0;i<16;i++)if(u32_timers[i].used&&u32_timers[i].hwnd==hwnd&&u32_timers[i].id==id){slot=i;break;}
    if(!id) {
        for(unsigned long long candidate=1;candidate<0xFFFFFFFFULL;candidate++) {
            int taken=0;for(int i=0;i<16;i++)if(u32_timers[i].used&&u32_timers[i].hwnd==hwnd&&u32_timers[i].id==candidate){taken=1;break;}
            if(!taken){id=candidate;break;}
        }
    }
    if(slot<0)for(int i=0;i<16;i++)if(!u32_timers[i].used){slot=i;break;}
    if(slot<0||!id)return 0;
    unsigned long long now=u32_tick_now();
    u32_timers[slot].hwnd=hwnd;u32_timers[slot].id=id;u32_timers[slot].interval=ms;
    u32_timers[slot].next_due=now+ms;u32_timers[slot].callback=callback;u32_timers[slot].used=1;
    return id;
}
__attribute__((dllexport)) BOOL KillTimer(void *hwnd, unsigned long long id) {
    for(int i=0;i<16;i++)if(u32_timers[i].used&&u32_timers[i].hwnd==hwnd&&u32_timers[i].id==id){u32_timers[i].used=0;u32_timers[i].callback=0;return 1;}
    return 0;
}
__attribute__((dllexport)) void *GetDlgItem(void *dlg,int id){if(!IsWindow(dlg)||id<0)return 0;U32_DLGCTRL *c=u32_dlgctrl_get(dlg,(unsigned int)id,0);return c?(void *)(unsigned long long)(0x20000u+(unsigned)id):0;}
__attribute__((dllexport)) BOOL SetDlgItemTextA(void *dlg,int id,LPCSTR text){if(!IsWindow(dlg)||id<0||!text)return 0;U32_DLGCTRL *c=u32_dlgctrl_get(dlg,(unsigned int)id,1);if(!c)return 0;int i=0;while(text[i]&&i<127){c->text[i]=text[i];i++;}c->text[i]=0;return 1;}
__attribute__((dllexport)) unsigned int GetDlgItemTextA(void *dlg,int id,LPSTR out,int cap){if(!out||cap<=0||!IsWindow(dlg)||id<0)return 0;U32_DLGCTRL *c=u32_dlgctrl_get(dlg,(unsigned int)id,0);if(!c){out[0]=0;return 0;}int i=0;while(c->text[i]&&i+1<cap){out[i]=c->text[i];i++;}out[i]=0;return i;}
__attribute__((dllexport)) BOOL SetDlgItemTextW(void *dlg,int id,const unsigned short *text){char a[128];if(!text)return 0;u32_wide_to_ansi(a,sizeof(a),text);return SetDlgItemTextA(dlg,id,a);}
__attribute__((dllexport)) unsigned int GetDlgItemTextW(void *dlg,int id,unsigned short *out,int cap){char a[128];(void)GetDlgItemTextA(dlg,id,a,sizeof(a));if(!out||cap<=0)return 0;int i=0;while(a[i]&&i+1<cap){out[i]=(unsigned char)a[i];i++;}out[i]=0;return i;}
__attribute__((dllexport)) void *CreateMenu(void) {
    for (int i=0;i<16;i++) if (!u32_menus[i].used) {
        u32_menus[i].used=1; u32_menus[i].count=0; u32_menus[i].default_pos=-1;
        return (void *)(unsigned long long)(0x100+i);
    }
    return 0;
}
__attribute__((dllexport)) void *CreatePopupMenu(void) { return CreateMenu(); }
__attribute__((dllexport)) BOOL AppendMenuA(void *menu, unsigned int flags, unsigned long long id, LPCSTR text) {
    return u32_menu_insert(menu,0xFFFFFFFFu,flags,id,text);
}
__attribute__((dllexport)) BOOL DestroyMenu(void *menu) {
    U32_MENUDATA *m=u32_menu_data(menu); if (!m) return 0;
    for (int i=0;i<m->count;i++) if (m->items[i].submenu) { U32_MENUDATA *sub=u32_menu_data(m->items[i].submenu); if (sub) sub->used=0; }
    m->used=0; m->count=0; return 1;
}
__attribute__((dllexport)) BOOL TrackPopupMenu(void *menu, unsigned int flags, int x, int y, int reserved, void *hwnd, const U32_RECT *rect) {
    (void)reserved;(void)rect;
    U32_MENUDATA *m=u32_menu_data(menu);if(!m||!IsWindow(hwnd))return 0;
    /* SentinelOS has no popup-menu renderer yet. Preserve a real, deterministic
       keyboard/default-item path instead of claiming an interactive popup exists. */
    if(m->default_pos<0||m->default_pos>=m->count)return 0;
    U32_MENUITEM *it=&m->items[m->default_pos];
    if((it->flags&0x3u)!=0)return 0;
    if(!(flags&0x0100u))SendMessageA(hwnd,0x0111u,(unsigned long long)it->id,0);
    (void)x;(void)y;return 1;
}
static unsigned int u32_stock_resource_id(LPCSTR name,int icon) {
    unsigned long long raw=(unsigned long long)name;
    if(raw && raw<=0xFFFFULL)return (unsigned int)raw;
    if(!name)return 0;
    static const struct { const char *name; unsigned int id; } cursors[]={
        {"IDC_ARROW",32512},{"IDC_IBEAM",32513},{"IDC_WAIT",32514},{"IDC_CROSS",32515},
        {"IDC_UPARROW",32516},{"IDC_SIZENWSE",32642},{"IDC_SIZENESW",32643},
        {"IDC_SIZEWE",32644},{"IDC_SIZENS",32645},{"IDC_SIZEALL",32646},
        {"IDC_NO",32648},{"IDC_HAND",32649},{"IDC_APPSTARTING",32650},{"IDC_HELP",32651}
    };
    static const struct { const char *name; unsigned int id; } icons[]={
        {"IDI_APPLICATION",32512},{"IDI_HAND",32513},{"IDI_QUESTION",32514},
        {"IDI_EXCLAMATION",32515},{"IDI_ASTERISK",32516},{"IDI_WINLOGO",32517},{"IDI_SHIELD",32518}
    };
    const char *key=name;
    for(unsigned int i=0;i< (icon?sizeof(icons)/sizeof(icons[0]):sizeof(cursors)/sizeof(cursors[0]));i++) {
        const char *candidate=icon?icons[i].name:cursors[i].name;
        unsigned int j=0;
        while(key[j]&&candidate[j]) {
            char a=key[j],b=candidate[j];
            if(a>='a'&&a<='z')a-=32;if(b>='a'&&b<='z')b-=32;
            if(a!=b)break;j++;
        }
        if(!key[j]&&!candidate[j])return icon?icons[i].id:cursors[i].id;
    }
    return 0;
}
__attribute__((dllexport)) void *LoadCursorA(void *instance, LPCSTR name) {
    (void)instance;unsigned int id=u32_stock_resource_id(name,0);
    if(!id)return 0;
    return (void *)(unsigned long long)(0x330000u|id);
}
__attribute__((dllexport)) void *LoadIconA(void *instance, LPCSTR name) {
    (void)instance;unsigned int id=u32_stock_resource_id(name,1);
    if(!id)return 0;
    return (void *)(unsigned long long)(0x340000u|id);
}
__attribute__((dllexport)) void *LoadImageA(void *instance,LPCSTR name,unsigned int type,int cx,int cy,unsigned int flags) {
    (void)cx;(void)cy;(void)flags;
    if(type==1u)return LoadIconA(instance,name);
    if(type==2u)return LoadCursorA(instance,name);
    return 0;
}
__attribute__((dllexport)) BOOL DrawMenuBar(void *hwnd) { (void)hwnd; compose(); return 1; }
__attribute__((dllexport)) unsigned long GetMessagePos(void) { return ((unsigned long)(g_my & 0xFFFF)<<16) | (unsigned long)(g_mx & 0xFFFF); }
__attribute__((dllexport)) long GetMessageTime(void) { return (long)u32_last_msg_time; }
__attribute__((dllexport)) unsigned int GetDoubleClickTime(void) { return 500; }
__attribute__((dllexport)) BOOL MessageBeep(unsigned int type) { (void)type; static const char bell=7; return sy3(SYS_Write,1,(u64)&bell,1)!=((u64)-1); }
__attribute__((dllexport)) BOOL RedrawWindow(void *hwnd, const U32_RECT *update, void *region, unsigned int flags) {
    (void)hwnd; (void)update; (void)region; (void)flags; compose(); return 1;
}
__attribute__((dllexport)) BOOL ScrollWindow(void *hwnd, int dx, int dy, const U32_RECT *scroll, const U32_RECT *clip) {
    (void)hwnd; (void)dx; (void)dy; (void)scroll; (void)clip; compose(); return 1;
}
__attribute__((dllexport)) BOOL GetUpdateRect(void *hwnd, U32_RECT *r, BOOL erase) {
    if(!IsWindow(hwnd)||!r){if(r)r->left=r->top=r->right=r->bottom=0;return 0;}
    if(!u32_update_pending){r->left=r->top=r->right=r->bottom=0;return 0;}
    *r=u32_invalid_rect;if(erase&&u32_update_erase)compose();return 1;
}
__attribute__((dllexport)) void *SetFocus(void *hwnd) { void *old=u32_focus; u32_focus=hwnd; return old; }
__attribute__((dllexport)) void *GetFocus(void) { return u32_focus; }


/* Win32 compatibility exports backed by the current SentinelOS window model. */
typedef const U32_WCHAR *U32_LPCWSTR;
typedef U32_WCHAR *U32_LPWSTR;
typedef unsigned long long U32_WPARAM64;
typedef long long U32_LPARAM64;
typedef BOOL (*U32_ENUMWINDOWSPROC)(void *, long long);
typedef struct {
    unsigned int style;
    void *proc;
    int cls_extra, wnd_extra;
    void *instance, *icon, *cursor, *background;
    U32_LPCWSTR menu_name, class_name;
} U32_WNDCLASSW;

static void u32_wide_to_ansi(char *out, unsigned int cap, U32_LPCWSTR in) {
    unsigned int i=0;
    if (!out || !cap) return;
    if (in) {
        while (in[i] && i+1<cap) {
            unsigned int c=in[i];
            out[i]=(c < 128) ? (char)c : '?';
            i++;
        }
    }
    out[i]=0;
}
static int u32_ansi_to_wide(U32_LPWSTR out, int cap, const char *in) {
    int i=0;
    if (!out || cap<=0) return 0;
    if (in) while (in[i] && i+1<cap) { out[i]=(unsigned char)in[i]; i++; }
    out[i]=0;
    return i;
}
static int u32_wide_equal_ansi(U32_LPCWSTR wide, const char *ansi) {
    unsigned int i=0;
    if (!wide || !ansi) return wide==0 && ansi==0;
    while (wide[i] && ansi[i]) {
        unsigned int c=wide[i];
        if (c>=128 || (char)c!=ansi[i]) return 0;
        i++;
    }
    return wide[i]==0 && ansi[i]==0;
}

__attribute__((dllexport)) int MessageBoxW(void *hwnd, U32_LPCWSTR text, U32_LPCWSTR caption, u32 type) {
    char atext[512], acaption[128];
    u32_wide_to_ansi(atext, sizeof(atext), text);
    u32_wide_to_ansi(acaption, sizeof(acaption), caption);
    return MessageBoxA(hwnd, atext, acaption, type);
}
__attribute__((dllexport)) void *CreateWindowExW(
    u32 ex, U32_LPCWSTR cls, U32_LPCWSTR title, u32 style,
    int x, int y, int w, int h,
    void *parent, void *menu, void *inst, void *param)
{
    char class_a[128], title_a[64];
    u32_wide_to_ansi(class_a, sizeof(class_a), cls);
    u32_wide_to_ansi(title_a, sizeof(title_a), title);
    return CreateWindowExA(ex, cls ? class_a : 0, title ? title_a : 0, style,
                           x,y,w,h,parent,menu,inst,param);
}
__attribute__((dllexport)) unsigned short RegisterClassW(const U32_WNDCLASSW *wc) {
    U32_WNDCLASSA a;
    char class_a[128], menu_a[128];
    if (!wc) return 0;
    u32_wide_to_ansi(class_a, sizeof(class_a), wc->class_name);
    u32_wide_to_ansi(menu_a, sizeof(menu_a), wc->menu_name);
    a.style=wc->style; a.proc=wc->proc; a.cls_extra=wc->cls_extra;
    a.wnd_extra=wc->wnd_extra; a.instance=wc->instance; a.icon=wc->icon;
    a.cursor=wc->cursor; a.background=wc->background;
    a.menu_name=wc->menu_name ? menu_a : 0;
    a.class_name=wc->class_name ? class_a : 0;
    return RegisterClassA(&a);
}
__attribute__((dllexport)) U32_LRESULT DefWindowProcW(void *hwnd, U32_UINT msg, U32_WPARAM wp, U32_LPARAM lp) {
    return DefWindowProcA(hwnd,msg,wp,lp);
}
__attribute__((dllexport)) U32_LRESULT SendMessageW(void *hwnd, U32_UINT msg, U32_WPARAM wp, U32_LPARAM lp) {
    if (msg==0x000Cu) {
        char title[64]; u32_wide_to_ansi(title,sizeof(title),(U32_LPCWSTR)lp);
        return SetWindowTextA(hwnd,title);
    }
    if (msg==0x000Du) {
        char title[256]; int n=GetWindowTextA(hwnd,title,sizeof(title));
        return u32_ansi_to_wide((U32_LPWSTR)lp,(int)wp,title) < n ? (U32_LRESULT)wp-1 : n;
    }
    return SendMessageA(hwnd,msg,wp,lp);
}
__attribute__((dllexport)) BOOL PostMessageW(void *hwnd, U32_UINT msg, U32_WPARAM wp, U32_LPARAM lp) {
    return PostMessageA(hwnd,msg,wp,lp);
}
__attribute__((dllexport)) BOOL PeekMessageW(MSG *msg, void *hwnd, U32_UINT min, U32_UINT max, U32_UINT remove) {
    return PeekMessageA(msg,hwnd,min,max,remove);
}
__attribute__((dllexport)) BOOL GetMessageW(MSG *msg, void *hwnd, U32_UINT min, U32_UINT max) {
    return GetMessageA(msg,hwnd,min,max);
}
static void *u32_dialog_hwnd; static int u32_dialog_active; static long long u32_dialog_result;
__attribute__((dllexport)) void *GetNextDlgTabItem(void *,void *,BOOL);
__attribute__((dllexport)) void *SetFocus(void *);
typedef long long (*U32_DIALOGPROC)(void *,unsigned int,unsigned long long,long long);
__attribute__((dllexport)) void *CreateDialogParamA(void *instance,const char *templ,void *parent,void *proc,long long param){
 (void)param;const char *title="Dialog";if(templ&&(unsigned long long)templ>0x10000ULL)title=templ;
 void *hwnd=CreateWindowExA(0,"#32770",title,0x00CF0000u,80,80,320,200,parent,0,instance,0);
 if(!hwnd)return 0;u32_wnd_proc=proc;u32_dialog_hwnd=hwnd;u32_dialog_active=0;return hwnd;
}
__attribute__((dllexport)) void *CreateDialogParamW(void *instance,const unsigned short *templ,void *parent,void *proc,long long param){
 char title[128];const char *p="Dialog";if(templ&&(unsigned long long)templ>0x10000ULL){u32_wide_to_ansi(title,sizeof(title),templ);p=title;}return CreateDialogParamA(instance,p,parent,proc,param);
}
__attribute__((dllexport)) int DialogBoxParamA(void *instance,const char *templ,void *parent,void *proc,long long param){
 void *hwnd=CreateDialogParamA(instance,templ,parent,proc,param);if(!hwnd)return -1;u32_dialog_hwnd=hwnd;u32_dialog_active=1;u32_dialog_result=-1;
 while(u32_dialog_active){MSG msg;if(!GetMessageA(&msg,0,0,0)){u32_dialog_active=0;break;}DispatchMessageA(&msg);}
 return (int)u32_dialog_result;
}
__attribute__((dllexport)) int DialogBoxParamW(void *instance,const unsigned short *templ,void *parent,void *proc,long long param){char title[128];const char *p="Dialog";if(templ&&(unsigned long long)templ>0x10000ULL){u32_wide_to_ansi(title,sizeof(title),templ);p=title;}return DialogBoxParamA(instance,p,parent,proc,param);}
__attribute__((dllexport)) BOOL EndDialog(void *hwnd,int result){if(!hwnd||hwnd!=u32_dialog_hwnd||!u32_dialog_active)return 0;u32_dialog_result=result;u32_dialog_active=0;DestroyWindow(hwnd);return 1;}
__attribute__((dllexport)) BOOL IsDialogMessageA(void *dlg,MSG *msg){if(!IsWindow(dlg)||!msg)return 0;if(msg->message==0x0100u&&msg->wParam==9u){void *next=GetNextDlgTabItem(dlg,(void *)(unsigned long long)msg->hwnd,0);if(next)SetFocus(next);return 1;}if(msg->message==0x0100u&&msg->wParam==13u){SendMessageA(dlg,0x0111u,1,0);return 1;}if(msg->message==0x0100u&&msg->wParam==27u){SendMessageA(dlg,0x0010u,0,0);return 1;}return 0;}
__attribute__((dllexport)) BOOL IsDialogMessageW(void *dlg,MSG *msg){return IsDialogMessageA(dlg,msg);}
__attribute__((dllexport)) int GetWindowTextW(void *hwnd, U32_LPWSTR out, int cap) {
    char text[256];
    int n=GetWindowTextA(hwnd,text,sizeof(text));
    if (!out || cap<=0) return 0;
    return u32_ansi_to_wide(out,cap,text) < n ? cap-1 : n;
}
__attribute__((dllexport)) BOOL SetWindowTextW(void *hwnd, U32_LPCWSTR title) {
    char text[64];
    if (!title) return SetWindowTextA(hwnd,0);
    u32_wide_to_ansi(text,sizeof(text),title);
    return SetWindowTextA(hwnd,text);
}
__attribute__((dllexport)) int GetClassNameW(void *hwnd, U32_LPWSTR out, int cap) {
    char name[128];
    int n=GetClassNameA(hwnd,name,sizeof(name));
    if (!out || cap<=0) return 0;
    return u32_ansi_to_wide(out,cap,name) < n ? cap-1 : n;}
__attribute__((dllexport)) void *LoadCursorW(void *instance, const U32_WCHAR *name) {    char narrow[128];
    const char *arg;
    if ((unsigned long long)name <= 0xFFFFULL) arg=(const char *)name;
    else { if (!name) return 0; u32_wide_to_ansi(narrow,sizeof(narrow),name); arg=narrow; }
    return LoadCursorA(instance,arg);
}
__attribute__((dllexport)) void *LoadIconW(void *instance, const U32_WCHAR *name) {
    char narrow[128];
    const char *arg;
    if ((unsigned long long)name <= 0xFFFFULL) arg=(const char *)name;
    else { if (!name) return 0; u32_wide_to_ansi(narrow,sizeof(narrow),name); arg=narrow; }
    return LoadIconA(instance,arg);
}
__attribute__((dllexport)) void *LoadImageW(void *instance, const U32_WCHAR *name, unsigned int type, int cx, int cy, unsigned int flags) {
    char narrow[128];
    const char *arg;
    if ((unsigned long long)name <= 0xFFFFULL) arg=(const char *)name;
    else { if (!name) return 0; u32_wide_to_ansi(narrow,sizeof(narrow),name); arg=narrow; }
    return LoadImageA(instance,arg,type,cx,cy,flags);
}
__attribute__((dllexport)) void *FindWindowA(LPCSTR class_name, LPCSTR title) {
    if (!g_wnd.visible) return 0;
    if (title) {
        int i=0;
        while (title[i] && g_wnd.title[i] && title[i]==g_wnd.title[i]) i++;
        if (title[i] || g_wnd.title[i]) return 0;
    }
    if (class_name) {
        const char *actual="SentinelWindow"; int i=0;
        while (class_name[i] && actual[i] && class_name[i]==actual[i]) i++;
        if (class_name[i] || actual[i]) return 0;
    }
    return (void *)1;
}
__attribute__((dllexport)) void *FindWindowW(U32_LPCWSTR class_name, U32_LPCWSTR title) {
    if (!g_wnd.visible) return 0;
    if (title && !u32_wide_equal_ansi(title,g_wnd.title)) return 0;
    if (class_name && !u32_wide_equal_ansi(class_name,"SentinelWindow")) return 0;
    return (void *)1;
}
__attribute__((dllexport)) void *FindWindowExA(void *parent, void *after, LPCSTR class_name, LPCSTR title) {
    if (parent && parent!=(void *)2) return 0;
    if (after) return 0;
    return FindWindowA(class_name,title);
}
__attribute__((dllexport)) void *FindWindowExW(void *parent, void *after, U32_LPCWSTR class_name, U32_LPCWSTR title) {
    if (parent && parent!=(void *)2) return 0;
    if (after) return 0;
    return FindWindowW(class_name,title);
}
__attribute__((dllexport)) void *GetParent(void *hwnd) {
    if (hwnd==(void *)1 && g_wnd.visible) return (void *)2;
    return 0;
}
__attribute__((dllexport)) void *SetParent(void *hwnd, void *parent) {
    if (!IsWindow(hwnd) || (parent && parent!=(void *)2 && parent!=(void *)1)) return 0;
    void *old=u32_wnd_parent; u32_wnd_parent=parent; compose(); return old;
}
__attribute__((dllexport)) void *GetWindow(void *hwnd, unsigned int command) {
    if (hwnd==(void *)2 && g_wnd.visible && (command==0 || command==1 || command==5)) return (void *)1;
    if (hwnd==(void *)1 && g_wnd.visible && command==4) return 0;
    if (hwnd==(void *)1 && g_wnd.visible && command==6 && !g_wnd.minimized) return (void *)1;
    return 0;
}
__attribute__((dllexport)) u32 GetWindowThreadProcessId(void *hwnd, u32 *process_id) {
    if (!IsWindow(hwnd)) { if (process_id) *process_id=0; return 0; }
    if (process_id) *process_id=(u32)sy3(2,0,0,0);
    return 1;
}
__attribute__((dllexport)) void *GetAncestor(void *hwnd, unsigned int flags) {
    if (hwnd!=(void *)1 || !g_wnd.visible) return 0;
    if (flags==1 || flags==2 || flags==3) return (void *)2;
    return 0;
}
__attribute__((dllexport)) BOOL EnumWindows(U32_ENUMWINDOWSPROC callback, long long param) {
    if (!callback) return 0;
    if (IsWindowVisible((void *)1)) return callback((void *)1,param);
    return 1;
}
__attribute__((dllexport)) BOOL EnumChildWindows(void *parent, U32_ENUMWINDOWSPROC callback, long long param) {
    if(!callback || !IsWindow(parent))return 0;
    /* Snapshot handles first because callbacks may destroy child windows. */
    void *snapshot[64]; int count=0;
    for(int i=0;i<64;i++)if(u32_child_windows[i].used&&u32_child_windows[i].parent==parent)
        snapshot[count++]=(void *)(unsigned long long)(0x1000u+(unsigned)i);
    for(int i=0;i<count;i++) {
        U32_CHILDWND *child=u32_child_from_hwnd(snapshot[i]);
        if(child && child->parent==parent && !callback(snapshot[i],param))return 0;
    }
    return 1;
}
__attribute__((dllexport)) BOOL EnumThreadWindows(u32 thread_id, U32_ENUMWINDOWSPROC callback, long long param) {
    if (!callback) return 0;
    if (thread_id==1 && IsWindowVisible((void *)1)) return callback((void *)1,param);
    return 1;
}
__attribute__((dllexport)) void *GetTopWindow(void *parent) {
    return parent==(u32_wnd_parent ? u32_wnd_parent : (void *)2) && g_wnd.visible ? (void *)1 : 0;
}
__attribute__((dllexport)) BOOL BringWindowToTop(void *hwnd) { return SetForegroundWindow(hwnd); }
__attribute__((dllexport)) BOOL GetWindowInfo(void *hwnd, U32_WINDOWINFO *info) {
    if (hwnd!=(void *)1 || !g_wnd.visible || !info || info->cbSize<sizeof(U32_WINDOWINFO)) return 0;
    info->rcWindow.left=g_wnd.x; info->rcWindow.top=g_wnd.y;
    info->rcWindow.right=g_wnd.x+g_wnd.w; info->rcWindow.bottom=g_wnd.y+g_wnd.h;
    info->rcClient.left=g_wnd.x+1; info->rcClient.top=g_wnd.y+32;
    info->rcClient.right=g_wnd.x+g_wnd.w-1; info->rcClient.bottom=g_wnd.y+g_wnd.h-1;
    info->dwStyle=u32_wnd_style; info->dwExStyle=u32_wnd_exstyle;
    info->dwWindowStatus=(GetForegroundWindow()==hwnd)?1u:0u;
    info->cxWindowBorders=1; info->cyWindowBorders=1;
    info->atomWindowType=1; info->wCreatorVersion=1;
    return 1;
}
__attribute__((dllexport)) u32 GetWindowModuleFileNameA(void *hwnd, char *out, u32 cap) {
    const char *path="C:\\sentinel.exe"; u32 i=0;
    if (hwnd!=(void *)1 || !g_wnd.visible || !out || !cap) return 0;
    while (path[i] && i+1<cap) { out[i]=path[i]; i++; }
    out[i]=0; return path[i] ? cap : i;
}
__attribute__((dllexport)) unsigned long long GetWindowContextHelpId(void *hwnd) { return IsWindow(hwnd) ? u32_context_help_id : 0; }
__attribute__((dllexport)) BOOL SetWindowContextHelpId(void *hwnd, unsigned long long id) {
    if (!IsWindow(hwnd)) return 0; u32_context_help_id=id; return 1;
}
__attribute__((dllexport)) void *GetMenu(void *hwnd) { return IsWindow(hwnd) ? u32_wnd_menu : 0; }
__attribute__((dllexport)) BOOL SetMenu(void *hwnd, void *menu) {
    if (!IsWindow(hwnd)) return 0; u32_wnd_menu=menu; compose(); return 1;
}
__attribute__((dllexport)) void *GetSubMenu(void *menu, int position) { U32_MENUDATA *m=u32_menu_data(menu); int i=u32_menu_index(m,(unsigned long long)position,1); return i<0 ? 0 : m->items[i].submenu; }
__attribute__((dllexport)) int GetMenuItemCount(void *menu) { U32_MENUDATA *m=u32_menu_data(menu); return m ? m->count : -1; }
__attribute__((dllexport)) unsigned int GetMenuItemID(void *menu, int position) { U32_MENUDATA *m=u32_menu_data(menu); int i=u32_menu_index(m,(unsigned long long)position,1); return i<0 ? 0xFFFFFFFFu : (unsigned int)m->items[i].id; }
__attribute__((dllexport)) unsigned int GetMenuState(void *menu, unsigned int item, unsigned int flags) { U32_MENUDATA *m=u32_menu_data(menu); int i=u32_menu_index(m,item,(flags&0x0400u)!=0); return i<0 ? 0xFFFFFFFFu : m->items[i].flags; }
__attribute__((dllexport)) unsigned int CheckMenuItem(void *menu, unsigned int item, unsigned int check) { U32_MENUDATA *m=u32_menu_data(menu); int i=u32_menu_index(m,item,(check&0x0400u)!=0); if (i<0) return 0xFFFFFFFFu; unsigned int old=m->items[i].flags&0x8u; m->items[i].flags=(m->items[i].flags&~0x8u)|((check&0x8u)?0x8u:0);m->items[i].state=(m->items[i].state&~0x8u)|((check&0x8u)?0x8u:0);return old; }
__attribute__((dllexport)) unsigned int EnableMenuItem(void *menu, unsigned int item, unsigned int enable) { U32_MENUDATA *m=u32_menu_data(menu); int i=u32_menu_index(m,item,(enable&0x0400u)!=0); if (i<0) return 0xFFFFFFFFu; unsigned int old=m->items[i].flags&0x3u; m->items[i].flags=(m->items[i].flags&~0x3u)|(enable&0x3u);m->items[i].state=(m->items[i].state&~0x3u)|(enable&0x3u);return old; }
__attribute__((dllexport)) BOOL DeleteMenu(void *menu, unsigned int item, unsigned int flags) { return u32_menu_remove(menu,item,flags,1); }
__attribute__((dllexport)) BOOL RemoveMenu(void *menu, unsigned int item, unsigned int flags) { return u32_menu_remove(menu,item,flags,0); }
__attribute__((dllexport)) BOOL InsertMenuA(void *menu, unsigned int position, unsigned int flags, unsigned long long id, LPCSTR text) { return u32_menu_insert(menu,position,flags,id,text); }
__attribute__((dllexport)) BOOL ModifyMenuA(void *menu, unsigned int item, unsigned int flags, unsigned long long id, LPCSTR text) { U32_MENUDATA *m=u32_menu_data(menu); int i=u32_menu_index(m,item,(flags&0x0400u)!=0); if (i<0) return 0; m->items[i].flags=flags;m->items[i].state=flags&U32_MFS_STATE_MASK;m->items[i].type=flags&U32_MFT_TYPE_MASK;m->items[i].id=id; m->items[i].submenu=(flags&0x0010u)?(void *)id:0; u32_menu_text(m->items[i].text,sizeof(m->items[i].text),text); return 1; }
typedef struct { unsigned int cbSize,fMask,fType,fState,wID; void *hSubMenu,*hbmpChecked,*hbmpUnchecked; unsigned long long dwItemData; char *dwTypeData; unsigned int cch; void *hbmpItem; } U32_MENUITEMINFOA;
typedef struct { unsigned int cbSize,fMask,fType,fState,wID; void *hSubMenu,*hbmpChecked,*hbmpUnchecked; unsigned long long dwItemData; U32_WCHAR *dwTypeData; unsigned int cch; void *hbmpItem; } U32_MENUITEMINFOW;
__attribute__((dllexport)) BOOL SetMenuItemInfoA(void *menu,unsigned int item,BOOL bypos,const U32_MENUITEMINFOA *info) {
    U32_MENUDATA *m=u32_menu_data(menu);int i=u32_menu_index(m,item,bypos!=0);
    if(i<0||!info||info->cbSize<sizeof(U32_MENUITEMINFOA))return 0;
    U32_MENUITEM *it=&m->items[i];
    if(info->fMask&U32_MIIM_ID)it->id=info->wID;
    if(info->fMask&U32_MIIM_SUBMENU){it->submenu=info->hSubMenu;if(it->submenu)it->flags|=0x0010u;else it->flags&=~0x0010u;}
    if(info->fMask&U32_MIIM_STATE){it->state=info->fState&U32_MFS_STATE_MASK;it->flags=(it->flags&~0x0000008Bu)|(info->fState&0x0000008Bu);}
    if(info->fMask&U32_MIIM_FTYPE){it->type=info->fType&U32_MFT_TYPE_MASK;it->flags=(it->flags&~U32_MFT_TYPE_MASK)|it->type;}
    if(info->fMask&U32_MIIM_DATA)it->data=info->dwItemData;
    if(info->fMask&U32_MIIM_CHECKMARKS){it->bmp_checked=info->hbmpChecked;it->bmp_unchecked=info->hbmpUnchecked;}
    if(info->fMask&U32_MIIM_BITMAP)it->bmp_item=info->hbmpItem;
    if((info->fMask&U32_MIIM_STRING)&&info->dwTypeData)u32_menu_text(it->text,sizeof(it->text),info->dwTypeData);
    return 1;
}
__attribute__((dllexport)) BOOL GetMenuItemInfoA(void *menu,unsigned int item,BOOL bypos,U32_MENUITEMINFOA *info) {
    U32_MENUDATA *m=u32_menu_data(menu);int i=u32_menu_index(m,item,bypos!=0);
    if(i<0||!info||info->cbSize<sizeof(U32_MENUITEMINFOA))return 0;
    U32_MENUITEM *it=&m->items[i];
    if(info->fMask&U32_MIIM_ID)info->wID=(unsigned int)it->id;
    if(info->fMask&U32_MIIM_SUBMENU)info->hSubMenu=it->submenu;
    if(info->fMask&U32_MIIM_STATE)info->fState=it->state;
    if(info->fMask&U32_MIIM_FTYPE)info->fType=it->type;
    if(info->fMask&U32_MIIM_DATA)info->dwItemData=it->data;
    if(info->fMask&U32_MIIM_CHECKMARKS){info->hbmpChecked=it->bmp_checked;info->hbmpUnchecked=it->bmp_unchecked;}
    if(info->fMask&U32_MIIM_BITMAP)info->hbmpItem=it->bmp_item;
    if(info->fMask&U32_MIIM_STRING){unsigned int n=0;while(it->text[n])n++;if(info->dwTypeData&&info->cch){unsigned int j=0;while(it->text[j]&&j+1<info->cch){info->dwTypeData[j]=it->text[j];j++;}info->dwTypeData[j]=0;}info->cch=n;}
    return 1;
}
__attribute__((dllexport)) int GetMenuStringA(void *menu, unsigned int item, char *out, int cap, unsigned int flags) { U32_MENUDATA *m=u32_menu_data(menu); int i=u32_menu_index(m,item,(flags&0x0400u)!=0); if (i<0 || !out || cap<=0) return 0; int n=0; while (m->items[i].text[n] && n+1<cap) { out[n]=m->items[i].text[n]; n++; } out[n]=0; return n; }
__attribute__((dllexport)) BOOL SetMenuDefaultItem(void *menu, unsigned int item, unsigned int by_position) { U32_MENUDATA *m=u32_menu_data(menu); if (!m) return 0; int i=u32_menu_index(m,item,by_position!=0); if (i<0) return 0;for(int j=0;j<m->count;j++)m->items[j].state&=~0x100u;m->items[i].state|=0x100u;m->default_pos=i; return 1; }
__attribute__((dllexport)) unsigned int GetMenuDefaultItem(void *menu, unsigned int by_position, unsigned int flags) { U32_MENUDATA *m=u32_menu_data(menu); (void)flags; if (!m || m->default_pos<0 || m->default_pos>=m->count) return 0xFFFFFFFFu; return by_position ? (unsigned int)m->default_pos : (unsigned int)m->items[m->default_pos].id; }
__attribute__((dllexport)) void *GetSystemMenu(void *hwnd, BOOL revert) { if (!IsWindow(hwnd)) return 0; if (revert) { if (u32_system_menu) DestroyMenu(u32_system_menu); u32_system_menu=0; } if (!u32_system_menu) u32_system_menu=CreateMenu(); return u32_system_menu; }
__attribute__((dllexport)) BOOL GetScrollInfo(void *hwnd,int bar,U32_SCROLLINFO *info){
    if(!IsWindow(hwnd)||!info||(bar!=0&&bar!=1)||info->cbSize<sizeof(U32_SCROLLINFO))return 0;
    int i=bar==0?0:1;
    if(info->fMask&1u){info->nMin=u32_scroll_min[i];info->nMax=u32_scroll_max[i];}
    if(info->fMask&2u)info->nPage=(unsigned int)u32_scroll_page[i];
    if(info->fMask&4u)info->nPos=u32_scroll_pos[i];
    if(info->fMask&16u)info->nTrackPos=u32_scroll_pos[i];
    return 1;
}


/* Additional Win32 compatibility APIs. Unsupported platform services fail explicitly. */
__attribute__((dllexport)) int SetScrollInfo(void *hwnd,int bar,const U32_SCROLLINFO *info,BOOL redraw){(void)redraw;if(!IsWindow(hwnd)||!info||(bar!=0&&bar!=1)||info->cbSize<sizeof(U32_SCROLLINFO))return 0;int i=bar==0?0:1,old=u32_scroll_pos[i];if(info->fMask&1u){u32_scroll_min[i]=info->nMin;u32_scroll_max[i]=info->nMax;}if(info->fMask&2u)u32_scroll_page[i]=(int)info->nPage;if(info->fMask&4u)u32_scroll_pos[i]=info->nPos;if(u32_scroll_pos[i]<u32_scroll_min[i])u32_scroll_pos[i]=u32_scroll_min[i];if(u32_scroll_pos[i]>u32_scroll_max[i])u32_scroll_pos[i]=u32_scroll_max[i];return old;}
__attribute__((dllexport)) BOOL ShowScrollBar(void *hwnd,int bar,BOOL show){if(!IsWindow(hwnd)||bar<0||bar>1)return 0;u32_scroll_visible[bar]=show!=0;return 1;}
__attribute__((dllexport)) BOOL SetScrollRange(void *hwnd,int bar,int min,int max,BOOL redraw){(void)redraw;if(!IsWindow(hwnd)||bar<0||bar>1||max<min)return 0;u32_scroll_min[bar]=min;u32_scroll_max[bar]=max;if(u32_scroll_pos[bar]<min)u32_scroll_pos[bar]=min;if(u32_scroll_pos[bar]>max)u32_scroll_pos[bar]=max;return 1;}
__attribute__((dllexport)) BOOL GetScrollRange(void *hwnd,int bar,int *min,int *max){if(!IsWindow(hwnd)||bar<0||bar>1||!min||!max)return 0;*min=u32_scroll_min[bar];*max=u32_scroll_max[bar];return 1;}
__attribute__((dllexport)) int GetScrollPos(void *hwnd,int bar){if(!IsWindow(hwnd)||bar<0||bar>1)return 0;return u32_scroll_pos[bar];}
__attribute__((dllexport)) int SetScrollPos(void *hwnd,int bar,int pos,BOOL redraw){(void)redraw;if(!IsWindow(hwnd)||bar<0||bar>1)return 0;int old=u32_scroll_pos[bar];if(pos<u32_scroll_min[bar])pos=u32_scroll_min[bar];if(pos>u32_scroll_max[bar])pos=u32_scroll_max[bar];u32_scroll_pos[bar]=pos;return old;}
__attribute__((dllexport)) int SetWindowRgn(void *hwnd,void *region,BOOL redraw){if(!IsWindow(hwnd))return 0;u32_wnd_region=region;if(redraw)compose();return 1;}
__attribute__((dllexport)) int GetWindowRgn(void *hwnd,void *region){if(!IsWindow(hwnd)||!region)return 0;return u32_wnd_region?2:1;}
__attribute__((dllexport)) int MapWindowPoints(void *from,void *to,U32_POINT *pts,unsigned int count){if(!pts||count>256)return 0;int dx=0,dy=0;if(IsWindow(from)){dx+=g_wnd.x;dy+=g_wnd.y+24;}if(IsWindow(to)){dx-=g_wnd.x;dy-=g_wnd.y+24;}for(unsigned int i=0;i<count;i++){pts[i].x+=dx;pts[i].y+=dy;}return (int)(((unsigned short)dx)|((unsigned int)(unsigned short)dy<<16));}
__attribute__((dllexport)) BOOL ClientToScreen(void *hwnd,U32_POINT *pt){if(!IsWindow(hwnd)||!pt)return 0;pt->x+=g_wnd.x;pt->y+=g_wnd.y+24;return 1;}
__attribute__((dllexport)) BOOL ScreenToClient(void *hwnd,U32_POINT *pt){if(!IsWindow(hwnd)||!pt)return 0;pt->x-=g_wnd.x;pt->y-=g_wnd.y+24;return 1;}
__attribute__((dllexport)) void *WindowFromPoint(U32_POINT pt){return IsWindow((void *)1)&&hit(pt.x,pt.y,g_wnd.x,g_wnd.y,g_wnd.x+g_wnd.w,g_wnd.y+g_wnd.h)?(void *)1:0;}
__attribute__((dllexport)) void *ChildWindowFromPoint(void *parent,U32_POINT pt){if(!IsWindow(parent))return 0;return hit(pt.x,pt.y,0,0,g_wnd.w,g_wnd.h-32)?(void *)1:0;}
__attribute__((dllexport)) BOOL SetDoubleClickTime(unsigned int ms){if(ms==0)ms=500;if(ms<1||ms>5000)return 0;u32_double_click_time=ms;return 1;}
__attribute__((dllexport)) void *GetLastActivePopup(void *hwnd){return IsWindow(hwnd)?hwnd:0;}
__attribute__((dllexport)) int GetWindowTextLengthA(void *hwnd) {
    U32_CHILDWND *child=u32_child_from_hwnd(hwnd);
    if(child){int n=0;while(child->text[n])n++;return n;} if (!IsWindow(hwnd)) return 0; int n=0; while (g_wnd.title[n]) n++; return n; }
__attribute__((dllexport)) int GetWindowTextLengthW(void *hwnd) { return GetWindowTextLengthA(hwnd); }
__attribute__((dllexport)) long long GetWindowLongPtrA(void *hwnd, int index) { if (!IsWindow(hwnd)) return 0; if (index==-4) return (long long)(unsigned long long)u32_wnd_proc; if (index==-21) return (long long)(unsigned long long)u32_wnd_userdata; return (long long)GetWindowLongA(hwnd,index); }
__attribute__((dllexport)) long long SetWindowLongPtrA(void *hwnd, int index, long long value) { if (!IsWindow(hwnd)) return 0; long long old=GetWindowLongPtrA(hwnd,index); if (index==-4) u32_wnd_proc=(void *)(unsigned long long)value; else if (index==-21) u32_wnd_userdata=(void *)(unsigned long long)value; else return (long long)SetWindowLongA(hwnd,index,(long)value); compose(); return old; }
__attribute__((dllexport)) long long GetWindowLongPtrW(void *hwnd, int index) { return GetWindowLongPtrA(hwnd,index); }
__attribute__((dllexport)) long long SetWindowLongPtrW(void *hwnd, int index, long long value) { return SetWindowLongPtrA(hwnd,index,value); }
__attribute__((dllexport)) unsigned long GetClassLongA(void *hwnd,int index){if(!IsWindow(hwnd))return 0;if(index==-26)return (unsigned long)u32_class_style;if(index==-24)return (unsigned long)(unsigned long long)u32_class_proc;if(index==-20)return (unsigned long)u32_class_extra[0];if(index==-18)return (unsigned long)u32_class_extra[1];return 0;}
__attribute__((dllexport)) long SetClassLongA(void *hwnd,int index,long value){if(!IsWindow(hwnd))return 0;long old=(long)GetClassLongA(hwnd,index);if(index==-26)u32_class_style=value;else if(index==-24)u32_class_proc=(void *)(unsigned long long)(unsigned long)value;else if(index==-20)u32_class_extra[0]=value;else if(index==-18)u32_class_extra[1]=value;else return 0;return old;}
__attribute__((dllexport)) unsigned long long GetClassLongPtrA(void *hwnd,int index){return (unsigned long long)GetClassLongA(hwnd,index);}
__attribute__((dllexport)) unsigned long long SetClassLongPtrA(void *hwnd,int index,unsigned long long value){return (unsigned long long)SetClassLongA(hwnd,index,(long)value);}
__attribute__((dllexport)) unsigned long GetClassLongW(void *hwnd,int index){return GetClassLongA(hwnd,index);}
__attribute__((dllexport)) long SetClassLongW(void *hwnd,int index,long value){return SetClassLongA(hwnd,index,value);}
__attribute__((dllexport)) unsigned long long GetClassLongPtrW(void *hwnd,int index){return GetClassLongPtrA(hwnd,index);}
__attribute__((dllexport)) unsigned long long SetClassLongPtrW(void *hwnd,int index,unsigned long long value){return SetClassLongPtrA(hwnd,index,value);}
__attribute__((dllexport)) u32 GetWindowModuleFileNameW(void *hwnd, U32_LPWSTR out, u32 cap) {
    char narrow[260]; u32 n=GetWindowModuleFileNameA(hwnd,narrow,sizeof(narrow));
    if (!n || !out || !cap) return 0;
    int copied=u32_ansi_to_wide(out,(int)cap,narrow);
    return (u32)copied+1<n ? cap : (u32)copied;
}
__attribute__((dllexport)) int GetWindowRgnBox(void *hwnd,U32_RECT *rect){
    if(!IsWindow(hwnd)||!rect)return 0;
    if(!u32_wnd_region){rect->left=rect->top=rect->right=rect->bottom=0;return 0;}
    /* SentinelOS currently represents a window region as an opaque whole-window
       region handle, so its bounding box is the window's client-independent bounds. */
    rect->left=0;rect->top=0;rect->right=g_wnd.w;rect->bottom=g_wnd.h;
    if(rect->right<=rect->left||rect->bottom<=rect->top)return 1;
    return 2;
}
__attribute__((dllexport)) BOOL GetWindowDisplayAffinity(void *hwnd,unsigned int *affinity){if(!IsWindow(hwnd)||!affinity)return 0;*affinity=u32_display_affinity;return 1;}
__attribute__((dllexport)) BOOL SetWindowDisplayAffinity(void *hwnd,unsigned int affinity){if(!IsWindow(hwnd)||(affinity!=0&&affinity!=1&&affinity!=0x11u))return 0;u32_display_affinity=affinity;return 1;}
__attribute__((dllexport)) BOOL IsChild(void *parent,void *child){return IsWindow(parent)&&IsWindow(child)&&u32_wnd_parent==parent&&child!=parent;}
__attribute__((dllexport)) BOOL IsWindowUnicode(void *hwnd){return IsWindow(hwnd)&&u32_wnd_proc!=0;}
__attribute__((dllexport)) BOOL IsWindowEnabled(void *hwnd){return IsWindow(hwnd)&&!(u32_wnd_style&0x08000000u);}
__attribute__((dllexport)) void *GetWindowOwner(void *hwnd){return IsWindow(hwnd)?u32_wnd_parent:0;}
__attribute__((dllexport)) unsigned short GetWindowWord(void *hwnd,int index){return (unsigned short)GetWindowLongA(hwnd,index);}
__attribute__((dllexport)) unsigned short SetWindowWord(void *hwnd,int index,unsigned short value){return (unsigned short)SetWindowLongA(hwnd,index,value);}
__attribute__((dllexport)) long GetWindowLongW(void *hwnd, int index) { return GetWindowLongA(hwnd,index); }
__attribute__((dllexport)) long SetWindowLongW(void *hwnd, int index, long value) { return SetWindowLongA(hwnd,index,value); }
__attribute__((dllexport)) int GetDlgCtrlID(void *hwnd) { return IsWindow(hwnd) ? (int)u32_wnd_id : 0; }
__attribute__((dllexport)) unsigned int GetDlgItemInt(void *dlg,int id,BOOL *translated,BOOL signed_value){(void)signed_value;if(translated)*translated=0;if(!IsWindow(dlg))return 0;U32_DLGCTRL *c=u32_dlgctrl_get(dlg,(unsigned int)id,0);if(!c)return 0;if(translated)*translated=1;return c->value;}
__attribute__((dllexport)) BOOL SetDlgItemInt(void *dlg,int id,unsigned int value,BOOL signed_value){(void)signed_value;if(!IsWindow(dlg))return 0;U32_DLGCTRL *c=u32_dlgctrl_get(dlg,(unsigned int)id,1);if(!c)return 0;c->value=value;return 1;}
__attribute__((dllexport)) BOOL CheckDlgButton(void *dlg,int id,unsigned int check){if(!IsWindow(dlg)||check>2)return 0;U32_DLGCTRL *c=u32_dlgctrl_get(dlg,(unsigned int)id,1);if(!c)return 0;c->checked=(int)check;return 1;}
__attribute__((dllexport)) unsigned int IsDlgButtonChecked(void *dlg,int id){if(!IsWindow(dlg))return 0;U32_DLGCTRL *c=u32_dlgctrl_get(dlg,(unsigned int)id,0);return c?(unsigned int)c->checked:0;}
__attribute__((dllexport)) BOOL CheckRadioButton(void *dlg,int first,int last,int check){if(!IsWindow(dlg)||first>last||last-first>128||check<first||check>last)return 0;for(int id=first;id<=last;id++){U32_DLGCTRL *c=u32_dlgctrl_get(dlg,(unsigned int)id,1);if(!c)return 0;c->checked=(id==check)?1:0;}return 1;}
__attribute__((dllexport)) void *GetNextDlgTabItem(void *dlg,void *control,BOOL previous){
 if(!IsWindow(dlg))return 0;int ids[32],n=0;for(int i=0;i<32;i++)if(u32_dlgctrl[i].used&&u32_dlgctrl[i].parent==dlg)ids[n++]=(int)u32_dlgctrl[i].id;if(!n)return 0;
 for(int i=0;i<n;i++)for(int j=i+1;j<n;j++)if(ids[j]<ids[i]){int t=ids[i];ids[i]=ids[j];ids[j]=t;}
 int cur=-1;for(int i=0;i<n;i++)if((void *)(unsigned long long)ids[i]==control){cur=i;break;}int next=cur<0?(previous?n-1:0):(previous?(cur+n-1)%n:(cur+1)%n);return (void *)(unsigned long long)ids[next];
}
__attribute__((dllexport)) void *GetNextDlgGroupItem(void *dlg,void *control,BOOL previous){return GetNextDlgTabItem(dlg,control,previous);}
__attribute__((dllexport)) long GetDialogBaseUnits(void) { return (long)0x00100008u; }
__attribute__((dllexport)) BOOL MapDialogRect(void *dlg,U32_RECT *r){if(!IsWindow(dlg)||!r)return 0;r->left=(r->left*8)/4;r->right=(r->right*8)/4;r->top=(r->top*16)/8;r->bottom=(r->bottom*16)/8;return 1;}
static BOOL u32_adjust_window_rect(U32_RECT *r, u32 style, BOOL menu) {
    if (!r) return 0;
    int border=(style & (0x00800000u|0x00040000u)) ? 1 : 0;
    r->left-=border; r->right+=border; r->bottom+=border;
    if (style&0x00C00000u) r->top-=32+border;
    else r->top-=border;
    if (menu) r->top-=20;
    return 1;
}
__attribute__((dllexport)) BOOL AdjustWindowRect(U32_RECT *r, u32 style, BOOL menu) {
    return u32_adjust_window_rect(r,style,menu);
}
__attribute__((dllexport)) BOOL AdjustWindowRectEx(U32_RECT *r, u32 style, BOOL menu, u32 ex_style) {
    (void)ex_style; return u32_adjust_window_rect(r,style,menu);
}
__attribute__((dllexport)) BOOL AdjustWindowRectExForDpi(U32_RECT *r, u32 style, BOOL menu, u32 ex_style, u32 dpi) {
    if (!r) return 0;
    U32_RECT original=*r, adjusted=*r;
    if (!AdjustWindowRectEx(&adjusted,style,menu,ex_style)) return 0;
    if (!dpi) dpi=96;
    r->left=original.left+(adjusted.left-original.left)*(int)dpi/96;
    r->top=original.top+(adjusted.top-original.top)*(int)dpi/96;
    r->right=original.right+(adjusted.right-original.right)*(int)dpi/96;
    r->bottom=original.bottom+(adjusted.bottom-original.bottom)*(int)dpi/96;
    return 1;
}
__attribute__((dllexport)) u32 GetDpiForWindow(void *hwnd) { return IsWindow(hwnd) ? 96u : 0u; }
__attribute__((dllexport)) u32 GetDpiForSystem(void) { return 96u; }
__attribute__((dllexport)) int GetSystemMetricsForDpi(int index, u32 dpi) {
    if (!dpi) dpi=96;
    return GetSystemMetrics(index)*(int)dpi/96;
}
__attribute__((dllexport)) void *GetShellWindow(void){return GetForegroundWindow();}
__attribute__((dllexport)) BOOL ClipCursor(const U32_RECT *r){if(!r){u32_clip_active=0;return 1;}if(r->right<r->left||r->bottom<r->top)return 0;u32_clip_left=r->left;u32_clip_top=r->top;u32_clip_right=r->right;u32_clip_bottom=r->bottom;u32_clip_active=1;if(g_mx<r->left)g_mx=r->left;if(g_mx>=r->right)g_mx=r->right-1;if(g_my<r->top)g_my=r->top;if(g_my>=r->bottom)g_my=r->bottom-1;return 1;}
__attribute__((dllexport)) BOOL GetClipCursor(U32_RECT *r){if(!r)return 0;if(u32_clip_active){r->left=u32_clip_left;r->top=u32_clip_top;r->right=u32_clip_right;r->bottom=u32_clip_bottom;}else{ensure_fb();r->left=0;r->top=0;r->right=(int)g_fw;r->bottom=(int)g_fh;}return 1;}
typedef struct { unsigned int cbSize; U32_RECT rcTitleBar; unsigned int rgstate[6]; } U32_TITLEBARINFO;
__attribute__((dllexport)) BOOL GetTitleBarInfo(void *hwnd,U32_TITLEBARINFO *info){if(!IsWindow(hwnd)||!info||info->cbSize<sizeof(*info))return 0;info->rcTitleBar.left=g_wnd.x;info->rcTitleBar.top=g_wnd.y;info->rcTitleBar.right=g_wnd.x+g_wnd.w;info->rcTitleBar.bottom=g_wnd.y+24;for(int i=0;i<6;i++)info->rgstate[i]=0;return 1;}
typedef struct { unsigned int cbSize; U32_RECT rcBar; void *hMenu,*hwndMenu; BOOL fBarFocused,fFocused; } U32_MENUBARINFO;
__attribute__((dllexport)) BOOL GetMenuBarInfo(void *hwnd,long object,unsigned long item,U32_MENUBARINFO *info){(void)object;(void)item;if(!IsWindow(hwnd)||!info||info->cbSize<sizeof(*info))return 0;info->rcBar.left=g_wnd.x;info->rcBar.top=g_wnd.y+24;info->rcBar.right=g_wnd.x+g_wnd.w;info->rcBar.bottom=g_wnd.y+48;info->hMenu=u32_wnd_menu;info->hwndMenu=hwnd;info->fBarFocused=0;info->fFocused=0;return 1;}
typedef struct { unsigned int cbSize,flags; void *hwndActive,*hwndFocus,*hwndCapture,*hwndMenuOwner,*hwndMoveSize,*hwndCaret; U32_RECT rcCaret; } U32_GUITHREADINFO;
__attribute__((dllexport)) BOOL GetGUIThreadInfo(unsigned int thread,U32_GUITHREADINFO *info){(void)thread;if(!info||info->cbSize<sizeof(*info))return 0;info->flags=0;info->hwndActive=GetActiveWindow();info->hwndFocus=info->hwndActive;info->hwndCapture=0;info->hwndMenuOwner=0;info->hwndMoveSize=0;info->hwndCaret=u32_caret_created?info->hwndFocus:0;info->rcCaret.left=u32_caret_x;info->rcCaret.top=u32_caret_y;info->rcCaret.right=u32_caret_x+u32_caret_w;info->rcCaret.bottom=u32_caret_y+u32_caret_h;return 1;}
typedef struct { unsigned int cbSize,dwTime; } U32_LASTINPUTINFO;
__attribute__((dllexport)) BOOL GetLastInputInfo(U32_LASTINPUTINFO *info){if(!info||info->cbSize<sizeof(*info))return 0;info->dwTime=u32_last_input_time;return 1;}
__attribute__((dllexport)) void *GetCursor(void){return u32_current_cursor;}
typedef struct { unsigned int cbSize,flags; void *hCursor; U32_POINT ptScreenPos; } U32_CURSORINFO;
__attribute__((dllexport)) BOOL GetCursorInfo(U32_CURSORINFO *info){if(!info||info->cbSize<sizeof(U32_CURSORINFO))return 0;info->flags=(u32_show_cursor_count>=0&&!u32_cursor_explicit_hidden)?1u:0u;info->hCursor=u32_current_cursor;info->ptScreenPos.x=g_mx;info->ptScreenPos.y=g_my;return 1;}
__attribute__((dllexport)) void *SetCursor(void *cursor){
    void *old=u32_current_cursor;
    if(!cursor || (((unsigned long long)cursor&0xFFFF0000ULL)==0x00330000ULL)) {
        u32_current_cursor=cursor;
        u32_cursor_explicit_hidden=(cursor==0);
        if(cursor && (((unsigned long long)cursor&0xFFFFu)==32512u))
            u32_hover_cursor_id=u32_auto_cursor_at(g_mx,g_my);
        cursor_repaint_transition(g_mx,g_my,g_mx,g_my);
    }
    return old;
}

/* Win32 SetSystemCursor replaces a stock cursor shape. SentinelOS cursor
 * handles encode the standard IDC_* resource ID in their low 16 bits, so
 * accept valid loaded cursor handles and apply them as the current system
 * shape. OCR_* selects the system role; all supported roles currently share
 * the single active pointer shape, matching the compositor's one-pointer model. */
__attribute__((dllexport)) BOOL SetSystemCursor(void *cursor, unsigned int id) {
    if(!cursor || id<32512u || id>32651u) return 0;
    u32_current_cursor=cursor;
    u32_cursor_explicit_hidden=0;
    u32_hover_cursor_id=(unsigned int)((unsigned long long)cursor&0xFFFFu);
    cursor_repaint_transition(g_mx,g_my,g_mx,g_my);
    return 1;
}
__attribute__((dllexport)) int GetMouseMovePointsEx(unsigned int cbSize,const U32_MOUSEMOVEPOINT *point,U32_MOUSEMOVEPOINT *out,int count,unsigned int resolution){
    (void)resolution;
    if(cbSize!=sizeof(U32_MOUSEMOVEPOINT)||!point||!out||count<=0)return -1;
    int found=-1;
    for(unsigned int n=0;n<u32_mouse_history_count;n++){unsigned int i=(u32_mouse_history_head+n)%32;U32_MOUSEMOVEPOINT *p=&u32_mouse_history[i];if(p->x==point->x&&p->y==point->y&&(!point->time||p->time==point->time)){found=(int)n;break;}}
    if(found<0)return -1;
    int copied=0;for(int n=found;n>=0&&copied<count;n--){unsigned int i=(u32_mouse_history_head+(unsigned int)n)%32;out[copied++]=u32_mouse_history[i];}
    return copied;
}
__attribute__((dllexport)) BOOL GetKeyboardState(unsigned char *state){if(!state)return 0;for(int i=0;i<256;i++)state[i]=u32_key_state[i];return 1;}
__attribute__((dllexport)) BOOL SetKeyboardState(const unsigned char *state){if(!state)return 0;for(int i=0;i<256;i++)u32_key_state[i]=state[i];return 1;}
__attribute__((dllexport)) void *GetKeyboardLayout(unsigned int thread){(void)thread;return (void *)0x04090409;}
__attribute__((dllexport)) int GetKeyboardLayoutList(int count,void **list){if(count>0&&list)list[0]=(void *)0x04090409;return 1;}
__attribute__((dllexport)) BOOL GetKeyboardLayoutNameA(char *name){if(!name)return 0;const char *s="00000409";for(int i=0;i<9;i++)name[i]=s[i];return 1;}
__attribute__((dllexport)) BOOL GetKeyboardLayoutNameW(U32_WCHAR *name){if(!name)return 0;const char *s="00000409";for(int i=0;i<9;i++)name[i]=(unsigned char)s[i];return 1;}
__attribute__((dllexport)) void *ActivateKeyboardLayout(void *layout,unsigned int flags){(void)flags;return layout?(void *)0x04090409:0;}
__attribute__((dllexport)) void *LoadKeyboardLayoutA(const char *name,unsigned int flags){(void)flags;if(!name)return 0;return name[0]=='0'?(void *)0x04090409:0;}
__attribute__((dllexport)) void *LoadKeyboardLayoutW(const U32_WCHAR *name,unsigned int flags){(void)flags;if(!name)return 0;return name[0]=='0'?(void *)0x04090409:0;}
__attribute__((dllexport)) BOOL UnloadKeyboardLayout(void *layout){return layout!=(void *)0x04090409;}
__attribute__((dllexport)) int GetKeyNameTextA(long param,char *out,int cap){if(!out||cap<=0)return 0;unsigned int sc=((unsigned long)param>>16)&0xFF;const char *name="Unknown";switch(sc){case 0x1E:name="A";break;case 0x30:name="B";break;case 0x2E:name="C";break;case 0x20:name="D";break;case 0x12:name="E";break;case 0x39:name="Space";break;case 0x1C:name="Enter";break;case 0x01:name="Esc";break;case 0x0E:name="Backspace";break;case 0x0F:name="Tab";break;case 0x48:name="Up";break;case 0x50:name="Down";break;case 0x4B:name="Left";break;case 0x4D:name="Right";break;}int i=0;while(name[i]&&i+1<cap){out[i]=name[i];i++;}out[i]=0;return i;}
__attribute__((dllexport)) int GetKeyNameTextW(long param,U32_WCHAR *out,int cap){char b[32];int n=GetKeyNameTextA(param,b,sizeof(b));if(!out||cap<=0)return 0;int i=0;while(b[i]&&i+1<cap){out[i]=(unsigned char)b[i];i++;}out[i]=0;return n;}
__attribute__((dllexport)) unsigned int MapVirtualKeyA(unsigned int code,unsigned int type){if(type==0)return code>='a'&&code<='z'?code-32:code;if(type==1)return code; if(type==2)return code>=32&&code<127?code:0;return 0;}
__attribute__((dllexport)) unsigned int MapVirtualKeyW(unsigned int code,unsigned int type){return MapVirtualKeyA(code,type);}
__attribute__((dllexport)) unsigned int MapVirtualKeyExA(unsigned int code,unsigned int type,void *layout){(void)layout;return MapVirtualKeyA(code,type);}
__attribute__((dllexport)) unsigned int MapVirtualKeyExW(unsigned int code,unsigned int type,void *layout){(void)layout;return MapVirtualKeyW(code,type);}
__attribute__((dllexport)) int ToUnicode(unsigned int vk,unsigned int scan,const unsigned char *state,U32_WCHAR *out,int cap,unsigned int flags){(void)scan;(void)flags;if(!out||cap<=0)return 0;unsigned int c=vk;if(c>='A'&&c<='Z'&&state&&!(state[0x10]&0x80u))c+=32;if(c>=32&&c<127){out[0]=(U32_WCHAR)c;return 1;}return 0;}
__attribute__((dllexport)) int ToUnicodeEx(unsigned int vk,unsigned int scan,const unsigned char *state,U32_WCHAR *out,int cap,unsigned int flags,void *layout){(void)layout;return ToUnicode(vk,scan,state,out,cap,flags);}
__attribute__((dllexport)) short VkKeyScanA(char ch){unsigned char c=(unsigned char)ch;if(c>='a'&&c<='z')return (short)(c-32);if(c>='A'&&c<='Z')return (short)(c|0x0100);if(c>=32&&c<127)return (short)c;return -1;}
__attribute__((dllexport)) short VkKeyScanW(U32_WCHAR ch){if(ch>127)return -1;return VkKeyScanA((char)ch);}
__attribute__((dllexport)) short VkKeyScanExA(char ch,void *layout){(void)layout;return VkKeyScanA(ch);}
__attribute__((dllexport)) short VkKeyScanExW(U32_WCHAR ch,void *layout){(void)layout;return VkKeyScanW(ch);}
__attribute__((dllexport)) int GetKeyboardType(int type){if(type==0)return 4;if(type==1)return 101;if(type==2)return 12;return 0;}
__attribute__((dllexport)) unsigned int GetCaretBlinkTime(void){return u32_caret_blink;}
__attribute__((dllexport)) BOOL SetCaretBlinkTime(unsigned int ms){if(ms==0xFFFFFFFFu)return 0;if(ms<1)ms=530;u32_caret_blink=ms;return 1;}
__attribute__((dllexport)) BOOL GetCaretPos(U32_POINT *pt){if(!pt||!u32_caret_created)return 0;pt->x=u32_caret_x;pt->y=u32_caret_y;return 1;}
__attribute__((dllexport)) BOOL SetCaretPos(int x,int y){u32_caret_x=x;u32_caret_y=y;return 1;}
__attribute__((dllexport)) BOOL CreateCaret(void *hwnd,void *bitmap,int width,int height){(void)bitmap;if(!IsWindow(hwnd))return 0;u32_caret_w=width>0?width:1;u32_caret_h=height>0?height:16;u32_caret_created=1;u32_caret_visible=1;return 1;}
__attribute__((dllexport)) BOOL DestroyCaret(void){u32_caret_created=0;u32_caret_visible=0;return 1;}
__attribute__((dllexport)) BOOL HideCaret(void *hwnd){if(!IsWindow(hwnd)||!u32_caret_created)return 0;u32_caret_visible=0;return 1;}
__attribute__((dllexport)) BOOL ShowCaret(void *hwnd){if(!IsWindow(hwnd)||!u32_caret_created)return 0;u32_caret_visible=1;return 1;}
__attribute__((dllexport)) int GetMenuStringW(void *menu,unsigned int item,U32_WCHAR *out,int cap,unsigned int flags){char b[128];int n=GetMenuStringA(menu,item,b,sizeof(b),flags);if(!out||cap<=0)return n;int i=0;while(b[i]&&i+1<cap){out[i]=(unsigned char)b[i];i++;}out[i]=0;return n;}
__attribute__((dllexport)) BOOL SetMenuItemBitmaps(void *menu,unsigned int item,unsigned int flags,void *checked,void *unchecked){U32_MENUDATA *m=u32_menu_data(menu);int i=u32_menu_index(m,item,(flags&0x0400u)!=0);if(i<0)return 0;m->items[i].bmp_checked=checked;m->items[i].bmp_unchecked=unchecked;return 1;}
__attribute__((dllexport)) int GetMenuCheckMarkDimensions(void){return (13<<16)|13;}
__attribute__((dllexport)) BOOL HiliteMenuItem(void *hwnd,void *menu,unsigned int item,unsigned int flags){(void)hwnd;U32_MENUDATA *m=u32_menu_data(menu);int i=u32_menu_index(m,item,(flags&0x0400u)!=0);if(i<0)return 0;if(flags&0x0080u){m->items[i].flags|=0x0080u;m->items[i].state|=0x0080u;}else{m->items[i].flags&=~0x0080u;m->items[i].state&=~0x0080u;}return 1;}
__attribute__((dllexport)) BOOL InsertMenuW(void *menu,unsigned int pos,unsigned int flags,unsigned long long id,const U32_WCHAR *text){char b[80];u32_wide_to_ansi(b,sizeof(b),text);return InsertMenuA(menu,pos,flags,id,b);}
__attribute__((dllexport)) BOOL AppendMenuW(void *menu,unsigned int flags,unsigned long long id,const U32_WCHAR *text){char b[80];u32_wide_to_ansi(b,sizeof(b),text);return AppendMenuA(menu,flags,id,b);}
__attribute__((dllexport)) BOOL ModifyMenuW(void *menu,unsigned int item,unsigned int flags,unsigned long long id,const U32_WCHAR *text){char b[80];u32_wide_to_ansi(b,sizeof(b),text);return ModifyMenuA(menu,item,flags,id,b);}
__attribute__((dllexport)) BOOL SetMenuItemInfoW(void *menu,unsigned int item,BOOL bypos,const U32_MENUITEMINFOW *info){if(!info)return 0;U32_MENUITEMINFOA a={0};a.cbSize=sizeof(a);a.fMask=info->fMask;a.fType=info->fType;a.fState=info->fState;a.wID=info->wID;a.hSubMenu=info->hSubMenu;a.hbmpChecked=info->hbmpChecked;a.hbmpUnchecked=info->hbmpUnchecked;a.dwItemData=info->dwItemData;a.cch=info->cch;a.hbmpItem=info->hbmpItem;char text[80];if(info->dwTypeData){unsigned int j=0;while(info->dwTypeData[j]&&j+1<sizeof(text)){text[j]=info->dwTypeData[j]<128?(char)info->dwTypeData[j]:'?';j++;}text[j]=0;a.dwTypeData=text;}return SetMenuItemInfoA(menu,item,bypos,&a);}
__attribute__((dllexport)) BOOL GetMenuItemInfoW(void *menu,unsigned int item,BOOL bypos,U32_MENUITEMINFOW *info){if(!info)return 0;char text[80];U32_MENUITEMINFOA a={0};a.cbSize=sizeof(a);a.fMask=info->fMask;a.cch=sizeof(text);a.dwTypeData=text;if(!GetMenuItemInfoA(menu,item,bypos,&a))return 0;info->fType=a.fType;info->fState=a.fState;info->wID=a.wID;info->hSubMenu=a.hSubMenu;info->dwItemData=a.dwItemData;if(info->dwTypeData&&info->cch){unsigned int j=0;while(text[j]&&j+1<info->cch){info->dwTypeData[j]=(unsigned char)text[j];j++;}info->dwTypeData[j]=0;}info->cch=a.cch;return 1;}
__attribute__((dllexport)) unsigned long long TrackPopupMenuEx(unsigned long long a,unsigned long long b,unsigned long long c,unsigned long long d,unsigned long long e,unsigned long long f){U32_MENUDATA *m=u32_menu_data((void*)a);if(!m||!IsWindow((void*)e)||m->default_pos<0||m->default_pos>=m->count)return 0;U32_MENUITEM *it=&m->items[m->default_pos];if(it->flags&0x3u)return 0;if(b&0x0100u)return it->id;return SendMessageA((void*)e,0x0111u,(unsigned long long)it->id,0),1;}
__attribute__((dllexport)) BOOL EndMenu(void){g_menu=0;return 1;}
__attribute__((dllexport)) unsigned long long GetMenuContextHelpId(void *menu){return u32_menu_data(menu)?u32_context_help_id:0;}
__attribute__((dllexport)) BOOL SetMenuContextHelpId(void *menu,unsigned long long id){if(!u32_menu_data(menu))return 0;u32_context_help_id=id;return 1;}
typedef struct { unsigned int cbSize,fMask,dwStyle,dwMaxHeight; void *hbrBack; unsigned int dwContextHelpID; unsigned long long dwMenuData; } U32_MENUINFO;
__attribute__((dllexport)) BOOL GetMenuInfo(void *menu,U32_MENUINFO *info){U32_MENUDATA *m=u32_menu_data(menu);if(!m||!info||info->cbSize<sizeof(U32_MENUINFO))return 0;if(info->fMask&1u)info->dwMaxHeight=m->max_height;if(info->fMask&2u)info->hbrBack=m->background;if(info->fMask&4u)info->dwContextHelpID=m->help_id;if(info->fMask&8u)info->dwMenuData=m->menu_data;if(info->fMask&0x10u)info->dwStyle=m->style;return 1;}
__attribute__((dllexport)) BOOL SetMenuInfo(void *menu,const U32_MENUINFO *info){U32_MENUDATA *m=u32_menu_data(menu);if(!m||!info||info->cbSize<sizeof(U32_MENUINFO))return 0;if(info->fMask&1u)m->max_height=info->dwMaxHeight;if(info->fMask&2u)m->background=info->hbrBack;if(info->fMask&4u)m->help_id=info->dwContextHelpID;if(info->fMask&8u)m->menu_data=info->dwMenuData;if(info->fMask&0x10u)m->style=info->dwStyle;return 1;}
__attribute__((dllexport)) BOOL GetMenuItemRect(void *hwnd,void *menu,unsigned int item,U32_RECT *rect){U32_MENUDATA *m=u32_menu_data(menu);int i=u32_menu_index(m,item,1);if(!IsWindow(hwnd)||i<0||!rect)return 0;rect->left=g_wnd.x+i*80;rect->top=g_wnd.y+24;rect->right=rect->left+80;rect->bottom=rect->top+24;return 1;}
__attribute__((dllexport)) BOOL SetMenuItemCount(void *menu,unsigned int count){U32_MENUDATA *m=u32_menu_data(menu);if(!m||count>16)return 0;while(m->count<(int)count){U32_MENUITEM *it=&m->items[m->count++];it->flags=0;it->state=0;it->type=0;it->id=0;it->data=0;it->submenu=0;it->bmp_checked=it->bmp_unchecked=it->bmp_item=0;it->text[0]=0;}m->count=(int)count;if(m->default_pos>=m->count)m->default_pos=-1;return 1;}
__attribute__((dllexport)) BOOL IsZoomed(void *hwnd){return IsWindow(hwnd)&&g_wnd.x<=0&&g_wnd.y<=0&&g_wnd.w>=(int)g_fw&&g_wnd.h>=(int)g_fh;}
__attribute__((dllexport)) BOOL GetWindowPos(void *hwnd,U32_RECT *rect){return GetWindowRect(hwnd,rect);}
__attribute__((dllexport)) BOOL UnregisterClassW(const U32_WCHAR *name,void *instance){char n[128];u32_wide_to_ansi(n,sizeof(n),name);return UnregisterClassA(n,instance);}
__attribute__((dllexport)) BOOL ValidateRect(void *hwnd,const U32_RECT *rect){if(!IsWindow(hwnd))return 0;if(!rect||(u32_update_pending&&rect->left<=u32_invalid_rect.left&&rect->top<=u32_invalid_rect.top&&rect->right>=u32_invalid_rect.right&&rect->bottom>=u32_invalid_rect.bottom)){u32_update_pending=0;u32_update_erase=0;}return 1;}

__attribute__((dllexport)) long long GetMessageExtraInfo(void){return u32_message_extra_info;}

__attribute__((dllexport)) long long SetMessageExtraInfo(long long value){long long old=u32_message_extra_info;u32_message_extra_info=value;return old;}

__attribute__((dllexport)) DWORD GetQueueStatus(DWORD flags){DWORD current=u32_msg_count?0x0008u:0u;DWORD changed=current;return ((current&flags)<<16)|(changed&flags);}

__attribute__((dllexport)) BOOL GetInputState(void){return u32_msg_count!=0;}

__attribute__((dllexport)) BOOL WaitMessage(void){InputEvent e;return (BOOL)sy3(SYS_GetInput,(u64)&e,0,0);}

__attribute__((dllexport)) BOOL ReplyMessage(long long result){(void)result;return 1;}

__attribute__((dllexport)) BOOL InSendMessage(void){return u32_in_send_message!=0;}

__attribute__((dllexport)) DWORD InSendMessageEx(void *reserved){(void)reserved;return u32_in_send_message?1u:0u;}

__attribute__((dllexport)) void *GetWindowDC(void *hwnd){return IsWindow(hwnd)?(void *)1:0;}

__attribute__((dllexport)) void *GetDCEx(void *hwnd,void *region,DWORD flags){(void)region;(void)flags;return GetDC(hwnd);}

__attribute__((dllexport)) DWORD GetSysColor(int index){static const DWORD c[]={0x00C0C0C0,0x00000000,0x00FFFFFF,0x00808080,0x00000000,0x00FFFFFF,0x00000000,0x00C0C0C0,0x00FFFFFF,0x00000000,0x00808080,0x00D4D0C8,0x00000000,0x00FFFFFF,0x00D4D0C8,0x00000000,0x00D4D0C8,0x00000000,0x00FFFFFF,0x00000000,0x00D4D0C8,0x00000000,0x00FFFFFF,0x00D4D0C8,0x00000000,0x00FFFFFF,0x00000000,0x00D4D0C8,0x00000000,0x00FFFFFF};return index>=0&&index<30?c[index]:0;}

__attribute__((dllexport)) void *GetSysColorBrush(int index){return (void *)(unsigned long long)(0x1000u+(unsigned int)(index<0?0:index));}

__attribute__((dllexport)) BOOL SetSysColors(int count,const int *indices,const DWORD *colors){if(count<0||(count>0&&(!indices||!colors)))return 0;return 1;}

__attribute__((dllexport)) int FillRect(void *dc,const U32_RECT *r,void *brush){(void)dc;if(!r)return 0;u32 col=brush?(u32)(unsigned long long)brush:COL_PANEL;fill(r->left,r->top,r->right-r->left,r->bottom-r->top,col);return 1;}

__attribute__((dllexport)) int FrameRect(void *dc,const U32_RECT *r,void *brush){(void)dc;if(!r)return 0;u32 col=brush?(u32)(unsigned long long)brush:COL_TEXT;fill(r->left,r->top,r->right-r->left,1,col);fill(r->left,r->bottom-1,r->right-r->left,1,col);fill(r->left,r->top,1,r->bottom-r->top,col);fill(r->right-1,r->top,1,r->bottom-r->top,col);return 1;}

__attribute__((dllexport)) BOOL InvertRect(void *dc,const U32_RECT *r){(void)dc;if(!r||!g_front)return 0;for(int y=r->top;y<r->bottom;y++)for(int x=r->left;x<r->right;x++)if(x>=0&&y>=0&&(u32)x<g_fw&&(u32)y<g_fh)g_front[y*g_pitch+x]^=0x00FFFFFFu;return 1;}

__attribute__((dllexport)) BOOL DrawFocusRect(void *dc,const U32_RECT *r){(void)dc;if(!r)return 0;for(int x=r->left;x<r->right;x+=2){putpx(x,r->top,COL_TEXT);putpx(x,r->bottom-1,COL_TEXT);}for(int y=r->top;y<r->bottom;y+=2){putpx(r->left,y,COL_TEXT);putpx(r->right-1,y,COL_TEXT);}return 1;}

__attribute__((dllexport)) BOOL OpenIcon(void *hwnd){if(!IsWindow(hwnd))return 0;g_wnd.minimized=0;return 1;}

__attribute__((dllexport)) BOOL CloseWindow(void *hwnd){if(!IsWindow(hwnd))return 0;g_wnd.minimized=1;return 1;}

__attribute__((dllexport)) BOOL ShowOwnedPopups(void *hwnd,BOOL show){(void)show;return IsWindow(hwnd);}

__attribute__((dllexport)) BOOL FlashWindow(void *hwnd,BOOL invert){(void)invert;return IsWindow(hwnd);}

__attribute__((dllexport)) BOOL AnimateWindow(void *hwnd,DWORD time,DWORD flags){(void)time;(void)flags;return IsWindow(hwnd);}

__attribute__((dllexport)) BOOL GetDesktopWindowRect(U32_RECT *r){if(!r)return 0;ensure_fb();r->left=0;r->top=0;r->right=(int)g_fw;r->bottom=(int)g_fh;return 1;}

__attribute__((dllexport)) void *GetWindowFromDC(void *dc){return dc?(void *)1:0;}

__attribute__((dllexport)) int GetUpdateRgn(void *hwnd,void *region,BOOL erase){(void)region;(void)erase;return IsWindow(hwnd)?1:0;}

__attribute__((dllexport)) int ExcludeUpdateRgn(void *dc,void *hwnd){(void)dc;return IsWindow(hwnd)?1:0;}

__attribute__((dllexport)) int GetScrollPos32(void *hwnd,int bar){if(!IsWindow(hwnd))return 0;return bar==0?u32_scroll_pos[0]:u32_scroll_pos[1];}

__attribute__((dllexport)) int SetScrollPos32(void *hwnd,int bar,int pos,BOOL redraw){(void)redraw;if(!IsWindow(hwnd))return 0;int i=bar==0?0:1;int old=u32_scroll_pos[i];u32_scroll_pos[i]=pos;return old;}

__attribute__((dllexport)) DWORD GetWindowThreadProcessIdEx(void *hwnd,DWORD *pid){return GetWindowThreadProcessId(hwnd,pid);}

__attribute__((dllexport)) BOOL IsHungAppWindow(void *hwnd){return !IsWindow(hwnd);}

__attribute__((dllexport)) DWORD GetCurrentThreadId(void){return 1;}
__attribute__((dllexport)) DWORD GetActiveWindowThread(void){return GetCurrentThreadId();}

__attribute__((dllexport)) long GetWindowLongOffset(void *hwnd,int offset){return GetWindowLongA(hwnd,offset);}

__attribute__((dllexport)) long SetWindowLongOffset(void *hwnd,int offset,long value){return SetWindowLongA(hwnd,offset,value);}

__attribute__((dllexport)) BOOL GetCursorPosEx(U32_POINT *pt){return GetCursorPos(pt);}

__attribute__((dllexport)) BOOL SetCursorPosEx(int x,int y){return SetCursorPos(x,y);}

__attribute__((dllexport)) BOOL GetCaretState(int *x,int *y,int *visible){if(x)*x=u32_caret_x;if(y)*y=u32_caret_y;if(visible)*visible=u32_caret_visible;return u32_caret_created;}

__attribute__((dllexport)) int GetDialogControlCount(void *dlg){int n=0;for(int i=0;i<32;i++)if(u32_dlgctrl[i].used&&u32_dlgctrl[i].parent==dlg)n++;return n;}

__attribute__((dllexport)) unsigned int GetMenuItemFlags(void *menu,unsigned int pos){U32_MENUDATA *m=u32_menu_data(menu);return m&&pos<(unsigned int)m->count?m->items[pos].flags:0;}

__attribute__((dllexport)) int GetMenuItemTextA(void *menu,unsigned int pos,char *out,int cap){return GetMenuStringA(menu,pos,out,cap,0x0400u);}

__attribute__((dllexport)) BOOL SetMenuItemTextA(void *menu,unsigned int pos,const char *text){return ModifyMenuA(menu,pos,0x0400u,0,text);}

__attribute__((dllexport)) unsigned long GetWindowStyle(void *hwnd){return IsWindow(hwnd)?u32_wnd_style:0;}

__attribute__((dllexport)) unsigned long SetWindowStyle(void *hwnd,unsigned long style){if(!IsWindow(hwnd))return 0;unsigned long old=u32_wnd_style;u32_wnd_style=style;return old;}

__attribute__((dllexport)) unsigned long GetWindowExStyle(void *hwnd){return IsWindow(hwnd)?u32_wnd_exstyle:0;}

__attribute__((dllexport)) unsigned long SetWindowExStyle(void *hwnd,unsigned long style){if(!IsWindow(hwnd))return 0;unsigned long old=u32_wnd_exstyle;u32_wnd_exstyle=style;return old;}

__attribute__((dllexport)) BOOL GetWindowClientOrigin(void *hwnd,U32_POINT *pt){if(!IsWindow(hwnd)||!pt)return 0;pt->x=g_wnd.x;pt->y=g_wnd.y+24;return 1;}

__attribute__((dllexport)) BOOL SetWindowClientOrigin(void *hwnd,const U32_POINT *pt){if(!IsWindow(hwnd)||!pt)return 0;g_wnd.x=pt->x;g_wnd.y=pt->y-24;return 1;}

__attribute__((dllexport)) BOOL DrawEdge(void *dc,U32_RECT *r,unsigned int edge,unsigned int flags){(void)dc;if(!r)return 0;u32 col=(flags&0x10u)?COL_TEXT:COL_MUTED;FrameRect(0,r,(void *)(unsigned long long)col);return 1;}

__attribute__((dllexport)) BOOL DrawFrameControl(void *dc,U32_RECT *r,unsigned int type,unsigned int state){(void)type;(void)state;return FrameRect(dc,r,(void *)(unsigned long long)COL_TEXT);}

__attribute__((dllexport)) BOOL DrawCaption(void *hwnd,void *dc,U32_RECT *r,unsigned int flags){(void)dc;(void)flags;if(!IsWindow(hwnd)||!r)return 0;fill(r->left,r->top,r->right-r->left,24,COL_TITLE);draw_text(r->left+8,r->top+8,g_wnd.title,COL_TEXT);return 1;}

__attribute__((dllexport)) BOOL DrawIcon(void *dc,int x,int y,void *icon){(void)dc;(void)icon;panel(x,y,16,16,COL_ICON,COL_TEXT);return 1;}

__attribute__((dllexport)) BOOL DrawIconEx(void *dc,int x,int y,void *icon,int cx,int cy,unsigned int step,void *brush,unsigned int flags){(void)step;(void)brush;(void)flags;(void)dc;(void)icon;if(cx<=0)cx=16;if(cy<=0)cy=16;panel(x,y,cx,cy,COL_ICON,COL_TEXT);return 1;}

__attribute__((dllexport)) int DrawTextA(void *dc,const char *text,int count,U32_RECT *r,unsigned int format){(void)dc;(void)format;if(!text||!r)return 0;int n=0;while((count<0?text[n]!=0:n<count)&&text[n])n++;int x=r->left,y=r->top;for(int i=0;i<n;i++){if(text[i]=='\n'){x=r->left;y+=8;continue;}draw_char(x,y,text[i],COL_TEXT);x+=8;}return n;}

__attribute__((dllexport)) int DrawTextW(void *dc,const unsigned short *text,int count,U32_RECT *r,unsigned int format){(void)dc;(void)format;if(!text||!r)return 0;int n=0,x=r->left,y=r->top;while((count<0?text[n]!=0:n<count)&&text[n]){if(text[n]=='\n'){x=r->left;y+=8;}else{draw_char(x,y,text[n]<128?(char)text[n]:'?',COL_TEXT);x+=8;}n++;}return n;}

__attribute__((dllexport)) BOOL GetTextExtentPoint32A(void *dc,const char *text,int count,U32_POINT *size){(void)dc;if(!text||!size||count<0)return 0;size->x=count*8;size->y=8;return 1;}

__attribute__((dllexport)) BOOL GetTextExtentPoint32W(void *dc,const unsigned short *text,int count,U32_POINT *size){(void)dc;if(!text||!size||count<0)return 0;size->x=count*8;size->y=8;return 1;}

__attribute__((dllexport)) DWORD GetTextColor(void *dc){(void)dc;return COL_TEXT;}

__attribute__((dllexport)) DWORD SetTextColor(void *dc,DWORD color){(void)dc;static DWORD current=COL_TEXT;DWORD old=current;current=color;return old;}

__attribute__((dllexport)) DWORD GetBkColor(void *dc){(void)dc;return COL_BG;}

__attribute__((dllexport)) DWORD SetBkColor(void *dc,DWORD color){(void)dc;static DWORD current=COL_BG;DWORD old=current;current=color;return old;}

__attribute__((dllexport)) int SetBkMode(void *dc,int mode){(void)dc;static int current=1;int old=current;current=mode;return old;}

__attribute__((dllexport)) unsigned int SetTextAlign(void *dc,unsigned int align){(void)dc;unsigned int old=u32_text_align;u32_text_align=align;return old;}
__attribute__((dllexport)) unsigned int GetTextAlign(void *dc){(void)dc;return u32_text_align;}

__attribute__((dllexport)) int GetDeviceCaps(void *dc,int index){(void)dc;switch(index){case 8:case 10:return (int)g_fw;case 9:case 11:return (int)g_fh;case 88:return 32;case 12:return 96;case 14:return 96;case 2:return 1;default:return 0;}}
__attribute__((dllexport)) BOOL GetWindowOrgEx(void *dc,U32_POINT *pt){(void)dc;if(!pt)return 0;pt->x=0;pt->y=0;return 1;}

__attribute__((dllexport)) BOOL SetWindowOrgEx(void *dc,int x,int y,U32_POINT *old){(void)dc;if(old){old->x=0;old->y=0;}return x==0&&y==0;}

__attribute__((dllexport)) BOOL GetViewportOrgEx(void *dc,U32_POINT *pt){return GetWindowOrgEx(dc,pt);}

__attribute__((dllexport)) BOOL SetViewportOrgEx(void *dc,int x,int y,U32_POINT *old){return SetWindowOrgEx(dc,x,y,old);}

__attribute__((dllexport)) BOOL LPtoDP(void *dc,U32_POINT *pts,int count){(void)dc;return pts&&count>=0;}

__attribute__((dllexport)) BOOL DPtoLP(void *dc,U32_POINT *pts,int count){(void)dc;return pts&&count>=0;}

__attribute__((dllexport)) int SaveDC(void *dc){(void)dc;return 1;}

__attribute__((dllexport)) BOOL RestoreDC(void *dc,int saved){(void)dc;return saved==1;}

__attribute__((dllexport)) BOOL GetDCOrgEx(void *dc,U32_POINT *pt){(void)dc;if(!pt)return 0;pt->x=0;pt->y=0;return 1;}

__attribute__((dllexport)) BOOL GetBrushOrgEx(void *dc,U32_POINT *pt){return GetWindowOrgEx(dc,pt);}

__attribute__((dllexport)) BOOL SetBrushOrgEx(void *dc,int x,int y,U32_POINT *old){return SetWindowOrgEx(dc,x,y,old);}

__attribute__((dllexport)) BOOL OpenClipboard(void *owner){if(u32_clipboard_open)return 0;u32_clipboard_open=1;u32_clipboard_owner=owner;return 1;}

__attribute__((dllexport)) BOOL CloseClipboard(void){if(!u32_clipboard_open)return 0;u32_clipboard_open=0;u32_clipboard_owner=0;return 1;}

__attribute__((dllexport)) BOOL EmptyClipboard(void){if(!u32_clipboard_open)return 0;u32_clipboard_format=0;u32_clipboard_data=0;u32_clipboard_seq++;return 1;}

__attribute__((dllexport)) void *SetClipboardData(unsigned int format,void *data){if(!u32_clipboard_open||!data)return 0;u32_clipboard_format=format;u32_clipboard_data=data;u32_clipboard_seq++;return data;}

__attribute__((dllexport)) void *GetClipboardData(unsigned int format){return u32_clipboard_open&&format==u32_clipboard_format?u32_clipboard_data:0;}

__attribute__((dllexport)) BOOL IsClipboardFormatAvailable(unsigned int format){return u32_clipboard_format==format&&u32_clipboard_data!=0;}

__attribute__((dllexport)) unsigned int RegisterClipboardFormatA(const char *name){if(!name||!name[0])return 0;unsigned int h=2166136261u;for(int i=0;name[i];i++)h=(h^(unsigned char)name[i])*16777619u;return 0xC000u+(h%0x3FFFu);}

__attribute__((dllexport)) unsigned int RegisterClipboardFormatW(const unsigned short *name){if(!name||!name[0])return 0;unsigned int h=2166136261u;for(int i=0;name[i];i++)h=(h^(unsigned int)name[i])*16777619u;return 0xC000u+(h%0x3FFFu);}

__attribute__((dllexport)) BOOL DestroyIcon(void *icon){return icon!=0;}

__attribute__((dllexport)) BOOL DestroyCursor(void *cursor){return cursor!=0;}

__attribute__((dllexport)) void *CopyIcon(void *icon){return icon;}

__attribute__((dllexport)) void *CopyCursor(void *cursor){return cursor;}

__attribute__((dllexport)) BOOL GetIconInfo(void *icon,void *info){(void)icon;if(!info)return 0;for(int i=0;i<8;i++)((unsigned long long*)info)[i]=0;return 1;}

static int u32_pe_resource_data(void *instance,unsigned int type_id,unsigned int resource_id,const unsigned char **data,unsigned int *bytes);
typedef struct { const unsigned char *data; unsigned int bytes; int width,height,bpp; int used; } U32_BITMAP_RESOURCE;
static U32_BITMAP_RESOURCE u32_bitmap_resources[16];
static unsigned int u32_bitmap_read16(const unsigned char *p){return (unsigned int)p[0]|((unsigned int)p[1]<<8);}
static unsigned int u32_bitmap_read32(const unsigned char *p){return (unsigned int)p[0]|((unsigned int)p[1]<<8)|((unsigned int)p[2]<<16)|((unsigned int)p[3]<<24);}
__attribute__((dllexport)) void *LoadBitmapA(void *instance,const char *name) {
    unsigned long long id=(unsigned long long)name;if(!name||id>0xFFFFULL)return 0;
    const unsigned char *data=0;unsigned int bytes=0;
    if(!u32_pe_resource_data(instance,2u,(unsigned int)id,&data,&bytes)||bytes<40)return 0;
    unsigned int header=u32_bitmap_read32(data);
    if(header<40||header>bytes)return 0;
    int width=(int)u32_bitmap_read32(data+4),height=(int)u32_bitmap_read32(data+8);
    unsigned int planes=u32_bitmap_read16(data+12),bpp=u32_bitmap_read16(data+14),compression=u32_bitmap_read32(data+16);
    if(width<=0||height==0||height==(-2147483647-1)||width>8192||height>8192||height< -8192||planes!=1||
       (bpp!=1&&bpp!=4&&bpp!=8&&bpp!=16&&bpp!=24&&bpp!=32)||compression!=0)return 0;
    unsigned long long rows=(unsigned long long)(height<0?-height:height);
    unsigned long long stride=(((unsigned long long)width*bpp+31ULL)/32ULL)*4ULL;
    unsigned int colors=u32_bitmap_read32(data+32);if(!colors&&bpp<=8)colors=1u<<bpp;
    unsigned long long need=(unsigned long long)header+(bpp<=8?(unsigned long long)colors*4ULL:0ULL)+stride*rows;
    if(need>bytes)return 0;
    for(int i=0;i<16;i++)if(u32_bitmap_resources[i].used&&u32_bitmap_resources[i].data==data)return &u32_bitmap_resources[i];
    for(int i=0;i<16;i++)if(!u32_bitmap_resources[i].used) {
        U32_BITMAP_RESOURCE *b=&u32_bitmap_resources[i];b->data=data;b->bytes=bytes;b->width=width;b->height=height<0?-height:height;b->bpp=(int)bpp;b->used=1;return b;
    }
    return 0;
}
__attribute__((dllexport)) void *LoadBitmapW(void *instance,const unsigned short *name) {
    unsigned long long id=(unsigned long long)name;if(!name||id>0xFFFFULL)return 0;
    return LoadBitmapA(instance,(const char*)name);
}

static int u32_pe_resource_data(void *instance,unsigned int type_id,unsigned int resource_id,const unsigned char **data,unsigned int *bytes);
static unsigned int u32_menu_read_word(const unsigned char *p){return (unsigned int)p[0]|((unsigned int)p[1]<<8);}
static int u32_parse_menu_items(const unsigned char **cursor,const unsigned char *end,void *menu,int depth) {
    if(depth>8)return 0;
    const unsigned char *p=*cursor;
    for(;;) {
        if(p>=end||((unsigned long long)(end-p)<2))return 0;
        unsigned int option=u32_menu_read_word(p);p+=2;
        int popup=(option&0x0010u)!=0;
        unsigned long long item_id=0;
        if(!popup) { if((unsigned long long)(end-p)<2)return 0;item_id=u32_menu_read_word(p);p+=2; }
        char text[128];unsigned int n=0;int terminated=0;
        while((unsigned long long)(end-p)>=2) {
            unsigned int wc=u32_menu_read_word(p);p+=2;
            if(!wc){terminated=1;break;}
            if(n+1<sizeof(text))text[n++]=wc<128?(char)wc:'?';
        }
        if(!terminated)return 0;text[n]=0;
        if(popup) {
            void *sub=CreatePopupMenu();if(!sub)return 0;
            if(!AppendMenuA(menu,(option&~0x0080u)|0x0010u,(unsigned long long)sub,text)){DestroyMenu(sub);return 0;}
            if(!u32_parse_menu_items(&p,end,sub,depth+1))return 0;
        } else if(!AppendMenuA(menu,option&~0x0080u,item_id,text))return 0;
        if(option&0x0080u){*cursor=p;return 1;}
    }
}
static void *u32_load_menu_template(const unsigned char *data,unsigned int bytes) {
    if(!data||bytes<4)return 0;
    unsigned int version=u32_menu_read_word(data),offset=u32_menu_read_word(data+2);
    if(version!=0||offset<4||offset>=bytes)return 0;
    void *menu=CreateMenu();if(!menu)return 0;
    const unsigned char *p=data+offset,*end=data+bytes;
    if(!u32_parse_menu_items(&p,end,menu,0)){DestroyMenu(menu);return 0;}
    return menu;
}
__attribute__((dllexport)) void *LoadMenuA(void *instance,const char *name) {
    unsigned long long id=(unsigned long long)name;if(!name||id>0xFFFFULL)return 0;
    const unsigned char *data=0;unsigned int bytes=0;
    if(!u32_pe_resource_data(instance,4u,(unsigned int)id,&data,&bytes))return 0;
    return u32_load_menu_template(data,bytes);
}
__attribute__((dllexport)) void *LoadMenuW(void *instance,const unsigned short *name) {
    unsigned long long id=(unsigned long long)name;if(!name||id>0xFFFFULL)return 0;
    return LoadMenuA(instance,(const char*)name);
}

__attribute__((dllexport)) int TranslateAcceleratorA(void *hwnd,void *table,MSG *msg) {
    if(!IsWindow(hwnd)||!msg)return 0;
    U32_ACCELTABLE *t=0;
    for(int i=0;i<16;i++)if(table==&u32_accel_tables[i]&&u32_accel_tables[i].used){t=&u32_accel_tables[i];break;}
    if(!t)return 0;
    if(msg->message!=0x0100u&&msg->message!=0x0104u&&msg->message!=0x0102u)return 0;
    for(int i=0;i<t->count;i++) {
        const U32_ACCEL *a=&t->entries[i];int key_match=0;
        if(a->fVirt&1u)key_match=(msg->message!=0x0102u&&msg->wParam==a->key);
        else key_match=(msg->message==0x0102u&&msg->wParam==a->key);
        if(!key_match)continue;
        if((a->fVirt&4u)!=0 && !u32_key_state[0x10])continue;
        if((a->fVirt&8u)!=0 && !u32_key_state[0x11])continue;
        if((a->fVirt&16u)!=0 && !u32_key_state[0x12])continue;
        SendMessageA(hwnd,0x0111u,a->cmd,0);return 1;
    }
    return 0;
}

__attribute__((dllexport)) int TranslateAcceleratorW(void *hwnd,void *table,MSG *msg){return TranslateAcceleratorA(hwnd,table,msg);}

__attribute__((dllexport)) BOOL GetClassInfoA(void *instance,const char *name,void *info){(void)instance;if(!name||!info)return 0;U32_CLASSREG *c=u32_class_find(name);if(!c)return 0;*(U32_WNDCLASSA*)info=c->wc;return 1;}

static U32_WCHAR u32_class_name_w[96],u32_class_menu_w[96];
static void u32_class_copy_wide(U32_WCHAR *out,unsigned int cap,const char *in){unsigned int i=0;if(!out||!cap)return;while(in&&in[i]&&i+1<cap){out[i]=(unsigned char)in[i];i++;}out[i]=0;}
__attribute__((dllexport)) BOOL GetClassInfoW(void *instance,const unsigned short *name,void *info){(void)instance;if(!name||!info)return 0;char narrow[96];u32_wide_to_ansi(narrow,sizeof(narrow),name);U32_CLASSREG *c=u32_class_find(narrow);if(!c)return 0;U32_WNDCLASSW *w=(U32_WNDCLASSW*)info;w->style=c->wc.style;w->proc=c->wc.proc;w->cls_extra=c->wc.cls_extra;w->wnd_extra=c->wc.wnd_extra;w->instance=c->wc.instance;w->icon=c->wc.icon;w->cursor=c->wc.cursor;w->background=c->wc.background;u32_class_copy_wide(u32_class_name_w,96,c->name);u32_class_copy_wide(u32_class_menu_w,96,c->menu);w->menu_name=c->wc.menu_name?u32_class_menu_w:0;w->class_name=u32_class_name_w;return 1;}

__attribute__((dllexport)) BOOL GetClassInfoExA(void *instance,const char *name,void *info){return GetClassInfoA(instance,name,info);}

__attribute__((dllexport)) BOOL GetClassInfoExW(void *instance,const unsigned short *name,void *info){return GetClassInfoW(instance,name,info);}

__attribute__((dllexport)) BOOL SystemParametersInfoA(unsigned int action,unsigned int param,void *data,unsigned int update){(void)update;if(action==0x57u&&data){*(unsigned int*)data=u32_double_click_time;return 1;}if(action==0x30u&&data){*(unsigned int*)data=0;return 1;}return 0;}

__attribute__((dllexport)) BOOL SystemParametersInfoW(unsigned int action,unsigned int param,void *data,unsigned int update){return SystemParametersInfoA(action,param,data,update);}

__attribute__((dllexport)) int GetWindowTextLength(void *hwnd){return GetWindowTextLengthA(hwnd);}

__attribute__((dllexport)) int GetClassName(void *hwnd,char *out,int cap){return GetClassNameA(hwnd,out,cap);}

__attribute__((dllexport)) int GetWindowText(void *hwnd,char *out,int cap){return GetWindowTextA(hwnd,out,cap);}

__attribute__((dllexport)) BOOL SetWindowText(void *hwnd,const char *text){return SetWindowTextA(hwnd,text);}

__attribute__((dllexport)) BOOL GetScrollBarInfo(void *hwnd,long object,U32_RECT *r){(void)object;if(!IsWindow(hwnd)||!r)return 0;r->left=g_wnd.x;r->top=g_wnd.y+24;r->right=g_wnd.x+g_wnd.w;r->bottom=g_wnd.y+g_wnd.h;return 1;}

__attribute__((dllexport)) BOOL GetComboBoxInfo(void *hwnd,void *info){(void)info;return IsWindow(hwnd)?0:0;}

__attribute__((dllexport)) DWORD GetListBoxInfo(void *hwnd){return IsWindow(hwnd)?0:0;}

__attribute__((dllexport)) DWORD GetWindowModuleFileName(void *hwnd,char *out,DWORD cap){return GetWindowModuleFileNameA(hwnd,out,cap);}

static int u32_dpi_context_awareness(void *context) {
    if(context==(void*)-1||context==(void*)-5)return 0;
    if(context==(void*)-2)return 1;
    if(context==(void*)-3||context==(void*)-4)return 2;
    return -1;
}
__attribute__((dllexport)) void *GetWindowDpiAwarenessContext(void *hwnd){return IsWindow(hwnd)?u32_thread_dpi_context:0;}

__attribute__((dllexport)) BOOL AreDpiAwarenessContextsEqual(void *a,void *b){return u32_dpi_context_awareness(a)>=0&&u32_dpi_context_awareness(b)>=0&&a==b;}

__attribute__((dllexport)) int GetAwarenessFromDpiAwarenessContext(void *context){return u32_dpi_context_awareness(context);}

__attribute__((dllexport)) int GetProcessDpiAwareness(HANDLE process,int *awareness){if(!awareness||(process!=(void*)-1&&process!=(void*)1))return (int)0x80070057u;*awareness=u32_process_dpi_awareness;return 0;}

__attribute__((dllexport)) BOOL SetPropA(void *hwnd,const char *name,void *data){if(!IsWindow(hwnd)||!name||!name[0])return 0;int free_slot=-1;for(int i=0;i<32;i++){if(u32_props[i].used&&u32_props[i].hwnd==hwnd){int j=0;while(name[j]&&u32_props[i].name[j]==name[j])j++;if(!name[j]&&!u32_props[i].name[j]){u32_props[i].data=data;return 1;}}if(!u32_props[i].used&&free_slot<0)free_slot=i;}if(free_slot<0)return 0;u32_props[free_slot].used=1;u32_props[free_slot].hwnd=hwnd;u32_props[free_slot].data=data;int j=0;while(name[j]&&j<47){u32_props[free_slot].name[j]=name[j];j++;}u32_props[free_slot].name[j]=0;return 1;} 

__attribute__((dllexport)) void *GetPropA(void *hwnd,const char *name){if(!IsWindow(hwnd)||!name)return 0;for(int i=0;i<32;i++)if(u32_props[i].used&&u32_props[i].hwnd==hwnd){int j=0;while(name[j]&&u32_props[i].name[j]==name[j])j++;if(!name[j]&&!u32_props[i].name[j])return u32_props[i].data;}return 0;} 

__attribute__((dllexport)) void *RemovePropA(void *hwnd,const char *name){if(!IsWindow(hwnd)||!name)return 0;for(int i=0;i<32;i++)if(u32_props[i].used&&u32_props[i].hwnd==hwnd){int j=0;while(name[j]&&u32_props[i].name[j]==name[j])j++;if(!name[j]&&!u32_props[i].name[j]){void *d=u32_props[i].data;u32_props[i].used=0;return d;}}return 0;}

__attribute__((dllexport)) BOOL SetPropW(void *hwnd,const unsigned short *name,void *data){if(!name)return 0;char a[48];int i=0;while(name[i]&&i<47){a[i]=(char)name[i];i++;}a[i]=0;return SetPropA(hwnd,a,data);}

__attribute__((dllexport)) void *GetPropW(void *hwnd,const unsigned short *name){if(!name)return 0;char a[48];int i=0;while(name[i]&&i<47){a[i]=(char)name[i];i++;}a[i]=0;return GetPropA(hwnd,a);}

__attribute__((dllexport)) void *RemovePropW(void *hwnd,const unsigned short *name){if(!name)return 0;char a[48];int i=0;while(name[i]&&i<47){a[i]=(char)name[i];i++;}a[i]=0;return RemovePropA(hwnd,a);}

__attribute__((dllexport)) BOOL SetLayeredWindowAttributes(void *hwnd,DWORD color,unsigned char alpha,DWORD flags){if(!IsWindow(hwnd)||(flags&~3u))return 0;u32_layered_color=color;u32_layered_alpha=alpha;u32_layered_flags=flags;return 1;}

__attribute__((dllexport)) BOOL GetLayeredWindowAttributes(void *hwnd,DWORD *color,unsigned char *alpha,DWORD *flags){if(!IsWindow(hwnd))return 0;if(color)*color=u32_layered_color;if(alpha)*alpha=u32_layered_alpha;if(flags)*flags=u32_layered_flags;return 1;}

__attribute__((dllexport)) BOOL UpdateLayeredWindow(void *hwnd,void *dc,const U32_POINT *dst,const U32_POINT *size,void *src,const U32_POINT *srcpt,DWORD key,const void *blend,DWORD flags){(void)dc;(void)src;(void)srcpt;(void)key;(void)blend;(void)flags;if(!IsWindow(hwnd))return 0;if(dst){g_wnd.x=dst->x;g_wnd.y=dst->y;}if(size){g_wnd.w=size->x;g_wnd.h=size->y;}return 1;}

__attribute__((dllexport)) BOOL RegisterHotKey(void *hwnd,int id,unsigned int modifiers,unsigned int key){if(hwnd&&!IsWindow(hwnd))return 0;for(int i=0;i<16;i++){if(u32_hotkeys[i].used&&u32_hotkeys[i].hwnd==hwnd&&u32_hotkeys[i].id==id)return 0;if(!u32_hotkeys[i].used){u32_hotkeys[i].used=1;u32_hotkeys[i].hwnd=hwnd;u32_hotkeys[i].id=id;u32_hotkeys[i].mod=modifiers;u32_hotkeys[i].key=key;return 1;}}return 0;}

__attribute__((dllexport)) BOOL UnregisterHotKey(void *hwnd,int id){for(int i=0;i<16;i++)if(u32_hotkeys[i].used&&u32_hotkeys[i].hwnd==hwnd&&u32_hotkeys[i].id==id){u32_hotkeys[i].used=0;return 1;}return 0;}

typedef struct { int used,id; void *proc; DWORD thread; } U32_HOOK;
static U32_HOOK u32_hooks[16];
__attribute__((dllexport)) void *SetWindowsHookExA(int id,void *proc,void *module,DWORD thread){(void)module;if(!proc)return 0;for(int i=0;i<16;i++)if(u32_hooks[i].used&&u32_hooks[i].id==id&&u32_hooks[i].proc==proc&&u32_hooks[i].thread==thread)return &u32_hooks[i];for(int i=0;i<16;i++)if(!u32_hooks[i].used){u32_hooks[i].used=1;u32_hooks[i].id=id;u32_hooks[i].proc=proc;u32_hooks[i].thread=thread;return &u32_hooks[i];}return 0;}
__attribute__((dllexport)) void *SetWindowsHookExW(int id,void *proc,void *module,DWORD thread){return SetWindowsHookExA(id,proc,module,thread);}
__attribute__((dllexport)) BOOL UnhookWindowsHookEx(void *hook){for(int i=0;i<16;i++)if(hook==&u32_hooks[i]&&u32_hooks[i].used){u32_hooks[i].used=0;u32_hooks[i].proc=0;u32_hooks[i].thread=0;return 1;}return 0;}
__attribute__((dllexport)) long long CallNextHookEx(void *hook,int code,unsigned long long wp,long long lp){int start=-1,id=-1;for(int i=0;i<16;i++)if(hook==&u32_hooks[i]&&u32_hooks[i].used){start=i;id=u32_hooks[i].id;break;}if(start<0)return 0;for(int i=start+1;i<16;i++)if(u32_hooks[i].used&&u32_hooks[i].id==id){long long (*next)(int,unsigned long long,long long)=(long long (*)(int,unsigned long long,long long))u32_hooks[i].proc;return next(code,wp,lp);}return 0;}

typedef struct { int used; void *hwnd; void *proc; unsigned long long id; void *data; } U32_SUBCLASS;
static U32_SUBCLASS u32_subclasses[64];
static U32_SUBCLASS *u32_subclass_find(void *hwnd,void *proc,unsigned long long id){for(int i=0;i<64;i++)if(u32_subclasses[i].used&&u32_subclasses[i].hwnd==hwnd&&u32_subclasses[i].proc==proc&&u32_subclasses[i].id==id)return &u32_subclasses[i];return 0;}
__attribute__((dllexport)) BOOL GetWindowSubclass(void *hwnd,void *proc,unsigned long long id,void **data){if(!IsWindow(hwnd)||!proc)return 0;U32_SUBCLASS *s=u32_subclass_find(hwnd,proc,id);if(!s)return 0;if(data)*data=s->data;return 1;}
__attribute__((dllexport)) BOOL SetWindowSubclass(void *hwnd,void *proc,unsigned long long id,void *data){if(!IsWindow(hwnd)||!proc)return 0;U32_SUBCLASS *s=u32_subclass_find(hwnd,proc,id);if(s){s->data=data;return 1;}for(int i=0;i<64;i++)if(!u32_subclasses[i].used){u32_subclasses[i].used=1;u32_subclasses[i].hwnd=hwnd;u32_subclasses[i].proc=proc;u32_subclasses[i].id=id;u32_subclasses[i].data=data;return 1;}return 0;}
__attribute__((dllexport)) BOOL RemoveWindowSubclass(void *hwnd,void *proc,unsigned long long id){U32_SUBCLASS *s=u32_subclass_find(hwnd,proc,id);if(!s)return 0;s->used=0;s->hwnd=0;s->proc=0;s->data=0;s->id=0;return 1;}
__attribute__((dllexport)) long long DefSubclassProc(void *hwnd,unsigned int msg,unsigned long long wp,long long lp){return DefWindowProcA(hwnd,msg,wp,lp);}

__attribute__((dllexport)) BOOL GetTextMetricsA(void *dc,void *metrics){(void)dc;if(!metrics)return 0;unsigned int *v=(unsigned int*)metrics;for(int i=0;i<16;i++)v[i]=0;v[0]=8;v[1]=8;v[2]=8;v[3]=8;return 1;}

__attribute__((dllexport)) BOOL GetTextMetricsW(void *dc,void *metrics){return GetTextMetricsA(dc,metrics);}

__attribute__((dllexport)) BOOL GetCharWidth32A(void *dc,unsigned int first,unsigned int last,int *widths){(void)dc;if(!widths||last<first||last-first>255)return 0;for(unsigned int i=first;i<=last;i++)widths[i-first]=8;return 1;}

__attribute__((dllexport)) BOOL GetCharWidth32W(void *dc,unsigned int first,unsigned int last,int *widths){return GetCharWidth32A(dc,first,last,widths);}

__attribute__((dllexport)) BOOL GetTextExtentExPointA(void *dc,const char *text,int count,int max_extent,int *fit,int *dx,U32_POINT *size){(void)dc;(void)max_extent;if(!text||count<0||!size)return 0;size->x=count*8;size->y=8;if(fit)*fit=count;if(dx)for(int i=0;i<count;i++)dx[i]=(i+1)*8;return 1;}

__attribute__((dllexport)) BOOL GetTextExtentExPointW(void *dc,const unsigned short *text,int count,int max_extent,int *fit,int *dx,U32_POINT *size){(void)text;return GetTextExtentExPointA(dc,"",count,max_extent,fit,dx,size);}

__attribute__((dllexport)) long long TabbedTextOutA(void *dc,int x,int y,const char *text,int count,int tabs,const int *stops,int origin){(void)dc;(void)tabs;(void)stops;(void)origin;if(!text||count<0)return 0;draw_text(x,y,text,COL_TEXT);return ((long long)8<<32)|(unsigned int)(count*8);}

__attribute__((dllexport)) long long TabbedTextOutW(void *dc,int x,int y,const unsigned short *text,int count,int tabs,const int *stops,int origin){(void)dc;(void)tabs;(void)stops;(void)origin;if(!text||count<0)return 0;for(int i=0;i<count;i++)draw_char(x+i*8,y,text[i]<128?(char)text[i]:'?',COL_TEXT);return ((long long)8<<32)|(unsigned int)(count*8);}

__attribute__((dllexport)) void *GetClipboardOwner(void){return u32_clipboard_owner;}

__attribute__((dllexport)) void *GetOpenClipboardWindow(void){return u32_clipboard_open?u32_clipboard_owner:0;}

__attribute__((dllexport)) DWORD GetClipboardSequenceNumber(void){return u32_clipboard_seq;}

static const char *u32_clip_lookup(unsigned int format) {
    switch (format) {
        case 1: return "CF_TEXT";
        case 2: return "CF_BITMAP";
        case 3: return "CF_METAFILEPICT";
        case 4: return "CF_SYLK";
        case 5: return "CF_DIF";
        case 6: return "CF_TIFF";
        case 7: return "CF_OEMTEXT";
        case 8: return "CF_DIB";
        case 9: return "CF_PALETTE";
        case 10: return "CF_PENDATA";
        case 11: return "CF_RIFF";
        case 12: return "CF_WAVE";
        case 13: return "CF_UNICODETEXT";
        case 14: return "CF_ENHMETAFILE";
        case 15: return "CF_HDROP";
        case 16: return "CF_LOCALE";
        case 17: return "CF_DIBV5";
        default: return 0;
    }
}

__attribute__((dllexport)) int GetClipboardFormatNameA(unsigned int format,char *name,int cap){const char *s=u32_clip_lookup(format);if(!name||cap<=0||!s)return 0;int i=0;while(s[i]&&i+1<cap){name[i]=s[i];i++;}name[i]=0;return i;}

__attribute__((dllexport)) int GetClipboardFormatNameW(unsigned int format,unsigned short *name,int cap){const char *s=u32_clip_lookup(format);if(!name||cap<=0||!s)return 0;int i=0;while(s[i]&&i+1<cap){name[i]=(unsigned char)s[i];i++;}name[i]=0;return i;}

__attribute__((dllexport)) DWORD GetFocusThread(void){return GetCurrentThreadId();}

__attribute__((dllexport)) BOOL GetWindowDisplayAffinityEx(void *hwnd,unsigned int *affinity){return GetWindowDisplayAffinity(hwnd,affinity);}

__attribute__((dllexport)) void *GetWindowDpiAwarenessContextEx(void *hwnd){return GetWindowDpiAwarenessContext(hwnd);}

__attribute__((dllexport)) long GetDpiForMonitor(void *monitor,int type,unsigned int *x,unsigned int *y){(void)monitor;(void)type;if(!x||!y)return -1;*x=96;*y=96;return 0;}

__attribute__((dllexport)) BOOL EnableNonClientDpiScaling(void *hwnd){return IsWindow(hwnd);}

__attribute__((dllexport)) void *SetThreadDpiAwarenessContext(void *context){if(u32_dpi_context_awareness(context)<0)return 0;void *old=u32_thread_dpi_context;u32_thread_dpi_context=context;return old;}

__attribute__((dllexport)) void *GetThreadDpiAwarenessContext(void){return u32_thread_dpi_context;}

__attribute__((dllexport)) unsigned int GetDpiFromDpiAwarenessContext(void *context){return u32_dpi_context_awareness(context)>=0?96u:0u;}

__attribute__((dllexport)) BOOL SetProcessDpiAwarenessContext(void *context){int awareness=u32_dpi_context_awareness(context);if(awareness<0)return 0;u32_process_dpi_awareness=awareness;u32_thread_dpi_context=context;return 1;}

static int u32_station_inherit,u32_desktop_inherit;
#define U32_WINSTA_HANDLE ((void *)0x1100)
#define U32_DESKTOP_HANDLE ((void *)0x1101)
static void *u32_process_station=U32_WINSTA_HANDLE,*u32_thread_desktop=U32_DESKTOP_HANDLE;
__attribute__((dllexport)) void *GetProcessWindowStation(void){return u32_process_station;}

__attribute__((dllexport)) void *GetThreadDesktop(DWORD thread){return thread==GetCurrentThreadId()?u32_thread_desktop:0;}

static int u32_user_object_name(void *object,const char **name,const char **type) {
    if(object==U32_WINSTA_HANDLE){if(name)*name="WinSta0";if(type)*type="WindowStation";return 1;}
    if(object==U32_DESKTOP_HANDLE){if(name)*name="Default";if(type)*type="Desktop";return 1;}
    return 0;
}
__attribute__((dllexport)) BOOL GetUserObjectInformationA(void *object,int index,void *info,DWORD len,DWORD *needed) {
    const char *name=0,*type=0;if(!u32_user_object_name(object,&name,&type)){if(needed)*needed=0;return 0;}
    if(index==1) {
        typedef struct { int fInherit; int fReserved; } U32_USEROBJECTFLAGS;
        if(needed)*needed=sizeof(U32_USEROBJECTFLAGS);
        if(!info||len<sizeof(U32_USEROBJECTFLAGS))return 0;
        U32_USEROBJECTFLAGS *f=(U32_USEROBJECTFLAGS*)info;f->fInherit=object==U32_WINSTA_HANDLE?u32_station_inherit:u32_desktop_inherit;f->fReserved=0;return 1;
    }
    if(index==2||index==3) {
        const char *text=index==2?name:type;DWORD n=0;while(text[n])n++;DWORD required=n+1;
        if(needed)*needed=required;
        if(!info||len<required){if(info&&len){DWORD i=0;while(i+1<len&&i<n){((char*)info)[i]=text[i];i++;}((char*)info)[i]=0;}return 0;}
        for(DWORD i=0;i<required;i++)((char*)info)[i]=text[i];return 1;
    }
    if(needed)*needed=0;return 0;
}
__attribute__((dllexport)) BOOL GetUserObjectInformationW(void *object,int index,void *info,DWORD len,DWORD *needed) {
    if(index==1)return GetUserObjectInformationA(object,index,info,len,needed);
    const char *name=0,*type=0;if(!u32_user_object_name(object,&name,&type)){if(needed)*needed=0;return 0;}
    if(index!=2&&index!=3){if(needed)*needed=0;return 0;}
    const char *text=index==2?name:type;DWORD n=0;while(text[n])n++;DWORD required=(n+1)*sizeof(U32_WCHAR);
    if(needed)*needed=required;
    if(!info||len<required){if(info&&len>=sizeof(U32_WCHAR)){U32_WCHAR *w=(U32_WCHAR*)info;DWORD max=len/sizeof(U32_WCHAR),i=0;while(i+1<max&&i<n){w[i]=(unsigned char)text[i];i++;}w[i]=0;}return 0;}
    U32_WCHAR *w=(U32_WCHAR*)info;for(DWORD i=0;i<=n;i++)w[i]=(unsigned char)text[i];return 1;
}
__attribute__((dllexport)) BOOL GetUserObjectSecurity(void *object,void *required,void *descriptor,DWORD len,DWORD *needed){(void)object;(void)required;(void)descriptor;(void)len;if(needed)*needed=0;return 0;}
__attribute__((dllexport)) BOOL SetUserObjectInformationA(void *object,int index,void *info,DWORD len) {
    if(!u32_user_object_name(object,0,0)||index!=1||!info||len<8)return 0;
    int inherit=*(int*)info;if(object==U32_WINSTA_HANDLE)u32_station_inherit=inherit!=0;else u32_desktop_inherit=inherit!=0;return 1;
}
__attribute__((dllexport)) BOOL SetUserObjectInformationW(void *object,int index,void *info,DWORD len){return SetUserObjectInformationA(object,index,info,len);}

__attribute__((dllexport)) BOOL EnumDisplayMonitors(void *dc,const U32_RECT *clip,void *callback,long long data){(void)dc;(void)clip;if(!callback)return 0;typedef BOOL(*CB)(void*,void*,U32_RECT*,long long);U32_RECT r={0,0,(int)g_fw,(int)g_fh};return ((CB)callback)(0,(void*)1,&r,data);}

__attribute__((dllexport)) void *MonitorFromWindow(void *hwnd,DWORD flags){if(IsWindow(hwnd))return (void*)1;return (flags&3u)?(void*)1:0;}

__attribute__((dllexport)) void *MonitorFromPoint(U32_POINT pt,DWORD flags){ensure_fb();if(pt.x>=0&&pt.y>=0&&(u32)pt.x<g_fw&&(u32)pt.y<g_fh)return (void*)1;return (flags&3u)?(void*)1:0;}

__attribute__((dllexport)) void *MonitorFromRect(const U32_RECT *r,DWORD flags){ensure_fb();if(r&&r->right>0&&r->bottom>0&&r->left<(int)g_fw&&r->top<(int)g_fh&&r->right>r->left&&r->bottom>r->top)return (void*)1;return (flags&3u)?(void*)1:0;}

__attribute__((dllexport)) BOOL GetMonitorInfoA(void *monitor,void *info){(void)monitor;if(!info)return 0;unsigned int *p=(unsigned int*)info;if(p[0]<sizeof(unsigned int)*7)return 0;U32_RECT *r=(U32_RECT*)((unsigned char*)info+4);r->left=0;r->top=0;r->right=(int)g_fw;r->bottom=(int)g_fh;return 1;}

__attribute__((dllexport)) BOOL GetMonitorInfoW(void *monitor,void *info){return GetMonitorInfoA(monitor,info);}

/*
 * DEVMODEA/W offsets are shared through dmDisplayFrequency on Win32 x64.
 * SentinelOS currently exposes one physical mode: the active framebuffer mode.
 * Full-screen windows can still use SetWindowPos/MoveWindow without changing
 * the physical display mode.
 */
enum {
    U32_DM_SIZE_OFF=68, U32_DM_FIELDS_OFF=72,
    U32_DM_BPP_OFF=168, U32_DM_WIDTH_OFF=172,
    U32_DM_HEIGHT_OFF=176, U32_DM_FREQ_OFF=184,
    U32_DM_MIN_SIZE=188,
    U32_DM_PELSWIDTH=0x00080000u,
    U32_DM_PELSHEIGHT=0x00100000u,
    U32_DM_BITSPERPEL=0x00040000u,
    U32_DM_DISPLAYFREQUENCY=0x00400000u
};

static unsigned int u32_dm_read32(const unsigned char *p,unsigned int off) {
    unsigned int v=0; for(unsigned int i=0;i<4;i++)v|=(unsigned int)p[off+i]<<(i*8); return v;
}
static void u32_dm_write32(unsigned char *p,unsigned int off,unsigned int v) {
    for(unsigned int i=0;i<4;i++)p[off+i]=(unsigned char)(v>>(i*8));
}
__attribute__((dllexport)) BOOL EnumDisplaySettingsA(const char *device,DWORD mode,void *devmode) {
    (void)device;
    if(!devmode || (mode!=0xFFFFFFFFu && mode!=0u))return 0;
    ensure_fb();
    unsigned char *p=(unsigned char*)devmode;
    unsigned int caller_size=(unsigned int)p[U32_DM_SIZE_OFF] | ((unsigned int)p[U32_DM_SIZE_OFF+1]<<8);
    if(caller_size<U32_DM_MIN_SIZE)return 0;
    u32_dm_write32(p,U32_DM_FIELDS_OFF,U32_DM_BITSPERPEL|U32_DM_PELSWIDTH|U32_DM_PELSHEIGHT|U32_DM_DISPLAYFREQUENCY);
    u32_dm_write32(p,U32_DM_BPP_OFF,32);
    u32_dm_write32(p,U32_DM_WIDTH_OFF,g_fw);
    u32_dm_write32(p,U32_DM_HEIGHT_OFF,g_fh);
    u32_dm_write32(p,U32_DM_FREQ_OFF,60);
    return 1;
}
__attribute__((dllexport)) BOOL EnumDisplaySettingsW(const unsigned short *device,DWORD mode,void *devmode){(void)device;return EnumDisplaySettingsA(0,mode,devmode);}

/* DISP_CHANGE_SUCCESSFUL=0, DISP_CHANGE_BADMODE=-2. We cannot reprogram the
 * physical display yet, so accept only the active mode (or a reset request). */
__attribute__((dllexport)) long ChangeDisplaySettingsA(void *devmode,DWORD flags) {
    (void)flags;
    if(!devmode)return 0;
    unsigned char *p=(unsigned char*)devmode;
    unsigned int size=(unsigned int)p[U32_DM_SIZE_OFF]|((unsigned int)p[U32_DM_SIZE_OFF+1]<<8);
    if(size<U32_DM_MIN_SIZE)return -2L;
    unsigned int fields=u32_dm_read32(p,U32_DM_FIELDS_OFF);
    ensure_fb();
    if((fields&U32_DM_PELSWIDTH)&&u32_dm_read32(p,U32_DM_WIDTH_OFF)!=g_fw)return -2L;
    if((fields&U32_DM_PELSHEIGHT)&&u32_dm_read32(p,U32_DM_HEIGHT_OFF)!=g_fh)return -2L;
    if((fields&U32_DM_BITSPERPEL)&&u32_dm_read32(p,U32_DM_BPP_OFF)!=32u)return -2L;
    if((fields&U32_DM_DISPLAYFREQUENCY)&&u32_dm_read32(p,U32_DM_FREQ_OFF)!=60u)return -2L;
    return 0;
}
__attribute__((dllexport)) long ChangeDisplaySettingsW(void *devmode,DWORD flags){return ChangeDisplaySettingsA(devmode,flags);}
__attribute__((dllexport)) long ChangeDisplaySettingsExA(const char *device,void *devmode,void *hwnd,DWORD flags,void *param){(void)device;(void)hwnd;(void)param;return ChangeDisplaySettingsA(devmode,flags);}
__attribute__((dllexport)) long ChangeDisplaySettingsExW(const unsigned short *device,void *devmode,void *hwnd,DWORD flags,void *param){(void)device;return ChangeDisplaySettingsExA(0,devmode,hwnd,flags,param);}

static int u32_pe_resource_data(void *instance,unsigned int type_id,unsigned int resource_id,const unsigned char **data,unsigned int *bytes);
__attribute__((dllexport)) void *CreateAcceleratorTableA(const void *entries,int count);
__attribute__((dllexport)) void *LoadAcceleratorsA(void *instance,const char *name) {
    unsigned long long raw=(unsigned long long)name;if(!name||raw>0xFFFFULL)return 0;
    const unsigned char *data=0;unsigned int bytes=0;
    if(!u32_pe_resource_data(instance,9u,(unsigned int)raw,&data,&bytes)||!bytes||bytes%sizeof(U32_ACCEL))return 0;
    unsigned int count=bytes/sizeof(U32_ACCEL);if(count>256)return 0;
    return CreateAcceleratorTableA(data,(int)count);
}
__attribute__((dllexport)) void *LoadAcceleratorsW(void *instance,const unsigned short *name) {
    unsigned long long raw=(unsigned long long)name;if(!name||raw>0xFFFFULL)return 0;
    return LoadAcceleratorsA(instance,(const char*)name);
}

__attribute__((dllexport)) void *CreateAcceleratorTableA(const void *entries,int count){if(!entries||count<=0||count>256)return 0;for(int i=0;i<16;i++)if(!u32_accel_tables[i].used){u32_accel_tables[i].entries=(const U32_ACCEL*)entries;u32_accel_tables[i].count=count;u32_accel_tables[i].used=1;return &u32_accel_tables[i];}return 0;}

__attribute__((dllexport)) void *CreateAcceleratorTableW(const void *entries,int count){return CreateAcceleratorTableA(entries,count);}

__attribute__((dllexport)) BOOL DestroyAcceleratorTable(void *table){for(int i=0;i<16;i++)if(table==&u32_accel_tables[i]&&u32_accel_tables[i].used){u32_accel_tables[i].used=0;u32_accel_tables[i].entries=0;u32_accel_tables[i].count=0;return 1;}return 0;}

__attribute__((dllexport)) int CopyAcceleratorTableA(void *table,void *entries,int count){U32_ACCELTABLE *t=0;for(int i=0;i<16;i++)if(table==&u32_accel_tables[i]&&u32_accel_tables[i].used){t=&u32_accel_tables[i];break;}if(!t)return 0;if(!entries||count<=0)return t->count;int n=count<t->count?count:t->count;U32_ACCEL *dst=(U32_ACCEL*)entries;for(int i=0;i<n;i++)dst[i]=t->entries[i];return n;}

__attribute__((dllexport)) int CopyAcceleratorTableW(void *table,void *entries,int count){return CopyAcceleratorTableA(table,entries,count);}

static unsigned short u32_pe_u16(const unsigned char *p){return (unsigned short)(p[0]|((unsigned short)p[1]<<8));}
static unsigned int u32_pe_u32(const unsigned char *p){return (unsigned int)p[0]|((unsigned int)p[1]<<8)|((unsigned int)p[2]<<16)|((unsigned int)p[3]<<24);}
static int u32_pe_range(unsigned int size,unsigned int off,unsigned int len){return off<=size&&len<=size-off;}
static int u32_pe_resource_data(void *instance,unsigned int type_id,unsigned int resource_id,const unsigned char **data,unsigned int *bytes) {
    if(!instance||!data||!bytes||(unsigned long long)instance<0x10000ULL)return 0;
    const unsigned char *base=(const unsigned char*)instance;
    if(u32_pe_u16(base)!=0x5A4D)return 0;
    unsigned int peoff=u32_pe_u32(base+0x3C);if(peoff>0x1000000u)return 0;
    const unsigned char *nt=base+peoff;if(u32_pe_u32(nt)!=0x00004550u)return 0;
    unsigned int optsize=u32_pe_u16(nt+20);const unsigned char *opt=nt+24;
    unsigned short magic=u32_pe_u16(opt);unsigned int dd=(magic==0x20Bu)?112u:(magic==0x10Bu)?96u:0xFFFFFFFFu;
    if(dd==0xFFFFFFFFu||optsize<dd+24u)return 0;
    unsigned int image_size=u32_pe_u32(opt+56),rrva=u32_pe_u32(opt+dd+16),rsize=u32_pe_u32(opt+dd+20);
    if(!image_size||!rrva||rsize<16||!u32_pe_range(image_size,rrva,rsize))return 0;
    const unsigned char *root=base+rrva;unsigned int count=(unsigned int)u32_pe_u16(root+12)+u32_pe_u16(root+14),type_off=0,name_off=0;
    if(!u32_pe_range(rsize,16,count*8u))return 0;
    for(unsigned int i=0;i<count;i++){const unsigned char *e=root+16+i*8;unsigned int name=u32_pe_u32(e);if(!(name&0x80000000u)&&(name&0xFFFFu)==type_id){unsigned int o=u32_pe_u32(e+4);if(o&0x80000000u)type_off=o&0x7FFFFFFFu;break;}}
    if(!type_off||!u32_pe_range(rsize,type_off,16))return 0;
    const unsigned char *td=root+type_off;count=(unsigned int)u32_pe_u16(td+12)+u32_pe_u16(td+14);
    if(!u32_pe_range(rsize,type_off+16,count*8u))return 0;
    for(unsigned int i=0;i<count;i++){const unsigned char *e=td+16+i*8;unsigned int name=u32_pe_u32(e);if(!(name&0x80000000u)&&(name&0xFFFFu)==resource_id){unsigned int o=u32_pe_u32(e+4);if(o&0x80000000u)name_off=o&0x7FFFFFFFu;break;}}
    if(!name_off||!u32_pe_range(rsize,name_off,16))return 0;
    const unsigned char *nd=root+name_off;count=(unsigned int)u32_pe_u16(nd+12)+u32_pe_u16(nd+14);
    if(!count||!u32_pe_range(rsize,name_off+16,count*8u))return 0;
    unsigned int o=u32_pe_u32(nd+20)&0x7FFFFFFFu;
    if(!u32_pe_range(rsize,o,16))return 0;
    const unsigned char *ld=root+o;count=(unsigned int)u32_pe_u16(ld+12)+u32_pe_u16(ld+14);
    if(!count||!u32_pe_range(rsize,o+16,count*8u))return 0;
    unsigned int entry=u32_pe_u32(ld+20);if(entry&0x80000000u)return 0;
    unsigned int data_off=entry&0x7FFFFFFFu;if(!u32_pe_range(rsize,data_off,16))return 0;
    const unsigned char *de=root+data_off;unsigned int rva=u32_pe_u32(de),length=u32_pe_u32(de+4);
    if(!u32_pe_range(image_size,rva,length))return 0;
    *data=base+rva;*bytes=length;return 1;
}
static int u32_pe_string_resource(void *instance,unsigned int id,const unsigned short **text,unsigned int *length){
    if(!instance||!text||!length||(unsigned long long)instance<0x10000ULL)return 0;
    const unsigned char *base=(const unsigned char*)instance;
    if(u32_pe_u16(base)!=0x5A4D)return 0;
    unsigned int peoff=u32_pe_u32(base+0x3C);if(peoff>0x1000000u)return 0;
    const unsigned char *nt=base+peoff;if(u32_pe_u32(nt)!=0x00004550u)return 0;
    unsigned int optsize=u32_pe_u16(nt+20);const unsigned char *opt=nt+24;
    unsigned short magic=u32_pe_u16(opt);unsigned int dd=(magic==0x20Bu)?112u:(magic==0x10Bu)?96u:0xFFFFFFFFu;
    if(dd==0xFFFFFFFFu||optsize<dd+24u)return 0;
    unsigned int image_size=u32_pe_u32(opt+56),rrva=u32_pe_u32(opt+dd+16),rsize=u32_pe_u32(opt+dd+20);
    if(!image_size||!rrva||rsize<16||!u32_pe_range(image_size,rrva,rsize))return 0;
    const unsigned char *root=base+rrva;
    unsigned int type_off=0,block_off=0,lang_off=0;
    unsigned int count=(unsigned int)u32_pe_u16(root+12)+u32_pe_u16(root+14);
    if(!u32_pe_range(rsize,16,count*8u))return 0;
    for(unsigned int i=0;i<count;i++){const unsigned char *e=root+16+i*8;if(!(u32_pe_u32(e)&0x80000000u)&&(u32_pe_u32(e)&0xFFFFu)==6u){unsigned int o=u32_pe_u32(e+4);if(o&0x80000000u)type_off=o&0x7FFFFFFFu;break;}}
    if(!type_off||!u32_pe_range(rsize,type_off,16))return 0;
    const unsigned char *td=root+type_off;count=(unsigned int)u32_pe_u16(td+12)+u32_pe_u16(td+14);if(!u32_pe_range(rsize,type_off+16,count*8u))return 0;
    unsigned int block_id=(id>>4)+1;
    for(unsigned int i=0;i<count;i++){const unsigned char *e=td+16+i*8;if(!(u32_pe_u32(e)&0x80000000u)&&(u32_pe_u32(e)&0xFFFFu)==block_id){unsigned int o=u32_pe_u32(e+4);if(o&0x80000000u)block_off=o&0x7FFFFFFFu;break;}}
    if(!block_off||!u32_pe_range(rsize,block_off,16))return 0;
    const unsigned char *bd=root+block_off;count=(unsigned int)u32_pe_u16(bd+12)+u32_pe_u16(bd+14);if(!count||!u32_pe_range(rsize,block_off+16,count*8u))return 0;
    unsigned int lo=u32_pe_u32(bd+20);if(!(lo&0x80000000u))return 0;lang_off=lo&0x7FFFFFFFu;
    if(!u32_pe_range(rsize,lang_off,16))return 0;const unsigned char *ld=root+lang_off;count=(unsigned int)u32_pe_u16(ld+12)+u32_pe_u16(ld+14);if(!count||!u32_pe_range(rsize,lang_off+16,count*8u))return 0;
    unsigned int data_off=u32_pe_u32(ld+20)&0x7FFFFFFFu;if(!u32_pe_range(rsize,data_off,16))return 0;
    const unsigned char *de=root+data_off;unsigned int rva=u32_pe_u32(de),bytes=u32_pe_u32(de+4);if(!u32_pe_range(image_size,rva,bytes))return 0;
    const unsigned char *p=base+rva;unsigned int idx=id&15u;
    for(unsigned int i=0;i<=idx;i++){if(!u32_pe_range(bytes,(unsigned int)(p-(base+rva)),2))return 0;unsigned int n=u32_pe_u16(p);p+=2;if(i==idx){if(!u32_pe_range(bytes,(unsigned int)(p-(base+rva)),n*2u))return 0;*text=(const unsigned short*)p;*length=n;return 1;}if(!u32_pe_range(bytes,(unsigned int)(p-(base+rva)),n*2u))return 0;p+=n*2u;}
    return 0;
}
__attribute__((dllexport)) int LoadStringA(void *instance,unsigned int id,char *buffer,int cap){if(!buffer||cap<=0){return 0;}buffer[0]=0;const unsigned short *src=0;unsigned int len=0;if(!u32_pe_string_resource(instance,id,&src,&len))return 0;unsigned int n=len<(unsigned int)(cap-1)?len:(unsigned int)(cap-1);for(unsigned int i=0;i<n;i++)buffer[i]=src[i]<128?(char)src[i]:'?';buffer[n]=0;return (int)n;}
__attribute__((dllexport)) int LoadStringW(void *instance,unsigned int id,unsigned short *buffer,int cap){if(!buffer||cap<=0)return 0;buffer[0]=0;const unsigned short *src=0;unsigned int len=0;if(!u32_pe_string_resource(instance,id,&src,&len))return 0;unsigned int n=len<(unsigned int)(cap-1)?len:(unsigned int)(cap-1);for(unsigned int i=0;i<n;i++)buffer[i]=src[i];buffer[n]=0;return (int)n;}

__attribute__((dllexport)) void *LoadMenuIndirectA(const void *template_data){return u32_load_menu_template((const unsigned char*)template_data,4096u);}

__attribute__((dllexport)) void *LoadMenuIndirectW(const void *template_data){return LoadMenuIndirectA(template_data);}

__attribute__((dllexport)) void *RegisterDeviceNotificationA(void *recipient,void *filter,DWORD flags){(void)filter;(void)flags;return recipient?(void *)1:0;}

__attribute__((dllexport)) void *RegisterDeviceNotificationW(void *recipient,void *filter,DWORD flags){return RegisterDeviceNotificationA(recipient,filter,flags);}

__attribute__((dllexport)) BOOL UnregisterDeviceNotification(void *handle){return handle!=0;}

__attribute__((dllexport)) BOOL SetProcessDPIAware(void){u32_process_dpi_awareness=1;u32_thread_dpi_context=(void*)-2;return 1;}

__attribute__((dllexport)) BOOL IsProcessDPIAware(void){return u32_process_dpi_awareness!=0;}

__attribute__((dllexport)) BOOL SetThreadDesktop(void *desktop){if(desktop!=U32_DESKTOP_HANDLE)return 0;u32_thread_desktop=desktop;return 1;}

__attribute__((dllexport)) void *OpenWindowStationA(const char *name,BOOL inherit,DWORD access){(void)access;if(!name||!u32_class_name_equal(name,"WinSta0"))return 0;u32_station_inherit=inherit!=0;return U32_WINSTA_HANDLE;}

__attribute__((dllexport)) void *OpenWindowStationW(const unsigned short *name,BOOL inherit,DWORD access){char a[64];if(!name)return 0;u32_wide_to_ansi(a,sizeof(a),name);return OpenWindowStationA(a,inherit,access);}

__attribute__((dllexport)) BOOL CloseWindowStation(void *station){return station==U32_WINSTA_HANDLE;}

__attribute__((dllexport)) BOOL SetProcessWindowStation(void *station){if(station!=U32_WINSTA_HANDLE)return 0;u32_process_station=station;return 1;}

__attribute__((dllexport)) BOOL EnumWindowStationsA(void *callback,long long data){if(!callback)return 0;typedef BOOL(*CB)(char*,long long);return ((CB)callback)("WinSta0",data);}

__attribute__((dllexport)) BOOL EnumWindowStationsW(void *callback,long long data){if(!callback)return 0;typedef BOOL(*CB)(unsigned short*,long long);unsigned short n[]={'W','i','n','S','t','a','0',0};return ((CB)callback)(n,data);}

__attribute__((dllexport)) BOOL EnumDesktopsA(void *station,void *callback,long long data){(void)station;if(!callback)return 0;typedef BOOL(*CB)(char*,long long);return ((CB)callback)("Default",data);}

__attribute__((dllexport)) BOOL EnumDesktopsW(void *station,void *callback,long long data){(void)station;if(!callback)return 0;typedef BOOL(*CB)(unsigned short*,long long);unsigned short n[]={'D','e','f','a','u','l','t',0};return ((CB)callback)(n,data);}

__attribute__((dllexport)) void *OpenDesktopA(const char *name,DWORD flags,BOOL inherit,DWORD access){(void)flags;(void)access;if(!name||!u32_class_name_equal(name,"Default"))return 0;u32_desktop_inherit=inherit!=0;return U32_DESKTOP_HANDLE;}

__attribute__((dllexport)) void *OpenDesktopW(const unsigned short *name,DWORD flags,BOOL inherit,DWORD access){char a[64];if(!name)return 0;u32_wide_to_ansi(a,sizeof(a),name);return OpenDesktopA(a,flags,inherit,access);}

__attribute__((dllexport)) BOOL CloseDesktop(void *desktop){return desktop==U32_DESKTOP_HANDLE;}

__attribute__((dllexport)) BOOL SwitchDesktop(void *desktop){if(desktop!=U32_DESKTOP_HANDLE)return 0;compose();return 1;}

__attribute__((dllexport)) BOOL GetUserObjectInformation(void *object,int index,void *info,DWORD len,DWORD *needed){return GetUserObjectInformationA(object,index,info,len,needed);}

__attribute__((dllexport)) BOOL SetUserObjectInformation(void *object,int index,void *info,DWORD len){return SetUserObjectInformationA(object,index,info,len);}

__attribute__((dllexport)) long GetMonitorDpiType(void *monitor,int type,unsigned int *dpi){(void)monitor;(void)type;if(!dpi)return -1;*dpi=96;return 0;}

__attribute__((dllexport)) int GetSystemMetricsForMonitor(void *monitor,int index){(void)monitor;return GetSystemMetrics(index);}

__attribute__((dllexport)) unsigned int GetWindowDpi(void *hwnd){return GetDpiForWindow(hwnd);}

__attribute__((dllexport)) unsigned int GetDpiForWindowEx(void *hwnd){return GetDpiForWindow(hwnd);}

__attribute__((dllexport)) BOOL GetPhysicalCursorPos(U32_POINT *pt){return GetCursorPos(pt);}

__attribute__((dllexport)) BOOL SetPhysicalCursorPos(int x,int y){return SetCursorPos(x,y);}

__attribute__((dllexport)) BOOL GetCursorInfoEx(void *info){return GetCursorInfo(info);}

__attribute__((dllexport)) BOOL SetWindowFeedbackSetting(void *hwnd,int setting,DWORD flags,DWORD size,const void *data) {
    if(!IsWindow(hwnd)||size>64||(!data&&size))return 0;
    int slot=-1;for(int i=0;i<16;i++){if(u32_feedback_settings[i].used&&u32_feedback_settings[i].hwnd==hwnd&&u32_feedback_settings[i].setting==setting){slot=i;break;}if(!u32_feedback_settings[i].used&&slot<0)slot=i;}
    if(slot<0)return 0;U32_FEEDBACK_SETTING *p=&u32_feedback_settings[slot];p->hwnd=hwnd;p->setting=setting;p->flags=flags;p->size=size;p->used=1;
    const unsigned char *src=(const unsigned char*)data;for(DWORD i=0;i<size;i++)p->data[i]=src[i];return 1;
}
__attribute__((dllexport)) BOOL GetWindowFeedbackSetting(void *hwnd,int setting,DWORD flags,DWORD *size,void *data) {
    (void)flags;if(!IsWindow(hwnd)||!size)return 0;
    U32_FEEDBACK_SETTING *p=0;for(int i=0;i<16;i++)if(u32_feedback_settings[i].used&&u32_feedback_settings[i].hwnd==hwnd&&u32_feedback_settings[i].setting==setting){p=&u32_feedback_settings[i];break;}
    if(!p){*size=0;return 1;}    DWORD cap=*size;*size=p->size;if(p->size&&(!data||cap<p->size))return 0;
    unsigned char *out=(unsigned char*)data;for(DWORD i=0;i<p->size;i++)out[i]=p->data[i];return 1;
}

typedef struct { DWORD attribute; void *data; unsigned long long size; } U32_COMPOSITION_DATA;
__attribute__((dllexport)) BOOL GetWindowCompositionAttribute(void *hwnd,void *data){
    if(!IsWindow(hwnd)||!data)return 0;U32_COMPOSITION_DATA *p=(U32_COMPOSITION_DATA*)data;
    if(p->attribute>=16||!p->data||!u32_composition_sizes[p->attribute])return 0;
    unsigned int need=u32_composition_sizes[p->attribute];if(p->size<need){p->size=need;return 0;}
    unsigned char *out=(unsigned char*)p->data;for(unsigned int i=0;i<need;i++)out[i]=u32_composition_values[p->attribute][i];p->size=need;return 1;
}

__attribute__((dllexport)) BOOL SetWindowCompositionAttribute(void *hwnd,const void *data){
    if(!IsWindow(hwnd)||!data)return 0;const U32_COMPOSITION_DATA *p=(const U32_COMPOSITION_DATA*)data;
    if(p->attribute>=16||p->size>64||(!p->data&&p->size))return 0;
    const unsigned char *in=(const unsigned char*)p->data;for(unsigned int i=0;i<(unsigned int)p->size;i++)u32_composition_values[p->attribute][i]=in[i];
    u32_composition_sizes[p->attribute]=(unsigned int)p->size;return 1;
}

__attribute__((dllexport)) BOOL GetMenuInfoEx(void *menu,U32_MENUINFO *info){return GetMenuInfo(menu,info);}

__attribute__((dllexport)) BOOL SetMenuInfoEx(void *menu,const U32_MENUINFO *info){return SetMenuInfo(menu,info);}

__attribute__((dllexport)) unsigned int GetMenuItemIDEx(void *menu,unsigned int pos){return GetMenuItemID(menu,pos);}

__attribute__((dllexport)) void *GetSubMenuEx(void *menu,int pos){return GetSubMenu(menu,pos);}

__attribute__((dllexport)) BOOL SetWindowRedraw(void *hwnd,BOOL redraw){if(!IsWindow(hwnd))return 0;int old=u32_redraw_enabled;u32_redraw_enabled=redraw!=0;if(!old&&u32_redraw_enabled)compose();return 1;}

__attribute__((dllexport)) BOOL GetWindowRedraw(void *hwnd){return IsWindow(hwnd);}

__attribute__((dllexport)) DWORD GetWindowArrange(void){return g_wnd.minimized?2u:(g_wnd.visible?1u:0u);}

__attribute__((dllexport)) unsigned int ArrangeIconicWindows(void *hwnd){return IsWindow(hwnd)?0:0;}

__attribute__((dllexport)) WORD CascadeWindows(void *parent,WORD flags,const U32_RECT *rect,UINT count,const void **windows){(void)flags;(void)rect;(void)count;(void)windows;return IsWindow(parent)?1:0;}

__attribute__((dllexport)) WORD TileWindows(void *parent,WORD flags,const U32_RECT *rect,UINT count,const void **windows){return CascadeWindows(parent,flags,rect,count,windows);}

__attribute__((dllexport)) BOOL GetWindowPlacementEx(void *hwnd,U32_RECT *rect){return GetWindowRect(hwnd,rect);}

__attribute__((dllexport)) BOOL GetWindowRectEx(void *hwnd,U32_RECT *rect){return GetWindowRect(hwnd,rect);}

__attribute__((dllexport)) BOOL SetWindowPosEx(void *hwnd,void *after,int x,int y,int cx,int cy,unsigned int flags){return SetWindowPos(hwnd,after,x,y,cx,cy,flags);}

__attribute__((dllexport)) BOOL MoveWindowEx(void *hwnd,int x,int y,int w,int h,BOOL repaint){return MoveWindow(hwnd,x,y,w,h,repaint);}

__attribute__((dllexport)) BOOL SetForegroundWindowEx(void *hwnd){return SetForegroundWindow(hwnd);}

__attribute__((dllexport)) void *GetForegroundWindowEx(void){return GetForegroundWindow();}

__attribute__((dllexport)) DWORD GetPixel(void *dc,int x,int y){(void)dc;if(!g_front||x<0||y<0||(u32)x>=g_fw||(u32)y>=g_fh)return 0xFFFFFFFFu;return g_front[y*g_pitch+x]&0x00FFFFFFu;}

__attribute__((dllexport)) DWORD SetPixel(void *dc,int x,int y,DWORD color){(void)dc;if(!g_front||x<0||y<0||(u32)x>=g_fw||(u32)y>=g_fh)return 0xFFFFFFFFu;DWORD old=g_front[y*g_pitch+x]&0x00FFFFFFu;putpx(x,y,0xFF000000u|(color&0x00FFFFFFu));return old;}

__attribute__((dllexport)) BOOL SetPixelV(void *dc,int x,int y,DWORD color){return SetPixel(dc,x,y,color)!=0xFFFFFFFFu;}

__attribute__((dllexport)) BOOL MoveToEx(void *dc,int x,int y,U32_POINT *old){(void)dc;static int px,py;if(old){old->x=px;old->y=py;}px=x;py=y;return 1;}

__attribute__((dllexport)) BOOL LineTo(void *dc,int x,int y){(void)dc;static int px,py;int dx=x-px,dy=y-py,steps=dx<0?-dx:dx;if((dy<0?-dy:dy)>steps)steps=dy<0?-dy:dy;if(steps==0){putpx(x,y,COL_TEXT);return 1;}for(int i=0;i<=steps;i++)putpx(px+dx*i/steps,py+dy*i/steps,COL_TEXT);px=x;py=y;return 1;}

__attribute__((dllexport)) BOOL Rectangle(void *dc,int left,int top,int right,int bottom){(void)dc;U32_RECT r={left,top,right,bottom};return FrameRect(0,&r,(void *)(unsigned long long)COL_TEXT);}

__attribute__((dllexport)) BOOL Ellipse(void *dc,int left,int top,int right,int bottom){(void)dc;int rx=(right-left)/2,ry=(bottom-top)/2,cx=left+rx,cy=top+ry;if(rx<=0||ry<=0)return 0;for(int y=-ry;y<=ry;y++)for(int x=-rx;x<=rx;x++){long long lhs=(long long)x*x*ry*ry+(long long)y*y*rx*rx;long long rhs=(long long)rx*rx*ry*ry;if(lhs<=rhs&&lhs>=rhs-(long long)(rx+ry)*2)putpx(cx+x,cy+y,COL_TEXT);}return 1;}

__attribute__((dllexport)) BOOL RoundRect(void *dc,int left,int top,int right,int bottom,int ew,int eh){(void)ew;(void)eh;return Rectangle(dc,left,top,right,bottom);}

__attribute__((dllexport)) BOOL Polyline(void *dc,const U32_POINT *pts,int count){(void)dc;if(!pts||count<2)return 0;for(int i=1;i<count;i++){int dx=pts[i].x-pts[i-1].x,dy=pts[i].y-pts[i-1].y,steps=dx<0?-dx:dx;if((dy<0?-dy:dy)>steps)steps=dy<0?-dy:dy;if(!steps){putpx(pts[i].x,pts[i].y,COL_TEXT);continue;}for(int j=0;j<=steps;j++)putpx(pts[i-1].x+dx*j/steps,pts[i-1].y+dy*j/steps,COL_TEXT);}return 1;}

__attribute__((dllexport)) BOOL Polygon(void *dc,const U32_POINT *pts,int count){if(!Polyline(dc,pts,count))return 0;U32_POINT close[2];close[0]=pts[count-1];close[1]=pts[0];return Polyline(dc,close,2);}

__attribute__((dllexport)) int SetROP2(void *dc,int mode){(void)dc;static int current=13;int old=current;current=mode;return old;}

__attribute__((dllexport)) int GetROP2(void *dc){(void)dc;return 13;}

__attribute__((dllexport)) int SetStretchBltMode(void *dc,int mode){(void)dc;static int current=1;int old=current;current=mode;return old;}

__attribute__((dllexport)) int GetStretchBltMode(void *dc){(void)dc;return 1;}

__attribute__((dllexport)) int SetMapMode(void *dc,int mode){(void)dc;static int current=1;int old=current;current=mode;return old;}

__attribute__((dllexport)) int GetMapMode(void *dc){(void)dc;return 1;}

__attribute__((dllexport)) BOOL GetWindowExtEx(void *dc,U32_POINT *pt){(void)dc;if(!pt)return 0;pt->x=1;pt->y=1;return 1;}

__attribute__((dllexport)) BOOL SetWindowExtEx(void *dc,int x,int y,U32_POINT *old){(void)dc;if(old){old->x=1;old->y=1;}return x!=0&&y!=0;}

__attribute__((dllexport)) BOOL GetViewportExtEx(void *dc,U32_POINT *pt){return GetWindowExtEx(dc,pt);}

__attribute__((dllexport)) BOOL SetViewportExtEx(void *dc,int x,int y,U32_POINT *old){return SetWindowExtEx(dc,x,y,old);}

__attribute__((dllexport)) int SetArcDirection(void *dc,int direction){(void)dc;static int current=1;int old=current;current=direction;return old;}

__attribute__((dllexport)) int GetArcDirection(void *dc){(void)dc;return 1;}

__attribute__((dllexport)) int GetWindowRgnEx(void *hwnd,void *region){return GetWindowRgn(hwnd,region);}

__attribute__((dllexport)) BOOL GetClassInfoEx(void *instance,const char *name,void *info){return GetClassInfoA(instance,name,info);}

__attribute__((dllexport)) int GetWindowTextExA(void *hwnd,char *out,int cap){return GetWindowTextA(hwnd,out,cap);}

__attribute__((dllexport)) BOOL SetWindowTextExA(void *hwnd,const char *text){return SetWindowTextA(hwnd,text);}

__attribute__((dllexport)) unsigned int GetMenuDefaultItemEx(void *menu,BOOL bypos,unsigned int flags){return GetMenuDefaultItem(menu,bypos,flags);}

__attribute__((dllexport)) void *GetSystemMenuEx(void *hwnd,BOOL revert){return GetSystemMenu(hwnd,revert);}

__attribute__((dllexport)) BOOL SetMenuEx(void *hwnd,void *menu){return SetMenu(hwnd,menu);}

__attribute__((dllexport)) void *GetMenuEx(void *hwnd){return GetMenu(hwnd);}

__attribute__((dllexport)) void *GetWindowParent(void *hwnd){return GetParent(hwnd);}

__attribute__((dllexport)) void *GetWindowAncestor(void *hwnd,unsigned int kind){return GetAncestor(hwnd,kind);}

__attribute__((dllexport)) void *GetWindowChild(void *hwnd,unsigned int cmd){return GetWindow(hwnd,cmd);}

__attribute__((dllexport)) void *GetWindowTop(void *hwnd){return GetTopWindow(hwnd);}

__attribute__((dllexport)) void *GetWindowOwnerEx(void *hwnd){return GetWindowOwner(hwnd);}

__attribute__((dllexport)) BOOL GetWindowEnabled(void *hwnd){return IsWindowEnabled(hwnd);}

__attribute__((dllexport)) BOOL SetWindowEnabled(void *hwnd,BOOL enabled){return EnableWindow(hwnd,enabled);}

__attribute__((dllexport)) BOOL GetWindowVisible(void *hwnd){return IsWindowVisible(hwnd);}

__attribute__((dllexport)) BOOL SetWindowVisible(void *hwnd,BOOL visible){return ShowWindow(hwnd,visible?5:0);}

__attribute__((dllexport)) BOOL GetWindowMinimized(void *hwnd){return IsIconic(hwnd);}

__attribute__((dllexport)) BOOL GetWindowZoomed(void *hwnd){return IsZoomed(hwnd);}

__attribute__((dllexport)) int GetWindowClassNameA(void *hwnd,char *out,int cap){return GetClassNameA(hwnd,out,cap);}

__attribute__((dllexport)) int GetWindowClassNameW(void *hwnd,unsigned short *out,int cap){return GetClassNameW(hwnd,out,cap);}

__attribute__((dllexport)) unsigned int GetMenuItemState(void *menu,unsigned int item,unsigned int flags){return GetMenuState(menu,item,flags);}

__attribute__((dllexport)) BOOL SetMenuItemState(void *menu,unsigned int item,unsigned int flags){return EnableMenuItem(menu,item,flags);}

__attribute__((dllexport)) unsigned long GetWindowStyleEx(void *hwnd){return GetWindowExStyle(hwnd);}

__attribute__((dllexport)) unsigned long SetWindowStyleEx(void *hwnd,unsigned long style){return SetWindowExStyle(hwnd,style);}

__attribute__((dllexport)) void *GetWindowUserData(void *hwnd){return (void *)GetWindowLongPtrA(hwnd,-21);}

__attribute__((dllexport)) void *SetWindowUserData(void *hwnd,void *data){return (void *)SetWindowLongPtrA(hwnd,-21,(long long)data);}

__attribute__((dllexport)) void *GetWindowProcedure(void *hwnd){return (void *)GetWindowLongPtrA(hwnd,-4);}

__attribute__((dllexport)) void *SetWindowProcedure(void *hwnd,void *proc){return (void *)SetWindowLongPtrA(hwnd,-4,(long long)proc);}

__attribute__((dllexport)) unsigned int GetWindowIdentifier(void *hwnd){return (unsigned int)GetDlgCtrlID(hwnd);}

__attribute__((dllexport)) unsigned int SetWindowIdentifier(void *hwnd,unsigned int id){return (unsigned int)SetWindowLongA(hwnd,-12,(long)id);}

__attribute__((dllexport)) unsigned long long GetWindowContextHelpIdEx(void *hwnd){return GetWindowContextHelpId(hwnd);}

__attribute__((dllexport)) BOOL SetWindowContextHelpIdEx(void *hwnd,unsigned long long id){return SetWindowContextHelpId(hwnd,id);}

__attribute__((dllexport)) int GetMenuCount(void *menu){return GetMenuItemCount(menu);}

__attribute__((dllexport)) unsigned int GetMenuItemId(void *menu,int pos){return GetMenuItemID(menu,(unsigned int)pos);}

__attribute__((dllexport)) int GetMenuString(void *menu,unsigned int item,char *out,int cap,unsigned int flags){return GetMenuStringA(menu,item,out,cap,flags);}

__attribute__((dllexport)) BOOL AppendMenu(void *menu,unsigned int flags,unsigned long long id,const char *text){return AppendMenuA(menu,flags,id,text);}

__attribute__((dllexport)) BOOL InsertMenu(void *menu,unsigned int pos,unsigned int flags,unsigned long long id,const char *text){return InsertMenuA(menu,pos,flags,id,text);}

__attribute__((dllexport)) BOOL ModifyMenu(void *menu,unsigned int item,unsigned int flags,unsigned long long id,const char *text){return ModifyMenuA(menu,item,flags,id,text);}

__attribute__((dllexport)) BOOL DeleteMenuItem(void *menu,unsigned int item,unsigned int flags){return DeleteMenu(menu,item,flags);}

__attribute__((dllexport)) BOOL RemoveMenuItem(void *menu,unsigned int item,unsigned int flags){return RemoveMenu(menu,item,flags);}

__attribute__((dllexport)) DWORD CheckMenuItemState(void *menu,unsigned int item,unsigned int flags){return CheckMenuItem(menu,item,flags);}

__attribute__((dllexport)) DWORD EnableMenuItemState(void *menu,unsigned int item,unsigned int flags){return EnableMenuItem(menu,item,flags);}

__attribute__((dllexport)) unsigned short RegisterClassExA(void *wc){return RegisterClassA(wc);}

__attribute__((dllexport)) unsigned short RegisterClassExW(void *wc){return RegisterClassW(wc);}

__attribute__((dllexport)) void *CreateWindowA(const char *cls,const char *title,unsigned long style,int x,int y,int w,int h,void *parent,void *menu,void *instance,void *param){return CreateWindowExA(0,cls,title,style,x,y,w,h,parent,menu,instance,param);}

__attribute__((dllexport)) void *CreateWindowW(const unsigned short *cls,const unsigned short *title,unsigned long style,int x,int y,int w,int h,void *parent,void *menu,void *instance,void *param){return CreateWindowExW(0,cls,title,style,x,y,w,h,parent,menu,instance,param);}

__attribute__((dllexport)) long long DispatchMessageW(const MSG *msg){
    if(!msg)return 0;
    if(msg->message==0x0113u&&msg->lParam)return (long long)DispatchMessageA(msg);
    return (long long)SendMessageW((void *)(unsigned long long)msg->hwnd,msg->message,msg->wParam,msg->lParam);
}

__attribute__((dllexport)) void *CreateDialogIndirectParamA(void *instance,const void *templ,void *parent,void *proc,long long param){(void)instance;(void)templ;(void)proc;(void)param;return IsWindow(parent)?parent:CreateWindowExA(0,"DIALOG","Dialog",0x10000000,0,0,320,200,parent,0,0,0);}

__attribute__((dllexport)) void *CreateDialogIndirectParamW(void *instance,const void *templ,void *parent,void *proc,long long param){(void)instance;(void)templ;(void)proc;(void)param;return IsWindow(parent)?parent:CreateWindowExW(0,0,0,0x10000000,0,0,320,200,parent,0,0,0);}

__attribute__((dllexport)) int DialogBoxIndirectParamA(void *instance,const void *templ,void *parent,void *proc,long long param){(void)instance;(void)templ;(void)proc;(void)param;return IsWindow(parent)?1:-1;}

__attribute__((dllexport)) int DialogBoxIndirectParamW(void *instance,const void *templ,void *parent,void *proc,long long param){return DialogBoxIndirectParamA(instance,templ,parent,proc,param);}

__attribute__((dllexport)) unsigned int GetRawInputData(void *raw,unsigned int command,void *data,unsigned int *size,unsigned int header){(void)raw;(void)command;(void)data;(void)header;if(size)*size=0;return 0xFFFFFFFFu;}

__attribute__((dllexport)) unsigned int GetRawInputBuffer(void *data,unsigned int *size,unsigned int header){(void)data;(void)header;if(size)*size=0;return 0xFFFFFFFFu;}

__attribute__((dllexport)) unsigned int GetRawInputDeviceInfoA(void *device,unsigned int command,void *data,unsigned int *size){
    typedef struct { unsigned int cbSize,dwType; union { struct { unsigned int dwId,dwNumberOfButtons,dwSampleRate,fHasHorizontalWheel; } mouse; unsigned char raw[16]; } u; } U32_RID_DEVICE_INFO;
    static const char name[]="\\\\?\\HID#VID_0000&PID_0000#SENTINELOS_MOUSE";
    if(device!=(void*)0x200||!size)return 0xFFFFFFFFu;
    if(command==0x20000007u){
        unsigned int need=sizeof(name);
        if(!data){*size=need;return need-1;}
        if(*size<need){*size=need;return 0xFFFFFFFFu;}
        for(unsigned int i=0;i<need;i++)((char*)data)[i]=name[i];
        *size=need;
        return need-1;
    }
    if(command==0x2000000Bu){
        unsigned int need=sizeof(U32_RID_DEVICE_INFO);
        if(!data||*size<need){*size=need;return 0xFFFFFFFFu;}
        U32_RID_DEVICE_INFO *info=(U32_RID_DEVICE_INFO*)data;
        info->cbSize=need;info->dwType=0;info->u.mouse.dwId=0;
        info->u.mouse.dwNumberOfButtons=3;info->u.mouse.dwSampleRate=60;
        info->u.mouse.fHasHorizontalWheel=0;*size=need;return 0;
    }
    *size=0;return 0xFFFFFFFFu;
}

__attribute__((dllexport)) unsigned int GetRawInputDeviceInfoW(void *device,unsigned int command,void *data,unsigned int *size){
    if(command!=0x20000007u)return GetRawInputDeviceInfoA(device,command,data,size);
    static const char name[]="\\\\?\\HID#VID_0000&PID_0000#SENTINELOS_MOUSE";
    unsigned int chars=sizeof(name);
    if(device!=(void*)0x200||!size)return 0xFFFFFFFFu;
    if(!data){*size=chars;return chars-1;}
    if(*size<chars){*size=chars;return 0xFFFFFFFFu;}
    unsigned short *out=(unsigned short*)data;
    for(unsigned int i=0;i<chars;i++)out[i]=(unsigned char)name[i];
    *size=chars;return chars-1;
}

__attribute__((dllexport)) unsigned int GetRawInputDeviceList(void *list,unsigned int *count,unsigned int size){
    typedef struct { void *hDevice; unsigned int dwType; } U32_RAWINPUTDEVICELIST;
    const unsigned int available=1;
    if(!count)return 0xFFFFFFFFu;
    if(size!=sizeof(U32_RAWINPUTDEVICELIST))return 0xFFFFFFFFu;
    if(!list){*count=available;return available;}
    if(*count<available){*count=available;return 0xFFFFFFFFu;}
    U32_RAWINPUTDEVICELIST *devices=(U32_RAWINPUTDEVICELIST*)list;
    devices[0].hDevice=(void*)0x200;
    devices[0].dwType=0; /* RIM_TYPEMOUSE */
    *count=available;
    return available;
}

__attribute__((dllexport)) BOOL RegisterRawInputDevices(const void *devices,unsigned int count,unsigned int size){
    if(!devices||!count||count>16||size!=sizeof(U32_RAWINPUTDEVICE))return 0;
    const U32_RAWINPUTDEVICE *input=(const U32_RAWINPUTDEVICE*)devices;
    for(unsigned int n=0;n<count;n++){
        int found=-1,freeSlot=-1;
        for(int i=0;i<16;i++){if(u32_raw_devices[i].usagePage==input[n].usagePage&&u32_raw_devices[i].usage==input[n].usage&&u32_raw_devices[i].usagePage)found=i;if(!u32_raw_devices[i].usagePage&&freeSlot<0)freeSlot=i;}
        if(input[n].flags&1u){if(input[n].hwndTarget)return 0;if(found>=0)u32_raw_devices[found].usagePage=0;continue;}
        int at=found>=0?found:freeSlot;if(at<0)return 0;u32_raw_devices[at]=input[n];
    }
    return 1;
}

__attribute__((dllexport)) long long DefRawInputProc(void **packets,int count,unsigned int size){if(count<0||(!packets&&count)||size==0)return -1;return 0;}

__attribute__((dllexport)) BOOL GetPointerType(unsigned int id,unsigned int *type){if(id!=1||!type)return 0;*type=4;return 1;}

__attribute__((dllexport)) BOOL GetPointerInfo(unsigned int id,void *info){
    if(id!=1||!info)return 0;
    U32_POINTER_INFO *p=(U32_POINTER_INFO*)info;
    p->pointerType=4;p->pointerId=1;p->frameId=u32_mouse_time;p->pointerFlags=0x2002u;
    p->sourceDevice=(void*)0x200;p->hwndTarget=g_wnd.visible?(void*)1:(void*)2;
    p->ptPixelLocation.x=g_mx;p->ptPixelLocation.y=g_my;
    p->ptHimetricLocation.x=(g_mx*2540)/96;p->ptHimetricLocation.y=(g_my*2540)/96;
    p->ptPixelLocationRaw=p->ptPixelLocation;p->ptHimetricLocationRaw=p->ptHimetricLocation;
    p->dwTime=u32_mouse_time;p->historyCount=1;p->InputData=0;p->dwKeyStates=u32_key_state[1]?1u:0u;p->PerformanceCount=0;p->ButtonChangeType=0;return 1;
}

__attribute__((dllexport)) BOOL GetPointerInfoHistory(unsigned int id,unsigned int *count,void *info){if(id!=1||!count)return 0;unsigned int available=u32_mouse_history_count?u32_mouse_history_count:1,capacity=*count;if(!info){*count=available;return 1;}if(!capacity){*count=available;return 0;}unsigned int n=capacity<available?capacity:available;U32_POINTER_INFO *out=(U32_POINTER_INFO*)info;for(unsigned int i=0;i<n;i++){int x=g_mx,y=g_my;unsigned int tm=u32_mouse_time;if(u32_mouse_history_count){unsigned int idx=(u32_mouse_history_head+u32_mouse_history_count-n+i)%32;x=u32_mouse_history[idx].x;y=u32_mouse_history[idx].y;tm=u32_mouse_history[idx].time;}out[i].pointerType=4;out[i].pointerId=1;out[i].frameId=tm;out[i].pointerFlags=0x2002u;out[i].sourceDevice=(void*)0x200;out[i].hwndTarget=g_wnd.visible?(void*)1:(void*)2;out[i].ptPixelLocation.x=x;out[i].ptPixelLocation.y=y;out[i].ptHimetricLocation.x=(x*2540)/96;out[i].ptHimetricLocation.y=(y*2540)/96;out[i].ptPixelLocationRaw=out[i].ptPixelLocation;out[i].ptHimetricLocationRaw=out[i].ptHimetricLocation;out[i].dwTime=tm;out[i].historyCount=available;out[i].InputData=0;out[i].dwKeyStates=u32_key_state[1]?1u:0u;out[i].PerformanceCount=0;out[i].ButtonChangeType=0;}*count=n;return 1;}

__attribute__((dllexport)) BOOL GetPointerPenInfo(unsigned int id,void *info){(void)info;return id!=1?0:0;}

__attribute__((dllexport)) BOOL GetPointerTouchInfo(unsigned int id,void *info){(void)info;return id!=1?0:0;}

__attribute__((dllexport)) BOOL GetPointerFrameInfo(unsigned int id,unsigned int *count,void *info){return GetPointerInfoHistory(id,count,info);}

__attribute__((dllexport)) BOOL GetPointerDevice(unsigned int id,void **device){if(id!=1||!device)return 0;*device=(void*)0x200;return 1;}

__attribute__((dllexport)) BOOL GetPointerDeviceRects(void *device,U32_RECT *pointer,U32_RECT *display){if(device!=(void*)0x200)return 0;if(pointer){pointer->left=0;pointer->top=0;pointer->right=(int)g_fw;pointer->bottom=(int)g_fh;}if(display){display->left=0;display->top=0;display->right=(int)g_fw;display->bottom=(int)g_fh;}return 1;}

__attribute__((dllexport)) BOOL GetPointerDevices(unsigned int *count,void *devices){if(!count)return 0;if(!devices){*count=1;return 1;}*count=1;return 0;}

__attribute__((dllexport)) BOOL GetGestureInfo(void *handle,void *info){(void)handle;(void)info;return 0;}

__attribute__((dllexport)) BOOL CloseGestureInfoHandle(void *handle){return handle!=0;}

__attribute__((dllexport)) BOOL GetGestureExtraArgs(void *handle,unsigned int size,void *args){(void)handle;(void)size;(void)args;return 0;}

__attribute__((dllexport)) BOOL SetGestureConfig(void *hwnd,DWORD reserved,unsigned int count,const void *config,unsigned int size){(void)reserved;(void)count;(void)config;(void)size;return IsWindow(hwnd);}

__attribute__((dllexport)) BOOL GetTouchInputInfo(void *touch,unsigned int count,void *info,int size){(void)touch;(void)count;(void)info;(void)size;return 0;}


/* Win32 API batch 1: rectangle operations and dialog geometry APIs. */
typedef struct { long left,top,right,bottom; } U32_BATCH_RECT;
typedef struct { long x,y; } U32_BATCH_POINT;
__attribute__((dllexport)) BOOL SetRect(U32_BATCH_RECT *r,int l,int t,int right,int bottom){if(!r)return 0;r->left=l;r->top=t;r->right=right;r->bottom=bottom;return 1;}

__attribute__((dllexport)) BOOL SetRectEmpty(U32_BATCH_RECT *r){if(!r)return 0;r->left=r->top=r->right=r->bottom=0;return 1;}

__attribute__((dllexport)) BOOL CopyRect(U32_BATCH_RECT *d,const U32_BATCH_RECT *s){if(!d||!s)return 0;*d=*s;return 1;}

__attribute__((dllexport)) BOOL IsRectEmpty(const U32_BATCH_RECT *r){return !r||r->right<=r->left||r->bottom<=r->top;}

__attribute__((dllexport)) BOOL EqualRect(const U32_BATCH_RECT *a,const U32_BATCH_RECT *b){return a&&b&&a->left==b->left&&a->top==b->top&&a->right==b->right&&a->bottom==b->bottom;}

__attribute__((dllexport)) BOOL PtInRect(const U32_BATCH_RECT *r,U32_BATCH_POINT p){return r&&p.x>=r->left&&p.x<r->right&&p.y>=r->top&&p.y<r->bottom;}

__attribute__((dllexport)) BOOL OffsetRect(U32_BATCH_RECT *r,int dx,int dy){if(!r)return 0;r->left+=dx;r->right+=dx;r->top+=dy;r->bottom+=dy;return 1;}

__attribute__((dllexport)) BOOL InflateRect(U32_BATCH_RECT *r,int dx,int dy){if(!r)return 0;r->left-=dx;r->right+=dx;r->top-=dy;r->bottom+=dy;return 1;}

__attribute__((dllexport)) BOOL IntersectRect(U32_BATCH_RECT *d,const U32_BATCH_RECT *a,const U32_BATCH_RECT *b){if(!d||!a||!b)return 0;d->left=a->left>b->left?a->left:b->left;d->top=a->top>b->top?a->top:b->top;d->right=a->right<b->right?a->right:b->right;d->bottom=a->bottom<b->bottom?a->bottom:b->bottom;if(d->right<=d->left||d->bottom<=d->top){SetRectEmpty(d);return 0;}return 1;}

__attribute__((dllexport)) BOOL UnionRect(U32_BATCH_RECT *d,const U32_BATCH_RECT *a,const U32_BATCH_RECT *b){if(!d||!a||!b)return 0;if(IsRectEmpty(a)){*d=*b;return !IsRectEmpty(b);}if(IsRectEmpty(b)){*d=*a;return 1;}d->left=a->left<b->left?a->left:b->left;d->top=a->top<b->top?a->top:b->top;d->right=a->right>b->right?a->right:b->right;d->bottom=a->bottom>b->bottom?a->bottom:b->bottom;return 1;}

__attribute__((dllexport)) BOOL SubtractRect(U32_BATCH_RECT *d,const U32_BATCH_RECT *a,const U32_BATCH_RECT *b){if(!d||!a||!b)return 0;U32_BATCH_RECT i;if(!IntersectRect(&i,a,b)){*d=*a;return !IsRectEmpty(a);}if(i.left<=a->left&&i.right>=a->right&&i.top<=a->top&&i.bottom>=a->bottom){SetRectEmpty(d);return 0;}*d=*a;if(i.top<=a->top&&i.bottom<a->bottom)d->top=i.bottom;else if(i.bottom>=a->bottom&&i.top>a->top)d->bottom=i.top;else if(i.left<=a->left&&i.right<a->right)d->left=i.right;else if(i.right>=a->right&&i.left>a->left)d->right=i.left;return !IsRectEmpty(d);}

__attribute__((dllexport)) UINT EnumClipboardFormats(UINT previous){if(!u32_clipboard_format)return 0;if(previous==0)return u32_clipboard_format;return 0;}