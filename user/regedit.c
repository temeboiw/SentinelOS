/* regedit.exe - SentinelOS native Registry Editor (V11 SOVEREIGN)
 *
 * Text-grid (80x40) registry editor built on the fileexplorer/app model:
 *   mainCRTStartup entry, sy3(9, dll, fn, 0) GetProcAddress bootstrap,
 *   CreateWindowExA main window, TextOutA char-grid rendering,
 *   GetMessage/TranslateMessage/DispatchMessage loop.
 *
 * Registry access goes through user-mode advapi32.dll (resolved at runtime),
 * backed by the persistent hive file magic/System32/config/registry.hive.
 *
 * Compiled freestanding (clang -target x86_64-windows-gnu -ffreestanding -nodefaultlib),
 * linked with lld-link /subsystem:windows /entry:mainCRTStartup.
 *
 * Honest status: browse/create/delete/edit of keys and values is IMPLEMENTED.
 * .reg import/export, copy/paste, favorites and mouse menus are STUBBED and are
 * called out as such in the Section 13 status report.
 */

typedef unsigned long long u64;
typedef long long i64;
typedef unsigned int u32;
typedef int BOOL;
typedef unsigned long DWORD;
typedef unsigned char BYTE;
typedef DWORD *LPDWORD;
typedef unsigned char *LPBYTE;
typedef const char *LPCSTR;
typedef char *LPSTR;
typedef void *HANDLE;
typedef void *HKEY;
typedef int LONG;
typedef unsigned short WORD;
typedef unsigned short WCHAR;
typedef unsigned int UINT;

#define NULL 0
#define TRUE 1
#define FALSE 0

/* ---- registry constants (mirror advapi32.c) ---- */
#define ERROR_SUCCESS 0
#define ERROR_INVALID_PARAMETER 87
#define ERROR_INVALID_HANDLE 6
#define ERROR_ACCESS_DENIED 5
#define ERROR_NO_MORE_ITEMS 259
#define ERROR_MORE_DATA 234
#define ERROR_NOT_SUPPORTED 50
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_ALREADY_EXISTS 183
#define ERROR_BADKEY 1000
#define ERROR_CANTOPEN 1105
#define ERROR_CALL_NOT_IMPLEMENTED 120

#define REG_NONE 0
#define REG_SZ 1
#define REG_EXPAND_SZ 2
#define REG_BINARY 3
#define REG_DWORD 4
#define REG_DWORD_BIG_ENDIAN 5
#define REG_LINK 6
#define REG_MULTI_SZ 7
#define REG_QWORD 11

#define KEY_QUERY_VALUE 0x0001
#define KEY_SET_VALUE 0x0002
#define KEY_CREATE_SUB_KEY 0x0004
#define KEY_ENUMERATE_SUB_KEYS 0x0008
#define KEY_READ 0x00020019u
#define KEY_WRITE 0x00020006u
#define KEY_ALL_ACCESS 0x000f003fu

#define HKEY_CLASSES_ROOT       ((HKEY)(unsigned long long)0x80000000ULL)
#define HKEY_CURRENT_USER       ((HKEY)(unsigned long long)0x80000001ULL)
#define HKEY_LOCAL_MACHINE      ((HKEY)(unsigned long long)0x80000002ULL)
#define HKEY_USERS              ((HKEY)(unsigned long long)0x80000003ULL)
#define HKEY_CURRENT_CONFIG     ((HKEY)(unsigned long long)0x80000005ULL)

/* ---- window/message constants ---- */
#define WM_CLOSE 0x0010
#define WM_KEYDOWN 0x0100
#define WM_CHAR 0x0102
#define VK_ESCAPE 27
#define VK_RETURN 13
#define VK_BACK 8
#define VK_TAB 9
#define VK_LEFT 37
#define VK_UP 38
#define VK_RIGHT 39
#define VK_DOWN 40

/* ---- layout (80x40 char grid) ---- */
enum {
    TITLE_ROW = 0,
    MENU_ROW = 1,
    ADDR_ROW = 3,
    HDR_ROW = 5,
    CONTENT_TOP = 6,
    CONTENT_BOTTOM = 32,
    STATUS_ROW = 34,
    HINT_ROW = 35,
    TREE_W = 34,
    DIALOG_TOP = 36,
    SCR_W = 80,
    SCR_H = 40,
    CONTENT_ROWS = 27
};

/* ---- syscalls ---- */
enum { SYS_Exit = 0, SYS_Write = 1, SYS_GetProc = 9, SYS_OpenFile = 5,
       SYS_ReadFile = 6, SYS_CloseFile = 22, SYS_QueryFileSize = 25,
       SYS_SetFilePosition = 26, SYS_CreateOutputFile = 51, SYS_WriteOutputFile = 52 };

static u64 sy3(u64 n, u64 a, u64 b, u64 c) {
    u64 r;
    __asm__ volatile(
        "mov %[n], %%rax\nmov %[a], %%rdi\nmov %[b], %%rsi\nmov %[c], %%rdx\nint $0x80\nmov %%rax, %[r]"
        : [r]"=r"(r) : [n]"r"(n),[a]"r"(a),[b]"r"(b),[c]"r"(c)
        : "rax","rdi","rsi","rdx","memory","cc");
    return r;
}
static void *gpa(const char *dll, const char *fn) {
    return (void *)sy3(SYS_GetProc, (u64)dll, (u64)fn, 0);
}
static void print(const char *s) {
    u64 n = 0; while (s[n]) n++;
    sy3(SYS_Write, 1, (u64)s, n);
}

/* ---------- Lato font ---------- */
/* regedit renders text through user32's TextOutA, which draws glyphs using
 * Lato-Regular.ttf when user32's TTF loader succeeded, else the font8x8
 * bitmap font.  regedit explicitly loads the Lato asset here so the
 * dependency is declared and the font is available at startup.  If the
 * asset cannot be loaded, rendering silently falls back to font8x8. */
#define LATO_PATH "assets/fonts/Lato-Regular.ttf"
#define LATO_MAX_SIZE (768u * 1024u)
static unsigned char g_lato_data[LATO_MAX_SIZE];
static int g_lato_loaded = 0;

static void lato_load(void) {
    u64 h = sy3(SYS_OpenFile, (u64)LATO_PATH, 0, 0);
    if (!h) return;
    u64 n = sy3(SYS_ReadFile, h, (u64)g_lato_data, LATO_MAX_SIZE);
    sy3(SYS_CloseFile, h, 0, 0);
    if (n > 0 && n <= LATO_MAX_SIZE) g_lato_loaded = 1;
}

/* ---- C library substitutes (freestanding) ---- */
static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void str_copy(char *d, const char *s) { int i = 0; while (s[i]) { d[i] = s[i]; i++; } d[i] = 0; }
static int str_cmp(const char *a, const char *b) { int i = 0; while (a[i] && a[i] == b[i]) i++; return (unsigned char)a[i] - (unsigned char)b[i]; }
static int str_cmpi(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) {
        char x = a[i] >= 'A' && a[i] <= 'Z' ? a[i] + 32 : a[i];
        char y = b[i] >= 'A' && b[i] <= 'Z' ? b[i] + 32 : b[i];
        if (x != y) return (unsigned char)x - (unsigned char)y;
        i++;
    }
    return (unsigned char)a[i] - (unsigned char)b[i];
}
static int str_starts(const char *s, const char *pre) {
    int i = 0; while (pre[i]) { if (s[i] != pre[i]) return 0; i++; } return 1;
}
static void str_cat(char *d, const char *s) { int n = str_len(d); int i = 0; while (s[i]) { d[n++] = s[i]; i++; } d[n] = 0; }
static void mem_set(void *p, int v, u64 n) { unsigned char *b = (unsigned char *)p; while (n--) *b++ = (unsigned char)v; }
static void mem_copy(void *d, const void *s, u64 n) { unsigned char *a = (unsigned char *)d; const unsigned char *b = (const unsigned char *)s; while (n--) *a++ = *b++; }
static int is_digit(char c) { return c >= '0' && c <= '9'; }

static void u32_to_hex(u32 v, char *out) {
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) { out[i] = hex[v & 15]; v >>= 4; }
    out[8] = 0;
}
static void u64_to_hex(u64 v, char *out) {
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 15; i >= 0; i--) { out[i] = hex[v & 15]; v >>= 4; }
    out[16] = 0;
}
static void u32_to_dec(u32 v, char *out) {
    char tmp[12]; int i = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    int o = 0;
    while (i > 0) out[o++] = tmp[--i];
    out[o] = 0;
}

/* ---- user32 API (resolved) ---- */
typedef void *(*CreateWindowExA_t)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, void *, void *, void *, void *);
typedef BOOL (*ShowWindow_t)(void *, int);
typedef BOOL (*UpdateWindow_t)(void *);
typedef BOOL (*TextOutA_t)(void *, int, int, LPCSTR, int);
typedef BOOL (*GetMessageA_t)(void *, void *, DWORD, DWORD);
typedef BOOL (*TranslateMessage_t)(const void *);
typedef long long (*DispatchMessageA_t)(const void *);
typedef int (*MessageBoxA_t)(void *, LPCSTR, LPCSTR, DWORD);
typedef BOOL (*InvalidateRect_t)(void *, void *, BOOL);
typedef void (*SetWindowDecorA_t)(void *, int, int, int, int, int, int, unsigned int, unsigned int, int, const char *);
typedef void (*BeginRedrawBatch_t)(void);
typedef void (*EndRedrawBatch_t)(void);
typedef struct { int left, top, right, bottom; } RECT;
typedef BOOL (*GetWindowRect_t)(void *, RECT *);

typedef struct {
    u32 hwnd, message;
    u64 wParam;
    i64 lParam;
    u32 time;
    int pt_x, pt_y;
} MSG;

/* ---- advapi32 API (resolved) ---- */
typedef LONG (*RegOpenKeyExA_t)(HKEY, LPCSTR, DWORD, DWORD, HKEY *);
typedef LONG (*RegCreateKeyExA_t)(HKEY, LPCSTR, DWORD, LPSTR, DWORD, DWORD, void *, HKEY *, DWORD *);
typedef LONG (*RegDeleteKeyA_t)(HKEY, LPCSTR);
typedef LONG (*RegDeleteTreeA_t)(HKEY, LPCSTR);
typedef LONG (*RegDeleteValueA_t)(HKEY, LPCSTR);
typedef LONG (*RegCloseKey_t)(HKEY);
typedef LONG (*RegFlushKey_t)(HKEY);
typedef LONG (*RegEnumKeyExA_t)(HKEY, DWORD, LPSTR, LPDWORD, LPDWORD, LPSTR, LPDWORD, void *);
typedef LONG (*RegEnumValueA_t)(HKEY, DWORD, LPSTR, LPDWORD, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
typedef LONG (*RegQueryValueExA_t)(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
typedef LONG (*RegSetValueExA_t)(HKEY, LPCSTR, DWORD, DWORD, const BYTE *, DWORD);
typedef LONG (*RegQueryInfoKeyA_t)(HKEY, LPSTR, LPDWORD, LPDWORD, LPDWORD, LPDWORD, LPDWORD, LPDWORD, LPDWORD, LPDWORD, void *, void *);
typedef LONG (*RegCopyTreeA_t)(HKEY, LPCSTR, HKEY, LPCSTR, DWORD);
typedef LONG (*RegConnectRegistryA_t)(LPCSTR, HKEY, HKEY *);
typedef LONG (*RegNotifyChangeKeyValue_t)(HKEY, BOOL, DWORD, void *, BOOL);

/* ---- resolved function pointers ---- */
static void *g_window;
static CreateWindowExA_t pCreate;
static ShowWindow_t pShow;
static UpdateWindow_t pUpdate;
static TextOutA_t pText;
static GetMessageA_t pGetMsg;
static TranslateMessage_t pTrans;
static DispatchMessageA_t pDisp;
static MessageBoxA_t pMsgBox;
static InvalidateRect_t pInv;
static SetWindowDecorA_t pDecor;
static BeginRedrawBatch_t pBeginBatch;
static EndRedrawBatch_t pEndBatch;
static GetWindowRect_t pGetRect;

static RegOpenKeyExA_t pRegOpenKeyExA;
static RegCreateKeyExA_t pRegCreateKeyExA;
static RegDeleteKeyA_t pRegDeleteKeyA;
static RegDeleteTreeA_t pRegDeleteTreeA;
static RegDeleteValueA_t pRegDeleteValueA;
static RegCloseKey_t pRegCloseKey;
static RegFlushKey_t pRegFlushKey;
static RegEnumKeyExA_t pRegEnumKeyExA;
static RegEnumValueA_t pRegEnumValueA;
static RegQueryValueExA_t pRegQueryValueExA;
static RegSetValueExA_t pRegSetValueExA;
static RegQueryInfoKeyA_t pRegQueryInfoKeyA;
static RegCopyTreeA_t pRegCopyTreeA;
static RegConnectRegistryA_t pRegConnectRegistryA;
static RegNotifyChangeKeyValue_t pRegNotifyChangeKeyValue;

static int apis_ok;
/* Live window origin, refreshed on mouse input so button hit-tests stay
 * window-relative even if USER32 moves the window. */
static int g_win_x = 0, g_win_y = 0;

/* ---------- tiny screen buffer (80x40 char grid) ---------- */
static char g_scr[SCR_H][SCR_W + 1];

static void scr_clear(void) {
    for (int r = 0; r < SCR_H; r++) {
        for (int c = 0; c < SCR_W; c++) g_scr[r][c] = ' ';
        g_scr[r][SCR_W] = 0;
    }
}
static void scr_puts(int r, int c, const char *s) {
    if (r < 0 || r >= SCR_H) return;
    while (*s && c < SCR_W) g_scr[r][c++] = *s++;
}
static void scr_fill(int r, int c0, int c1, char ch) {
    if (r < 0 || r >= SCR_H) return;
    if (c0 < 0) c0 = 0;
    if (c1 > SCR_W) c1 = SCR_W;
    for (int c = c0; c < c1; c++) g_scr[r][c] = ch;
}
static void scr_draw(void) {
    for (int r = 0; r < SCR_H; r++)
        pText(g_window, 0, r, g_scr[r], SCR_W);
}

/* ---------- paths ---------- */
static HKEY path_root_handle(const char *p) {
    if (str_starts(p, "HKEY_CLASSES_ROOT\\")) return HKEY_CLASSES_ROOT;
    if (str_starts(p, "HKEY_CURRENT_USER\\")) return HKEY_CURRENT_USER;
    if (str_starts(p, "HKEY_LOCAL_MACHINE\\")) return HKEY_LOCAL_MACHINE;
    if (str_starts(p, "HKEY_USERS\\")) return HKEY_USERS;
    if (str_starts(p, "HKEY_CURRENT_CONFIG\\")) return HKEY_CURRENT_CONFIG;
    return 0;
}
static const char *path_relative(const char *p) {
    const char *s = p;
    while (*s && *s != '\\') s++;
    return (*s == '\\') ? s + 1 : "";
}
/* Open a full path like "HKEY_LOCAL_MACHINE\SOFTware\Foo". Returns handle (caller closes
 * unless it is one of the predefined roots). Intermediate handles are closed internally. */
static HKEY open_path(const char *fullpath, DWORD access) {
    HKEY root = path_root_handle(fullpath);
    if (!root) return 0;
    const char *rel = path_relative(fullpath);
    HKEY h = root;
    HKEY prev = root;
    while (*rel) {
        char comp[128]; int i = 0;
        while (*rel && *rel != '\\' && i < 127) comp[i++] = *rel++;
        comp[i] = 0;
        if (*rel == '\\') rel++;
        if (i == 0) continue;
        HKEY nh = 0;
        LONG r = pRegOpenKeyExA(h, comp, 0, access, &nh);
        if (r != ERROR_SUCCESS) r = pRegOpenKeyExA(h, comp, 0, KEY_READ, &nh);
        if (r != ERROR_SUCCESS) { if (h != root) pRegCloseKey(h); return 0; }
        if (prev != root) pRegCloseKey(prev);
        prev = h; h = nh;
    }
    if (h == root) return root;
    return h;
}
static void path_last(const char *path, char *out, int outsz) {
    const char *s = path + str_len(path);
    while (s > path && s[-1] != '\\') s--;
    int i = 0; while (*s && i < outsz - 1) { out[i++] = *s++; }
    out[i] = 0;
}
static void path_parent(const char *path, char *out, int outsz) {
    int n = str_len(path);
    while (n > 0 && path[n - 1] != '\\') n--;
    if (n > 0) n--;
    if (n < 0) n = 0;
    if (n >= outsz) n = outsz - 1;
    mem_copy(out, path, n);
    out[n] = 0;
}
static void path_join(const char *base, const char *name, char *out, int outsz) {
    int n = 0;
    while (*base && n < outsz - 1) { out[n++] = *base++; }
    if (n > 0 && out[n - 1] != '\\' && n < outsz - 1) out[n++] = '\\';
    while (*name && n < outsz - 1) { out[n++] = *name++; }
    out[n] = 0;
}

/* ---------- registry value type names ---------- */
static const char *reg_type_name(DWORD t) {
    switch (t) {
        case REG_NONE: return "None";
        case REG_SZ: return "SZ";
        case REG_EXPAND_SZ: return "ExpandSz";
        case REG_BINARY: return "Binary";
        case REG_DWORD: return "DWORD";
        case REG_DWORD_BIG_ENDIAN: return "DWORD-BE";
        case REG_LINK: return "Link";
        case REG_MULTI_SZ: return "MultiSz";
        case REG_QWORD: return "QWORD";
        default: return "?";
    }
}

/* ---------- UI state ---------- */
#define MAX_TREE 1024
typedef struct {
    char path[260];
    char name[128];
    int depth;
    int expanded;
    int has_children;
} TreeNode;
static TreeNode g_tree[MAX_TREE];
static int g_tree_count;
static int g_tree_sel;
static int g_tree_scroll;

#define MAX_VALS 256
typedef struct {
    char name[128];
    DWORD type;
    char data[256];
    int data_len;
} ValEntry;
static ValEntry g_vals[MAX_VALS];
static int g_val_count;
static int g_val_sel;
static int g_val_scroll;

static char g_status[128];
static char g_hint[128];

static int g_focus = 0;   /* 0 = tree, 1 = values */
static int g_dlg_active = 0;
static int g_dlg_kind = 0; /* 1=newkey 2=newval 3=editval 4=renamekey 6=confirm 7=msg 8=find */
static char g_dlg_buf[260];
static int g_dlg_len;
static char g_dlg_prompt[160];
static char g_dlg_title[80];

#define MENU_NONE -1
static int g_menu_active = 0;
static int g_menu_sel = 0;
static int g_menu_item = -1;

/* ---------- expanded-key tracking ---------- */
static int g_exp_count;
static char g_expanded[256][260];

static int is_expanded(const char *path) {
    for (int i = 0; i < g_exp_count; i++)
        if (str_cmp(g_expanded[i], path) == 0) return 1;
    return 0;
}
static void set_expanded(const char *path, int v) {
    if (v) {
        for (int i = 0; i < g_exp_count; i++)
            if (str_cmp(g_expanded[i], path) == 0) return;
        if (g_exp_count < 256) str_copy(g_expanded[g_exp_count++], path);
    } else {
        for (int i = 0; i < g_exp_count; i++) {
            if (str_cmp(g_expanded[i], path) == 0) {
                for (int j = i; j + 1 < g_exp_count; j++) str_copy(g_expanded[j], g_expanded[j + 1]);
                g_exp_count--;
                return;
            }
        }
    }
}

/* ---------- tree building ---------- */
static int count_subkeys(const char *path) {
    HKEY h = open_path(path, KEY_ENUMERATE_SUB_KEYS);
    if (!h) return 0;
    int n = 0;
    char name[128]; DWORD nsz;
    for (DWORD i = 0;; i++) {
        nsz = sizeof(name);
        if (pRegEnumKeyExA(h, i, name, &nsz, 0, 0, 0, 0) != ERROR_SUCCESS) break;
        n++;
    }
    pRegCloseKey(h);
    return n;
}
static void build_rec(const char *path, int depth) {
    if (g_tree_count >= MAX_TREE) return;
    TreeNode *tn = &g_tree[g_tree_count++];
    str_copy(tn->path, path);
    path_last(path, tn->name, sizeof(tn->name));
    tn->depth = depth;
    tn->expanded = is_expanded(path);
    tn->has_children = count_subkeys(path) > 0;

    if (tn->expanded && depth < 5) {
        HKEY h = open_path(path, KEY_ENUMERATE_SUB_KEYS);
        if (h) {
            char name[128]; DWORD nsz;
            for (DWORD i = 0;; i++) {
                nsz = sizeof(name);
                LONG r = pRegEnumKeyExA(h, i, name, &nsz, 0, 0, 0, 0);
                if (r != ERROR_SUCCESS) break;
                char child[380]; path_join(path, name, child, sizeof(child));
                build_rec(child, depth + 1);
                if (g_tree_count >= MAX_TREE) break;
            }
            pRegCloseKey(h);
        }
    }
}
static void build_tree(const char *root_path) {
    g_tree_count = 0;
    set_expanded(root_path, 1);
    build_rec(root_path, 0);
    if (g_tree_sel >= g_tree_count) g_tree_sel = g_tree_count > 0 ? g_tree_count - 1 : 0;
    if (g_tree_sel < 0) g_tree_sel = 0;
    if (g_tree_sel < g_tree_scroll) g_tree_scroll = g_tree_sel;
    if (g_tree_sel >= g_tree_scroll + CONTENT_ROWS) g_tree_scroll = g_tree_sel - (CONTENT_ROWS - 1);
}
static int find_tree_index(const char *path) {
    for (int i = 0; i < g_tree_count; i++)
        if (str_cmp(g_tree[i].path, path) == 0) return i;
    return -1;
}

/* ---------- value enumeration ---------- */
static void refresh_values(const char *path) {
    g_val_count = 0;
    g_val_sel = 0;
    g_val_scroll = 0;
    HKEY h = open_path(path, KEY_READ | KEY_ENUMERATE_SUB_KEYS);
    if (!h) return;
    char name[128]; DWORD nsz; DWORD type; BYTE data[512]; DWORD dsz;
    for (DWORD i = 0; i < MAX_VALS && g_val_count < MAX_VALS; i++) {
        nsz = sizeof(name); dsz = sizeof(data);
        LONG r = pRegEnumValueA(h, i, name, &nsz, 0, &type, data, &dsz);
        if (r != ERROR_SUCCESS) break;
        ValEntry *v = &g_vals[g_val_count++];
        str_copy(v->name, name);
        v->type = type;
        v->data_len = 0;
        if (dsz && dsz <= 512) { mem_copy(v->data, data, dsz); v->data_len = (int)dsz; }
    }
    pRegCloseKey(h);
    if (g_val_sel >= g_val_count) g_val_sel = g_val_count > 0 ? g_val_count - 1 : 0;
}
static void format_value_data(const ValEntry *v, char *out, int outsz) {
    out[0] = 0;
    switch (v->type) {
        case REG_SZ:
        case REG_EXPAND_SZ: {
            int n = v->data_len < outsz - 1 ? v->data_len : outsz - 1;
            mem_copy(out, v->data, n);
            out[n] = 0;
            int m = str_len(out);
            while (m > 0 && out[m - 1] == 0) out[--m] = 0;
            break;
        }
        case REG_DWORD: {
            if (v->data_len >= 4) {
                u32 val = (u32)v->data[0] | ((u32)v->data[1] << 8) |
                          ((u32)v->data[2] << 16) | ((u32)v->data[3] << 24);
                char h[16]; u32_to_hex(val, h);
                str_copy(out, h);
            } else str_copy(out, "(bad)");
            break;
        }
        case REG_QWORD: {
            if (v->data_len >= 8) {
                u64 val = 0;
                for (int i = 7; i >= 0; i--) val = (val << 8) | v->data[i];
                char h[32]; u64_to_hex(val, h);
                str_copy(out, h);
            } else str_copy(out, "(bad)");
            break;
        }
        case REG_BINARY: {
            int n = v->data_len < 16 ? v->data_len : 16;
            int o = 0;
            static const char hex[] = "0123456789ABCDEF";
            for (int i = 0; i < n && o + 3 < outsz; i++) {
                out[o++] = hex[(v->data[i] >> 4) & 15];
                out[o++] = hex[v->data[i] & 15];
                if (i + 1 < n) out[o++] = ' ';
            }
            out[o] = 0;
            if (v->data_len > 16) { int m = str_len(out); if (m + 4 < outsz) { out[m++] = '.'; out[m++] = '.'; out[m] = 0; } }
            break;
        }
        case REG_MULTI_SZ: {
            int o = 0;
            for (int i = 0; i < v->data_len && o + 2 < outsz; i++) {
                char ch = v->data[i];
                if (ch == 0) { out[o++] = '/'; out[o++] = ' '; }
                else if (ch >= 32 && ch < 127) out[o++] = ch;
            }
            out[o] = 0;
            break;
        }
        default:
            str_copy(out, "(unsupported)");
            break;
    }
}
static const char *sel_tree_path(void) {
    if (g_tree_count == 0) return "";
    return g_tree[g_tree_sel].path;
}
static void set_status(const char *s) { str_copy(g_status, s); }

/* ---------- forward declarations ---------- */
static void draw_screen(void);
static void draw_dialog(void);
static void dlg_commit(void);
static LONG do_create_key(const char *name);
static LONG do_delete_key(void);
static LONG do_rename_key(const char *newname);
static LONG do_new_value(const char *name, DWORD type, const void *data, DWORD size);
static LONG do_edit_value(ValEntry *v, const void *data, DWORD size);
static LONG do_delete_value(void);
static void cmd_refresh(void);
static void cmd_newkey(void);
static void cmd_newval(void);
static void cmd_delete(void);
static void cmd_renamekey(void);
static void cmd_editval(void);
static void cmd_find(void);
static void cmd_about(void);
static void cmd_import(void);
static void cmd_export(void);

/* ---------- drawing ---------- */
static void draw_title(void) {
    const char *t = "Registry Editor";
    int n = str_len(t);
    int c = (SCR_W - n) / 2;
    if (c < 0) c = 0;
    scr_fill(TITLE_ROW, 0, SCR_W, ' ');
    scr_puts(TITLE_ROW, c, t);
}
static void draw_menu_bar(void) {
    scr_fill(MENU_ROW, 0, SCR_W, ' ');
    static const char *labels[5] = { "File", "Edit", "View", "Favorites", "Help" };
    static const char *hotkey[5] = { "(N)ew", "(N)ew val", "(R)efresh", "—", "(A)bout" };
    int x = 2;
    for (int m = 0; m < 5; m++) {
        int len = str_len(labels[m]);
        BOOL sel = g_menu_active && g_menu_sel == m;
        if (sel) { scr_fill(MENU_ROW, x - 1, x + len + 1, '#'); }
        scr_puts(MENU_ROW, x, labels[m]);
        int hx = x + len + 2;
        const char *h = hotkey[m];
        while (*h && hx < SCR_W - 2) g_scr[MENU_ROW][hx++] = *h++;
        x += len + str_len(hotkey[m]) + 6;
    }
    if (g_menu_active) scr_puts(MENU_ROW, SCR_W - 12, " [Esc=close]");
}
static void draw_address_bar(const char *path) {
    scr_fill(ADDR_ROW, 0, SCR_W, ' ');
    g_scr[ADDR_ROW][0] = '[';
    scr_puts(ADDR_ROW, 2, path);
    scr_puts(ADDR_ROW, SCR_W - 12, " [F3=Find]");
    for (int c = 0; c < SCR_W; c++) g_scr[ADDR_ROW + 1][c] = '-';
}
static void draw_headers(void) {
    scr_fill(HDR_ROW, 0, SCR_W, ' ');
    scr_puts(HDR_ROW, 0, "Key");
    scr_puts(HDR_ROW, 36, "Name");
    scr_puts(HDR_ROW, 54, "Type");
    scr_puts(HDR_ROW, 66, "Data");
    g_scr[HDR_ROW][34] = '|';
    g_scr[HDR_ROW][35] = '|';
}

static void draw_tree_pane(void) {
    for (int v = 0; v < CONTENT_ROWS; v++) {
        int idx = g_tree_scroll + v;
        int r = CONTENT_TOP + v;
        scr_fill(r, 0, TREE_W, ' ');
        if (idx < 0 || idx >= g_tree_count) continue;
        TreeNode *tn = &g_tree[idx];
        BOOL sel = (g_tree_sel == idx && g_focus == 0);
        int c = 0;
        for (int d = 0; d < tn->depth * 2 && c < TREE_W - 2; d++) g_scr[r][c++] = ' ';
        if (tn->has_children) {
            g_scr[r][c++] = tn->expanded ? 'v' : '>';
            g_scr[r][c++] = ' ';
        } else {
            g_scr[r][c++] = ' ';
            g_scr[r][c++] = ' ';
        }
        const char *nm = tn->name;
        while (*nm && c < TREE_W - 1) g_scr[r][c++] = *nm++;
        if (sel) g_scr[r][0] = '>';
    }
    for (int r = CONTENT_TOP; r <= CONTENT_BOTTOM; r++) g_scr[r][34] = '|';
}

static void draw_values_pane(void) {
    for (int v = 0; v < CONTENT_ROWS; v++) {
        int idx = g_val_scroll + v;
        int r = CONTENT_TOP + v;
        scr_fill(r, 36, SCR_W, ' ');
        if (idx < 0 || idx >= g_val_count) continue;
        ValEntry *val = &g_vals[idx];
        BOOL sel = (g_val_sel == idx && g_focus == 1);
        int c = 36;
        const char *nm = val->name;
        while (*nm && c < 52) g_scr[r][c++] = *nm++;
        c = 54;
        const char *tp = reg_type_name(val->type);
        while (*tp && c < 64) g_scr[r][c++] = *tp++;
        c = 66;
        char dfmt[256];
        format_value_data(val, dfmt, sizeof(dfmt));
        char *df = dfmt;
        while (*df && c < SCR_W) g_scr[r][c++] = *df++;
        if (sel) g_scr[r][36] = '>';
    }
}

static void draw_status(void) {
    scr_fill(STATUS_ROW, 0, SCR_W, ' ');
    scr_puts(STATUS_ROW, 0, g_status);
    scr_puts(STATUS_ROW, 55, g_hint);
}
static void draw_hint(void) {
    scr_fill(HINT_ROW, 0, SCR_W, ' ');
    const char *h =
        "Arrows/WASD=navigate  Enter=open  BS=up  Tab=focus  m=menu  F5=refresh  i=new key  n=new value  e=edit  Del=delete";
    scr_puts(HINT_ROW, 0, h);
}

/* ---------- toolbar (file-explorer style clickable buttons) ---------- */
/* Charcoal pills with a signal-red accent, identical to file explorer.
 * Coordinates mirror file explorer's toolbar band (y = 50..74). */
static void draw_toolbar(void) {
    if (!pDecor) return;
    pDecor(g_window, 10, 14, 50, 50, 24, 8, 0xFF252A33, 0xFF454B56, 1, "New Key");
    pDecor(g_window, 11, 72, 50, 52, 24, 8, 0xFF252A33, 0xFF454B56, 1, "New Val");
    pDecor(g_window, 12, 132, 50, 40, 24, 8, 0xFF252A33, 0xFF454B56, 1, "Edit");
    pDecor(g_window, 13, 180, 50, 48, 24, 8, 0xFF252A33, 0xFF454B56, 1, "Delete");
    pDecor(g_window, 14, 236, 50, 52, 24, 8, 0xFF252A33, 0xFF454B56, 1, "Rename");
    pDecor(g_window, 15, 296, 50, 40, 24, 8, 0xFF252A33, 0xFF454B56, 1, "Find");
    pDecor(g_window, 16, 344, 50, 52, 24, 8, 0xFF252A33, 0xFF454B56, 1, "Refresh");
}

static void draw_screen(void) {
    scr_clear();
    draw_title();
    draw_menu_bar();
    draw_address_bar(sel_tree_path());
    draw_toolbar();
    draw_headers();
    draw_tree_pane();
    draw_values_pane();
    draw_status();
    draw_hint();
    if (g_dlg_active) draw_dialog();
    scr_draw();
}

/* ---------- dialogs ---------- */
static void draw_dialog(void) {
    int top = DIALOG_TOP;
    scr_fill(top, 0, SCR_W, '-');
    scr_puts(top + 1, 2, g_dlg_title);
    scr_puts(top + 2, 2, g_dlg_prompt);
    char line[260];
    int n = 0;
    line[n++] = '>';
    line[n++] = ' ';
    for (int i = 0; i < g_dlg_len && n < 258; i++) line[n++] = g_dlg_buf[i];
    line[n++] = '_';
    line[n] = 0;
    scr_puts(top + 3, 2, line);
    scr_puts(top + 4, 2, "Enter=OK   Esc=Cancel");
    scr_fill(top + 5, 0, SCR_W, '-');
}

/* ---------- string / conversion helpers ---------- */
/* Case-insensitive substring search; returns pointer into haystack or 0. */
static char *strstri(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (const char *h = haystack; *h; h++) {
        const char *a = h, *b = needle;
        while (*a && *b) {
            char x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
            char y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
            if (x != y) break;
            a++; b++;
        }
        if (!*b) return (char *)h;
    }
    return 0;
}
static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static int hex_to_u32(const char *s, u32 *out) {
    u32 v = 0; int n = 0;
    while (*s) {
        int d = hex_val(*s);
        if (d < 0) break;
        v = (v << 4) | (u32)d;
        s++; n++;
    }
    *out = v;
    return n > 0;
}
static int hex_to_u64(const char *s, u64 *out) {
    u64 v = 0; int n = 0;
    while (*s) {
        int d = hex_val(*s);
        if (d < 0) break;
        v = (v << 4) | (u64)d;
        s++; n++;
    }
    *out = v;
    return n > 0;
}
/* Parse hex pairs (e.g. "01 AB 02") into byte array. Returns byte count. */
static int parse_hex_pairs(const char *s, BYTE *out, int maxlen) {
    int n = 0;
    while (*s && n < maxlen) {
        while (*s == ' ' || *s == '\t' || *s == ',') s++;
        if (!*s) break;
        int d1 = hex_val(*s++);
        if (d1 < 0) break;
        while (*s == ' ' || *s == '\t' || *s == ',') s++;
        int d2 = hex_val(*s++);
        if (d2 < 0) { out[n++] = (BYTE)d1; break; }
        out[n++] = (BYTE)((d1 << 4) | d2);
    }
    return n;
}
/* Convert type name string ("SZ","DWORD","QWORD","Binary","MultiSz") to REG_*. */
static DWORD type_name_to_id(const char *s) {
    if (str_cmpi(s, "SZ") == 0) return REG_SZ;
    if (str_cmpi(s, "DWORD") == 0) return REG_DWORD;
    if (str_cmpi(s, "QWORD") == 0) return REG_QWORD;
    if (str_cmpi(s, "Binary") == 0) return REG_BINARY;
    if (str_cmpi(s, "MultiSz") == 0) return REG_MULTI_SZ;
    if (str_cmpi(s, "None") == 0) return REG_NONE;
    return (DWORD)-1;
}
/* Convert string data to bytes for a new value, per type. Returns byte count or 0 on failure. */
static int convert_data(const char *data_str, DWORD type, BYTE *out, int maxlen) {
    switch (type) {
        case REG_SZ:
        case REG_EXPAND_SZ: {
            int n = str_len(data_str);
            if (n + 1 > maxlen) return 0;
            mem_copy(out, data_str, n);
            out[n] = 0;
            return n + 1;
        }
        case REG_DWORD: {
            u32 v;
            if (!hex_to_u32(data_str, &v)) return 0;
            if (maxlen < 4) return 0;
            out[0] = (BYTE)(v & 0xFF);
            out[1] = (BYTE)((v >> 8) & 0xFF);
            out[2] = (BYTE)((v >> 16) & 0xFF);
            out[3] = (BYTE)((v >> 24) & 0xFF);
            return 4;
        }
        case REG_QWORD: {
            u64 v;
            if (!hex_to_u64(data_str, &v)) return 0;
            if (maxlen < 8) return 0;
            for (int i = 0; i < 8; i++) out[i] = (BYTE)((v >> (i * 8)) & 0xFF);
            return 8;
        }
        case REG_BINARY:
            return parse_hex_pairs(data_str, out, maxlen);
        case REG_MULTI_SZ: {
            int o = 0;
            const char *p = data_str;
            while (*p) {
                const char *start = p;
                while (*p && *p != '/') p++;
                int len = (int)(p - start);
                if (o + len + 1 > maxlen) return 0;
                mem_copy(out + o, start, len);
                o += len;
                out[o++] = 0;
                if (*p == '/') p++;
            }
            if (o + 1 > maxlen) return 0;
            out[o++] = 0;
            return o;
        }
        default:
            return 0;
    }
}
/* Convert string data to bytes for editing an existing value, per type. Returns byte count or 0 on failure. */
static int convert_edit_data(const char *data_str, DWORD type, BYTE *out, int maxlen) {
    switch (type) {
        case REG_SZ:
        case REG_EXPAND_SZ: {
            int n = str_len(data_str);
            if (n + 1 > maxlen) return 0;
            mem_copy(out, data_str, n);
            out[n] = 0;
            return n + 1;
        }
        case REG_DWORD: {
            u32 v;
            if (!hex_to_u32(data_str, &v)) return 0;
            if (maxlen < 4) return 0;
            out[0] = (BYTE)(v & 0xFF);
            out[1] = (BYTE)((v >> 8) & 0xFF);
            out[2] = (BYTE)((v >> 16) & 0xFF);
            out[3] = (BYTE)((v >> 24) & 0xFF);
            return 4;
        }
        case REG_QWORD: {
            u64 v;
            if (!hex_to_u64(data_str, &v)) return 0;
            if (maxlen < 8) return 0;
            for (int i = 0; i < 8; i++) out[i] = (BYTE)((v >> (i * 8)) & 0xFF);
            return 8;
        }
        case REG_BINARY:
            return parse_hex_pairs(data_str, out, maxlen);
        case REG_MULTI_SZ: {
            int o = 0;
            const char *p = data_str;
            while (*p) {
                const char *start = p;
                while (*p && *p != '/') p++;
                int len = (int)(p - start);
                if (o + len + 1 > maxlen) return 0;
                mem_copy(out + o, start, len);
                o += len;
                out[o++] = 0;
                if (*p == '/') p++;
            }
            return o;
        }
        default:
            return 0;
    }
}

/* ---------- .reg export helpers ---------- */
static int export_write_line(char *buf, int pos, int bufsz, const char *s) {
    int n = str_len(s);
    if (pos + n >= bufsz) return pos;
    mem_copy(buf + pos, s, n);
    return pos + n;
}
static int export_val_line(char *out, int outsz, const char *name,
                           DWORD type, const BYTE *data, DWORD dsz) {
    int o = 0;
    out[o++] = '"';
    int n = str_len(name);
    for (int i = 0; i < n && o + 1 < outsz; i++) out[o++] = name[i];
    out[o++] = '"';
    out[o++] = '=';
    if (type == REG_SZ || type == REG_EXPAND_SZ) {
        out[o++] = '"';
        for (DWORD i = 0; i < dsz && o + 2 < outsz; i++) {
            char c = (char)data[i];
            if (c == 0) break;
            out[o++] = c;
        }
        out[o++] = '"';
    } else if (type == REG_DWORD) {
        if (dsz >= 4) {
            u32 v = (u32)data[0] | ((u32)data[1] << 8) |
                    ((u32)data[2] << 16) | ((u32)data[3] << 24);
            const char *prefix = "dword:";
            int pn = str_len(prefix);
            for (int i = 0; i < pn && o < outsz; i++) out[o++] = prefix[i];
            char h[16]; u32_to_hex(v, h);
            for (int i = 0; i < 8 && o < outsz; i++) out[o++] = h[i];
        }
    } else if (type == REG_QWORD) {
        const char *prefix = "hex(b):";
        int pn = str_len(prefix);
        for (int i = 0; i < pn && o < outsz; i++) out[o++] = prefix[i];
        for (DWORD i = 0; i < dsz && o + 1 < outsz; i++) {
            if (i > 0) out[o++] = ',';
            char h[4];
            h[0] = "0123456789ABCDEF"[(data[i] >> 4) & 15];
            h[1] = "0123456789ABCDEF"[data[i] & 15];
            h[2] = 0;
            for (int j = 0; j < 2 && o < outsz; j++) out[o++] = h[j];
        }
    } else if (type == REG_BINARY) {
        const char *prefix = "hex:";
        int pn = str_len(prefix);
        for (int i = 0; i < pn && o < outsz; i++) out[o++] = prefix[i];
        for (DWORD i = 0; i < dsz && o + 1 < outsz; i++) {
            if (i > 0) out[o++] = ',';
            char h[4];
            h[0] = "0123456789ABCDEF"[(data[i] >> 4) & 15];
            h[1] = "0123456789ABCDEF"[data[i] & 15];
            h[2] = 0;
            for (int j = 0; j < 2 && o < outsz; j++) out[o++] = h[j];
        }
    } else if (type == REG_MULTI_SZ) {
        const char *prefix = "hex(7):";
        int pn = str_len(prefix);
        for (int i = 0; i < pn && o < outsz; i++) out[o++] = prefix[i];
        for (DWORD i = 0; i < dsz && o + 1 < outsz; i++) {
            if (i > 0) out[o++] = ',';
            char h[4];
            h[0] = "0123456789ABCDEF"[(data[i] >> 4) & 15];
            h[1] = "0123456789ABCDEF"[data[i] & 15];
            h[2] = 0;
            for (int j = 0; j < 2 && o < outsz; j++) out[o++] = h[j];
        }
    }
    out[o++] = '\r';
    out[o++] = '\n';
    out[o] = 0;
    return o;
}
static int export_rec(char *buf, int pos, int bufsz, const char *path) {
    pos = export_write_line(buf, pos, bufsz, "[");
    pos = export_write_line(buf, pos, bufsz, path);
    pos = export_write_line(buf, pos, bufsz, "]\r\n");
    HKEY h = open_path(path, KEY_READ);
    if (!h) return pos;
    char name[128]; DWORD nsz; DWORD type; BYTE data[512]; DWORD dsz;
    for (DWORD i = 0; i < MAX_VALS; i++) {
        nsz = sizeof(name); dsz = sizeof(data);
        if (pRegEnumValueA(h, i, name, &nsz, 0, &type, data, &dsz) != ERROR_SUCCESS) break;
        char line[512];
        export_val_line(line, sizeof(line), name, type, data, dsz);
        pos = export_write_line(buf, pos, bufsz, line);
    }
    pRegCloseKey(h);
    pos = export_write_line(buf, pos, bufsz, "\r\n");
    HKEY h2 = open_path(path, KEY_ENUMERATE_SUB_KEYS);
    if (!h2) return pos;
    char subname[128]; DWORD subnsz;
    for (DWORD i = 0; i < MAX_TREE; i++) {
        subnsz = sizeof(subname);
        if (pRegEnumKeyExA(h2, i, subname, &subnsz, 0, 0, 0, 0) != ERROR_SUCCESS) break;
        char subpath[380]; path_join(path, subname, subpath, sizeof(subpath));
        pos = export_rec(buf, pos, bufsz, subpath);
    }
    pRegCloseKey(h2);
    return pos;
}

/* Create all intermediate keys along a full path. Returns handle (caller closes unless root). */
static HKEY create_path_full(const char *fullpath) {
    HKEY root = path_root_handle(fullpath);
    if (!root) return 0;
    const char *rel = path_relative(fullpath);
    HKEY h = root;
    int opened_root = 1;
    while (*rel) {
        char comp[128]; int i = 0;
        while (*rel && *rel != '\\' && i < 127) comp[i++] = *rel++;
        comp[i] = 0;
        if (*rel == '\\') rel++;
        if (i == 0) continue;
        HKEY nh = 0;
        DWORD disp;
        LONG r = pRegCreateKeyExA(h, comp, 0, 0, 0, KEY_ALL_ACCESS, 0, &nh, &disp);
        if (r != ERROR_SUCCESS) {
            if (!opened_root) pRegCloseKey(h);
            return 0;
        }
        if (!opened_root) pRegCloseKey(h);
        opened_root = 0;
        h = nh;
    }
    if (opened_root) return root;
    return h;
}

static void dlg_commit(void) {
    switch (g_dlg_kind) {
        case 1: /* new key */
            if (do_create_key(g_dlg_buf) == ERROR_SUCCESS) cmd_refresh();
            break;
        case 2: { /* new value: name|type|data */
            char name[128]; char typestr[32];
            int i = 0;
            while (g_dlg_buf[i] && g_dlg_buf[i] != '|' && i < 127) { name[i] = g_dlg_buf[i]; i++; }
            name[i] = 0;
            if (g_dlg_buf[i] != '|') { set_status("Use: name|type|data"); break; }
            i++;
            int j = 0;
            while (g_dlg_buf[i] && g_dlg_buf[i] != '|' && j < 31) { typestr[j++] = g_dlg_buf[i++]; }
            typestr[j] = 0;
            if (g_dlg_buf[i] != '|') { set_status("Use: name|type|data"); break; }
            i++;
            DWORD type = type_name_to_id(typestr);
            if (type == (DWORD)-1) { set_status("Unknown type"); break; }
            BYTE data[512];
            int dsz = convert_data(&g_dlg_buf[i], type, data, sizeof(data));
            if (dsz <= 0) { set_status("Bad data"); break; }
            if (do_new_value(name, type, data, (DWORD)dsz) == ERROR_SUCCESS) cmd_refresh();
            break; }
        case 3: { /* edit value */
            if (g_val_count == 0 || g_val_sel < 0 || g_val_sel >= g_val_count) break;
            ValEntry *v = &g_vals[g_val_sel];
            BYTE data[512];
            int dsz = convert_edit_data(g_dlg_buf, v->type, data, sizeof(data));
            if (dsz <= 0) { set_status("Bad data"); break; }
            if (do_edit_value(v, data, (DWORD)dsz) == ERROR_SUCCESS) cmd_refresh();
            break; }
        case 6: { /* confirm delete */
            char ch = g_dlg_len ? g_dlg_buf[0] : 0;
            if (ch == 'y' || ch == 'Y') {
                if (g_focus == 1) do_delete_value();
                else do_delete_key();
                cmd_refresh();
            }
            break; }
        case 4: /* rename key */
            if (do_rename_key(g_dlg_buf) == ERROR_SUCCESS) cmd_refresh();
            break;
        case 8: { /* find */
            if (g_dlg_len == 0) { set_status("Enter search text"); break; }
            int found = -1;
            for (int i = 0; i < g_tree_count; i++) {
                if (strstri(g_tree[i].path, g_dlg_buf) || strstri(g_tree[i].name, g_dlg_buf)) { found = i; break; }
            }
            char msg[160];
            if (found >= 0) {
                str_copy(msg, "Found: ");
                str_cat(msg, g_tree[found].path);
                set_status(msg);
            } else {
                for (int i = 0; i < g_val_count; i++) {
                    if (strstri(g_vals[i].name, g_dlg_buf)) { found = i; break; }
                    char dfmt[256]; format_value_data(&g_vals[i], dfmt, sizeof(dfmt));
                    if (strstri(dfmt, g_dlg_buf)) { found = i; break; }
                }
                if (found >= 0) {
                    str_copy(msg, "Found: ");
                    str_cat(msg, sel_tree_path());
                    str_cat(msg, "\\");
                    str_cat(msg, g_vals[found].name);
                    set_status(msg);
                } else {
                    set_status("Not found");
                }
            }
            break; }
        default: break;
    }
}
static void handle_dlg_key(u64 vk) {
    if (vk == VK_RETURN) { g_dlg_active = 0; dlg_commit(); draw_screen(); return; }
    if (vk == VK_ESCAPE) { g_dlg_active = 0; draw_screen(); return; }
    if (vk == VK_BACK) { if (g_dlg_len > 0) g_dlg_buf[--g_dlg_len] = 0; draw_screen(); return; }
    if (vk >= 32 && vk <= 126) {
        if (g_dlg_len < 255) g_dlg_buf[g_dlg_len++] = (char)vk;
        g_dlg_buf[g_dlg_len] = 0;
        draw_screen();
        return;
    }
}

/* ---------- registry operations ---------- */
static int is_root_handle(HKEY h) {
    return h == HKEY_CLASSES_ROOT || h == HKEY_CURRENT_USER || h == HKEY_LOCAL_MACHINE ||
           h == HKEY_USERS || h == HKEY_CURRENT_CONFIG;
}
static void maybe_close(HKEY h) { if (!is_root_handle(h)) pRegCloseKey(h); }

static LONG do_create_key(const char *name) {
    char parent[260]; path_parent(sel_tree_path(), parent, sizeof(parent));
    HKEY h = open_path(parent, KEY_ALL_ACCESS | KEY_CREATE_SUB_KEY);
    if (!h) { set_status("Cannot open parent key"); return ERROR_CANTOPEN; }
    DWORD disp; HKEY res;
    LONG r = pRegCreateKeyExA(h, name, 0, 0, 0, KEY_ALL_ACCESS, 0, &res, &disp);
    maybe_close(h);
    if (res) maybe_close(res);
    if (r != ERROR_SUCCESS) { set_status("Create key failed"); return r; }
    set_status("Key created");
    return ERROR_SUCCESS;
}
static LONG do_delete_key(void) {
    const char *path = sel_tree_path();
    char parent[260]; char name[128];
    path_parent(path, parent, sizeof(parent));
    path_last(path, name, sizeof(name));
    HKEY h = open_path(parent, KEY_ALL_ACCESS);
    if (!h) { set_status("Cannot open parent"); return ERROR_CANTOPEN; }
    LONG r = pRegDeleteTreeA(h, name);
    maybe_close(h);
    if (r != ERROR_SUCCESS) { set_status("Delete key failed"); return r; }
    set_status("Key deleted");
    return ERROR_SUCCESS;
}
static LONG do_rename_key(const char *newname) {
    const char *path = sel_tree_path();
    if (!path || !*path) { set_status("No key selected"); return ERROR_INVALID_PARAMETER; }
    char parent[260]; char oldname[128];
    path_parent(path, parent, sizeof(parent));
    path_last(path, oldname, sizeof(oldname));
    if (!*parent || !*oldname) { set_status("Bad path"); return ERROR_INVALID_PARAMETER; }
    if (!*newname) { set_status("Bad name"); return ERROR_INVALID_PARAMETER; }
    if (str_cmp(oldname, newname) == 0) { set_status("Same name"); return ERROR_SUCCESS; }
    HKEY h = open_path(parent, KEY_ALL_ACCESS);
    if (!h) { set_status("Cannot open parent"); return ERROR_CANTOPEN; }
    LONG r = pRegCopyTreeA(h, oldname, h, newname, 0);
    if (r != ERROR_SUCCESS) { maybe_close(h); set_status("Copy key failed"); return r; }
    r = pRegDeleteTreeA(h, oldname);
    maybe_close(h);
    if (r != ERROR_SUCCESS) { set_status("Delete old key failed"); return r; }
    set_status("Key renamed");
    return ERROR_SUCCESS;
}
static LONG do_new_value(const char *name, DWORD type, const void *data, DWORD size) {
    HKEY h = open_path(sel_tree_path(), KEY_ALL_ACCESS | KEY_SET_VALUE);
    if (!h) { set_status("Cannot open key"); return ERROR_CANTOPEN; }
    LONG r = pRegSetValueExA(h, name, 0, type, (const BYTE *)data, size);
    maybe_close(h);
    if (r != ERROR_SUCCESS) { set_status("Set value failed"); return r; }
    set_status("Value set");
    return ERROR_SUCCESS;
}
static LONG do_edit_value(ValEntry *v, const void *data, DWORD size) {
    HKEY h = open_path(sel_tree_path(), KEY_ALL_ACCESS | KEY_SET_VALUE);
    if (!h) { set_status("Cannot open key"); return ERROR_CANTOPEN; }
    LONG r = pRegSetValueExA(h, v->name, 0, v->type, (const BYTE *)data, size);
    maybe_close(h);
    if (r != ERROR_SUCCESS) { set_status("Edit value failed"); return r; }
    set_status("Value updated");
    return ERROR_SUCCESS;
}
static LONG do_delete_value(void) {
    ValEntry *v = &g_vals[g_val_sel];
    HKEY h = open_path(sel_tree_path(), KEY_ALL_ACCESS | KEY_SET_VALUE);
    if (!h) { set_status("Cannot open key"); return ERROR_CANTOPEN; }
    LONG r = pRegDeleteValueA(h, v->name);
    maybe_close(h);
    if (r != ERROR_SUCCESS) { set_status("Delete value failed"); return r; }
    set_status("Value deleted");
    return ERROR_SUCCESS;
}

/* ---------- commands ---------- */
static void cmd_refresh(void) {
    build_tree(sel_tree_path());
    refresh_values(sel_tree_path());
    set_status("Refreshed");
}
static void cmd_newkey(void) {
    g_dlg_active = 1; g_dlg_kind = 1; g_dlg_len = 0;
    str_copy(g_dlg_title, "New Key");
    str_copy(g_dlg_prompt, "Key name:");
}
static void cmd_newval(void) {
    g_dlg_active = 1; g_dlg_kind = 2; g_dlg_len = 0;
    str_copy(g_dlg_title, "New Value");
    str_copy(g_dlg_prompt, "Value name|type|data:");
}
static void cmd_delete(void) {
    g_dlg_active = 1; g_dlg_kind = 6; g_dlg_len = 0;
    str_copy(g_dlg_title, g_focus == 1 ? "Delete Value" : "Delete Key");
    str_copy(g_dlg_prompt, g_focus == 1 ? "Delete this value? (y/n)" : "Delete this key and all subkeys? (y/n)");
}
static void cmd_renamekey(void) {
    g_dlg_active = 1; g_dlg_kind = 4; g_dlg_len = 0;
    str_copy(g_dlg_title, "Rename Key");
    str_copy(g_dlg_prompt, "New name:");
}
static void cmd_editval(void) {
    if (g_val_count == 0) return;
    g_dlg_active = 1; g_dlg_kind = 3; g_dlg_len = 0;
    str_copy(g_dlg_title, "Edit Value");
    str_copy(g_dlg_prompt, "New data:");
    str_copy(g_dlg_buf, g_vals[g_val_sel].data);
    g_dlg_len = str_len(g_dlg_buf);
    /* commit handler for kind 3 edits the selected value as a string */
}
static void cmd_find(void) {
    g_dlg_active = 1; g_dlg_kind = 8; g_dlg_len = 0;
    str_copy(g_dlg_title, "Find");
    str_copy(g_dlg_prompt, "Search text:");
}
static void cmd_about(void) {
    g_dlg_active = 1; g_dlg_kind = 7; g_dlg_len = 0;
    str_copy(g_dlg_title, "About");
    str_copy(g_dlg_prompt, "SentinelOS Registry Editor (regedit.exe)");
    scr_puts(38, 2, "V11 SOVEREIGN  |  text-grid UI over persistent hive");
}
static void cmd_import(void) {
    u64 h = sy3(SYS_OpenFile, (u64)"import.reg", 0, 0);
    if (!h) { set_status("Cannot open import.reg"); return; }
    u64 fsize = sy3(SYS_QueryFileSize, h, 0, 0);
    if (fsize == 0 || fsize > 65536) { sy3(SYS_CloseFile, h, 0, 0); set_status("File too big"); return; }
    static char ibuf[65536];
    u64 nread = sy3(SYS_ReadFile, h, (u64)ibuf, (u64)fsize);
    sy3(SYS_CloseFile, h, 0, 0);
    if (nread == 0) { set_status("Read failed"); return; }
    ibuf[nread] = 0;

    int count = 0;
    char curkey[260]; curkey[0] = 0;
    char *p = ibuf;
    while (*p) {
        while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
        if (!*p) break;
        if (*p == '[') {
            p++;
            char *end = p;
            while (*end && *end != ']') end++;
            if (*end == ']') {
                int klen = (int)(end - p);
                if (klen > 0 && klen < 260) {
                    mem_copy(curkey, p, klen);
                    curkey[klen] = 0;
                    HKEY hk = open_path(curkey, KEY_ALL_ACCESS | KEY_SET_VALUE);
                    if (!hk) hk = create_path_full(curkey);
                    if (hk) { maybe_close(hk); count++; }
                }
                p = end + 1;
            }
        } else if (*p == '"') {
            p++;
            char *end = p;
            while (*end && *end != '"') end++;
            if (*end == '"') {
                int nlen = (int)(end - p);
                char vname[128];
                if (nlen > 0 && nlen < 128) {
                    mem_copy(vname, p, nlen);
                    vname[nlen] = 0;
                    p = end + 1;
                    while (*p && *p != '=') p++;
                    if (*p == '=') p++;
                    while (*p == ' ' || *p == '\t') p++;

                    DWORD type = REG_SZ;
                    BYTE data[512]; int dsz = 0;
                    if (*p == '"') {
                        p++;
                        char *estr = p;
                        while (*estr && *estr != '"') estr++;
                        int slen = (int)(estr - p);
                        if (slen > 0 && slen + 1 <= 512) {
                            mem_copy(data, p, slen);
                            data[slen] = 0;
                            dsz = slen + 1;
                            type = REG_SZ;
                        }
                        p = estr;
                        if (*p == '"') p++;
                    } else if (str_starts(p, "dword:")) {
                        p += 6;
                        u32 v;
                        if (hex_to_u32(p, &v)) {
                            data[0] = (BYTE)(v & 0xFF); data[1] = (BYTE)((v >> 8) & 0xFF);
                            data[2] = (BYTE)((v >> 16) & 0xFF); data[3] = (BYTE)((v >> 24) & 0xFF);
                            dsz = 4; type = REG_DWORD;
                        }
                    } else if (str_starts(p, "hex(b):")) {
                        p += 7;
                        dsz = parse_hex_pairs(p, data, sizeof(data));
                        type = REG_QWORD;
                    } else if (str_starts(p, "hex:")) {
                        p += 4;
                        dsz = parse_hex_pairs(p, data, sizeof(data));
                        type = REG_BINARY;
                    } else {
                        int slen = str_len(p);
                        if (slen > 0 && slen + 1 <= 512) { mem_copy(data, p, slen); data[slen] = 0; dsz = slen + 1; type = REG_SZ; }
                    }

                    if (dsz > 0) {
                        HKEY hk = open_path(curkey, KEY_ALL_ACCESS | KEY_SET_VALUE);
                        if (!hk) hk = create_path_full(curkey);
                        if (hk) {
                            LONG r = pRegSetValueExA(hk, vname, 0, type, data, (DWORD)dsz);
                            maybe_close(hk);
                            if (r == ERROR_SUCCESS) count++;
                        }
                    }
                }
            }
        }
        while (*p && *p != '\r' && *p != '\n') p++;
    }

    char msg[128];
    str_copy(msg, "Imported ");
    char cnt[12]; u32_to_dec((u32)count, cnt);
    str_cat(msg, cnt);
    str_cat(msg, " value(s)");
    set_status(msg);
}
static void cmd_export(void) {
    const char *path = sel_tree_path();
    if (!path || !*path) { set_status("No key selected"); return; }
    static char regbuf[8192];
    int pos = 0;
    pos = export_write_line(regbuf, pos, (int)sizeof(regbuf), "Windows Registry Editor Version 5.00\r\n\r\n");
    pos = export_rec(regbuf, pos, (int)sizeof(regbuf), path);
    u64 out = sy3(SYS_CreateOutputFile, (u64)"export.reg", 0, 0);
    if (!out) { set_status("Cannot create export.reg"); return; }
    sy3(SYS_WriteOutputFile, out, (u64)regbuf, (u64)pos);
    sy3(SYS_CloseFile, out, 0, 0);
    set_status("Exported to export.reg");
}

/* ---------- input handling ---------- */
static void tree_move(int delta) {
    int target = g_tree_sel + delta;
    int max = g_tree_count > 0 ? g_tree_count - 1 : 0;
    if (target < 0) target = 0;
    if (target > max) target = max;
    g_tree_sel = target;
    if (g_tree_sel < g_tree_scroll) g_tree_scroll = g_tree_sel;
    if (g_tree_sel >= g_tree_scroll + CONTENT_ROWS) g_tree_scroll = g_tree_sel - (CONTENT_ROWS - 1);
    refresh_values(sel_tree_path());
    g_val_sel = 0; g_val_scroll = 0;
}
static void tree_expand_toggle(void) {
    if (g_tree_count == 0) return;
    TreeNode *tn = &g_tree[g_tree_sel];
    if (!tn->has_children) return;
    set_expanded(tn->path, !tn->expanded);
    build_tree(sel_tree_path());
    int ni = find_tree_index(tn->path);
    if (ni >= 0) g_tree_sel = ni;
    refresh_values(sel_tree_path());
}
static void tree_select_parent(void) {
    if (g_tree_count == 0) return;
    TreeNode *tn = &g_tree[g_tree_sel];
    if (tn->expanded) { set_expanded(tn->path, 0); build_tree(sel_tree_path()); return; }
    if (tn->depth == 0) return;
    char par[260]; path_parent(tn->path, par, sizeof(par));
    int ni = find_tree_index(par);
    if (ni >= 0) {
        g_tree_sel = ni;
        if (g_tree_sel < g_tree_scroll) g_tree_scroll = g_tree_sel;
        if (g_tree_sel >= g_tree_scroll + CONTENT_ROWS) g_tree_scroll = g_tree_sel - (CONTENT_ROWS - 1);
    }
    refresh_values(sel_tree_path());
}
static void val_move(int delta) {
    int max = g_val_count > 0 ? g_val_count - 1 : 0;
    int target = g_val_sel + delta;
    if (target < 0) target = 0;
    if (target > max) target = max;
    g_val_sel = target;
    if (g_val_sel < g_val_scroll) g_val_scroll = g_val_sel;
    if (g_val_sel >= g_val_scroll + CONTENT_ROWS) g_val_scroll = g_val_sel - (CONTENT_ROWS - 1);
}

static void handle_main_key(u64 vk) {
    switch (vk) {
        case VK_RETURN:
            if (g_focus == 0 && g_tree_count > 0 && g_tree[g_tree_sel].has_children)
                tree_expand_toggle();
            break;
        case VK_ESCAPE:
            if (g_focus == 1) g_focus = 0;
            break;
        case VK_BACK:
            tree_select_parent();
            break;
        case VK_TAB:
            g_focus = g_focus == 0 ? 1 : 0;
            break;
        case 'w': case 'W': case 'k': case 'K':
        case VK_UP:
            tree_move(-1);
            break;
        case 's': case 'S': case 'j': case 'J':
        case VK_DOWN:
            tree_move(1);
            break;
        case 'a': case 'A': case 'h': case 'H':
        case VK_LEFT:
            tree_select_parent();
            break;
        case 'd': case 'D': case 'l': case 'L':
        case VK_RIGHT:
            tree_expand_toggle();
            break;
        case 'm': case 'M':
            g_menu_active = !g_menu_active;
            g_menu_item = -1;
            break;
        case 'r': case 'R':
            cmd_refresh();
            break;
        case 'i': case 'I':
            cmd_newkey();
            break;
        case 'n': case 'N':
            cmd_newval();
            break;
        case 'e': case 'E':
            cmd_editval();
            break;
        case 46: /* guess: Delete */
            cmd_delete();
            break;
        case 'f': case 'F':
            cmd_find();
            break;
    }
    draw_screen();
}

/* ---------- menu ---------- */
#define MENU_FILE 0
#define MENU_EDIT 1
#define MENU_VIEW 2
#define MENU_FAV 3
#define MENU_HELP 4

static int menu_item_count(int menu) {
    switch (menu) {
        case MENU_FILE: return 4;
        case MENU_EDIT: return 6;
        case MENU_VIEW: return 1;
        case MENU_FAV: return 0;
        case MENU_HELP: return 1;
    }
    return 0;
}
static const char *menu_item_label(int menu, int item) {
    switch (menu) {
        case MENU_FILE:
            switch (item) { case 0: return "New key"; case 1: return "Export"; case 2: return "Import"; case 3: return "Exit"; }
            break;
        case MENU_EDIT:
            switch (item) { case 0: return "New key"; case 1: return "New value"; case 2: return "Delete"; case 3: return "Rename"; case 4: return "Copy"; case 5: return "Paste"; }
            break;
        case MENU_VIEW:
            if (item == 0) return "Refresh";
            break;
        case MENU_HELP:
            if (item == 0) return "About";
            break;
    }
    return "";
}
static void draw_menu_dropdown(void) {
    /* draw the open dropdown under the selected menu at rows 2..2+count */
    int top = 2;
    int count = menu_item_count(g_menu_sel);
    int x = 2 + g_menu_sel * 14; /* rough column of the menu label */
    if (x < 0) x = 0;
    scr_fill(top, x, x + 20, ' ');
    for (int i = 0; i < count; i++) {
        int r = top + i;
        if (r >= SCR_H) break;
        scr_fill(r, x, x + 20, ' ');
        const char *label = menu_item_label(g_menu_sel, i);
        scr_puts(r, x + 1, label);
        if (i == g_menu_item) { scr_fill(r, x, x + 1, '>'); }
    }
}
static void menu_exec(int menu, int item) {
    g_menu_active = 0;
    g_menu_item = -1;
    switch (menu) {
        case MENU_FILE:
            switch (item) {
                case 0: cmd_newkey(); break;
                case 1: cmd_export(); break;
                case 2: cmd_import(); break;
                case 3: set_status("Exit: close the window"); break;
            }
            break;
        case MENU_EDIT:
            switch (item) {
                case 0: cmd_newkey(); break;
                case 1: cmd_newval(); break;
                case 2: cmd_delete(); break;
                case 3: cmd_renamekey(); break;
                case 4: set_status("Copy not implemented"); break;
                case 5: set_status("Paste not implemented"); break;
            }
            break;
        case MENU_VIEW:
            cmd_refresh();
            break;
        case MENU_HELP:
            cmd_about();
            break;
    }
    draw_screen();
}
static void handle_menu_key(u64 vk) {
    switch (vk) {
        case VK_ESCAPE:
            g_menu_active = 0; g_menu_item = -1; draw_screen(); return;
        case VK_RETURN:
            if (g_menu_item >= 0) { menu_exec(g_menu_sel, g_menu_item); }
            else { g_menu_item = 0; draw_screen(); }
            return;
        case 'a': case 'A': case VK_LEFT:
            g_menu_sel = g_menu_sel > 0 ? g_menu_sel - 1 : 4;
            g_menu_item = -1; draw_screen(); return;
        case 'd': case 'D': case VK_RIGHT:
            g_menu_sel = g_menu_sel < 4 ? g_menu_sel + 1 : 0;
            g_menu_item = -1; draw_screen(); return;
        case 'w': case 'W': case 'k': case 'K':
        case VK_UP:
            if (g_menu_item < 0) g_menu_item = 0;
            else if (g_menu_item > 0) g_menu_item--;
            draw_screen(); return;
        case 's': case 'S': case 'j': case 'J':
        case VK_DOWN:
            if (g_menu_item < 0) g_menu_item = 0;
            else g_menu_item++;
            draw_screen(); return;
        case 'n': case 'N':
            if (g_menu_sel == MENU_FILE) { menu_exec(MENU_FILE, 0); return; }
            if (g_menu_sel == MENU_EDIT) { menu_exec(MENU_EDIT, 0); return; }
            break;
        case 'x': case 'X':
            if (g_menu_sel == MENU_FILE) { menu_exec(MENU_FILE, 3); return; }
            break;
        case 'r': case 'R':
            if (g_menu_sel == MENU_VIEW) { menu_exec(MENU_VIEW, 0); return; }
            break;
    }
}

/* ---------- message dispatch ---------- */
static void handle_message(MSG *msg);
static void handle_mouse(MSG *msg);

static void handle_message(MSG *msg) {
    if (g_dlg_active) { handle_dlg_key(msg->wParam); return; }
    if (g_menu_active) { handle_menu_key(msg->wParam); return; }
    if (msg->message == WM_KEYDOWN) handle_main_key(msg->wParam);
    if (msg->message == 0x0201) handle_mouse(msg);
}

static void handle_mouse(MSG *msg) {
    if (pGetRect) {
        RECT rc;
        if (pGetRect(g_window, &rc)) { g_win_x = rc.left; g_win_y = rc.top; }
    }
    int relx = msg->pt_x - g_win_x;
    int rely = msg->pt_y - g_win_y;
    /* Toolbar button band (y = 50..74), same band as file explorer. */
    if (rely < 50 || rely >= 74) return;
    if (relx >= 14 && relx < 64)      { cmd_newkey(); draw_screen(); return; }
    if (relx >= 72 && relx < 124)     { cmd_newval(); draw_screen(); return; }
    if (relx >= 132 && relx < 172)    { cmd_editval(); draw_screen(); return; }
    if (relx >= 180 && relx < 228)    { cmd_delete(); draw_screen(); return; }
    if (relx >= 236 && relx < 288)    { cmd_renamekey(); draw_screen(); return; }
    if (relx >= 296 && relx < 336)    { cmd_find(); draw_screen(); return; }
    if (relx >= 344 && relx < 396)    { cmd_refresh(); draw_screen(); return; }
}

/* ---------- entry point ---------- */
void mainCRTStartup(void) {
    pCreate = (CreateWindowExA_t)gpa("user32.dll", "CreateWindowExA");
    pShow = (ShowWindow_t)gpa("user32.dll", "ShowWindow");
    pUpdate = (UpdateWindow_t)gpa("user32.dll", "UpdateWindow");
    pText = (TextOutA_t)gpa("user32.dll", "TextOutA");
    pGetMsg = (GetMessageA_t)gpa("user32.dll", "GetMessageA");
    pTrans = (TranslateMessage_t)gpa("user32.dll", "TranslateMessage");
    pDisp = (DispatchMessageA_t)gpa("user32.dll", "DispatchMessageA");
    pMsgBox = (MessageBoxA_t)gpa("user32.dll", "MessageBoxA");
    pInv = (InvalidateRect_t)gpa("user32.dll", "InvalidateRect");
    pDecor = (SetWindowDecorA_t)gpa("user32.dll", "SetWindowDecorA");
    pBeginBatch = (BeginRedrawBatch_t)gpa("user32.dll", "BeginRedrawBatch");
    pEndBatch = (EndRedrawBatch_t)gpa("user32.dll", "EndRedrawBatch");
    pGetRect = (GetWindowRect_t)gpa("user32.dll", "GetWindowRect");

    pRegOpenKeyExA = (RegOpenKeyExA_t)gpa("advapi32.dll", "RegOpenKeyExA");
    pRegCreateKeyExA = (RegCreateKeyExA_t)gpa("advapi32.dll", "RegCreateKeyExA");
    pRegDeleteKeyA = (RegDeleteKeyA_t)gpa("advapi32.dll", "RegDeleteKeyA");
    pRegDeleteTreeA = (RegDeleteTreeA_t)gpa("advapi32.dll", "RegDeleteTreeA");
    pRegDeleteValueA = (RegDeleteValueA_t)gpa("advapi32.dll", "RegDeleteValueA");
    pRegCloseKey = (RegCloseKey_t)gpa("advapi32.dll", "RegCloseKey");
    pRegFlushKey = (RegFlushKey_t)gpa("advapi32.dll", "RegFlushKey");
    pRegEnumKeyExA = (RegEnumKeyExA_t)gpa("advapi32.dll", "RegEnumKeyExA");
    pRegEnumValueA = (RegEnumValueA_t)gpa("advapi32.dll", "RegEnumValueA");
    pRegQueryValueExA = (RegQueryValueExA_t)gpa("advapi32.dll", "RegQueryValueExA");
    pRegSetValueExA = (RegSetValueExA_t)gpa("advapi32.dll", "RegSetValueExA");
    pRegQueryInfoKeyA = (RegQueryInfoKeyA_t)gpa("advapi32.dll", "RegQueryInfoKeyA");
    pRegCopyTreeA = (RegCopyTreeA_t)gpa("advapi32.dll", "RegCopyTreeA");
    pRegConnectRegistryA = (RegConnectRegistryA_t)gpa("advapi32.dll", "RegConnectRegistryA");
    pRegNotifyChangeKeyValue = (RegNotifyChangeKeyValue_t)gpa("advapi32.dll", "RegNotifyChangeKeyValue");

    if (!pCreate || !pShow || !pText || !pGetMsg || !pRegOpenKeyExA || !pRegEnumKeyExA ||
        !pRegEnumValueA || !pRegSetValueExA || !pRegCloseKey || !pRegDeleteValueA) {
        print("regedit: missing required API\n");
        for (;;) __asm__ volatile("pause");
    }

    apis_ok = 1;

    lato_load();

    if (pBeginBatch) pBeginBatch();
    g_window = pCreate(0, "RegEdit", "Registry Editor", 0, 0, 0, 0, 0, 0, 0, 0, 0);
    if (!g_window) {
        print("regedit: window creation failed\n");
        for (;;) __asm__ volatile("pause");
    }
    if (pShow) pShow(g_window, 1);
    if (pUpdate) pUpdate(g_window);

    build_tree("HKEY_LOCAL_MACHINE");
    refresh_values(sel_tree_path());
    str_copy(g_status, "Ready");
    str_copy(g_hint, "Use arrows/WASD to navigate, Enter to open");
    draw_screen();
    if (pEndBatch) pEndBatch();

    MSG msg;
    for (;;) {
        if (!pGetMsg(&msg, 0, 0, 0)) break;
        if (msg.message == 0x0012) break; /* WM_QUIT */
        pTrans(&msg);
        pDisp(&msg);
        if (msg.message == WM_CLOSE) break;
        handle_message(&msg);
        draw_screen();
    }

    if (pEndBatch) pEndBatch();
    sy3(SYS_Exit, 0, 0, 0);
    for (;;) __asm__ volatile("pause");
}
