typedef unsigned int u32;
typedef unsigned long long u64;
typedef int BOOL;
typedef const char *LPCSTR;
typedef long long i64;

enum { SYS_Write=1, SYS_MapFb=8, SYS_GetInput=10 };

static u64 sy3(u64 n, u64 a, u64 b, u64 c) {
    u64 r;
    __asm__ volatile(
        "mov %[n], %%rax\nmov %[a], %%rdi\nmov %[b], %%rsi\nmov %[c], %%rdx\nint $0x80\nmov %%rax, %[r]"
        : [r]"=r"(r) : [n]"r"(n),[a]"r"(a),[b]"r"(b),[c]"r"(c)
        : "rax","rdi","rsi","rdx","memory","cc");
    return r;
}

typedef struct { u64 va; u32 w, h, pitch; } FBInfo;
typedef struct { int type, x, y, btn; char key; } InputEvent;

static FBInfo g_fb;
static u32 *g_pix;
static u32 g_pitch;
static int g_mx = 640, g_my = 400;

static const unsigned char FONT[38][7] = {
{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
{0x0E,0x11,0x10,0x17,0x11,0x11,0x0E},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},{0x01,0x01,0x01,0x01,0x11,0x11,0x0E},
{0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
{0x11,0x1B,0x15,0x11,0x11,0x11,0x11},{0x11,0x19,0x15,0x13,0x11,0x11,0x11},
{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
{0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
{0x11,0x11,0x11,0x11,0x11,0x11,0x0E},{0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
{0x11,0x11,0x11,0x15,0x15,0x1B,0x11},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
{0x11,0x11,0x0A,0x04,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
{0x0E,0x11,0x01,0x06,0x08,0x10,0x1F},{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
{0,0,0,0,0,0,0},{0,0,0x04,0,0x04,0,0},
};

static int gi(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= '0' && c <= '9') return 26 + (c - '0');
    if (c == ':') return 37;
    return 36;
}
static void putpx(int x, int y, u32 col) {
    if (!g_pix || x < 0 || y < 0 || (u32)x >= g_fb.w || (u32)y >= g_fb.h) return;
    g_pix[y * g_pitch + x] = col;
}
static void fill(int x, int y, int w, int h, u32 col) {
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) putpx(xx, yy, col);
}
static void draw_char(int x, int y, char ch, u32 col) {
    const unsigned char *g = FONT[gi(ch)];
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < 5; c++)
            if (g[r] & (0x10 >> c)) putpx(x + c, y + r, col);
}
static void draw_text(int x, int y, const char *s, u32 col) {
    for (; *s; s++) { draw_char(x, y, *s, col); x += 6; }
}

static u32 cur_under[20][20];
static int cur_saved, cur_sx, cur_sy;

static void cursor_erase(void) {
    if (!cur_saved || !g_pix) return;
    for (int j = 0; j < 20; j++)
        for (int i = 0; i < 20; i++)
            putpx(cur_sx + i, cur_sy + j, cur_under[j][i]);
    cur_saved = 0;
}
static void cursor_draw(int x, int y) {
    if (!g_pix) return;
    cursor_erase();
    cur_sx = x; cur_sy = y;
    for (int j = 0; j < 20; j++)
        for (int i = 0; i < 20; i++) {
            int px = x + i, py = y + j;
            if (px < 0 || py < 0 || (u32)px >= g_fb.w || (u32)py >= g_fb.h)
                cur_under[j][i] = 0;
            else
                cur_under[j][i] = g_pix[py * g_pitch + px];
        }
    cur_saved = 1;
    for (int i = 0; i < 16; i++) {
        putpx(x, y + i, 0xFFFFFF00);
        putpx(x + 1, y + i, 0xFF000000);
        if (i < 10) {
            putpx(x + i, y + i, 0xFFFFFF00);
            putpx(x + i + 1, y + i, 0xFF000000);
        }
    }
}

typedef struct { int x, y, w, h; char title[64]; int visible; } WINDOW;
static WINDOW g_wnd;
static int g_quit;
static char g_lines[40][80];
static int g_nlines;

static void ensure_fb(void) {
    if (g_pix) return;
    if (!sy3(SYS_MapFb, (u64)&g_fb, 0, 0) || !g_fb.va) return;
    g_pix = (u32 *)g_fb.va;
    g_pitch = g_fb.pitch / 4;
}
static void paint_window(void) {
    ensure_fb();
    if (!g_pix || !g_wnd.visible) return;
    cur_saved = 0;
    int x = g_wnd.x, y = g_wnd.y, w = g_wnd.w, h = g_wnd.h;
    fill(0, 0, (int)g_fb.w, (int)g_fb.h, 0xFF1A1A2E);
    fill(0, (int)g_fb.h - 48, (int)g_fb.w, 48, 0xFF16213E);
    draw_text(16, (int)g_fb.h - 30, "START", 0xFFFFFFFF);
    draw_text((int)g_fb.w - 40, (int)g_fb.h - 30, "C:", 0xFFAAAAAA);
    fill(x, y, w, h, 0xFF2D2D30);
    fill(x, y, w, 28, 0xFF0078D7);
    draw_text(x + 10, y + 10, g_wnd.title, 0xFFFFFFFF);
    fill(x + w - 28, y + 4, 20, 20, 0xFFE81123);
    draw_text(x + w - 24, y + 10, "X", 0xFFFFFFFF);
    fill(x + 2, y + 28, w - 4, h - 30, 0xFFFFFFFF);
    for (int i = 0; i < g_nlines; i++)
        draw_text(x + 12, y + 36 + i * 12, g_lines[i], 0xFF000000);
    cursor_draw(g_mx, g_my);
}

__attribute__((dllexport))
void *CreateWindowExA(u32 ex, LPCSTR cls, LPCSTR title, u32 style,
                      int x, int y, int w, int h,
                      void *parent, void *menu, void *inst, void *param) {
    (void)ex;(void)cls;(void)style;(void)parent;(void)menu;(void)inst;(void)param;
    ensure_fb();
    if (x < 0) x = 80; if (y < 0) y = 60;
    if (w < 200) w = 640; if (h < 100) h = 480;
    g_wnd.x = x; g_wnd.y = y; g_wnd.w = w; g_wnd.h = h;
    int i = 0; if (title) for (; title[i] && i < 63; i++) g_wnd.title[i] = title[i];
    g_wnd.title[i] = 0; g_wnd.visible = 1; g_quit = 0; g_nlines = 0;
    paint_window();
    return (void *)1;
}
__attribute__((dllexport)) BOOL ShowWindow(void *hwnd, int cmd) {
    (void)hwnd;(void)cmd; g_wnd.visible = 1; paint_window(); return 1;
}
__attribute__((dllexport)) BOOL UpdateWindow(void *hwnd) {
    (void)hwnd; paint_window(); return 1;
}
__attribute__((dllexport)) BOOL TextOutA(void *hdc, int x, int y, LPCSTR str, int len) {
    (void)hdc;(void)x;
    ensure_fb();
    if (!g_wnd.visible) return 0;
    if (len < 0) { len = 0; while (str[len]) len++; }
    if (y >= 0 && y < 40) {
        int n = len < 79 ? len : 79;
        for (int i = 0; i < n; i++) g_lines[y][i] = str[i];
        g_lines[y][n] = 0;
        if (y >= g_nlines) g_nlines = y + 1;
    }
    paint_window();
    return 1;
}
__attribute__((dllexport)) BOOL InvalidateRect(void *hwnd, void *rc, BOOL erase) {
    (void)hwnd;(void)rc;(void)erase; paint_window(); return 1;
}

typedef struct {
    u32 hwnd, message; u64 wParam; i64 lParam; u32 time; int pt_x, pt_y;
} MSG;

__attribute__((dllexport))
BOOL GetMessageA(MSG *msg, void *hwnd, u32 min, u32 max) {
    (void)hwnd;(void)min;(void)max;
    ensure_fb();
    for (;;) {
        InputEvent ev = {0};
        if (sy3(SYS_GetInput, (u64)&ev, 0, 0)) {
            if (ev.type == 1) {
                msg->message = 0x0100;
                msg->wParam = (u64)(unsigned char)ev.key;
                return 1;
            }
            if (ev.type == 2 || ev.type == 3) {
                if (ev.x != g_mx || ev.y != g_my)
                    cursor_draw(ev.x, ev.y);
                g_mx = ev.x; g_my = ev.y;
                msg->message = (ev.type == 3) ? 0x0201 : 0x0200;
                msg->pt_x = ev.x; msg->pt_y = ev.y;
                return 1;
            }
        }
        __asm__ volatile("pause");
    }
}
__attribute__((dllexport)) BOOL TranslateMessage(const MSG *m) { (void)m; return 1; }
__attribute__((dllexport)) i64 DispatchMessageA(const MSG *m) { (void)m; return 0; }
__attribute__((dllexport))
int MessageBoxA(void *hwnd, LPCSTR text, LPCSTR caption, u32 type) {
    (void)hwnd;(void)type;
    sy3(SYS_Write, 1, (u64)"[MessageBox] ", 13);
    if (caption) { u64 n=0; while(caption[n])n++; sy3(SYS_Write,1,(u64)caption,n); sy3(SYS_Write,1,(u64)": ",2); }
    if (text) { u64 n=0; while(text[n])n++; sy3(SYS_Write,1,(u64)text,n); }
    sy3(SYS_Write, 1, (u64)"\n", 1);
    return 1;
}
__attribute__((dllexport)) BOOL DestroyWindow(void *hwnd) { (void)hwnd; g_quit = 1; return 1; }
__attribute__((dllexport)) BOOL PostQuitMessage(int code) { (void)code; g_quit = 1; return 1; }
