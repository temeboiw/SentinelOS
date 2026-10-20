/* File Explorer – modern list; click row = list index (not TextOut line) */
typedef unsigned long long u64;
typedef unsigned int DWORD;
typedef int BOOL;
typedef const char *LPCSTR;

enum { SYS_Write=1, SYS_CreateProcess=4, SYS_GetProc=9, SYS_Chdir=12 };

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

typedef void *(*CreateWindowExA_t)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, void *, void *, void *, void *);
typedef BOOL (*ShowWindow_t)(void *, int);
typedef BOOL (*UpdateWindow_t)(void *);
typedef BOOL (*TextOutA_t)(void *, int, int, LPCSTR, int);
typedef BOOL (*GetMessageA_t)(void *, void *, DWORD, DWORD);
typedef BOOL (*TranslateMessage_t)(const void *);
typedef long long (*DispatchMessageA_t)(const void *);
typedef int (*MessageBoxA_t)(void *, LPCSTR, LPCSTR, DWORD);
typedef DWORD (*QueryDirectoryA_t)(DWORD, char *, DWORD);
typedef BOOL (*SetCurrentDirectoryA_t)(LPCSTR);
typedef BOOL (*InvalidateRect_t)(void *, void *, BOOL);


typedef struct {
    DWORD hwnd, message;
    unsigned long long wParam;
    long long lParam;
    DWORD time;
    int x, y;
} MSG;

#define MAX_ENT 48
static char g_names[MAX_ENT][64];
static int g_isdir[MAX_ENT];
static int g_count;
static char g_path[128];
static int g_win_y = 40; /* must match CreateWindowEx y */
static int g_last_file_row = -1;
static int g_hover_row = -1;

/* Reserve one character at the start of every row for the hover marker.
   Changing it never shifts the filename or any neighboring controls. */
static void draw_rows(TextOutA_t pText, InvalidateRect_t pInv) {
    pText(0, 0, 0, g_path, -1);
    pText(0, 0, 1, "Click folders | double-click EXE/MSI | .. goes up", -1);
    for (int i = 0; i < g_count; i++) {
        char line[80];
        int p = 0;
        line[p++] = (i == g_hover_row) ? '>' : ' ';
        if (g_isdir[i]) {
            line[p++]='['; line[p++]='D'; line[p++]='I'; line[p++]='R'; line[p++]=']'; line[p++]=' ';
        } else if (ends_with(g_names[i], ".exe")) {
            line[p++]='['; line[p++]='E'; line[p++]='X'; line[p++]='E'; line[p++]=']'; line[p++]=' ';
        } else if (ends_with(g_names[i], ".dll")) {
            line[p++]='['; line[p++]='D'; line[p++]='L'; line[p++]='L'; line[p++]=']'; line[p++]=' ';
        } else if (ends_with(g_names[i], ".msi")) {
            line[p++]='['; line[p++]='M'; line[p++]='S'; line[p++]='I'; line[p++]=']'; line[p++]=' ';
        } else {
            line[p++]='['; line[p++]='F'; line[p++]='I'; line[p++]='L'; line[p++]=']'; line[p++]=' ';
        }
        for (int j = 0; g_names[i][j] && p < 78; j++) line[p++] = g_names[i][j];
        line[p] = 0;
        pText(0, 0, 2 + i, line, -1);
    }
    for (int i = g_count; i < 28; i++)
        pText(0, 0, 2 + i, " ", -1);
    if (pInv) pInv(0, 0, 1);
}


static void path_init(void) {
    g_path[0]='C'; g_path[1]=':'; g_path[2]='\\'; g_path[3]=0;
}
static int ends_with(const char *n, const char *ext) {
    int i = 0, j = 0;
    while (n[i]) i++;
    while (ext[j]) j++;
    if (i < j) return 0;
    for (int k = 0; k < j; k++) {
        char a = n[i - j + k], b = ext[k];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
    }
    return 1;
}

static void reload(QueryDirectoryA_t pQuery, TextOutA_t pText, InvalidateRect_t pInv) {
    g_count = 0;
    g_hover_row = -1;

    /* Always provide a parent-directory entry, even if the VFS omits it. */
    g_names[g_count][0] = '.';
    g_names[g_count][1] = '.';
    g_names[g_count][2] = 0;
    g_isdir[g_count++] = 1;

    for (DWORD i = 0; i < MAX_ENT - 1 && g_count < MAX_ENT; i++) {
        char name[64];
        name[0] = 0;
        DWORD t = pQuery(i, name, 64);
        if (t == 1) break;

        /* Avoid duplicate parent entries if the filesystem supplies one. */
        if (name[0] == '.' && name[1] == '.' && !name[2]) continue;

        int k = 0;
        for (; name[k] && k < 63; k++) g_names[g_count][k] = name[k];
        g_names[g_count][k] = 0;
        g_isdir[g_count] = (t == 2);
        if (!g_isdir[g_count]) {
            int dot = 0;
            for (int j = 0; g_names[g_count][j]; j++)
                if (g_names[g_count][j] == '.') { dot = 1; break; }
            if (!dot) g_isdir[g_count] = 1;
        }
        g_count++;
    }

    /* TextOut lines: 0=path 1=hint 2..=entries => click row = line - 2. */
    draw_rows(pText, pInv);
}

void mainCRTStartup(void) {
    char k32[] = {'k','e','r','n','e','l','3','2','.','d','l','l',0};
    char u32[] = {'u','s','e','r','3','2','.','d','l','l',0};

    QueryDirectoryA_t pQuery = (QueryDirectoryA_t)gpa(k32, "QueryDirectoryA");
    SetCurrentDirectoryA_t pChdir = (SetCurrentDirectoryA_t)gpa(k32, "SetCurrentDirectoryA");
    CreateWindowExA_t pCreate = (CreateWindowExA_t)gpa(u32, "CreateWindowExA");
    ShowWindow_t pShow = (ShowWindow_t)gpa(u32, "ShowWindow");
    UpdateWindow_t pUpdate = (UpdateWindow_t)gpa(u32, "UpdateWindow");
    TextOutA_t pText = (TextOutA_t)gpa(u32, "TextOutA");
    GetMessageA_t pGetMsg = (GetMessageA_t)gpa(u32, "GetMessageA");
    TranslateMessage_t pTrans = (TranslateMessage_t)gpa(u32, "TranslateMessage");
    DispatchMessageA_t pDisp = (DispatchMessageA_t)gpa(u32, "DispatchMessageA");
    MessageBoxA_t pMsgBox = (MessageBoxA_t)gpa(u32, "MessageBoxA");
    InvalidateRect_t pInv = (InvalidateRect_t)gpa(u32, "InvalidateRect");

    if (!pQuery || !pCreate || !pShow || !pText || !pGetMsg) {
        print("fileexplorer: missing API\n");
        for (;;) __asm__ volatile("pause");
    }

    path_init();
    print("fileexplorer: create window\n");
    g_win_y = 40;
    void *hwnd = pCreate(0, "Explorer", "File Explorer", 0, 40, g_win_y, 760, 520, 0, 0, 0, 0);
    if (pShow) pShow(hwnd, 1);
    reload(pQuery, pText, pInv);
    if (pUpdate) pUpdate(hwnd);
    print("fileexplorer: ready\n");

    MSG msg;
    for (;;) {
        if (!pGetMsg(&msg, 0, 0, 0)) continue;
        if (pTrans) pTrans(&msg);
        if (pDisp) pDisp(&msg);

        /*
         * Mouse move: update a fixed-width hover marker. The marker occupies
         * a reserved character, so row text and controls never jump sideways.
         */
        if (msg.message == 0x0200) {
            int line = (msg.y - (g_win_y + 44)) / 16;
            int row = line - 2;
            if (row < 0 || row >= g_count) row = -1;
            if (row != g_hover_row) {
                g_hover_row = row;
                draw_rows(pText, pInv);
            }
        }

        if (msg.message == 0x0201) {
            /*
             * user32 draws line i at: wnd_y + 44 + i*16
             * lines 0,1 = path/hint; entries start at line 2
             * => entry_index = line - 2
             */
            int line = (msg.y - (g_win_y + 44)) / 16;
            int row = line - 2;
            if (row >= 0 && row < g_count) {
                print("fileexplorer: click ");
                print(g_names[row]);
                print("\n");
                if (g_isdir[row]) {
                    g_last_file_row = -1;
                    int ok = 0;
                    if (pChdir) ok = pChdir(g_names[row]);
                    if (!ok) ok = (sy3(SYS_Chdir, (u64)g_names[row], 0, 0) == 0);
                    if (ok) {
                        if (g_names[row][0]=='.' && g_names[row][1]=='.' && !g_names[row][2]) {
                            int n = 0; while (g_path[n]) n++;
                            if (n > 3) {
                                n--; while (n > 2 && g_path[n] != '\\') n--;
                                if (g_path[n]=='\\') n++;
                                g_path[n] = 0;
                                if (n <= 3) path_init();
                            }
                        } else {
                            int n = 0; while (g_path[n]) n++;
                            if (n > 0 && g_path[n-1] != '\\') g_path[n++] = '\\';
                            for (int k = 0; g_names[row][k] && n < 120; k++)
                                g_path[n++] = g_names[row][k];
                            g_path[n] = 0;
                        }
                        print("fileexplorer: now ");
                        print(g_path);
                        print("\n");
                        reload(pQuery, pText, pInv);
                    } else {
                        print("fileexplorer: chdir failed\n");
                    }
                } else {
                    int double_click = (g_last_file_row == row);
                    g_last_file_row = row;

                    if (double_click && ends_with(g_names[row], ".exe")) {
                        unsigned long long pid = sy3(
                            SYS_CreateProcess, (u64)g_names[row], 0, 0);
                        if ((long long)pid < 0 || pid == 0) {
                            print("fileexplorer: executable is not loaded by the bootloader: ");
                            print(g_names[row]);
                            print("\n");
                            if (pMsgBox) pMsgBox(0,
                                "This EXE is not loaded by SentinelOS yet.",
                                "Cannot launch executable", 0);
                        }
                        g_last_file_row = -1;
                    } else if (double_click && ends_with(g_names[row], ".msi")) {
                        static char msiexec_name[] = "msiexec.exe";
                        unsigned long long pid = sy3(
                            SYS_CreateProcess, (u64)msiexec_name,
                            (u64)g_names[row], 0);
                        if ((long long)pid < 0 || pid == 0) {
                            print("fileexplorer: msiexec.exe is not loaded\n");
                            if (pMsgBox) pMsgBox(0,
                                "MSI package inspection is not available yet.",
                                "Windows Installer", 0);
                        }
                        g_last_file_row = -1;
                    } else if (double_click) {
                        if (pMsgBox) pMsgBox(0, g_names[row],
                            ends_with(g_names[row], ".dll") ?
                            "DLL library (not directly executable)" :
                            "File", 0);
                        g_last_file_row = -1;
                    }
                }
            }
        }
    }
}
