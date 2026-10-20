
#include "ntdll_user.h"

typedef unsigned int u32;
typedef long long i64;

typedef struct {
    u32 hwnd, message;
    u64 wParam;
    i64 lParam;
    u32 time;
    int pt_x, pt_y;
} MSG;

typedef void *(*CreateWindowExA_t)(
    u32, const char *, const char *, u32,
    int, int, int, int,
    void *, void *, void *, void *
);
typedef int (*ShowWindow_t)(void *, int);
typedef int (*GetMessageA_t)(MSG *, void *, u32, u32);
typedef int (*TranslateMessage_t)(const MSG *);
typedef i64 (*DispatchMessageA_t)(const MSG *);
typedef int (*TextOutA_t)(void *, int, int, const char *, int);
typedef void (*DrawTaskbar_t)(void);
typedef int (*InvalidateRect_t)(void *, void *, int);
typedef int (*UpdateWindow_t)(void *);
typedef void (*BeginRedrawBatch_t)(void);
typedef void (*EndRedrawBatch_t)(void);

static CreateWindowExA_t pCreate;
static ShowWindow_t pShow;
static GetMessageA_t pGet;
static TranslateMessage_t pTrans;
static DispatchMessageA_t pDisp;
static TextOutA_t pText;
static DrawTaskbar_t pBar;
static InvalidateRect_t pInv;
static UpdateWindow_t pUpd;
static BeginRedrawBatch_t pBeginBatch;
static EndRedrawBatch_t pEndBatch;

static char g_search[32];
static int g_search_len;
static int g_launched;

static int hit_icon(int x, int y) {
    return x >= 28 && x < 100 && y >= 64 && y < 150;
}

static int equals_ci(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;

        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';

        if (ca != cb) return 0;
    }

    return *a == 0 && *b == 0;
}

static void redraw_desktop(void **hwnd) {
    if (pBeginBatch) pBeginBatch();
    *hwnd = pCreate
        ? pCreate(0, "Desktop", "Desktop", 0,
                  0, 0, 1, 1, 0, 0, 0, 0)
        : 0;

    if (pShow)
        pShow(*hwnd, 1);

    if (pText) {
        pText(0, 0, 4, "Welcome to SentinelOS", -1);
        pText(0, 0, 5, "A fresh breeze for your desktop", -1);
        pText(0, 0, 7, "Search apps below: type cmd and press Enter", -1);
        pText(0, 0, 8, "Search: ", -1);
    }

    if (pBar) pBar();
    if (pInv) pInv(0, 0, 1);
    if (pUpd) pUpd(*hwnd);
    if (pEndBatch) pEndBatch();
}

static void draw_search(void *hwnd) {
    char line[64];
    const char prefix[] = "Search: ";
    int i = 0;

    while (prefix[i]) {
        line[i] = prefix[i];
        i++;
    }

    for (int j = 0; j < g_search_len && i < 60; j++)
        line[i++] = g_search[j];

    line[i] = 0;

    if (pText) pText(hwnd, 0, 8, line, -1);
    if (pInv) pInv(hwnd, 0, 1);
    if (pUpd) pUpd(hwnd);
}

static void launch_app(const char *name, void **hwnd) {
    if (g_launched)
        return;

    g_launched = 1;
    NtWriteConsole("explorer: launching application\n");

    u64 pid = (u64)NtCreateProcess(name);

    if (pid == 0 || pid == (u64)-1) {
        NtWriteConsole("explorer: application launch failed\n");
        if (pText)
            pText(*hwnd, 0, 9, "Could not launch application.", -1);
        if (pInv) pInv(*hwnd, 0, 1);
        if (pUpd) pUpd(*hwnd);
    } else {
        g_search_len = 0;
        g_search[0] = 0;
        redraw_desktop(hwnd);
    }

    g_launched = 0;
}

static void handle_search_key(MSG *msg, void **hwnd) {
    if (msg->message != 0x0100)
        return;

    char c = (char)(msg->wParam & 0xFF);

    if (c == '\b') {
        if (g_search_len > 0) {
            g_search[--g_search_len] = 0;
            draw_search(*hwnd);
        }
        return;
    }

    if (c == '\n' || c == '\r') {
        if (equals_ci(g_search, "cmd")) {
            launch_app("cmd.exe", hwnd);
        } else if (g_search_len != 0) {
            if (pText)
                pText(*hwnd, 0, 9, "App not found. Try cmd.", -1);
            if (pInv) pInv(*hwnd, 0, 1);
            if (pUpd) pUpd(*hwnd);
        }
        return;
    }

    if (c >= 32 && c <= 126 && g_search_len < 30) {
        g_search[g_search_len++] = c;
        g_search[g_search_len] = 0;
        draw_search(*hwnd);
    }
}

void explorer_main(void) {
    NtWriteConsole("explorer: Frutiger Aero desktop shell\n");

    char u32n[] = {
        'u','s','e','r','3','2','.','d','l','l',0
    };

    pCreate = (CreateWindowExA_t)NtGetProcAddress(u32n, "CreateWindowExA");
    pShow   = (ShowWindow_t)NtGetProcAddress(u32n, "ShowWindow");
    pGet    = (GetMessageA_t)NtGetProcAddress(u32n, "GetMessageA");
    pTrans  = (TranslateMessage_t)NtGetProcAddress(u32n, "TranslateMessage");
    pDisp   = (DispatchMessageA_t)NtGetProcAddress(u32n, "DispatchMessageA");
    pText   = (TextOutA_t)NtGetProcAddress(u32n, "TextOutA");
    pBar    = (DrawTaskbar_t)NtGetProcAddress(u32n, "DrawTaskbar");
    pInv    = (InvalidateRect_t)NtGetProcAddress(u32n, "InvalidateRect");
    pUpd    = (UpdateWindow_t)NtGetProcAddress(u32n, "UpdateWindow");
    pBeginBatch = (BeginRedrawBatch_t)NtGetProcAddress(u32n, "BeginRedrawBatch");
    pEndBatch = (EndRedrawBatch_t)NtGetProcAddress(u32n, "EndRedrawBatch");

    if (!pGet) {
        NtWriteConsole("explorer: no user32 keyboard API\n");
        for (;;) __asm__ volatile("pause");
    }

    void *hwnd = 0;
    redraw_desktop(&hwnd);

    NtWriteConsole("explorer: glass desktop ready; search supports cmd\n");

    MSG msg;

    for (;;) {
        if (!pGet(&msg, 0, 0, 0))
            continue;

        if (pTrans) pTrans(&msg);
        if (pDisp) pDisp(&msg);

        handle_search_key(&msg, &hwnd);

        if (msg.message == 0x0201 &&
            !g_launched &&
            hit_icon(msg.pt_x, msg.pt_y)) {
            launch_app("fileexplorer.exe", &hwnd);
        }
    }
}
