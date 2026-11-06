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
typedef u64 (*SetTimer_t)(void *, u64, u32, void *);
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
static SetTimer_t pSetTimer;
static UpdateWindow_t pUpd;
static BeginRedrawBatch_t pBeginBatch;
static EndRedrawBatch_t pEndBatch;

static int g_launched;

typedef struct { int x, y, w, h; const char *label; const char *app; } DesktopIcon;

/* Desktop icons drawn on the desktop surface. Each icon occupies a fixed
 * cell; clicking inside the cell launches the associated application. */
static const DesktopIcon g_desktop_icons[] = {
    { 28, 64, 72, 86, "File Explorer", "fileexplorer.exe" },
    { 108, 64, 72, 86, "Task Manager", "taskmgr.exe" },
    { 188, 64, 72, 86, "Settings", "settings.exe" },
    { 268, 64, 72, 86, "Command Prompt", "cmd.exe" },
};
#define DESKTOP_ICON_COUNT (sizeof(g_desktop_icons) / sizeof(g_desktop_icons[0]))

static int find_desktop_icon(int x, int y) {
    for (unsigned int i = 0; i < DESKTOP_ICON_COUNT; i++) {
        if (x >= g_desktop_icons[i].x && x < g_desktop_icons[i].x + g_desktop_icons[i].w &&
            y >= g_desktop_icons[i].y && y < g_desktop_icons[i].y + g_desktop_icons[i].h)
            return (int)i;
    }
    return -1;
}

static void redraw_desktop(void **hwnd) {
    if (pBeginBatch) pBeginBatch();
    *hwnd = pCreate
        ? pCreate(0, "Desktop", "Desktop", 0,
                  0, 0, 800, 600, 0, 0, 0, 0)
        : 0;

    if (pShow)
        pShow(*hwnd, 1);

    if (pText) {
        pText(0, 0, 4, "Welcome to SentinelOS", -1);
        pText(0, 0, 5, "Your workspace, ready when you are", -1);
        /* Draw desktop icons with labels. */
        for (unsigned int i = 0; i < DESKTOP_ICON_COUNT; i++) {
            const DesktopIcon *ic = &g_desktop_icons[i];
            pText(0, ic->x, ic->y, "[APP]", -1);
            pText(0, ic->x, ic->y + 12, ic->label, -1);
        }
    }

    if (pBar) pBar();
    /* EndRedrawBatch performs the one initial desktop composition. */
    if (pUpd) pUpd(*hwnd);
    if (pEndBatch) pEndBatch();
}

static void launch_app(const char *name, void **hwnd) {
    if (g_launched)
        return;

    g_launched = 1;
    NtWriteConsole("explorer: launching application\n");

    u64 pid = (u64)NtCreateProcess(name);

    if (pid == 0 || pid == (u64)-1 || pid == (u64)-2) {
        NtWriteConsole("explorer: application launch failed\n");
        if (pText)
            pText(*hwnd, 0, 9, "Could not launch application.", -1);
        if (pUpd) pUpd(*hwnd);
    }

    /* Full layer refresh so the desktop, windows, and taskbar are all
     * re-presented after launching an app. EndRedrawBatch runs a full
     * compose that re-lists every live window from the shared kernel
     * wm_tab, so the new app's taskbar button appears. DrawTaskbar is
     * then called to re-present the taskbar layer (layer 3) on top of
     * any app windows that the application process may have presented
     * after Explorer's compose, ensuring the taskbar stays visible. */
    if (pBeginBatch) pBeginBatch();
    if (pEndBatch) pEndBatch();
    if (pBar) pBar();

    /* compose() clears the desktop layer, so restore the welcome text
     * and desktop icons that redraw_desktop() originally drew. */
    if (pText) {
        pText(0, 0, 4, "Welcome to SentinelOS", -1);
        pText(0, 0, 5, "Your workspace, ready when you are", -1);
        for (unsigned int i = 0; i < DESKTOP_ICON_COUNT; i++) {
            const DesktopIcon *ic = &g_desktop_icons[i];
            pText(0, ic->x, ic->y, "[APP]", -1);
            pText(0, ic->x, ic->y + 12, ic->label, -1);
        }
    }

    g_launched = 0;
}

void explorer_main(void) {
    NtWriteConsole("explorer: modern glass desktop shell\n");

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
    pSetTimer = (SetTimer_t)NtGetProcAddress(u32n, "SetTimer");
    pUpd    = (UpdateWindow_t)NtGetProcAddress(u32n, "UpdateWindow");
    pBeginBatch = (BeginRedrawBatch_t)NtGetProcAddress(u32n, "BeginRedrawBatch");
    pEndBatch = (EndRedrawBatch_t)NtGetProcAddress(u32n, "EndRedrawBatch");

    if (!pCreate || !pShow || !pGet || !pText) {
        NtWriteConsole("explorer: required USER32 window or keyboard API is unavailable\n");
        for (;;) __asm__ volatile("pause");
    }

    void *hwnd = 0;
    redraw_desktop(&hwnd);
    if (!hwnd) {
        NtWriteConsole("explorer: could not create the desktop window\n");
        for (;;) __asm__ volatile("pause");
    }
    /* Repaint the taskbar layer frequently so it stays on top of app windows
       that the application process presents to scanout between Explorer
       messages. 200 ms keeps the taskbar visible without a busy loop. */
    if (pSetTimer && hwnd) (void)pSetTimer(hwnd, 1, 200, 0);

    NtWriteConsole("explorer: modern desktop ready; search supports cmd and taskmgr\n");

    MSG msg;

    for (;;) {
        if (!pGet(&msg, 0, 0, 0))
            continue;

        if (pTrans) pTrans(&msg);
        if (pDisp) pDisp(&msg);
        if (msg.message == 0x0113 && pBar) pBar(); /* WM_TIMER: refresh clock/date */

        if (msg.message == 0x0201 && !g_launched) {
            int idx = find_desktop_icon(msg.pt_x, msg.pt_y);
            if (idx >= 0)
                launch_app(g_desktop_icons[idx].app, &hwnd);
        }

        /* The taskbar is refreshed by the launch path above and by the
         * WM_TIMER tick. Re-composing the whole shell on every mouse or key
         * message made Explorer fight application windows for the screen. */
    }
}