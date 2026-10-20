
typedef unsigned int u32;
typedef unsigned long long u64;
typedef int BOOL;
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
    if (!g_front || !g_pitch) return;

    cursor_restore();
    cur_ox = x;
    cur_oy = y;

    for (int j = 0; j < 16; j++) {
        for (int i = 0; i < 16; i++) {
            int yy = y + j, xx = x + i;

            if (yy < 0 || (u32)yy >= g_fh ||
                xx < 0 || (u32)xx >= g_fw) {
                cur_under[j][i] = COL_BG;
                continue;
            }

            cur_under[j][i] = g_front[yy * g_pitch + xx];

            if (i == 0 || j == 0 || i == 15 || j == 15)
                g_front[yy * g_pitch + xx] = COL_WHITE;
            else if (i < 3 || j < 3)
                g_front[yy * g_pitch + xx] = COL_ACCENT;
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
        compose:
        ;
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
    (void)ex; (void)cls; (void)style;
    (void)parent; (void)menu; (void)inst; (void)param;

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
typedef struct { int left, top, right, bottom; } U32_RECT;
typedef struct { int x, y; } U32_POINT;
typedef struct { void *hdc; int erase; U32_RECT paint; int restore, inc_update; unsigned char reserved[32]; } U32_PAINTSTRUCT;
typedef struct { unsigned int style; void *proc; int cls_extra, wnd_extra; void *instance, icon, cursor, background; LPCSTR menu_name, class_name; } U32_WNDCLASSA;
static int u32_show_cursor_count;
static void *u32_capture;
static void *u32_focus;
static void *u32_active;
static unsigned int u32_next_menu = 10;

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
__attribute__((dllexport)) long GetWindowLongA(void *hwnd, int index) { (void)hwnd; (void)index; return 0; }
__attribute__((dllexport)) long SetWindowLongA(void *hwnd, int index, long value) { (void)hwnd; (void)index; (void)value; return 0; }
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
__attribute__((dllexport)) void *CreateMenu(void) { return (void *)(unsigned long long)(u32_next_menu++); }
__attribute__((dllexport)) void *CreatePopupMenu(void) { return CreateMenu(); }
__attribute__((dllexport)) BOOL AppendMenuA(void *menu, unsigned int flags, unsigned long long id, LPCSTR text) {
    (void)menu; (void)flags; (void)id; (void)text; return 1;
}
__attribute__((dllexport)) BOOL DestroyMenu(void *menu) { (void)menu; return 1; }
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
