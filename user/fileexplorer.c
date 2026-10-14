typedef unsigned long long u64;
typedef unsigned int DWORD;
typedef int BOOL;
typedef void *HWND;
typedef const char *LPCSTR;

enum { SYS_Write=1, SYS_GetProc=9 };

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

typedef struct {
    DWORD hwnd, message;
    unsigned long long wParam;
    long long lParam;
    DWORD time;
    int x, y;
} MSG;

void mainCRTStartup(void) {
    char k32[] = {'k','e','r','n','e','l','3','2','.','d','l','l',0};
    char u32[] = {'u','s','e','r','3','2','.','d','l','l',0};

    QueryDirectoryA_t pQuery = (QueryDirectoryA_t)gpa(k32, "QueryDirectoryA");
    CreateWindowExA_t pCreate = (CreateWindowExA_t)gpa(u32, "CreateWindowExA");
    ShowWindow_t pShow = (ShowWindow_t)gpa(u32, "ShowWindow");
    UpdateWindow_t pUpdate = (UpdateWindow_t)gpa(u32, "UpdateWindow");
    TextOutA_t pText = (TextOutA_t)gpa(u32, "TextOutA");
    GetMessageA_t pGetMsg = (GetMessageA_t)gpa(u32, "GetMessageA");
    TranslateMessage_t pTrans = (TranslateMessage_t)gpa(u32, "TranslateMessage");
    DispatchMessageA_t pDisp = (DispatchMessageA_t)gpa(u32, "DispatchMessageA");
    MessageBoxA_t pMsgBox = (MessageBoxA_t)gpa(u32, "MessageBoxA");

    if (!pQuery || !pCreate || !pShow || !pUpdate || !pText || !pGetMsg) {
        print("fileexplorer: missing API\n");
        for (;;) __asm__ volatile("pause");
    }

    if (pMsgBox) pMsgBox(0, "Opening C:\\System32", "File Explorer", 0);
    print("fileexplorer: create window\n");

    HWND hwnd = pCreate(0, "FileExplorer", "C:\\System32 - File Explorer",
                        0, 100, 80, 700, 500, 0, 0, 0, 0);
    pShow(hwnd, 1);
    pUpdate(hwnd);

    for (DWORD i = 0; i < 32; i++) {
        char name[64];
        for (int k = 0; k < 64; k++) name[k] = 0;
        DWORD st = pQuery(i, name, 64);
        if (st == 1) break;
        if (st != 0) break;
        char line[80];
        int p = 0;
        const char *pre = "C:\\System32\\";
        for (int j = 0; pre[j]; j++) line[p++] = pre[j];
        for (int j = 0; name[j] && p < 78; j++) line[p++] = name[j];
        line[p] = 0;
        pText(0, 0, (int)i, line, -1);
    }

    print("fileexplorer: enter message loop (never exits)\n");
    MSG msg;
    for (;;) {
        if (!pGetMsg(&msg, 0, 0, 0)) {
            print("fileexplorer: GetMessage returned 0 (ignored)\n");
            continue;
        }
        if (pTrans) pTrans(&msg);
        if (pDisp) pDisp(&msg);
    }
}
