
typedef unsigned int u32;
typedef unsigned long long u64;
typedef int BOOL;
typedef const char *LPCSTR;
typedef long long i64;

enum {
    SYS_Exit=0, SYS_Write=1, SYS_Alloc=3, SYS_MapFb=8, SYS_GetInput=10,
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
static u32 *g_front, *g_back, *g_draw;
static u32 g_pitch, g_fw, g_fh, g_draw_pitch;
static int g_mx = 640, g_my = 400;
static int g_menu, g_drag, g_drag_ox, g_drag_oy;
static int g_search_focus;
static char g_search_text[32];
static int g_search_len;
static u32 cur_under[16][16];
static int cur_on, cur_ox, cur_oy;
static WINDOW g_wnd;
#define CMD_TEXT_LINES 512
static char g_lines[CMD_TEXT_LINES][80];
static int g_nlines, g_tb_n, g_tb_pids[8];
static int g_scroll_top, g_scroll_drag, g_scroll_grab, g_batch_follow;

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

    if (g_draw && (u32)x < g_fw && (u32)y < g_fh)
        g_draw[y * g_draw_pitch + x] = col;
}

static void fill(int x, int y, int w, int h, u32 col) {
    if (!g_front || !g_draw || w <= 0 || h <= 0) return;

    /* Clip once, then write contiguous scanlines without per-pixel checks. */
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)g_fw) x1 = (int)g_fw;
    if (y1 > (int)g_fh) y1 = (int)g_fh;
    if (x0 >= x1 || y0 >= y1) return;

    int count = x1 - x0;
    for (int yy = y0; yy < y1; yy++) {
        u32 *p = g_draw + (u64)yy * g_draw_pitch + x0;
        for (int xx = 0; xx < count; xx++) p[xx] = col;
    }
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

            /*
             * Save-under belongs to the shared physical framebuffer.
             * A process-local backbuffer is NOT the canonical desktop:
             * restoring from it overwrites windows owned by other processes
             * and produces the slow CRT-like disappearance effect.
             */
            g_front[(u64)yy * g_pitch + xx] = cur_under[j][i];
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

            /*
             * Small 16x16 arrow cursor. Coordinates are relative to the
             * arrow tip (0,0), not a hollow rectangle.
             * '#' = white edge, 'B' = dark outline, 'W' = white fill.
             */
            /* Classic filled desktop pointer: dark outline, white face,
               tapered tip and a visible stem. '.' remains transparent. */
            /* A proper filled pointer: black outline, white face, grey edge/shadow. */
            static const char *arrow[16] = {
                "BB..............",
                "BWB.............",
                "BWWB............",
                "BWWWB...........",
                "BWWWWB..........",
                "BWWWWWB.........",
                "BWWWWWWB........",
                "BWWWWWWWB.......",
                "BWWWWWWWWB......",
                "BWWWWBBBBBB.....",
                "BWWB..BSS.......",
                "BWB.BWBSS.......",
                "BB...BWBSS......",
                "B.....BWBSS.....",
                "......BWBSS.....",
                ".......BBSS....."
            };
            char pix = arrow[j][i];
            if (pix == 'B') g_front[yy * g_pitch + xx] = 0xFF101820;
            else if (pix == 'W') g_front[yy * g_pitch + xx] = COL_WHITE;
            else if (pix == 'S') g_front[yy * g_pitch + xx] = 0xFF8B949E;
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
    g_draw = g_front;
    g_draw_pitch = g_pitch;

    /*
     * Compose the desktop off-screen, then copy a complete frame to the
     * framebuffer. This avoids exposing the clear-and-redraw sequence.
     */
    if (!g_back && g_fw && g_fh &&
        (u64)g_fw * (u64)g_fh * sizeof(u32) <= 16ull * 1024 * 1024) {
        u64 bytes = (u64)g_fw * (u64)g_fh * sizeof(u32);
        u64 va = sy3(SYS_Alloc, 0x60000000ULL, bytes, 0);
        if (va) g_back = (u32 *)va;
    }
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

static void compose(void);

/*
 * Search typing changes only the Start menu. Repaint and present just the
 * taskbar/menu strip instead of rebuilding the entire desktop per keypress.
 */
static void redraw_taskbar_region(void) {
    ensure_fb();
    if (!g_front) return;
    if (!g_back) {
        compose();
        return;
    }

    cursor_restore();
    g_draw = g_back;
    g_draw_pitch = g_fw;
    draw_taskbar();

    int y0 = (int)g_fh - 44 - 146 - 4;
    if (y0 < 0) y0 = 0;
    for (int y = y0; y < (int)g_fh; y++) {
        u32 *dst = g_front + (u64)y * g_pitch;
        u32 *src = g_back + (u64)y * g_fw;
        for (u32 x = 0; x < g_fw; x++)
            dst[x] = src[x];
    }

    g_draw = g_front;
    g_draw_pitch = g_pitch;
    cursor_save_draw(g_mx, g_my);
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
    g_draw = g_back ? g_back : g_front;
    g_draw_pitch = g_back ? g_fw : g_pitch;
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

        int content_x = x + 14;
        int content_y = y + 44;
        int visible_rows = (h - 52) / 16;
        if (visible_rows < 1) visible_rows = 1;
        int max_scroll = g_nlines > visible_rows ? g_nlines - visible_rows : 0;
        if (g_scroll_top > max_scroll) g_scroll_top = max_scroll;
        for (int row = 0; row < visible_rows; row++) {
            int i = g_scroll_top + row;
            if (i >= g_nlines) break;
            const char *s = g_lines[i];
            int tx = content_x;
            int ty = content_y + row * 16;
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

        /* Native-style vertical scrollbar for terminal history. */
        {
            int sx = x + w - 14;
            int sy = y + 36;
            int sh = h - 44;
            int total = g_nlines > visible_rows ? g_nlines : visible_rows;
            if (sh < 12) sh = 12;
            fill(sx, sy, 10, sh, COL_TITLE);
            int thumb_h = total > 0 ? (sh * visible_rows) / total : sh;
            if (thumb_h < 18) thumb_h = 18;
            if (thumb_h > sh) thumb_h = sh;
            int thumb_range = sh - thumb_h;
            int thumb_y = sy;
            if (max_scroll > 0)
                thumb_y += (thumb_range * g_scroll_top) / max_scroll;
            fill(sx + 2, thumb_y, 6, thumb_h, COL_MUTED);
            fill(sx + 3, thumb_y + 2, 4, thumb_h > 4 ? thumb_h - 4 : 1, COL_ACCENT);
        }
    }

    draw_taskbar();

    if (g_back) {
        /*
         * Each process has a private user32 backbuffer, but all processes
         * share the physical framebuffer. Copying a private full-screen
         * frame here lets one app erase every other app's window.
         *
         * The shell (its 1x1 invisible window) owns the desktop background.
         * Normal windows may present only their own window rectangle and
         * the taskbar strip; never blast their private desktop over peers.
         */
        int regions[2][4];
        int region_count = 0;
        if (!g_wnd.visible || g_wnd.w <= 2) {
            regions[region_count][0] = 0;
            regions[region_count][1] = 0;
            regions[region_count][2] = (int)g_fw;
            regions[region_count++][3] = (int)g_fh;
        } else {
            regions[region_count][0] = g_wnd.x;
            regions[region_count][1] = g_wnd.y;
            regions[region_count][2] = g_wnd.x + g_wnd.w;
            regions[region_count++][3] = g_wnd.y + g_wnd.h;
            regions[region_count][0] = 0;
            regions[region_count][1] = (int)g_fh - 44;
            regions[region_count][2] = (int)g_fw;
            regions[region_count++][3] = (int)g_fh;
        }
        for (int r = 0; r < region_count; r++) {
            int x0 = regions[r][0], y0 = regions[r][1];
            int x1 = regions[r][2], y1 = regions[r][3];
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 > (int)g_fw) x1 = (int)g_fw;
            if (y1 > (int)g_fh) y1 = (int)g_fh;
            for (int yy = y0; yy < y1; yy++) {
                u32 *dst = g_front + (u64)yy * g_pitch + x0;
                u32 *src = g_back + (u64)yy * g_fw + x0;
                for (int xx = x0; xx < x1; xx++)
                    dst[xx - x0] = src[xx - x0];
            }
        }
    }

    g_draw = g_front;
    g_draw_pitch = g_pitch;
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

        /* Scrollbar click/drag takes precedence over title-bar dragging. */
        {
            int visible_rows = (g_wnd.h - 52) / 16;
            if (visible_rows < 1) visible_rows = 1;
            int max_scroll = g_nlines > visible_rows ? g_nlines - visible_rows : 0;
            int sx = wx + ww - 14, sy = wy + 36, sh = g_wnd.h - 44;
            int total = g_nlines > visible_rows ? g_nlines : visible_rows;
            if (sh < 12) sh = 12;
            int thumb_h = total > 0 ? (sh * visible_rows) / total : sh;
            if (thumb_h < 18) thumb_h = 18;
            if (thumb_h > sh) thumb_h = sh;
            int thumb_range = sh - thumb_h;
            int thumb_y = sy + (max_scroll > 0 ? (thumb_range * g_scroll_top) / max_scroll : 0);
            if (max_scroll > 0 && hit(x, y, sx, sy, sx + 12, sy + sh)) {
                if (y >= thumb_y && y < thumb_y + thumb_h) {
                    g_scroll_drag = 1;
                    g_scroll_grab = y - thumb_y;
                } else {
                    int target = y < thumb_y ? g_scroll_top - visible_rows : g_scroll_top + visible_rows;
                    if (target < 0) target = 0;
                    if (target > max_scroll) target = max_scroll;
                    g_scroll_top = target;
                    compose();
                }
                return;
            }
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
    g_scroll_top = 0;
    g_scroll_drag = 0;
    g_batch_follow = 1;
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

    if (y == -1) {
        g_nlines = 0;
        g_scroll_top = 0;
        g_scroll_drag = 0;
        for (int i = 0; i < CMD_TEXT_LINES; i++) g_lines[i][0] = 0;
        return 1;
    }
    if (y == -2) {
        int visible_rows = (g_wnd.h - 52) / 16;
        if (visible_rows < 1) visible_rows = 1;
        g_batch_follow = g_scroll_top >= (g_nlines > visible_rows ? g_nlines - visible_rows : 0);
        return 1;
    }
    if (y >= 0 && y < CMD_TEXT_LINES) {
        int n = len < 79 ? len : 79;
        for (int i = 0; i < n; i++) g_lines[y][i] = str[i];
        g_lines[y][n] = 0;
        if (y >= g_nlines) g_nlines = y + 1;
        if (g_batch_follow) {
            int visible_rows = (g_wnd.h - 52) / 16;
            if (visible_rows < 1) visible_rows = 1;
            g_scroll_top = g_nlines > visible_rows ? g_nlines - visible_rows : 0;
        }
    }

    return 1;
}


static void redraw_window_contents(void) {
    if (!g_front || !g_wnd.visible || g_wnd.minimized) return;

    /* Keep the backbuffer as the cursor-free canonical desktop image. */
    cursor_restore();
    g_draw = g_back ? g_back : g_front;
    g_draw_pitch = g_back ? g_fw : g_pitch;

    int x = g_wnd.x, y = g_wnd.y, w = g_wnd.w, h = g_wnd.h;
    fill(x + 4, y + 36, w - 8, h - 42, COL_BG);

    int content_x = x + 14;
    int content_y = y + 44;
    int visible_rows = (h - 52) / 16;
    if (visible_rows < 1) visible_rows = 1;
    int max_scroll = g_nlines > visible_rows ? g_nlines - visible_rows : 0;
    if (g_scroll_top > max_scroll) g_scroll_top = max_scroll;

    for (int row = 0; row < visible_rows; row++) {
        int i = g_scroll_top + row;
        if (i >= g_nlines) break;
        const char *s = g_lines[i];
        int tx = content_x;
        int ty = content_y + row * 16;
        u32 ic = 0;

        if (s[0] == '[' && s[1] == 'D' && s[2] == 'I')
            ic = COL_WARN;
        else if (s[0] == '[' && s[1] == 'E')
            ic = COL_ACCENT2;
        else if (s[0] == '[' && s[1] == 'D' && s[2] == 'L')
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

    /* Keep the scrollbar in sync with the terminal history. */
    int sx = x + w - 14, sy = y + 36, sh = h - 44;
    int total = g_nlines > visible_rows ? g_nlines : visible_rows;
    if (sh < 12) sh = 12;
    fill(sx, sy, 10, sh, COL_TITLE);
    int thumb_h = total > 0 ? (sh * visible_rows) / total : sh;
    if (thumb_h < 18) thumb_h = 18;
    if (thumb_h > sh) thumb_h = sh;
    int thumb_range = sh - thumb_h;
    int thumb_y = sy;
    if (max_scroll > 0)
        thumb_y += (thumb_range * g_scroll_top) / max_scroll;
    fill(sx + 2, thumb_y, 6, thumb_h, COL_MUTED);
    fill(sx + 3, thumb_y + 2, 4, thumb_h > 4 ? thumb_h - 4 : 1, COL_ACCENT);

    /* Present only the changed client area; no full-screen copy per key. */
    if (g_back) {
        int x0 = x + 4, y0 = y + 36;
        int x1 = x + w - 4, y1 = y + h - 6;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > (int)g_fw) x1 = (int)g_fw;
        if (y1 > (int)g_fh) y1 = (int)g_fh;
        for (int yy = y0; yy < y1; yy++) {
            u32 *dst = g_front + (u64)yy * g_pitch + x0;
            u32 *src = g_back + (u64)yy * g_fw + x0;
            for (int xx = x0; xx < x1; xx++) dst[xx - x0] = src[xx - x0];
        }
    }

    g_draw = g_front;
    g_draw_pitch = g_pitch;
    cursor_save_draw(g_mx, g_my);
}


/* Minimal GDI bridge used by gdi32.dll. Coordinates are client-relative.
 * Draw into this process' user32 surface and present only the affected area. */
static void gdi_present(int x,int y,int w,int h) {
    if(!g_front||w<=0||h<=0)return;
    int x0=g_wnd.x+4+x,y0=g_wnd.y+36+y,x1=x0+w,y1=y0+h;
    if(x0<0)x0=0;if(y0<0)y0=0;
    if(x1>(int)g_fw)x1=(int)g_fw;if(y1>(int)g_fh)y1=(int)g_fh;
    if(g_back)for(int yy=y0;yy<y1;yy++) {
        u32 *dst=g_front+(u64)yy*g_pitch+x0;
        u32 *src=g_back+(u64)yy*g_fw+x0;
        for(int xx=x0;xx<x1;xx++)dst[xx-x0]=src[xx-x0];
    }
    g_draw=g_front;g_draw_pitch=g_pitch;
    cursor_save_draw(g_mx,g_my);
}
static void gdi_begin(void) {
    ensure_fb();cursor_restore();
    g_draw=g_back?g_back:g_front;
    g_draw_pitch=g_back?g_fw:g_pitch;
}
__attribute__((dllexport)) BOOL GdiFillRect(int x,int y,int w,int h,u32 color) {
    if(w<=0||h<=0)return 0;
    gdi_begin();fill(g_wnd.x+4+x,g_wnd.y+36+y,w,h,color);gdi_present(x,y,w,h);return 1;
}
__attribute__((dllexport)) BOOL GdiLine(int x1,int y1,int x2,int y2,u32 color) {
    gdi_begin();
    int dx=x2>=x1?x2-x1:x1-x2, sx=x1<x2?1:-1;
    int dy=y2>=y1?y1-y2:y2-y1, sy=y1<y2?1:-1, err=dx+dy;
    int minx=x1<x2?x1:x2,maxx=x1>x2?x1:x2,miny=y1<y2?y1:y2,maxy=y1>y2?y1:y2;
    for(;;) {
        putpx(g_wnd.x+4+x1,g_wnd.y+36+y1,color);
        if(x1==x2&&y1==y2)break;
        int e2=2*err;
        if(e2>=dy){err+=dy;x1+=sx;}
        if(e2<=dx){err+=dx;y1+=sy;}
    }
    gdi_present(minx,miny,maxx-minx+1,maxy-miny+1);return 1;
}
__attribute__((dllexport)) BOOL GdiTextOut(int x,int y,LPCSTR str,int len,u32 color) {
    if(!str)return 0;if(len<0){len=0;while(str[len])len++;}
    gdi_begin();
    for(int i=0;i<len;i++)draw_char(g_wnd.x+4+x+i*8,g_wnd.y+36+y,str[i],color);
    gdi_present(x,y,len*8,8);return 1;
}

__attribute__((dllexport))
BOOL InvalidateRect(void *hwnd, void *rc, BOOL erase) {
    (void)hwnd;
    (void)rc;
    (void)erase;

    if (g_batch_follow) {
        int visible_rows = (g_wnd.h - 52) / 16;
        if (visible_rows < 1) visible_rows = 1;
        g_scroll_top = g_nlines > visible_rows ? g_nlines - visible_rows : 0;
    }
    redraw_window_contents();
    return 1;
}

__attribute__((dllexport))
BOOL GetMessageA(MSG *msg, void *hwnd, u32 min, u32 max) {
    (void)hwnd;
    if (!msg) return 0;
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

                        redraw_taskbar_region();
                        continue;
                    }

                    if (key == '\n' || key == '\r') {
                        submit_search();
                        redraw_taskbar_region();
                        continue;
                    }

                    if (key >= 32 && key <= 126 &&
                        g_search_len < 22) {
                        g_search_text[g_search_len++] = key;
                        g_search_text[g_search_len] = 0;
                        redraw_taskbar_region();
                    }

                    continue;
                }

                msg->hwnd = 0;
                msg->message = 0x0100;
                msg->wParam = (u64)(unsigned char)ev.key;
                msg->lParam = 0;
                msg->time = 0;
                msg->pt_x = g_mx;
                msg->pt_y = g_my;
                return 1;
            }

            if (ev.type == 4) {
                /* Signed wheel ticks: positive is up, negative is down. */
                if (g_wnd.visible && !g_wnd.minimized &&
                    hit(ev.x, ev.y, g_wnd.x, g_wnd.y,
                        g_wnd.x + g_wnd.w, g_wnd.y + g_wnd.h)) {
                    int visible_rows = (g_wnd.h - 52) / 16;
                    if (visible_rows < 1) visible_rows = 1;
                    int max_scroll = g_nlines > visible_rows ? g_nlines - visible_rows : 0;
                    int old_scroll_top = g_scroll_top;
                    g_scroll_top -= (int)(signed char)ev.key * 3;
                    if (g_scroll_top < 0) g_scroll_top = 0;
                    if (g_scroll_top > max_scroll) g_scroll_top = max_scroll;
                    g_batch_follow = (g_scroll_top == max_scroll);
                    /* Avoid repainting when wheel input cannot change the view. */
                    if (g_scroll_top != old_scroll_top) compose();
                }
                continue;
            }

            if (ev.type == 2 || ev.type == 3) {
                g_mx = ev.x;
                g_my = ev.y;

                msg->hwnd = 0;
                msg->message = (ev.type == 3) ? 0x0201 : 0x0200;
                msg->wParam = (u64)(unsigned int)ev.btn;
                msg->lParam = 0;
                msg->time = 0;
                msg->pt_x = ev.x;
                msg->pt_y = ev.y;

                if (g_scroll_drag && (ev.btn & 1)) {
                    int visible_rows = (g_wnd.h - 52) / 16;
                    if (visible_rows < 1) visible_rows = 1;
                    int max_scroll = g_nlines > visible_rows ? g_nlines - visible_rows : 0;
                    int sy = g_wnd.y + 36;
                    int sh = g_wnd.h - 44;
                    int total = g_nlines > visible_rows ? g_nlines : visible_rows;
                    if (sh < 12) sh = 12;
                    int thumb_h = total > 0 ? (sh * visible_rows) / total : sh;
                    if (thumb_h < 18) thumb_h = 18;
                    if (thumb_h > sh) thumb_h = sh;
                    int thumb_range = sh - thumb_h;
                    int py = ev.y - g_scroll_grab - sy;
                    if (py < 0) py = 0;
                    if (py > thumb_range) py = thumb_range;
                    g_scroll_top = thumb_range > 0 ? (py * max_scroll) / thumb_range : 0;
                    compose();
                    continue;
                }

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
                    g_scroll_drag = 0;
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

/* Query the current process's window geometry and visible text scroll offset. */
__attribute__((dllexport))
BOOL GetWindowRect(void *hwnd, int *rect) {
    (void)hwnd;
    if (!rect || !g_wnd.visible || g_wnd.w <= 0 || g_wnd.h <= 0)
        return 0;
    rect[0] = g_wnd.x;
    rect[1] = g_wnd.y;
    rect[2] = g_wnd.x + g_wnd.w;
    rect[3] = g_wnd.y + g_wnd.h;
    return 1;
}

__attribute__((dllexport))
int GetScrollPosition(void) {
    return g_scroll_top;
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