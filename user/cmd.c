
#include "ntdll_user.h"

typedef unsigned int u32;
typedef int BOOL;

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
typedef int (*TextOutA_t)(void *, int, int, const char *, int);
typedef int (*InvalidateRect_t)(void *, void *, int);

static void *g_window;
static TextOutA_t g_text;
static InvalidateRect_t g_invalidate;
static char g_lines[20][80];
static int g_line_count;

static void print_line(const char *s) {
    if (g_line_count >= 20) {
        for (int i = 1; i < 20; i++) {
            for (int j = 0; j < 80; j++)
                g_lines[i - 1][j] = g_lines[i][j];
        }
        g_line_count = 19;
    }

    int i = 0;
    while (s[i] && i < 78) {
        g_lines[g_line_count][i] = s[i];
        i++;
    }
    g_lines[g_line_count][i] = 0;
    g_line_count++;

    if (g_text) {
        for (int n = 0; n < g_line_count; n++)
            g_text(g_window, 0, n, g_lines[n], -1);
    }
    if (g_invalidate)
        g_invalidate(g_window, 0, 1);
}

static int equals(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == 0 && b[i] == 0;
}

static void run_command(const char *cmd) {
    if (equals(cmd, "help")) {
        print_line("Commands:");
        print_line("  help       List commands");
        print_line("  ver        Show OS version");
        print_line("  clear      Clear the screen");
        print_line("  ipconfig   Show network status");
        print_line("  exit       Close CMD");
    } else if (equals(cmd, "ver")) {
        print_line("SentinelOS [experimental x86-64]");
    } else if (equals(cmd, "ipconfig") ||
               equals(cmd, "ipconfig /all")) {
        print_line("Network diagnostics:");
        print_line("No network configuration API yet.");
        print_line("A working NIC driver and network stack");
        print_line("are required for real IP information.");
    } else if (equals(cmd, "clear")) {
        g_line_count = 0;
    } else if (equals(cmd, "exit")) {
        NtExit(0);
    } else if (cmd[0]) {
        print_line("Unknown command. Type help.");
    }
}

void mainCRTStartup(void) {
    char u32name[] = "user32.dll";

    CreateWindowExA_t create =
        (CreateWindowExA_t)NtGetProcAddress(
            u32name, "CreateWindowExA");
    ShowWindow_t show =
        (ShowWindow_t)NtGetProcAddress(
            u32name, "ShowWindow");
    GetMessageA_t getmsg =
        (GetMessageA_t)NtGetProcAddress(
            u32name, "GetMessageA");
    g_text = (TextOutA_t)NtGetProcAddress(
        u32name, "TextOutA");
    g_invalidate = (InvalidateRect_t)NtGetProcAddress(
        u32name, "InvalidateRect");

    if (!create || !show || !getmsg || !g_text) {
        NtWriteConsole("cmd: required user32 APIs missing\n");
        NtExit(1);
    }

    g_window = create(
        0, "CMD", "SentinelOS CMD",
        0, 120, 70, 680, 420,
        0, 0, 0, 0);

    show(g_window, 1);

    print_line("SentinelOS Command Prompt");
    print_line("Type help for available commands.");
    print_line("");

    /*
     * Input editing and Enter handling will be connected
     * after confirming this process can open its window.
     */
    MSG msg;
    for (;;) {
        if (!getmsg(&msg, 0, 0, 0))
            continue;
    }
}
