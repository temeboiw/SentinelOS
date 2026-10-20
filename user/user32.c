
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

enum {
    SYS_Exit=0, SYS_Write=1, SYS_MapFb=8, SYS_GetInput=10,
    SYS_Shutdown=11, SYS_WmReg=15, SYS_WmMin=16,
    SYS_WmUnreg=17, SYS_WmQuery=18, SYS_WmRestore=19,
    SYS_WmRect=20, SYS_CreateProcess=4
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

typedef struct { u64 va; u32 w, h, pitch; } FBInfo;
typedef struct { int type, x, y, btn; char key; } InputEvent;
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
static u32 *g_front, g_pitch, g_fw, g_fh;
static int g_mx = 640, g_my = 400;
static int g_menu, g_drag, g_drag_ox, g_drag_oy;
static int g_search_focus;
static char g_search_text[32];
static int g_search_len;
static u32 cur_under[16][16];
static int cur_on, cur_ox, cur_oy;
static WINDOW g_wnd;
static u32 u32_wnd_style, u32_wnd_exstyle, u32_wnd_id;
static void *u32_wnd_proc, *u32_wnd_userdata, *u32_wnd_menu, *u32_wnd_parent;
static unsigned long long u32_context_help_id;
static long u32_class_style; static void *u32_class_proc;
static long u32_class_extra[2];
static int u32_scroll_min[2],u32_scroll_max[2],u32_scroll_page[2],u32_scroll_pos[2],u32_scroll_visible[2];
static unsigned int u32_double_click_time=500;
static int u32_caret_x,u32_caret_y,u32_caret_w=1,u32_caret_h=16,u32_caret_created,u32_caret_visible=1;
static unsigned char u32_key_state[256]; static unsigned int u32_window_affinity;
typedef struct { void *parent; unsigned int id,value; int checked,used; } U32_DLGCTRL;
static U32_DLGCTRL u32_dlgctrl[32];
static U32_DLGCTRL *u32_dlgctrl_get(void *parent,unsigned int id,int create){int free_slot=-1;for(int i=0;i<32;i++){if(u32_dlgctrl[i].used&&u32_dlgctrl[i].parent==parent&&u32_dlgctrl[i].id==id)return &u32_dlgctrl[i];if(!u32_dlgctrl[i].used&&free_slot<0)free_slot=i;}if(create&&free_slot>=0){U32_DLGCTRL *c=&u32_dlgctrl[free_slot];c->used=1;c->parent=parent;c->id=id;c->value=0;c->checked=0;return c;}return 0;}
static unsigned int u32_caret_blink=530;
static int u32_clip_left,u32_clip_top,u32_clip_right,u32_clip_bottom,u32_clip_active;
static struct {void *hwnd;char name[48];void *data;int used;} u32_props[32];
static int u32_clipboard_open; static unsigned int u32_clipboard_format,u32_clipboard_seq; static void *u32_clipboard_data,*u32_clipboard_owner;
static char g_lines[40][80];
static int g_nlines, g_tb_n, g_tb_pids[8];

#define COL_BG       0xFF0D1117
#define COL_PANEL    0xFF161B22
#define COL_TITLE    0xFF21262D
#define COL_ACCENT   0xFF58A6FF
#define COL_ACCENT2  0xFF3FB950
#define COL_WARN     0xFFD29922
#define COL_DANGER   0xFFF85149
#define COL_TEXT     0xFFF0F6FC
#define COL_MUTED    0xFF8B949E
#define COL_TASK     0xE0161B22
#define COL_TASKLINE 0xFF30363D
#define COL_WHITE    0xFFFFFFFF
#define COL_ICON     0xFF1F6FEB

#include "font8x8.h"

static void putpx(int x, int y, u32 col) {
    if (!g_front || x < 0 || y < 0 ||
        (u32)x >= g_fw || (u32)y >= g_fh)
        return;

    g_front[y * g_pitch + x] = col;
}

static void fill(int x, int y, int w, int h, u32 col) {
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++)
            putpx(xx, yy, col);
}

static void panel(int x, int y, int w, int h, u32 f, u32 b) {
    fill(x, y, w, h, f);
    fill(x, y, w, 1, b);
    fill(x, y + h - 1, w, 1, b);
    fill(x, y, 1, h, b);
    fill(x + w - 1, y, 1, h, b);
}

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
    for (; *s; s++) {
        draw_char(x, y, *s, col);
        x += 8;
    }
}

static void cursor_restore(void) {
    if (!cur_on || !g_front) return;

    for (int j = 0; j < 16; j++) {
        int yy = cur_oy + j;
        if (yy < 0 || (u32)yy >= g_fh) continue;

        for (int i = 0; i < 16; i++) {
            int xx = cur_ox + i;
            if (xx < 0 || (u32)xx >= g_fw) continue;

            g_front[yy * g_pitch + xx] = cur_under[j][i];
        }
    }

    cur_on = 0;
}

static void cursor_save_draw(int x, int y) {
    /* Classic 16x16 arrow: black outline, white body, blue inner highlight. */
    static const char shape[16][17] = {
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
    if (!g_front || !g_pitch) return;

    cursor_restore();
    cur_ox = x;
    cur_oy = y;

    for (int j = 0; j < 16; j++) {
        for (int i = 0; i < 16; i++) {
            int yy = y + j, xx = x + i;
            char p = shape[j][i];
            if (yy < 0 || (u32)yy >= g_fh ||
                xx < 0 || (u32)xx >= g_fw) {
                cur_under[j][i] = COL_BG;
                continue;
            }
            cur_under[j][i] = g_front[yy * g_pitch + xx];
            if (p == '#') g_front[yy * g_pitch + xx] = 0xFF000000;
            else if (p == 'w') g_front[yy * g_pitch + xx] = COL_WHITE;
            else if (p == '+') g_front[yy * g_pitch + xx] = COL_ACCENT;
        }
    }

    cur_on = 1;
}

static void present_cursor_only(void) {
    cursor_save_draw(g_mx, g_my);
}

static void ensure_fb(void) {
    if (!sy3(SYS_MapFb, (u64)&g_fb, 0, 0) || !g_fb.va)
        return;

    g_front = (u32 *)g_fb.va;
    g_fw = g_fb.w;
    g_fh = g_fb.h;
    g_pitch = g_fb.pitch / 4;
}

static int hit(int x, int y, int x0, int y0, int x1, int y1) {    return x >= x0 && x < x1 && y >= y0 && y < y1;
}

static int search_is_cmd(void) {
    const char *a = g_search_text;
    const char *b = "cmd";

    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;

        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';

        if (ca != cb) return 0;
    }

    return *a == 0 && *b == 0;
}

static void submit_search(void) {
    g_search_text[g_search_len] = 0;

    if (search_is_cmd()) {
        static char cmd_name[] = "cmd.exe";

        g_menu = 0;
        g_search_focus = 0;
        /*
         * SYS_CreateProcess is syscall 4 in this OS.
         * The process must also be loaded by the bootloader.
         */
        u64 result = sy3(
            SYS_CreateProcess,
            (u64)cmd_name, 0, 0
        );

        if (!result || result == (u64)-1) {
            static const char error[] = "Start menu: failed to launch cmd.exe\n";
            sy3(SYS_Write, 1, (u64)error,
                sizeof(error) - 1);
        }
    } else if (g_search_len > 0) {
        static const char message[] =
            "Start menu: only cmd is supported right now\n";
        sy3(SYS_Write, 1, (u64)message,
            sizeof(message) - 1);
    }

    g_search_focus = 0;
}

/*
 * Draw the taskbar and Start menu.
 */
static void draw_taskbar(void) {
    int h = (int)g_fh;
    int w = (int)g_fw;
    int ty = h - 44;

    fill(0, ty, w, 44, COL_TASK);
    fill(0, ty, w, 1, COL_TASKLINE);

    panel(8, ty + 6, 70, 32, COL_ACCENT, COL_ACCENT);
    draw_text(20, ty + 16, "START", COL_WHITE);

    g_tb_n = 0;
    int ax = 90;

    for (int i = 0; i < 8; i++) {
        char name[40];
        int minv = 0;

        u64 pid = sy3(
            SYS_WmQuery, (u64)i, (u64)name, (u64)&minv
        );

        if (!pid) break;

        g_tb_pids[g_tb_n++] = (int)pid;

        int bw = 110;
        panel(ax, ty + 6, bw, 32,
              minv ? COL_PANEL : COL_TITLE, COL_TASKLINE);

        if (!minv)
            fill(ax, ty + 34, bw, 2, COL_ACCENT);

        draw_text(ax + 8, ty + 16,
                  name[0] ? name : "APP", COL_TEXT);

        ax += bw + 6;
        if (ax > w - 80) break;
    }

    draw_text(w - 48, ty + 16, "C:", COL_MUTED);

    if (g_menu) {
        int mw = 190;
        int mh = 142;
        int mx = 8;
        int my = ty - mh - 4;

        panel(mx, my, mw, mh, COL_PANEL, COL_TASKLINE);
        fill(mx, my, mw, 3, COL_ACCENT);

        /* Search field inside Start menu. */
        panel(
            mx + 10, my + 12, mw - 20, 26,
            COL_BG,
            g_search_focus ? COL_ACCENT : COL_TASKLINE
        );

        if (g_search_len > 0) {
            g_search_text[g_search_len] = 0;
            draw_text(
                mx + 17, my + 21,
                g_search_text, COL_TEXT
            );
        } else {
            draw_text(
                mx + 17, my + 21,
                "SEARCH APPS", COL_MUTED
            );
        }

        draw_text(mx + 14, my + 52, "SHUTDOWN", COL_DANGER);
        draw_text(mx + 14, my + 76, "RESTART", COL_WARN);
        draw_text(mx + 14, my + 100, "CANCEL", COL_MUTED);
    }
}

static void compose(void) {
    ensure_fb();
    if (!g_front) return;

    if (g_wnd.visible) {
        for (int i = 0; i < 8; i++) {
            char name[40];
            int minv = 0;

            u64 pid = sy3(
                SYS_WmQuery, (u64)i, (u64)name, (u64)&minv
            );

            if (!pid) break;

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

    cursor_restore();
    fill(0, 0, (int)g_fw, (int)g_fh, COL_BG);

    draw_text(16, 14, "SENTINEL OS", COL_ACCENT);
    draw_text(16, 28, "v1", COL_MUTED);

    {
        int ix = 36, iy = 72, s = 48;

        fill(ix + 6, iy + 6, s, s, 0xFF0A1628);
        fill(ix, iy, s, s, COL_ICON);
        fill(ix + 4, iy + 4, s - 8, 10, COL_ACCENT);
        fill(ix + 10, iy + 20, s - 20, 6, COL_WHITE);
        fill(ix + 10, iy + 30, s - 20, 6, COL_WHITE);
        fill(ix + 10, iy + 40, s - 24, 6, COL_MUTED);

        draw_text(ix - 4, iy + s + 8, "File Explorer", COL_TEXT);
    }

    if (g_wnd.visible && !g_wnd.minimized) {
        int x = g_wnd.x, y = g_wnd.y;
        int w = g_wnd.w, h = g_wnd.h;

        panel(x, y, w, h, COL_PANEL, COL_TASKLINE);
        fill(x, y, w, 32, COL_TITLE);
        fill(x, y, w, 2, COL_ACCENT);

        draw_text(x + 12, y + 12, g_wnd.title, COL_TEXT);

        int by = y + 4;
        int bx = x + w - 56;

        fill(bx, by, 24, 24, COL_PANEL);
        fill(bx, by, 24, 1, COL_TASKLINE);
        fill(bx, by + 23, 24, 1, COL_TASKLINE);
        fill(bx, by, 1, 24, COL_TASKLINE);
        fill(bx + 23, by, 1, 24, COL_TASKLINE);
        fill(bx + 6, by + 11, 12, 3, COL_TEXT);

        bx = x + w - 28;
        fill(bx, by, 24, 24, COL_DANGER);
        draw_text(bx + 8, by + 8, "X", COL_WHITE);

        fill(x + 4, y + 36, w - 8, h - 42, COL_BG);

        for (int i = 0; i < g_nlines; i++) {
            const char *s = g_lines[i];
            int tx = x + 14;
            int ty = y + 44 + i * 16;
            u32 ic = 0;

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

    draw_taskbar();
    cursor_save_draw(g_mx, g_my);
}

static void on_click(int x, int y) {
    int h = (int)g_fh;
    int ty = h - 44;

    if (g_wnd.visible && !g_wnd.minimized) {
        int wx = g_wnd.x, wy = g_wnd.y;
        int ww = g_wnd.w, by = wy + 4;
        int bx = wx + ww - 28;

        if (hit(x, y, bx, by, bx + 24, by + 24)) {
            sy3(SYS_WmUnreg, 0, 0, 0);
            sy3(SYS_Exit, 0, 0, 0);
            return;
        }

        bx = wx + ww - 56;

        if (hit(x, y, bx, by, bx + 24, by + 24)) {
            g_wnd.minimized = 1;
            sy3(SYS_WmMin, 1, 0, 0);
            compose();
            return;
        }

        if (hit(x, y, wx, wy, wx + ww - 60, wy + 32)) {
            g_drag = 1;
            g_drag_ox = x - wx;
            g_drag_oy = y - wy;
            return;
        }
    }

    if (y >= ty) {
        int ax = 90;

        for (int i = 0; i < g_tb_n; i++) {
            int bw = 110;

            if (hit(x, y, ax, ty + 6, ax + bw, ty + 38)) {
                sy3(SYS_WmRestore, (u64)g_tb_pids[i], 0, 0);

                if (g_wnd.visible) {
                    g_wnd.minimized = 0;
                    sy3(SYS_WmMin, 0, 0, 0);
                }

                compose();
                return;
            }

            ax += bw + 6;
        }

        if (hit(x, y, 8, ty + 6, 78, ty + 38)) {
            g_menu = !g_menu;
            g_search_focus = 0;

            if (g_menu) {
                g_search_len = 0;
                g_search_text[0] = 0;
            }

            compose();
            return;
        }
    }

    if (g_menu) {
        int mw = 190, mh = 142;
        int mx = 8, my = ty - mh - 4;

        /* Clicking the search box focuses it. */
        if (hit(x, y,
                mx + 10, my + 12,
                mx + mw - 10, my + 38)) {
            g_search_focus = 1;
            compose();
            return;
        }

        if (hit(x, y, mx, my + 42, mx + mw, my + 68)) {
            sy3(SYS_Shutdown, 0, 0, 0);
            return;
        }

        if (hit(x, y, mx, my + 66, mx + mw, my + 92)) {
            sy3(SYS_Shutdown, 1, 0, 0);
            return;
        }

        if (hit(x, y, mx, my + 92, mx + mw, my + 118)) {
            g_menu = 0;
            g_search_focus = 0;
            compose();
            return;
        }

        /* Click elsewhere closes the menu. */
        g_menu = 0;
        g_search_focus = 0;
        compose();
        return;
    }

    g_menu = 0;
}

__attribute__((dllexport))
void *CreateWindowExA(
    u32 ex, LPCSTR cls, LPCSTR title, u32 style,
    int x, int y, int w, int h,
    void *parent, void *menu, void *inst, void *param
) {
    (void)cls; (void)inst; (void)param;
    u32_wnd_style=style; u32_wnd_exstyle=ex; u32_wnd_id=0;
    u32_wnd_proc=0; u32_wnd_userdata=0; u32_wnd_menu=menu; u32_wnd_parent=parent;
    u32_context_help_id=0;

    ensure_fb();

    g_wnd.x = x;
    g_wnd.y = y;
    g_wnd.w = (w < 80 ? 640 : w);
    g_wnd.h = (h < 80 ? 400 : h);

    if (w <= 2) {
        g_wnd.w = 1;
        g_wnd.h = 1;
        g_wnd.visible = 0;
    } else {
        g_wnd.visible = 1;
    }

    int i = 0;

    if (title)
        for (; title[i] && i < 63; i++)
            g_wnd.title[i] = title[i];

    g_wnd.title[i] = 0;
    g_wnd.minimized = 0;
    g_nlines = 0;
    g_menu = 0;
    g_search_focus = 0;
    g_search_len = 0;
    g_drag = 0;

    if (g_wnd.visible) {
        sy3(SYS_WmReg, 0, (u64)g_wnd.title, 0);

        int rect[4] = { g_wnd.x, g_wnd.y, g_wnd.w, g_wnd.h };
        sy3(SYS_WmRect, (u64)rect, 0, 0);
    }

    compose();
    return (void *)1;
}

__attribute__((dllexport))
BOOL ShowWindow(void *hwnd, int cmd) {
    (void)hwnd;
    (void)cmd;

    if (g_wnd.w > 2) {
        g_wnd.visible = 1;
        g_wnd.minimized = 0;
        sy3(SYS_WmMin, 0, 0, 0);
    }

    compose();
    return 1;
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
    (void)x;

    if (len < 0) {
        len = 0;
        while (str[len]) len++;
    }

    if (y >= 0 && y < 40) {
        int n = len < 79 ? len : 79;

        for (int i = 0; i < n; i++)
            g_lines[y][i] = str[i];

        g_lines[y][n] = 0;

        if (y >= g_nlines)
            g_nlines = y + 1;    }

    return 1;
}

__attribute__((dllexport))
BOOL InvalidateRect(void *hwnd, void *rc, BOOL erase) {
    (void)hwnd;
    (void)rc;
    (void)erase;

    compose();
    return 1;
}

__attribute__((dllexport))
BOOL GetMessageA(MSG *msg, void *hwnd, u32 min, u32 max) {
    (void)hwnd;
    (void)min;
    (void)max;

    if (!g_front) ensure_fb();

    for (;;) {
        InputEvent ev = {0};

        if (sy3(SYS_GetInput, (u64)&ev, 0, 0)) {
            if (ev.type == 1) {
                char key = ev.key;

                /*
                 * Handle keyboard input while the Start menu
                 * search box has focus.
                 */
                if (g_menu && g_search_focus) {
                    if (key == '\b') {
                        if (g_search_len > 0) {
                            g_search_len--;
                            g_search_text[g_search_len] = 0;
                        }

                        compose();
                        continue;
                    }

                    if (key == '\n' || key == '\r') {
                        submit_search();
                        compose();
                        continue;
                    }

                    if (key >= 32 && key <= 126 &&
                        g_search_len < 22) {
                        g_search_text[g_search_len++] = key;
                        g_search_text[g_search_len] = 0;
                        compose();
                    }

                    continue;
                }

                msg->message = 0x0100;
                msg->wParam = (u64)(unsigned char)ev.key;
                msg->pt_x = g_mx;
                msg->pt_y = g_my;
                return 1;
            }

            if (ev.type == 2 || ev.type == 3) {
                g_mx = ev.x;
                g_my = ev.y;

                msg->message = (ev.type == 3) ? 0x0201 : 0x0200;
                msg->pt_x = ev.x;
                msg->pt_y = ev.y;

                if (g_drag && (ev.btn & 1)) {
                    g_wnd.x = ev.x - g_drag_ox;
                    g_wnd.y = ev.y - g_drag_oy;

                    if (g_wnd.x < 0) g_wnd.x = 0;
                    if (g_wnd.y < 0) g_wnd.y = 0;

                    int rect[4] = {
                        g_wnd.x, g_wnd.y, g_wnd.w, g_wnd.h
                    };

                    sy3(SYS_WmRect, (u64)rect, 0, 0);
                    compose();
                    return 1;
                }

                if (ev.type == 3) {
                    g_drag = 0;
                    on_click(ev.x, ev.y);
                } else {
                    present_cursor_only();
                }

                return 1;
            }
        }

        __asm__ volatile("pause");
    }
}

__attribute__((dllexport))
BOOL TranslateMessage(const MSG *m) {
    (void)m;
    return 1;
}

__attribute__((dllexport))
i64 DispatchMessageA(const MSG *m) {
    (void)m;
    return 0;
}

__attribute__((dllexport))
int MessageBoxA(void *hwnd, LPCSTR text, LPCSTR caption, u32 type) {
    (void)hwnd;
    (void)type;

    sy3(SYS_Write, 1, (u64)"[MessageBox] ", 13);

    if (caption) {
        u64 n = 0;
        while (caption[n]) n++;

        sy3(SYS_Write, 1, (u64)caption, n);
        sy3(SYS_Write, 1, (u64)": ", 2);
    }

    if (text) {
        u64 n = 0;
        while (text[n]) n++;

        sy3(SYS_Write, 1, (u64)text, n);
    }

    sy3(SYS_Write, 1, (u64)"\n", 1);
    return 1;
}

__attribute__((dllexport))
BOOL DestroyWindow(void *hwnd) {
    (void)hwnd;

    sy3(SYS_WmUnreg, 0, 0, 0);
    g_wnd.visible = 0;
    compose();

    return 1;
}

__attribute__((dllexport))
BOOL PostQuitMessage(int code) {
    (void)code;
    return 1;
}

__attribute__((dllexport))
BOOL ExitWindowsEx(u32 flags, u32 reason) {
    (void)reason;
    sy3(SYS_Shutdown, (flags & 2) ? 1 : 0, 0, 0);
    return 1;
}

__attribute__((dllexport))
void DrawTaskbar(void) {
    compose();
}

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
typedef struct { int left, top, right, bottom; } U32_RECT;
typedef struct { unsigned int cbSize,fMask; int nMin,nMax; unsigned int nPage; int nPos,nTrackPos; } U32_SCROLLINFO;
typedef struct { int x, y; } U32_POINT;
typedef struct { unsigned int length, flags, showCmd; U32_POINT ptMinPosition, ptMaxPosition; U32_RECT rcNormalPosition; } U32_WINDOWPLACEMENT;
typedef struct { u32 cbSize; U32_RECT rcWindow, rcClient; u32 dwStyle, dwExStyle, dwWindowStatus, cxWindowBorders, cyWindowBorders; unsigned short atomWindowType, wCreatorVersion; } U32_WINDOWINFO;
typedef struct { unsigned int flags; unsigned long long id; void *submenu; void *bmp_checked,*bmp_unchecked; char text[80]; } U32_MENUITEM;
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
    U32_MENUITEM *it=&m->items[at]; it->flags=flags; it->id=id; it->submenu=(flags&0x0010u)?(void *)id:0;
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
static int u32_show_cursor_count;
static void *u32_capture;
static void *u32_focus;
static void *u32_active;

__attribute__((dllexport)) BOOL GetClientRect(void *hwnd, U32_RECT *r) {
    (void)hwnd; if (!r || !g_wnd.visible) return 0;
    r->left=0; r->top=0; r->right=g_wnd.w; r->bottom=g_wnd.h-32; return 1;
}
__attribute__((dllexport)) BOOL MoveWindow(void *hwnd, int x, int y, int w, int h, BOOL repaint) {
    (void)hwnd; if (!g_wnd.visible || w<1 || h<1) return 0;
    g_wnd.x=x; g_wnd.y=y; g_wnd.w=w; g_wnd.h=h;
    { int r[4]={x,y,w,h}; sy3(SYS_WmRect,(u64)r,0,0); }
    if (repaint) compose(); return 1;
}
__attribute__((dllexport)) BOOL SetWindowPos(void *hwnd, void *after, int x, int y, int cx, int cy, unsigned int flags) {
    (void)after; if (flags & 0x0002u) { x=g_wnd.x; y=g_wnd.y; }
    if (flags & 0x0001u) { cx=g_wnd.w; cy=g_wnd.h; }
    return MoveWindow(hwnd,x,y,cx,cy,!(flags&0x0008u));
}
__attribute__((dllexport)) BOOL GetWindowRect(void *hwnd, U32_RECT *r) {
    (void)hwnd; if (!r || !g_wnd.visible) return 0;
    r->left=g_wnd.x; r->top=g_wnd.y; r->right=g_wnd.x+g_wnd.w; r->bottom=g_wnd.y+g_wnd.h; return 1;
}
__attribute__((dllexport)) BOOL IsWindow(void *hwnd) { return hwnd==(void *)1 && g_wnd.visible; }
__attribute__((dllexport)) BOOL IsWindowVisible(void *hwnd) { return IsWindow(hwnd) && !g_wnd.minimized; }
__attribute__((dllexport)) BOOL EnableWindow(void *hwnd, BOOL enable) { (void)hwnd; (void)enable; return 1; }
__attribute__((dllexport)) BOOL IsIconic(void *hwnd) { (void)hwnd; return g_wnd.visible && g_wnd.minimized; }
__attribute__((dllexport)) void *GetForegroundWindow(void) { return g_wnd.visible && !g_wnd.minimized ? (void *)1 : (void *)0; }
__attribute__((dllexport)) BOOL SetForegroundWindow(void *hwnd) { if (!IsWindow(hwnd)) return 0; u32_active=hwnd; return 1; }
__attribute__((dllexport)) void *GetActiveWindow(void) { return u32_active ? u32_active : GetForegroundWindow(); }
__attribute__((dllexport)) void *SetActiveWindow(void *hwnd) { void *old=GetActiveWindow(); u32_active=hwnd; return old; }
__attribute__((dllexport)) int GetWindowTextA(void *hwnd, LPSTR out, int cap) {
    int i=0; (void)hwnd; if (!out || cap<=0 || !g_wnd.visible) return 0;
    while (g_wnd.title[i] && i+1<cap) { out[i]=g_wnd.title[i]; i++; } out[i]=0; return i;
}
__attribute__((dllexport)) BOOL SetWindowTextA(void *hwnd, LPCSTR title) {
    int i=0; (void)hwnd; if (!title || !g_wnd.visible) return 0;
    while (title[i] && i<63) { g_wnd.title[i]=title[i]; i++; } g_wnd.title[i]=0; compose(); return 1;
}
__attribute__((dllexport)) int GetClassNameA(void *hwnd, LPSTR out, int cap) {
    const char *s="SentinelWindow"; int i=0; (void)hwnd;
    if (!out || cap<=0) return 0;
    while (s[i] && i+1<cap) { out[i]=s[i]; i++; } out[i]=0; return i;
}
__attribute__((dllexport)) unsigned short RegisterClassA(const U32_WNDCLASSA *wc) {
    (void)wc; return 1;
}
__attribute__((dllexport)) BOOL UnregisterClassA(LPCSTR name, void *instance) {
    (void)name; (void)instance; return 1;
}
__attribute__((dllexport)) U32_LRESULT DefWindowProcA(void *hwnd, U32_UINT msg, U32_WPARAM wp, U32_LPARAM lp) {
    (void)hwnd; (void)msg; (void)wp; (void)lp; return 0;
}
__attribute__((dllexport)) U32_LRESULT SendMessageA(void *hwnd, U32_UINT msg, U32_WPARAM wp, U32_LPARAM lp) {
    if (msg==0x000Cu) return SetWindowTextA(hwnd,(LPCSTR)lp);
    if (msg==0x000Du) return GetWindowTextA(hwnd,(LPSTR)lp,(int)wp);
    if (msg==0x0010u) { g_wnd.visible=0; sy3(SYS_WmUnreg,0,0,0); compose(); return 1; }
    if (msg==0x000Fu) { compose(); return 1; }
    return DefWindowProcA(hwnd,msg,wp,lp);
}
__attribute__((dllexport)) BOOL PostMessageA(void *hwnd, U32_UINT msg, U32_WPARAM wp, U32_LPARAM lp) {
    (void)hwnd; (void)msg; (void)wp; (void)lp; return 0;
}
__attribute__((dllexport)) BOOL PeekMessageA(MSG *msg, void *hwnd, U32_UINT min, U32_UINT max, U32_UINT remove) {
    (void)msg; (void)hwnd; (void)min; (void)max; (void)remove; return 0;
}
__attribute__((dllexport)) short GetKeyState(int key) { (void)key; return 0; }
__attribute__((dllexport)) short GetAsyncKeyState(int key) { (void)key; return 0; }
__attribute__((dllexport)) BOOL GetCursorPos(U32_POINT *p) {
    if (!p) return 0; p->x=g_mx; p->y=g_my; return 1;
}
__attribute__((dllexport)) BOOL SetCursorPos(int x, int y) {
    g_mx=x; g_my=y; compose(); return 1;
}
__attribute__((dllexport)) int ShowCursor(BOOL show) {
    if (show) u32_show_cursor_count++; else u32_show_cursor_count--;
    compose(); return u32_show_cursor_count;
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
    (void)hwnd; if (!ps) return (void *)0; ps->hdc=GetDC(hwnd); ps->erase=0;
    ps->paint.left=0; ps->paint.top=0; ps->paint.right=(int)g_fw; ps->paint.bottom=(int)g_fh; return ps->hdc;
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
    (void)hwnd; (void)ms; (void)callback; return id ? id : 1;
}
__attribute__((dllexport)) BOOL KillTimer(void *hwnd, unsigned long long id) { (void)hwnd; (void)id; return 1; }
__attribute__((dllexport)) void *GetDlgItem(void *dlg, int id) { (void)dlg; (void)id; return (void *)0; }
__attribute__((dllexport)) BOOL SetDlgItemTextA(void *dlg, int id, LPCSTR text) { (void)dlg; (void)id; (void)text; return 0; }
__attribute__((dllexport)) unsigned int GetDlgItemTextA(void *dlg, int id, LPSTR out, int cap) {
    (void)dlg; (void)id; if (out && cap>0) out[0]=0; return 0;
}
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
    (void)menu; (void)flags; (void)x; (void)y; (void)reserved; (void)hwnd; (void)rect; return 0;
}
__attribute__((dllexport)) void *LoadCursorA(void *instance, LPCSTR name) { (void)instance; (void)name; return (void *)1; }
__attribute__((dllexport)) void *LoadIconA(void *instance, LPCSTR name) { (void)instance; (void)name; return (void *)1; }
__attribute__((dllexport)) void *LoadImageA(void *instance, LPCSTR name, unsigned int type, int cx, int cy, unsigned int flags) {
    (void)instance; (void)name; (void)type; (void)cx; (void)cy; (void)flags; return (void *)0;
}
__attribute__((dllexport)) BOOL DrawMenuBar(void *hwnd) { (void)hwnd; compose(); return 1; }
__attribute__((dllexport)) unsigned long GetMessagePos(void) { return ((unsigned long)(g_my & 0xFFFF)<<16) | (unsigned long)(g_mx & 0xFFFF); }
__attribute__((dllexport)) long GetMessageTime(void) { return 0; }
__attribute__((dllexport)) unsigned int GetDoubleClickTime(void) { return 500; }
__attribute__((dllexport)) BOOL MessageBeep(unsigned int type) { (void)type; return 0; }
__attribute__((dllexport)) BOOL RedrawWindow(void *hwnd, const U32_RECT *update, void *region, unsigned int flags) {
    (void)hwnd; (void)update; (void)region; (void)flags; compose(); return 1;
}
__attribute__((dllexport)) BOOL ScrollWindow(void *hwnd, int dx, int dy, const U32_RECT *scroll, const U32_RECT *clip) {
    (void)hwnd; (void)dx; (void)dy; (void)scroll; (void)clip; compose(); return 1;
}
__attribute__((dllexport)) BOOL GetUpdateRect(void *hwnd, U32_RECT *r, BOOL erase) {
    (void)hwnd; (void)erase; if (r) { r->left=0; r->top=0; r->right=0; r->bottom=0; } return 0;
}
__attribute__((dllexport)) void *SetFocus(void *hwnd) { void *old=u32_focus; u32_focus=hwnd; return old; }
__attribute__((dllexport)) void *GetFocus(void) { return u32_focus; }


/* Compatibility export placeholders: signatures and semantics still need implementation. */
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
__attribute__((dllexport)) unsigned long long CreateDialogParamA(void) { return 0; }
__attribute__((dllexport)) unsigned long long CreateDialogParamW(void) { return 0; }
__attribute__((dllexport)) unsigned long long DialogBoxParamA(void) { return 0; }
__attribute__((dllexport)) unsigned long long DialogBoxParamW(void) { return 0; }
__attribute__((dllexport)) unsigned long long EndDialog(void) { return 0; }
__attribute__((dllexport)) unsigned long long IsDialogMessageA(void) { return 0; }
__attribute__((dllexport)) unsigned long long IsDialogMessageW(void) { return 0; }
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
    return u32_ansi_to_wide(out,cap,name) < n ? cap-1 : n;
}
__attribute__((dllexport)) unsigned long long LoadCursorW(void) { return 0; }
__attribute__((dllexport)) unsigned long long LoadIconW(void) { return 0; }
__attribute__((dllexport)) unsigned long long LoadImageW(void) { return 0; }
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
    if (!callback) return 0;
    (void)parent; (void)param;
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
__attribute__((dllexport)) BOOL SetWindowPlacement(void *hwnd, const U32_WINDOWPLACEMENT *p) {
    if (hwnd!=(void *)1 || !g_wnd.visible || !p || p->length<sizeof(U32_WINDOWPLACEMENT)) return 0;
    int w=p->rcNormalPosition.right-p->rcNormalPosition.left;
    int h=p->rcNormalPosition.bottom-p->rcNormalPosition.top;
    if (w>0 && h>0) MoveWindow(hwnd,p->rcNormalPosition.left,p->rcNormalPosition.top,w,h,0);
    if (p->showCmd==0) { g_wnd.visible=0; sy3(SYS_WmUnreg,0,0,0); }
    else if (p->showCmd==2 || p->showCmd==6 || p->showCmd==7) { g_wnd.minimized=1; sy3(SYS_WmMin,0,0,0); }
    else { g_wnd.visible=1; g_wnd.minimized=0; sy3(SYS_WmRestore,0,0,0); }
    compose(); return 1;
}
__attribute__((dllexport)) BOOL GetWindowPlacement(void *hwnd, U32_WINDOWPLACEMENT *p) {
    if (hwnd!=(void *)1 || !g_wnd.visible || !p || p->length<sizeof(U32_WINDOWPLACEMENT)) return 0;
    p->flags=0; p->showCmd=g_wnd.minimized ? 2u : 1u;
    p->ptMinPosition.x=0; p->ptMinPosition.y=0; p->ptMaxPosition.x=0; p->ptMaxPosition.y=0;
    p->rcNormalPosition.left=g_wnd.x; p->rcNormalPosition.top=g_wnd.y;
    p->rcNormalPosition.right=g_wnd.x+g_wnd.w; p->rcNormalPosition.bottom=g_wnd.y+g_wnd.h;
    return 1;
}
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
__attribute__((dllexport)) unsigned int CheckMenuItem(void *menu, unsigned int item, unsigned int check) { U32_MENUDATA *m=u32_menu_data(menu); int i=u32_menu_index(m,item,(check&0x0400u)!=0); if (i<0) return 0xFFFFFFFFu; unsigned int old=m->items[i].flags&0x8u; m->items[i].flags=(m->items[i].flags&~0x8u)|((check&0x8u)?0x8u:0); return old; }
__attribute__((dllexport)) unsigned int EnableMenuItem(void *menu, unsigned int item, unsigned int enable) { U32_MENUDATA *m=u32_menu_data(menu); int i=u32_menu_index(m,item,(enable&0x0400u)!=0); if (i<0) return 0xFFFFFFFFu; unsigned int old=m->items[i].flags&0x3u; m->items[i].flags=(m->items[i].flags&~0x3u)|(enable&0x3u); return old; }
__attribute__((dllexport)) BOOL DeleteMenu(void *menu, unsigned int item, unsigned int flags) { return u32_menu_remove(menu,item,flags,1); }
__attribute__((dllexport)) BOOL RemoveMenu(void *menu, unsigned int item, unsigned int flags) { return u32_menu_remove(menu,item,flags,0); }
__attribute__((dllexport)) BOOL InsertMenuA(void *menu, unsigned int position, unsigned int flags, unsigned long long id, LPCSTR text) { return u32_menu_insert(menu,position,flags,id,text); }
__attribute__((dllexport)) BOOL ModifyMenuA(void *menu, unsigned int item, unsigned int flags, unsigned long long id, LPCSTR text) { U32_MENUDATA *m=u32_menu_data(menu); int i=u32_menu_index(m,item,(flags&0x0400u)!=0); if (i<0) return 0; m->items[i].flags=flags; m->items[i].id=id; m->items[i].submenu=(flags&0x0010u)?(void *)id:0; u32_menu_text(m->items[i].text,sizeof(m->items[i].text),text); return 1; }
typedef struct { unsigned int cbSize,fMask,fType,fState,wID; void *hSubMenu,*hbmpChecked,*hbmpUnchecked; unsigned long long dwItemData; char *dwTypeData; unsigned int cch; void *hbmpItem; } U32_MENUITEMINFOA;
#define U32_MIIM_STATE 0x00000001u
#define U32_MIIM_ID 0x00000002u
#define U32_MIIM_SUBMENU 0x00000004u
#define U32_MIIM_DATA 0x00000020u
#define U32_MIIM_STRING 0x00000040u
#define U32_MIIM_FTYPE 0x00000100u
typedef struct { unsigned int cbSize,fMask,fType,fState,wID; void *hSubMenu,*hbmpChecked,*hbmpUnchecked; unsigned long long dwItemData; U32_WCHAR *dwTypeData; unsigned int cch; void *hbmpItem; } U32_MENUITEMINFOW;
__attribute__((dllexport)) BOOL SetMenuItemInfoA(void *menu,unsigned int item,BOOL bypos,const U32_MENUITEMINFOA *info){U32_MENUDATA *m=u32_menu_data(menu);int i=u32_menu_index(m,item,bypos!=0);if(i<0||!info||info->cbSize<sizeof(U32_MENUITEMINFOA))return 0;U32_MENUITEM *it=&m->items[i];if(info->fMask&U32_MIIM_ID)it->id=info->wID;if(info->fMask&U32_MIIM_SUBMENU)it->submenu=info->hSubMenu;if(info->fMask&U32_MIIM_STATE)it->flags=(it->flags&~0x103u)|(info->fState&0x103u);if(info->fMask&U32_MIIM_FTYPE)it->flags=(it->flags&~0x100u)|(info->fType&0x100u);if(info->fMask&U32_MIIM_DATA)it->id=info->dwItemData;if((info->fMask&U32_MIIM_STRING)&&info->dwTypeData)u32_menu_text(it->text,sizeof(it->text),info->dwTypeData);return 1;}
__attribute__((dllexport)) BOOL GetMenuItemInfoA(void *menu,unsigned int item,BOOL bypos,U32_MENUITEMINFOA *info){U32_MENUDATA *m=u32_menu_data(menu);int i=u32_menu_index(m,item,bypos!=0);if(i<0||!info||info->cbSize<sizeof(U32_MENUITEMINFOA))return 0;U32_MENUITEM *it=&m->items[i];if(info->fMask&U32_MIIM_ID)info->wID=(unsigned int)it->id;if(info->fMask&U32_MIIM_SUBMENU)info->hSubMenu=it->submenu;if(info->fMask&U32_MIIM_STATE)info->fState=it->flags&0x103u;if(info->fMask&U32_MIIM_FTYPE)info->fType=it->flags&0x100u;if(info->fMask&U32_MIIM_DATA)info->dwItemData=it->id;if(info->fMask&U32_MIIM_STRING){unsigned int n=0;while(it->text[n])n++;if(info->dwTypeData&&info->cch){unsigned int j=0;while(it->text[j]&&j+1<info->cch){info->dwTypeData[j]=it->text[j];j++;}info->dwTypeData[j]=0;}info->cch=n;}return 1;}
__attribute__((dllexport)) int GetMenuStringA(void *menu, unsigned int item, char *out, int cap, unsigned int flags) { U32_MENUDATA *m=u32_menu_data(menu); int i=u32_menu_index(m,item,(flags&0x0400u)!=0); if (i<0 || !out || cap<=0) return 0; int n=0; while (m->items[i].text[n] && n+1<cap) { out[n]=m->items[i].text[n]; n++; } out[n]=0; return n; }
__attribute__((dllexport)) BOOL SetMenuDefaultItem(void *menu, unsigned int item, unsigned int by_position) { U32_MENUDATA *m=u32_menu_data(menu); if (!m) return 0; int i=u32_menu_index(m,item,by_position!=0); if (i<0) return 0; m->default_pos=i; return 1; }
__attribute__((dllexport)) unsigned int GetMenuDefaultItem(void *menu, unsigned int by_position, unsigned int flags) { U32_MENUDATA *m=u32_menu_data(menu); (void)flags; if (!m || m->default_pos<0 || m->default_pos>=m->count) return 0xFFFFFFFFu; return by_position ? (unsigned int)m->default_pos : (unsigned int)m->items[m->default_pos].id; }
__attribute__((dllexport)) void *GetSystemMenu(void *hwnd, BOOL revert) { if (!IsWindow(hwnd)) return 0; if (revert) { if (u32_system_menu) DestroyMenu(u32_system_menu); u32_system_menu=0; } if (!u32_system_menu) u32_system_menu=CreateMenu(); return u32_system_menu; }
__attribute__((dllexport)) unsigned long long GetScrollInfo(void) { return 0; }


/* Additional Win32 entry points. These ABI placeholders are intentionally conservative;
   replace with OS-backed implementations as the corresponding subsystems mature. */
__attribute__((dllexport)) int SetScrollInfo(void *hwnd,int bar,const U32_SCROLLINFO *info,BOOL redraw){(void)redraw;if(!IsWindow(hwnd)||!info||(bar!=0&&bar!=1)||info->cbSize<sizeof(U32_SCROLLINFO))return 0;int i=bar==0?0:1,old=u32_scroll_pos[i];if(info->fMask&1u)u32_scroll_min[i]=info->nMin;if(info->fMask&2u)u32_scroll_max[i]=info->nMax;if(info->fMask&4u)u32_scroll_page[i]=(int)info->nPage;if(info->fMask&8u)u32_scroll_pos[i]=info->nPos;if(u32_scroll_pos[i]<u32_scroll_min[i])u32_scroll_pos[i]=u32_scroll_min[i];if(u32_scroll_pos[i]>u32_scroll_max[i])u32_scroll_pos[i]=u32_scroll_max[i];return old;}
__attribute__((dllexport)) BOOL ShowScrollBar(void *hwnd,int bar,BOOL show){if(!IsWindow(hwnd)||bar<0||bar>1)return 0;u32_scroll_visible[bar]=show!=0;return 1;}
__attribute__((dllexport)) BOOL SetScrollRange(void *hwnd,int bar,int min,int max,BOOL redraw){(void)redraw;if(!IsWindow(hwnd)||bar<0||bar>1||max<min)return 0;u32_scroll_min[bar]=min;u32_scroll_max[bar]=max;if(u32_scroll_pos[bar]<min)u32_scroll_pos[bar]=min;if(u32_scroll_pos[bar]>max)u32_scroll_pos[bar]=max;return 1;}
__attribute__((dllexport)) BOOL GetScrollRange(void *hwnd,int bar,int *min,int *max){if(!IsWindow(hwnd)||bar<0||bar>1||!min||!max)return 0;*min=u32_scroll_min[bar];*max=u32_scroll_max[bar];return 1;}
__attribute__((dllexport)) int GetScrollPos(void *hwnd,int bar){if(!IsWindow(hwnd)||bar<0||bar>1)return 0;return u32_scroll_pos[bar];}
__attribute__((dllexport)) int SetScrollPos(void *hwnd,int bar,int pos,BOOL redraw){(void)redraw;if(!IsWindow(hwnd)||bar<0||bar>1)return 0;int old=u32_scroll_pos[bar];if(pos<u32_scroll_min[bar])pos=u32_scroll_min[bar];if(pos>u32_scroll_max[bar])pos=u32_scroll_max[bar];u32_scroll_pos[bar]=pos;return old;}
__attribute__((dllexport)) int SetWindowRgn(void *hwnd,void *region,BOOL redraw){(void)hwnd;(void)region;(void)redraw;return 0;}
__attribute__((dllexport)) int GetWindowRgn(void *hwnd,void *region){(void)hwnd;(void)region;return 0;}
__attribute__((dllexport)) int MapWindowPoints(void *from,void *to,U32_POINT *pts,unsigned int count){if(!pts||count>256)return 0;int dx=0,dy=0;if(IsWindow(from)){dx+=g_wnd.x;dy+=g_wnd.y+24;}if(IsWindow(to)){dx-=g_wnd.x;dy-=g_wnd.y+24;}for(unsigned int i=0;i<count;i++){pts[i].x+=dx;pts[i].y+=dy;}return (int)(((unsigned short)dx)|((unsigned int)(unsigned short)dy<<16));}
__attribute__((dllexport)) BOOL ClientToScreen(void *hwnd,U32_POINT *pt){if(!IsWindow(hwnd)||!pt)return 0;pt->x+=g_wnd.x;pt->y+=g_wnd.y+24;return 1;}
__attribute__((dllexport)) BOOL ScreenToClient(void *hwnd,U32_POINT *pt){if(!IsWindow(hwnd)||!pt)return 0;pt->x-=g_wnd.x;pt->y-=g_wnd.y+24;return 1;}
__attribute__((dllexport)) void *WindowFromPoint(U32_POINT pt){return IsWindow((void *)1)&&hit(pt.x,pt.y,g_wnd.x,g_wnd.y,g_wnd.x+g_wnd.w,g_wnd.y+g_wnd.h)?(void *)1:0;}
__attribute__((dllexport)) void *ChildWindowFromPoint(void *parent,U32_POINT pt){if(!IsWindow(parent))return 0;return hit(pt.x,pt.y,0,0,g_wnd.w,g_wnd.h-32)?(void *)1:0;}
__attribute__((dllexport)) BOOL SetDoubleClickTime(unsigned int ms){if(ms==0)ms=500;if(ms<1||ms>5000)return 0;u32_double_click_time=ms;return 1;}
__attribute__((dllexport)) void *GetLastActivePopup(void *hwnd){return IsWindow(hwnd)?hwnd:0;}
__attribute__((dllexport)) int GetWindowTextLengthA(void *hwnd) { if (!IsWindow(hwnd)) return 0; int n=0; while (g_wnd.title[n]) n++; return n; }
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
__attribute__((dllexport)) int GetWindowRgnBox(void *hwnd,U32_RECT *rect){(void)hwnd;if(rect){rect->left=rect->top=rect->right=rect->bottom=0;}return 0;}
__attribute__((dllexport)) BOOL GetWindowDisplayAffinity(void *hwnd,unsigned int *affinity){if(!IsWindow(hwnd)||!affinity)return 0;*affinity=u32_window_affinity;return 1;}
__attribute__((dllexport)) BOOL SetWindowDisplayAffinity(void *hwnd,unsigned int affinity){if(!IsWindow(hwnd)||(affinity!=0&&affinity!=1&&affinity!=0x11u))return 0;u32_window_affinity=affinity;return 1;}
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
__attribute__((dllexport)) void *GetNextDlgTabItem(void *dlg,void *control,BOOL previous){(void)control;(void)previous;return (void *)0;}
__attribute__((dllexport)) void *GetNextDlgGroupItem(void *dlg,void *control,BOOL previous){(void)control;(void)previous;return (void *)0;}
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
__attribute__((dllexport)) BOOL GetLastInputInfo(U32_LASTINPUTINFO *info){if(!info||info->cbSize<sizeof(*info))return 0;info->dwTime=0;return 1;}
__attribute__((dllexport)) void *GetCursor(void){return (void *)1;}
typedef struct { unsigned int cbSize,flags; void *hCursor; U32_POINT ptScreenPos; } U32_CURSORINFO;
__attribute__((dllexport)) BOOL GetCursorInfo(U32_CURSORINFO *info){if(!info||info->cbSize<sizeof(U32_CURSORINFO))return 0;info->flags=1;info->hCursor=(void *)1;info->ptScreenPos.x=g_mx;info->ptScreenPos.y=g_my;return 1;}
__attribute__((dllexport)) void *SetCursor(void *cursor){static void *current=(void *)1;void *old=current;current=cursor;return old;}
__attribute__((dllexport)) unsigned long long GetMouseMovePointsEx(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
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
__attribute__((dllexport)) BOOL HiliteMenuItem(void *hwnd,void *menu,unsigned int item,unsigned int flags){(void)hwnd;U32_MENUDATA *m=u32_menu_data(menu);int i=u32_menu_index(m,item,(flags&0x0400u)!=0);if(i<0)return 0;if(flags&0x0080u)m->items[i].flags|=0x0080u;else m->items[i].flags&=~0x0080u;return 1;}
__attribute__((dllexport)) BOOL InsertMenuW(void *menu,unsigned int pos,unsigned int flags,unsigned long long id,const U32_WCHAR *text){char b[80];u32_wide_to_ansi(b,sizeof(b),text);return InsertMenuA(menu,pos,flags,id,b);}
__attribute__((dllexport)) BOOL AppendMenuW(void *menu,unsigned int flags,unsigned long long id,const U32_WCHAR *text){char b[80];u32_wide_to_ansi(b,sizeof(b),text);return AppendMenuA(menu,flags,id,b);}
__attribute__((dllexport)) BOOL ModifyMenuW(void *menu,unsigned int item,unsigned int flags,unsigned long long id,const U32_WCHAR *text){char b[80];u32_wide_to_ansi(b,sizeof(b),text);return ModifyMenuA(menu,item,flags,id,b);}
__attribute__((dllexport)) BOOL SetMenuItemInfoW(void *menu,unsigned int item,BOOL bypos,const U32_MENUITEMINFOW *info){if(!info)return 0;U32_MENUITEMINFOA a={0};a.cbSize=sizeof(a);a.fMask=info->fMask;a.fType=info->fType;a.fState=info->fState;a.wID=info->wID;a.hSubMenu=info->hSubMenu;a.hbmpChecked=info->hbmpChecked;a.hbmpUnchecked=info->hbmpUnchecked;a.dwItemData=info->dwItemData;a.cch=info->cch;a.hbmpItem=info->hbmpItem;char text[80];if(info->dwTypeData){unsigned int j=0;while(info->dwTypeData[j]&&j+1<sizeof(text)){text[j]=info->dwTypeData[j]<128?(char)info->dwTypeData[j]:'?';j++;}text[j]=0;a.dwTypeData=text;}return SetMenuItemInfoA(menu,item,bypos,&a);}
__attribute__((dllexport)) BOOL GetMenuItemInfoW(void *menu,unsigned int item,BOOL bypos,U32_MENUITEMINFOW *info){if(!info)return 0;char text[80];U32_MENUITEMINFOA a={0};a.cbSize=sizeof(a);a.fMask=info->fMask;a.cch=sizeof(text);a.dwTypeData=text;if(!GetMenuItemInfoA(menu,item,bypos,&a))return 0;info->fType=a.fType;info->fState=a.fState;info->wID=a.wID;info->hSubMenu=a.hSubMenu;info->dwItemData=a.dwItemData;if(info->dwTypeData&&info->cch){unsigned int j=0;while(text[j]&&j+1<info->cch){info->dwTypeData[j]=(unsigned char)text[j];j++;}info->dwTypeData[j]=0;}info->cch=a.cch;return 1;}
__attribute__((dllexport)) unsigned long long TrackPopupMenuEx(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) BOOL EndMenu(void){g_menu=0;return 1;}
__attribute__((dllexport)) unsigned long long GetMenuContextHelpId(void *menu){return u32_menu_data(menu)?u32_context_help_id:0;}
__attribute__((dllexport)) BOOL SetMenuContextHelpId(void *menu,unsigned long long id){if(!u32_menu_data(menu))return 0;u32_context_help_id=id;return 1;}
typedef struct { unsigned int cbSize,fMask,dwStyle,dwMaxHeight; void *hbrBack; unsigned int dwContextHelpID; unsigned long long dwMenuData; } U32_MENUINFO;
__attribute__((dllexport)) BOOL GetMenuInfo(void *menu,U32_MENUINFO *info){U32_MENUDATA *m=u32_menu_data(menu);if(!m||!info||info->cbSize<sizeof(U32_MENUINFO))return 0;if(info->fMask&1u)info->dwMaxHeight=m->max_height;if(info->fMask&2u)info->hbrBack=m->background;if(info->fMask&4u)info->dwContextHelpID=m->help_id;if(info->fMask&8u)info->dwMenuData=m->menu_data;if(info->fMask&0x10u)info->dwStyle=m->style;return 1;}
__attribute__((dllexport)) BOOL SetMenuInfo(void *menu,const U32_MENUINFO *info){U32_MENUDATA *m=u32_menu_data(menu);if(!m||!info||info->cbSize<sizeof(U32_MENUINFO))return 0;if(info->fMask&1u)m->max_height=info->dwMaxHeight;if(info->fMask&2u)m->background=info->hbrBack;if(info->fMask&4u)m->help_id=info->dwContextHelpID;if(info->fMask&8u)m->menu_data=info->dwMenuData;if(info->fMask&0x10u)m->style=info->dwStyle;return 1;}
__attribute__((dllexport)) BOOL GetMenuItemRect(void *hwnd,void *menu,unsigned int item,U32_RECT *rect){U32_MENUDATA *m=u32_menu_data(menu);int i=u32_menu_index(m,item,1);if(!IsWindow(hwnd)||i<0||!rect)return 0;rect->left=g_wnd.x+i*80;rect->top=g_wnd.y+24;rect->right=rect->left+80;rect->bottom=rect->top+24;return 1;}
__attribute__((dllexport)) BOOL SetMenuItemCount(void *menu,unsigned int count){U32_MENUDATA *m=u32_menu_data(menu);if(!m||count>16)return 0;while(m->count<(int)count){U32_MENUITEM *it=&m->items[m->count++];it->flags=0;it->id=0;it->submenu=0;it->text[0]=0;}m->count=(int)count;if(m->default_pos>=m->count)m->default_pos=-1;return 1;}
__attribute__((dllexport)) BOOL IsZoomed(void *hwnd){(void)hwnd;return 0;}
__attribute__((dllexport)) BOOL GetWindowPos(void *hwnd,U32_RECT *rect){return GetWindowRect(hwnd,rect);}
__attribute__((dllexport)) unsigned long long UnregisterClassW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) BOOL ValidateRect(void *hwnd,const U32_RECT *rect){(void)rect;return IsWindow(hwnd);}

__attribute__((dllexport)) long long GetMessageExtraInfo(void){return 0;}

__attribute__((dllexport)) long long SetMessageExtraInfo(long long value){static long long extra;long long old=extra;extra=value;return old;}

__attribute__((dllexport)) DWORD GetQueueStatus(DWORD flags){(void)flags;return 0;}

__attribute__((dllexport)) BOOL GetInputState(void){return 0;}

__attribute__((dllexport)) BOOL WaitMessage(void){InputEvent e;return (BOOL)sy3(SYS_GetInput,(u64)&e,0,0);}

__attribute__((dllexport)) BOOL ReplyMessage(long long result){(void)result;return 1;}

__attribute__((dllexport)) BOOL InSendMessage(void){return 0;}

__attribute__((dllexport)) DWORD InSendMessageEx(void *reserved){(void)reserved;return 0;}

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

__attribute__((dllexport)) unsigned int SetTextAlign(void *dc,unsigned int align){(void)dc;static unsigned int current=0;unsigned int old=current;current=align;return old;}

__attribute__((dllexport)) unsigned int GetTextAlign(void *dc){(void)dc;return 0;}

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

__attribute__((dllexport)) void *LoadBitmapA(void *instance,const char *name){(void)instance;return name?(void *)1:0;}

__attribute__((dllexport)) void *LoadBitmapW(void *instance,const unsigned short *name){(void)instance;return name?(void *)1:0;}

__attribute__((dllexport)) void *LoadMenuA(void *instance,const char *name){(void)instance;(void)name;return CreateMenu();}

__attribute__((dllexport)) void *LoadMenuW(void *instance,const unsigned short *name){(void)instance;(void)name;return CreateMenu();}

__attribute__((dllexport)) int TranslateAcceleratorA(void *hwnd,void *table,MSG *msg){(void)table;return IsWindow(hwnd)&&msg?0:0;}

__attribute__((dllexport)) int TranslateAcceleratorW(void *hwnd,void *table,MSG *msg){return TranslateAcceleratorA(hwnd,table,msg);}

__attribute__((dllexport)) BOOL GetClassInfoA(void *instance,const char *name,void *info){(void)instance;(void)name;if(!info)return 0;return 0;}

__attribute__((dllexport)) BOOL GetClassInfoW(void *instance,const unsigned short *name,void *info){(void)instance;(void)name;if(!info)return 0;return 0;}

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

__attribute__((dllexport)) void *GetWindowDpiAwarenessContext(void *hwnd){return IsWindow(hwnd)?(void *)1:0;}

__attribute__((dllexport)) BOOL AreDpiAwarenessContextsEqual(void *a,void *b){return a==b;}

__attribute__((dllexport)) int GetAwarenessFromDpiAwarenessContext(void *context){return context?1:-1;}

__attribute__((dllexport)) int GetProcessDpiAwareness(HANDLE process,int *awareness){if(!awareness||(process!=(void*)-1&&process!=(void*)1))return 0;*awareness=1;return 0;}

__attribute__((dllexport)) BOOL SetPropA(void *hwnd,const char *name,void *data){if(!IsWindow(hwnd)||!name||!name[0])return 0;int free_slot=-1;for(int i=0;i<32;i++){if(u32_props[i].used&&u32_props[i].hwnd==hwnd){int j=0;while(name[j]&&u32_props[i].name[j]==name[j])j++;if(!name[j]&&!u32_props[i].name[j]){u32_props[i].data=data;return 1;}}if(!u32_props[i].used&&free_slot<0)free_slot=i;}if(free_slot<0)return 0;u32_props[free_slot].used=1;u32_props[free_slot].hwnd=hwnd;u32_props[free_slot].data=data;int j=0;while(name[j]&&j<47){u32_props[free_slot].name[j]=name[j];j++;}u32_props[free_slot].name[j]=0;return 1;} 

__attribute__((dllexport)) void *GetPropA(void *hwnd,const char *name){if(!IsWindow(hwnd)||!name)return 0;for(int i=0;i<32;i++)if(u32_props[i].used&&u32_props[i].hwnd==hwnd){int j=0;while(name[j]&&u32_props[i].name[j]==name[j])j++;if(!name[j]&&!u32_props[i].name[j])return u32_props[i].data;}return 0;} 

__attribute__((dllexport)) void *RemovePropA(void *hwnd,const char *name){if(!IsWindow(hwnd)||!name)return 0;for(int i=0;i<32;i++)if(u32_props[i].used&&u32_props[i].hwnd==hwnd){int j=0;while(name[j]&&u32_props[i].name[j]==name[j])j++;if(!name[j]&&!u32_props[i].name[j]){void *d=u32_props[i].data;u32_props[i].used=0;return d;}}return 0;}

__attribute__((dllexport)) BOOL SetPropW(void *hwnd,const unsigned short *name,void *data){if(!name)return 0;char a[48];int i=0;while(name[i]&&i<47){a[i]=(char)name[i];i++;}a[i]=0;return SetPropA(hwnd,a,data);}

__attribute__((dllexport)) void *GetPropW(void *hwnd,const unsigned short *name){if(!name)return 0;char a[48];int i=0;while(name[i]&&i<47){a[i]=(char)name[i];i++;}a[i]=0;return GetPropA(hwnd,a);}

__attribute__((dllexport)) void *RemovePropW(void *hwnd,const unsigned short *name){if(!name)return 0;char a[48];int i=0;while(name[i]&&i<47){a[i]=(char)name[i];i++;}a[i]=0;return RemovePropA(hwnd,a);}

__attribute__((dllexport)) BOOL SetLayeredWindowAttributes(void *hwnd,DWORD color,unsigned char alpha,DWORD flags){if(!IsWindow(hwnd))return 0;u32_window_affinity=(u32_window_affinity&0xFFFF0000u)|((DWORD)alpha<<8)|(flags&0xFFu);return 1;}

__attribute__((dllexport)) BOOL GetLayeredWindowAttributes(void *hwnd,DWORD *color,unsigned char *alpha,DWORD *flags){if(!IsWindow(hwnd))return 0;if(color)*color=0;if(alpha)*alpha=(unsigned char)((u32_window_affinity>>8)&0xFF);if(flags)*flags=u32_window_affinity&0xFF;return 1;}

__attribute__((dllexport)) BOOL UpdateLayeredWindow(void *hwnd,void *dc,const U32_POINT *dst,const U32_POINT *size,void *src,const U32_POINT *srcpt,DWORD key,const void *blend,DWORD flags){(void)dc;(void)src;(void)srcpt;(void)key;(void)blend;(void)flags;if(!IsWindow(hwnd))return 0;if(dst){g_wnd.x=dst->x;g_wnd.y=dst->y;}if(size){g_wnd.w=size->x;g_wnd.h=size->y;}return 1;}

__attribute__((dllexport)) BOOL RegisterHotKey(void *hwnd,int id,unsigned int modifiers,unsigned int key){if(hwnd&&!IsWindow(hwnd))return 0;static struct {void *hwnd;int id;unsigned int mod,key;int used;} h[16];for(int i=0;i<16;i++){if(h[i].used&&h[i].hwnd==hwnd&&h[i].id==id)return 0;if(!h[i].used){h[i].used=1;h[i].hwnd=hwnd;h[i].id=id;h[i].mod=modifiers;h[i].key=key;return 1;}}return 0;}

__attribute__((dllexport)) BOOL UnregisterHotKey(void *hwnd,int id){(void)hwnd;(void)id;return 0;}

__attribute__((dllexport)) void *SetWindowsHookExA(int id,void *proc,void *module,DWORD thread){(void)id;(void)module;(void)thread;if(!proc)return 0;static void *hooks[16];for(int i=0;i<16;i++)if(!hooks[i]){hooks[i]=proc;return &hooks[i];}return 0;}

__attribute__((dllexport)) void *SetWindowsHookExW(int id,void *proc,void *module,DWORD thread){return SetWindowsHookExA(id,proc,module,thread);}

__attribute__((dllexport)) BOOL UnhookWindowsHookEx(void *hook){if(!hook)return 0;void **p=(void**)hook;if(*p==0)return 0;*p=0;return 1;}

__attribute__((dllexport)) long long CallNextHookEx(void *hook,int code,unsigned long long wp,long long lp){(void)hook;(void)code;(void)wp;(void)lp;return 0;}

__attribute__((dllexport)) BOOL GetWindowSubclass(void *hwnd,void *proc,unsigned long long id,void **data){(void)hwnd;(void)proc;(void)id;if(data)*data=0;return 0;}

__attribute__((dllexport)) BOOL SetWindowSubclass(void *hwnd,void *proc,unsigned long long id,void *data){(void)proc;(void)id;(void)data;return IsWindow(hwnd);}

__attribute__((dllexport)) BOOL RemoveWindowSubclass(void *hwnd,void *proc,unsigned long long id){(void)proc;(void)id;return IsWindow(hwnd);}

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

__attribute__((dllexport)) int GetClipboardFormatNameA(unsigned int format,char *name,int cap){(void)format;if(name&&cap>0)name[0]=0;return 0;}

__attribute__((dllexport)) int GetClipboardFormatNameW(unsigned int format,unsigned short *name,int cap){(void)format;if(name&&cap>0)name[0]=0;return 0;}

__attribute__((dllexport)) DWORD GetFocusThread(void){return GetCurrentThreadId();}

__attribute__((dllexport)) BOOL GetWindowDisplayAffinityEx(void *hwnd,unsigned int *affinity){return GetWindowDisplayAffinity(hwnd,affinity);}

__attribute__((dllexport)) void *GetWindowDpiAwarenessContextEx(void *hwnd){return GetWindowDpiAwarenessContext(hwnd);}

__attribute__((dllexport)) long GetDpiForMonitor(void *monitor,int type,unsigned int *x,unsigned int *y){(void)monitor;(void)type;if(!x||!y)return -1;*x=96;*y=96;return 0;}

__attribute__((dllexport)) BOOL EnableNonClientDpiScaling(void *hwnd){return IsWindow(hwnd);}

__attribute__((dllexport)) void *SetThreadDpiAwarenessContext(void *context){static void *current=(void *)1;void *old=current;current=context;return old;}

__attribute__((dllexport)) void *GetThreadDpiAwarenessContext(void){return (void *)1;}

__attribute__((dllexport)) unsigned int GetDpiFromDpiAwarenessContext(void *context){return context?96:0;}

__attribute__((dllexport)) BOOL SetProcessDpiAwarenessContext(void *context){return context!=0;}

__attribute__((dllexport)) void *GetProcessWindowStation(void){return (void *)1;}

__attribute__((dllexport)) void *GetThreadDesktop(DWORD thread){return thread==GetCurrentThreadId()?(void *)1:0;}

__attribute__((dllexport)) BOOL GetUserObjectInformationA(void *object,int index,void *info,DWORD len,DWORD *needed){(void)object;(void)index;if(needed)*needed=0;if(info&&len)((char*)info)[0]=0;return 0;}

__attribute__((dllexport)) BOOL GetUserObjectInformationW(void *object,int index,void *info,DWORD len,DWORD *needed){return GetUserObjectInformationA(object,index,info,len,needed);}

__attribute__((dllexport)) BOOL GetUserObjectSecurity(void *object,void *required,void *descriptor,DWORD len,DWORD *needed){(void)object;(void)required;(void)descriptor;(void)len;if(needed)*needed=0;return 0;}

__attribute__((dllexport)) BOOL SetUserObjectInformationA(void *object,int index,void *info,DWORD len){(void)object;(void)index;(void)info;(void)len;return 0;}

__attribute__((dllexport)) BOOL SetUserObjectInformationW(void *object,int index,void *info,DWORD len){return SetUserObjectInformationA(object,index,info,len);}

__attribute__((dllexport)) BOOL EnumDisplayMonitors(void *dc,const U32_RECT *clip,void *callback,long long data){(void)dc;(void)clip;if(!callback)return 0;typedef BOOL(*CB)(void*,void*,U32_RECT*,long long);U32_RECT r={0,0,(int)g_fw,(int)g_fh};return ((CB)callback)(0,(void*)1,&r,data);}

__attribute__((dllexport)) void *MonitorFromWindow(void *hwnd,DWORD flags){(void)flags;return IsWindow(hwnd)?(void *)1:(void *)1;}

__attribute__((dllexport)) void *MonitorFromPoint(U32_POINT pt,DWORD flags){(void)pt;(void)flags;return (void *)1;}

__attribute__((dllexport)) void *MonitorFromRect(const U32_RECT *r,DWORD flags){(void)r;(void)flags;return (void *)1;}

__attribute__((dllexport)) BOOL GetMonitorInfoA(void *monitor,void *info){(void)monitor;if(!info)return 0;unsigned int *p=(unsigned int*)info;if(p[0]<sizeof(unsigned int)*7)return 0;U32_RECT *r=(U32_RECT*)((unsigned char*)info+4);r->left=0;r->top=0;r->right=(int)g_fw;r->bottom=(int)g_fh;return 1;}

__attribute__((dllexport)) BOOL GetMonitorInfoW(void *monitor,void *info){return GetMonitorInfoA(monitor,info);}

__attribute__((dllexport)) BOOL EnumDisplaySettingsA(const char *device,DWORD mode,void *devmode){(void)device;if(!devmode||mode!=0xFFFFFFFFu)return 0;unsigned int *p=(unsigned int*)devmode;p[0]=sizeof(unsigned int)*32;p[4]=g_fw;p[5]=g_fh;p[6]=32;p[7]=60;return 1;}

__attribute__((dllexport)) BOOL EnumDisplaySettingsW(const unsigned short *device,DWORD mode,void *devmode){(void)device;return EnumDisplaySettingsA(0,mode,devmode);}

__attribute__((dllexport)) long ChangeDisplaySettingsA(void *devmode, DWORD flags){(void)devmode;(void)flags;return 0;}

__attribute__((dllexport)) long ChangeDisplaySettingsW(void *devmode,DWORD flags){return ChangeDisplaySettingsA(devmode,flags);}

__attribute__((dllexport)) long ChangeDisplaySettingsExA(const char *device,void *devmode,void *hwnd,DWORD flags,void *param){(void)device;(void)hwnd;(void)param;return ChangeDisplaySettingsA(devmode,flags);}

__attribute__((dllexport)) long ChangeDisplaySettingsExW(const unsigned short *device,void *devmode,void *hwnd,DWORD flags,void *param){(void)device;return ChangeDisplaySettingsExA(0,devmode,hwnd,flags,param);}

__attribute__((dllexport)) void *LoadAcceleratorsA(void *instance,const char *name){(void)instance;return name?(void *)1:0;}

__attribute__((dllexport)) void *LoadAcceleratorsW(void *instance,const unsigned short *name){(void)instance;return name?(void *)1:0;}

__attribute__((dllexport)) void *CreateAcceleratorTableA(const void *entries,int count){if(!entries||count<=0)return 0;return (void *)entries;}

__attribute__((dllexport)) void *CreateAcceleratorTableW(const void *entries,int count){return CreateAcceleratorTableA(entries,count);}

__attribute__((dllexport)) BOOL DestroyAcceleratorTable(void *table){return table!=0;}

__attribute__((dllexport)) int CopyAcceleratorTableA(void *table,void *entries,int count){(void)table;(void)entries;(void)count;return 0;}

__attribute__((dllexport)) int CopyAcceleratorTableW(void *table,void *entries,int count){return CopyAcceleratorTableA(table,entries,count);}

__attribute__((dllexport)) int LoadStringA(void *instance,unsigned int id,char *buffer,int cap){(void)instance;(void)id;if(buffer&&cap>0)buffer[0]=0;return 0;}

__attribute__((dllexport)) int LoadStringW(void *instance,unsigned int id,unsigned short *buffer,int cap){(void)instance;(void)id;if(buffer&&cap>0)buffer[0]=0;return 0;}

__attribute__((dllexport)) void *LoadMenuIndirectA(const void *template_data){return template_data?CreateMenu():0;}

__attribute__((dllexport)) void *LoadMenuIndirectW(const void *template_data){return LoadMenuIndirectA(template_data);}

__attribute__((dllexport)) void *RegisterDeviceNotificationA(void *recipient,void *filter,DWORD flags){(void)filter;(void)flags;return recipient?(void *)1:0;}

__attribute__((dllexport)) void *RegisterDeviceNotificationW(void *recipient,void *filter,DWORD flags){return RegisterDeviceNotificationA(recipient,filter,flags);}

__attribute__((dllexport)) BOOL UnregisterDeviceNotification(void *handle){return handle!=0;}

__attribute__((dllexport)) BOOL SetProcessDPIAware(void){return 1;}

__attribute__((dllexport)) BOOL IsProcessDPIAware(void){return 1;}

__attribute__((dllexport)) BOOL SetThreadDesktop(void *desktop){return desktop!=0;}

__attribute__((dllexport)) void *OpenWindowStationA(const char *name,BOOL inherit,DWORD access){(void)inherit;(void)access;return name?(void *)1:0;}

__attribute__((dllexport)) void *OpenWindowStationW(const unsigned short *name,BOOL inherit,DWORD access){(void)inherit;(void)access;return name?(void *)1:0;}

__attribute__((dllexport)) BOOL CloseWindowStation(void *station){return station!=0;}

__attribute__((dllexport)) BOOL SetProcessWindowStation(void *station){return station!=0;}

__attribute__((dllexport)) BOOL EnumWindowStationsA(void *callback,long long data){if(!callback)return 0;typedef BOOL(*CB)(char*,long long);return ((CB)callback)("WinSta0",data);}

__attribute__((dllexport)) BOOL EnumWindowStationsW(void *callback,long long data){if(!callback)return 0;typedef BOOL(*CB)(unsigned short*,long long);unsigned short n[]={'W','i','n','S','t','a','0',0};return ((CB)callback)(n,data);}

__attribute__((dllexport)) BOOL EnumDesktopsA(void *station,void *callback,long long data){(void)station;if(!callback)return 0;typedef BOOL(*CB)(char*,long long);return ((CB)callback)("Default",data);}

__attribute__((dllexport)) BOOL EnumDesktopsW(void *station,void *callback,long long data){(void)station;if(!callback)return 0;typedef BOOL(*CB)(unsigned short*,long long);unsigned short n[]={'D','e','f','a','u','l','t',0};return ((CB)callback)(n,data);}

__attribute__((dllexport)) void *OpenDesktopA(const char *name,DWORD flags,BOOL inherit,DWORD access){(void)flags;(void)inherit;(void)access;return name?(void *)1:0;}

__attribute__((dllexport)) void *OpenDesktopW(const unsigned short *name,DWORD flags,BOOL inherit,DWORD access){(void)flags;(void)inherit;(void)access;return name?(void *)1:0;}

__attribute__((dllexport)) BOOL CloseDesktop(void *desktop){return desktop!=0;}

__attribute__((dllexport)) BOOL SwitchDesktop(void *desktop){return desktop!=0;}

__attribute__((dllexport)) BOOL GetUserObjectInformation(void *object,int index,void *info,DWORD len,DWORD *needed){return GetUserObjectInformationA(object,index,info,len,needed);}

__attribute__((dllexport)) BOOL SetUserObjectInformation(void *object,int index,void *info,DWORD len){return SetUserObjectInformationA(object,index,info,len);}

__attribute__((dllexport)) long GetMonitorDpiType(void *monitor,int type,unsigned int *dpi){(void)monitor;(void)type;if(!dpi)return -1;*dpi=96;return 0;}

__attribute__((dllexport)) int GetSystemMetricsForMonitor(void *monitor,int index){(void)monitor;return GetSystemMetrics(index);}

__attribute__((dllexport)) unsigned int GetWindowDpi(void *hwnd){return GetDpiForWindow(hwnd);}

__attribute__((dllexport)) unsigned int GetDpiForWindowEx(void *hwnd){return GetDpiForWindow(hwnd);}

__attribute__((dllexport)) BOOL GetPhysicalCursorPos(U32_POINT *pt){return GetCursorPos(pt);}

__attribute__((dllexport)) BOOL SetPhysicalCursorPos(int x,int y){return SetCursorPos(x,y);}

__attribute__((dllexport)) BOOL GetCursorInfoEx(void *info){return GetCursorInfo(info);}

__attribute__((dllexport)) BOOL SetWindowFeedbackSetting(void *hwnd,int setting,DWORD flags,DWORD size,const void *data){(void)setting;(void)flags;(void)size;(void)data;return IsWindow(hwnd);}

__attribute__((dllexport)) BOOL GetWindowFeedbackSetting(void *hwnd,int setting,DWORD flags,DWORD *size,void *data){(void)setting;(void)flags;if(!IsWindow(hwnd)||!size)return 0;if(data&&*size)((unsigned char*)data)[0]=0;*size=0;return 1;}

__attribute__((dllexport)) BOOL GetWindowCompositionAttribute(void *hwnd,void *data){(void)data;return IsWindow(hwnd)?0:0;}

__attribute__((dllexport)) BOOL SetWindowCompositionAttribute(void *hwnd,const void *data){(void)data;return IsWindow(hwnd);}

__attribute__((dllexport)) BOOL GetMenuInfoEx(void *menu,U32_MENUINFO *info){return GetMenuInfo(menu,info);}

__attribute__((dllexport)) BOOL SetMenuInfoEx(void *menu,const U32_MENUINFO *info){return SetMenuInfo(menu,info);}

__attribute__((dllexport)) unsigned int GetMenuItemIDEx(void *menu,unsigned int pos){return GetMenuItemID(menu,pos);}

__attribute__((dllexport)) void *GetSubMenuEx(void *menu,int pos){return GetSubMenu(menu,pos);}

__attribute__((dllexport)) BOOL SetWindowRedraw(void *hwnd,BOOL redraw){(void)redraw;return IsWindow(hwnd);}

__attribute__((dllexport)) BOOL GetWindowRedraw(void *hwnd){return IsWindow(hwnd);}

__attribute__((dllexport)) DWORD GetWindowArrange(void){return 0;}

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

__attribute__((dllexport)) long long DispatchMessageW(const MSG *msg){if(!msg)return 0;return (long long)SendMessageW((void *)(unsigned long long)msg->hwnd,msg->message,msg->wParam,msg->lParam);}

__attribute__((dllexport)) void *CreateDialogIndirectParamA(void *instance,const void *templ,void *parent,void *proc,long long param){(void)instance;(void)templ;(void)proc;(void)param;return IsWindow(parent)?parent:CreateWindowExA(0,"DIALOG","Dialog",0x10000000,0,0,320,200,parent,0,0,0);}

__attribute__((dllexport)) void *CreateDialogIndirectParamW(void *instance,const void *templ,void *parent,void *proc,long long param){(void)instance;(void)templ;(void)proc;(void)param;return IsWindow(parent)?parent:CreateWindowExW(0,0,0,0x10000000,0,0,320,200,parent,0,0,0);}

__attribute__((dllexport)) int DialogBoxIndirectParamA(void *instance,const void *templ,void *parent,void *proc,long long param){(void)instance;(void)templ;(void)proc;(void)param;return IsWindow(parent)?1:-1;}

__attribute__((dllexport)) int DialogBoxIndirectParamW(void *instance,const void *templ,void *parent,void *proc,long long param){return DialogBoxIndirectParamA(instance,templ,parent,proc,param);}

__attribute__((dllexport)) unsigned int GetRawInputData(void *raw,unsigned int command,void *data,unsigned int *size,unsigned int header){(void)raw;(void)command;(void)data;(void)header;if(size)*size=0;return 0xFFFFFFFFu;}

__attribute__((dllexport)) unsigned int GetRawInputBuffer(void *data,unsigned int *size,unsigned int header){(void)data;(void)header;if(size)*size=0;return 0xFFFFFFFFu;}

__attribute__((dllexport)) unsigned int GetRawInputDeviceInfoA(void *device,unsigned int command,void *data,unsigned int *size){(void)device;(void)command;(void)data;if(size)*size=0;return 0xFFFFFFFFu;}

__attribute__((dllexport)) unsigned int GetRawInputDeviceInfoW(void *device,unsigned int command,void *data,unsigned int *size){return GetRawInputDeviceInfoA(device,command,data,size);}

__attribute__((dllexport)) unsigned int GetRawInputDeviceList(void *list,unsigned int *count,unsigned int size){(void)list;(void)size;if(count)*count=0;return 0;}

__attribute__((dllexport)) BOOL RegisterRawInputDevices(const void *devices,unsigned int count,unsigned int size){(void)devices;(void)count;(void)size;return 0;}

__attribute__((dllexport)) long long DefRawInputProc(void **packets,int count,unsigned int size){(void)packets;(void)count;(void)size;return 0;}

__attribute__((dllexport)) BOOL GetPointerType(unsigned int id,unsigned int *type){(void)id;if(!type)return 0;*type=2;return 1;}

__attribute__((dllexport)) BOOL GetPointerInfo(unsigned int id,void *info){(void)id;(void)info;return 0;}

__attribute__((dllexport)) BOOL GetPointerInfoHistory(unsigned int id,unsigned int *count,void *info){(void)id;(void)info;if(count)*count=0;return 0;}

__attribute__((dllexport)) BOOL GetPointerPenInfo(unsigned int id,void *info){(void)id;(void)info;return 0;}

__attribute__((dllexport)) BOOL GetPointerTouchInfo(unsigned int id,void *info){(void)id;(void)info;return 0;}

__attribute__((dllexport)) BOOL GetPointerFrameInfo(unsigned int id,unsigned int *count,void *info){(void)id;(void)info;if(count)*count=0;return 0;}

__attribute__((dllexport)) BOOL GetPointerDevice(unsigned int id,void **device){(void)id;if(device)*device=0;return 0;}

__attribute__((dllexport)) BOOL GetPointerDeviceRects(void *device,U32_RECT *pointer,U32_RECT *display){(void)device;if(pointer){pointer->left=pointer->top=pointer->right=pointer->bottom=0;}if(display){display->left=display->top=0;display->right=(int)g_fw;display->bottom=(int)g_fh;}return 0;}

__attribute__((dllexport)) BOOL GetPointerDevices(unsigned int *count,void *devices){(void)devices;if(count)*count=0;return 1;}

__attribute__((dllexport)) BOOL GetGestureInfo(void *handle,void *info){(void)handle;(void)info;return 0;}

__attribute__((dllexport)) BOOL CloseGestureInfoHandle(void *handle){return handle!=0;}

__attribute__((dllexport)) BOOL GetGestureExtraArgs(void *handle,unsigned int size,void *args){(void)handle;(void)size;(void)args;return 0;}

__attribute__((dllexport)) BOOL SetGestureConfig(void *hwnd,DWORD reserved,unsigned int count,const void *config,unsigned int size){(void)reserved;(void)count;(void)config;(void)size;return IsWindow(hwnd);}

__attribute__((dllexport)) BOOL GetTouchInputInfo(void *touch,unsigned int count,void *info,int size){(void)touch;(void)count;(void)info;(void)size;return 0;}
