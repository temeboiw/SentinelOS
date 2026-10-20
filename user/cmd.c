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

#define LINE_COUNT 20
#define LINE_SIZE  80
#define CMD_SIZE   96
#define DIR_MAX    16
#define FILE_MAX   32
#define PATH_SIZE  64
#define DATA_SIZE  256

typedef struct { int left, top, right, bottom; } CMD_RECT;

typedef struct {
    char path[PATH_SIZE];
    int used;
} ShellDir;

typedef struct {
    char path[PATH_SIZE];
    char data[DATA_SIZE];
    int used;
} ShellFile;

static void *g_window;
static TextOutA_t g_text;
static InvalidateRect_t g_invalidate;
static char g_lines[LINE_COUNT][LINE_SIZE];
static int g_line_count;
static char g_command[CMD_SIZE];
static int g_command_len;
static ShellDir g_dirs[DIR_MAX];
static ShellFile g_files[FILE_MAX];
static char g_cwd[PATH_SIZE];

static int str_len(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int equals(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return 0;
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static int starts_with(const char *s, const char *prefix) {
    int i = 0;
    while (prefix[i]) {
        if (s[i] != prefix[i]) return 0;
        i++;
    }
    return 1;
}

static void str_copy(char *dst, const char *src, int cap) {
    int i = 0;
    if (cap <= 0) return;
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void trim_right(char *s) {
    int n = str_len(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = 0;
}

static void print_line(const char *s) {
    if (g_line_count >= LINE_COUNT) {
        for (int i = 1; i < LINE_COUNT; i++)
            for (int j = 0; j < LINE_SIZE; j++)
                g_lines[i - 1][j] = g_lines[i][j];
        g_line_count = LINE_COUNT - 1;
    }

    str_copy(g_lines[g_line_count], s, LINE_SIZE);
    g_line_count++;
    if (g_text) {
        for (int n = 0; n < g_line_count; n++)
            g_text(g_window, 0, n, g_lines[n], -1);
    }
    if (g_invalidate) g_invalidate(g_window, 0, 1);
}

static void redraw_prompt(void) {
    char line[LINE_SIZE];
    int n = 0;
    const char *prefix = "C:\\>";
    while (prefix[n] && n < LINE_SIZE - 1) {
        line[n] = prefix[n];
        n++;
    }
    for (int i = 0; g_command[i] && n < LINE_SIZE - 1; i++)
        line[n++] = g_command[i];
    line[n] = 0;

    if (g_text) g_text(g_window, 0, g_line_count, line, -1);
    /* Typing changes only the prompt row. Do not invalidate the entire client
       area: USER32 composes the desktop behind every invalidated pixel. */
    if (g_invalidate) {
        CMD_RECT dirty = { 0, 44 + g_line_count * 16,
                           680, 44 + (g_line_count + 1) * 16 };
        g_invalidate(g_window, &dirty, 0);
    }
}

/* Canonicalize a shell path into a root-relative path using backslashes. */
static int normalize_path(const char *input, char *out, int cap) {
    int i = 0, n = 0;
    if (!input || !input[0] || cap < 2) return -1;

    if ((input[0] == 'C' || input[0] == 'c') &&
        input[1] == ':' && (input[2] == '\\' || input[2] == '/'))
        i = 3;
    else if (input[0] == '\\' || input[0] == '/')
        i = 1;

    while (input[i] == '\\' || input[i] == '/') i++;

    while (input[i]) {
        char c = input[i++];
        if (c == '/') c = '\\';
        if (c == '\\') {
            if (n == 0 || out[n - 1] == '\\') continue;
        }
        if (n >= cap - 1) return -1;
        out[n++] = c;
    }
    while (n > 0 && out[n - 1] == '\\') n--;
    out[n] = 0;
    return n ? 0 : -1;
}

static int dir_exists(const char *path) {
    if (!path[0]) return 1; /* C:\ root */
    for (int i = 0; i < DIR_MAX; i++)
        if (g_dirs[i].used && equals(g_dirs[i].path, path))
            return 1;
    return 0;
}

static int file_index(const char *path) {
    for (int i = 0; i < FILE_MAX; i++)
        if (g_files[i].used && equals(g_files[i].path, path))
            return i;
    return -1;
}

static int parent_path(const char *path, char *parent, int cap) {
    int n = str_len(path), slash = -1;
    for (int i = 0; i < n; i++)
        if (path[i] == '\\') slash = i;
    if (slash < 0) {
        parent[0] = 0;
        return 0;
    }
    if (slash >= cap) return -1;
    for (int i = 0; i < slash; i++) parent[i] = path[i];
    parent[slash] = 0;
    return 0;
}

static void command_mkdir(const char *arg) {
    char path[PATH_SIZE], parent[PATH_SIZE];
    if (normalize_path(arg, path, sizeof path) != 0) {
        print_line("Usage: mkdir <folder>");
        return;
    }
    if (dir_exists(path) || file_index(path) >= 0) {
        print_line("A file or directory already exists.");
        return;
    }
    if (parent_path(path, parent, sizeof parent) != 0 ||
        !dir_exists(parent)) {
        print_line("Parent directory not found.");
        return;
    }
    for (int i = 0; i < DIR_MAX; i++) {
        if (!g_dirs[i].used) {
            str_copy(g_dirs[i].path, path, PATH_SIZE);
            g_dirs[i].used = 1;
            print_line("Directory created.");
            return;
        }
    }
    print_line("Directory table full.");
}

static void command_echo(const char *arg) {
    char text[DATA_SIZE], path[PATH_SIZE], parent[PATH_SIZE];
    int i = 0, out = 0, gt = -1;
    while (arg[i]) {
        if (arg[i] == '>' && (i == 0 || arg[i - 1] == ' ')) {
            gt = i;
            break;
        }
        i++;
    }
    if (gt < 0) {
        print_line(arg);
        return;
    }

    while (gt > 0 && arg[gt - 1] == ' ') gt--;
    for (i = 0; i < gt && out < DATA_SIZE - 1; i++)
        text[out++] = arg[i];
    text[out] = 0;

    i = gt;
    while (arg[i] && arg[i] != '>') i++;
    if (arg[i] == '>') i++;
    while (arg[i] == ' ') i++;

    char raw[PATH_SIZE];
    out = 0;
    while (arg[i] && arg[i] != ' ' && out < PATH_SIZE - 1)
        raw[out++] = arg[i++];
    raw[out] = 0;

    if (!raw[0] || normalize_path(raw, path, sizeof path) != 0) {
        print_line("Usage: echo <text> > <path>");
        return;
    }
    if (parent_path(path, parent, sizeof parent) != 0 ||
        !dir_exists(parent)) {
        print_line("Parent directory not found.");
        return;
    }

    int fi = file_index(path);
    if (fi < 0) {
        for (fi = 0; fi < FILE_MAX; fi++)
            if (!g_files[fi].used) break;
        if (fi == FILE_MAX) {
            print_line("File table full.");
            return;
        }
        g_files[fi].used = 1;
        str_copy(g_files[fi].path, path, PATH_SIZE);
    }

    str_copy(g_files[fi].data, text, DATA_SIZE);
    print_line("File created.");
}

static void command_cat(const char *arg) {
    char path[PATH_SIZE];
    if (normalize_path(arg, path, sizeof path) != 0) {
        print_line("Usage: cat <file>");
        return;
    }
    int fi = file_index(path);
    if (fi < 0) {
        print_line("File not found.");
        return;
    }
    if (!g_files[fi].data[0]) {
        print_line("");
        return;
    }

    /* Print contents as lines, keeping the terminal output bounded. */
    const char *s = g_files[fi].data;
    char line[LINE_SIZE];
    while (*s) {
        int n = 0;
        while (*s && *s != '\n' && n < LINE_SIZE - 1)
            line[n++] = *s++;
        line[n] = 0;
        print_line(line);
        if (*s == '\n') s++;
    }
}

static void command_dir(void) {
    print_line("Directory of C:\\");
    for (int i = 0; i < DIR_MAX; i++) {
        if (g_dirs[i].used) {
            char line[LINE_SIZE];
            str_copy(line, "<DIR> ", sizeof line);
            int n = str_len(line);
            str_copy(line + n, g_dirs[i].path, LINE_SIZE - n);
            print_line(line);
        }
    }
    for (int i = 0; i < FILE_MAX; i++) {
        if (g_files[i].used) {
            char line[LINE_SIZE];
            str_copy(line, "       ", sizeof line);
            int n = str_len(line);
            str_copy(line + n, g_files[i].path, LINE_SIZE - n);
            print_line(line);
        }
    }
}

static void run_command(const char *raw) {
    char cmd[CMD_SIZE], *arg;
    str_copy(cmd, raw, sizeof cmd);
    trim_right(cmd);
    if (!cmd[0]) return;

    arg = cmd;
    while (*arg && *arg != ' ') arg++;
    if (*arg) *arg++ = 0;
    while (*arg == ' ') arg++;

    if (equals(cmd, "help")) {
        print_line("Commands: help, ver, ipconfig, cls, dir, mkdir, echo, cat, exit");
        print_line("Example: mkdir test");
        print_line("echo Hello from SentinelOS > test\\hello.txt");
        print_line("cat test\\hello.txt");
    } else if (equals(cmd, "ver")) {
        print_line("SentinelOS [experimental x86-64]");
    } else if (equals(cmd, "ipconfig")) {
        print_line("SentinelOS IP Configuration");
        print_line("Ethernet adapter: no network interface registered");
        print_line("IPv4 address . . . : not configured");
        print_line("Subnet mask . . .  : none");
        print_line("Default gateway .  : none");
        print_line("DNS servers . . .  : none");
        print_line("DHCP . . . . . .  : unavailable (network driver/IP stack missing)");
    } else if (equals(cmd, "cls") || equals(cmd, "clear")) {
        g_line_count = 0;
        for (int i = 0; i < LINE_COUNT; i++) g_lines[i][0] = 0;
    } else if (equals(cmd, "dir")) {
        command_dir();
    } else if (equals(cmd, "mkdir")) {
        command_mkdir(arg);
    } else if (equals(cmd, "echo")) {
        command_echo(arg);
    } else if (equals(cmd, "cat")) {
        command_cat(arg);
    } else if (equals(cmd, "exit")) {
        NtExit(0);
    } else {
        print_line("Unknown command. Type help.");
    }
}

void mainCRTStartup(void) {
    char u32name[] = "user32.dll";

    CreateWindowExA_t create =
        (CreateWindowExA_t)NtGetProcAddress(u32name, "CreateWindowExA");
    ShowWindow_t show =
        (ShowWindow_t)NtGetProcAddress(u32name, "ShowWindow");
    GetMessageA_t getmsg =
        (GetMessageA_t)NtGetProcAddress(u32name, "GetMessageA");
    g_text = (TextOutA_t)NtGetProcAddress(u32name, "TextOutA");
    g_invalidate = (InvalidateRect_t)NtGetProcAddress(u32name, "InvalidateRect");

    if (!create || !show || !getmsg || !g_text) {
        NtWriteConsole("cmd: required user32 APIs missing\n");
        NtExit(1);
    }

    for (int i = 0; i < DIR_MAX; i++) g_dirs[i].used = 0;
    for (int i = 0; i < FILE_MAX; i++) g_files[i].used = 0;
    g_cwd[0] = 0;
    g_command_len = 0;
    g_command[0] = 0;

    g_window = create(0, "CMD", "SentinelOS CMD",
                      0, 120, 70, 680, 420, 0, 0, 0, 0);
    show(g_window, 1);

    print_line("SentinelOS Command Prompt");
    print_line("Type help for available commands.");
    print_line("");

    redraw_prompt();

    MSG msg;
    for (;;) {
        if (!getmsg(&msg, 0, 0, 0)) continue;
        if (msg.message != 0x0100) continue; /* WM_KEYDOWN */

        char key = (char)(msg.wParam & 0xFF);
        if (key == '\r' || key == '\n') {
            print_line("C:\\>");
            /* Replace the last prompt line with the entered command. */
            if (g_line_count > 0) {
                int n = str_len(g_lines[g_line_count - 1]);
                if (n + g_command_len < LINE_SIZE - 1) {
                    for (int i = 0; i < g_command_len; i++)
                        g_lines[g_line_count - 1][n++] = g_command[i];
                    g_lines[g_line_count - 1][n] = 0;
                }
                if (g_text)
                    g_text(g_window, 0, g_line_count - 1,
                           g_lines[g_line_count - 1], -1);
            }
            g_command[g_command_len] = 0;
            run_command(g_command);
            g_command_len = 0;
            g_command[0] = 0;
            redraw_prompt();
            continue;
        }

        if (key == '\b') {
            if (g_command_len > 0)
                g_command[--g_command_len] = 0;
            redraw_prompt();
            continue;
        }

        if (((unsigned char)key >= 32) && g_command_len < CMD_SIZE - 1) {
            g_command[g_command_len++] = key;
            g_command[g_command_len] = 0;
            redraw_prompt();
        }
    }
}
