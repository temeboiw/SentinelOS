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
typedef void *(*CreateFileA_t)(LPCSTR, DWORD, DWORD, void *, DWORD, DWORD, void *);
typedef BOOL (*GetFileSizeEx_t)(void *, long long *);
typedef BOOL (*CloseHandle_t)(void *);
typedef BOOL (*InvalidateRect_t)(void *, void *, BOOL);
typedef BOOL (*GetWindowRect_t)(void *, void *);
typedef void (*BeginRedrawBatch_t)(void);
typedef void (*EndRedrawBatch_t)(void);
typedef struct { int left, top, right, bottom; } FE_RECT;


typedef struct {
    DWORD hwnd, message;
    unsigned long long wParam;
    long long lParam;
    DWORD time;
    int x, y;
} MSG;

#define MAX_ENT 256
#define VISIBLE_ROWS 28
static char g_names[MAX_ENT][256];
static int g_isdir[MAX_ENT];
static int g_count;
static char g_path[128];
static int g_win_y = 40; /* live window origin */
static int g_context_menu = 0;
static int g_context_row = -1;
static int g_context_start_line = 2;
/* Switch-based strings avoid absolute pointers in PE read-only data. */
#define CONTEXT_ITEM_COUNT 7
static const char *context_item(int i) {
    switch (i) {
        case 0: return "Open";
        case 1: return "Properties";
        case 2: return "Copy (read-only)";
        case 3: return "Cut (read-only)";
        case 4: return "Delete (read-only)";
        case 5: return "Refresh";
        case 6: return "Close menu";
        default: return "";
    }
}
static int g_last_file_row = -1;
static int g_hover_row = -1;
static int g_scroll = 0;
static int g_home = 1;
static int g_sort_mode = 0;
static char g_filter[64];
static int g_filter_len = 0;
static int g_search_mode = 0;
static void *g_window;

static int ends_with(const char *n, const char *ext);
static int name_contains_ci(const char *name, const char *needle) {
    if (!needle || !needle[0]) return 1;
    for (int i=0; name[i]; i++) {
        int j=0;
        while (needle[j] && name[i+j]) {
            char a=name[i+j], b=needle[j];
            if(a>='A'&&a<='Z')a+=32;
            if(b>='A'&&b<='Z')b+=32;
            if(a!=b)break;
            j++;
        }
        if(!needle[j])return 1;
    }
    return 0;
}
static int name_compare_ci(const char *a,const char *b) {
    int i=0;
    while(a[i]&&b[i]) {
        char x=a[i],y=b[i];
        if(x>='A'&&x<='Z')x+=32;
        if(y>='A'&&y<='Z')y+=32;
        if(x!=y)return x-y;
        i++;
    }
    return (unsigned char)a[i]-(unsigned char)b[i];
}
#define HOME_ITEM_COUNT 10
static const char *home_item(int i) {
    switch (i) {
        case 0: return "Quick access";
        case 1: return "Desktop";
        case 2: return "Documents";
        case 3: return "Downloads";
        case 4: return "Pictures";
        case 5: return "This PC (C:)";
        case 6: return "Windows";
        case 7: return "System32 (protected)";
        case 8: return "Users";
        case 9: return "Program Files";
        default: return "";
    }
}

/* Draw one visible row and invalidate only that row. */
static void draw_visible_row(TextOutA_t pText, InvalidateRect_t pInv, int visible) {
    if (visible < 0 || visible >= VISIBLE_ROWS) return;
    int i = g_scroll + visible;
    char line[80];
    int p = 0;
    int item_count = g_home ? HOME_ITEM_COUNT : g_count;
    if (i >= item_count) {
        line[0]=' '; line[1]=0;
    } else if (g_home) {
        line[p++] = (i == g_hover_row) ? '>' : ' ';
        const char *hn=home_item(i);
        line[p++]='[';line[p++]='P';line[p++]='I';line[p++]='N';line[p++]=']';line[p++]=' ';
        for(int j=0;hn[j]&&p<78;j++)line[p++]=hn[j];
        line[p]=0;
    } else {
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
    }
    pText(g_window, 0, 2 + visible, line, -1);
    if (pInv) {
        FE_RECT dirty = { 0, 44 + (2 + visible) * 16,
                          760, 44 + (3 + visible) * 16 };
        pInv(g_window, &dirty, 0);
    }
}

static void draw_rows(TextOutA_t pText, InvalidateRect_t pInv) {
    if (!pText) { print("fileexplorer: draw_rows missing TextOutA\n"); return; }
    print("fileexplorer: draw_rows stage=title-before\n");
    pText(g_window, 0, 0, g_home ? "Home | Quick access" : g_path, -1);
    print("fileexplorer: draw_rows stage=title-after\n");
    if(g_home) {
        print("fileexplorer: draw_rows stage=hint-before\n");
        pText(g_window,0,1,"Pinned locations | H: Home | /: search | S: sort",-1);
        print("fileexplorer: draw_rows stage=hint-after\n");
    } else if(g_filter_len) {
        char hint[80];int q=0;const char *prefix="Search: ";
        while(prefix[q]){hint[q]=prefix[q];q++;}
        for(int j=0;g_filter[j]&&q<78;j++)hint[q++]=g_filter[j];
        hint[q]=0;pText(g_window,0,1,hint,-1);
    } else pText(g_window,0,1,"H: Home | /: search | S: sort | double-click to open",-1);
    for (int visible = 0; visible < VISIBLE_ROWS; visible++) {
        if (visible == 0) print("fileexplorer: draw_rows stage=first-row-before\n");
        draw_visible_row(pText, 0, visible);
        if (visible == 0) print("fileexplorer: draw_rows stage=first-row-after\n");
    }
    if (g_context_menu) {
        /* Overlay a compact in-window context menu; the backing rows are
           redrawn when it closes, so no pixels are left behind. */
        for (int i=0; i<CONTEXT_ITEM_COUNT; i++) {
            char item[80]; int n=0;
            item[n++]='>'; item[n++]=' ';
            for (int j=0; context_item(i)[j] && n<78; j++) item[n++]=context_item(i)[j];
            item[n]=0;
            pText(g_window, 0, g_context_start_line+i,
                  "                                                                                ", -1);
            pText(g_window, 3, g_context_start_line+i, item, -1);
        }
    }
    if (pInv) {
        FE_RECT dirty = { 0, 0, 760, 520 };
        pInv(g_window, &dirty, 1);
    }
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

/* Read-only file size query backed by the real kernel32 file-handle APIs. */
static void append_file_size(char *details, int *pos, int cap, const char *name,
                             CreateFileA_t pOpen, GetFileSizeEx_t pSize,
                             CloseHandle_t pClose) {
    if (!details || !pos || !name || !pOpen || !pSize || !pClose) return;
    void *h = pOpen(name, 0, 0, 0, 3, 0, 0);
    if (!h || h == (void *)-1) return;
    long long bytes = 0;
    if (pSize(h, &bytes) && bytes >= 0) {
        const char *label = "\nSize: ";
        for (int i=0; label[i] && *pos<cap-1; i++) details[(*pos)++]=label[i];
        char digits[24]; int count=0; u64 value=(u64)bytes;
        do { digits[count++] = (char)('0' + value % 10); value /= 10; } while (value && count < 23);
        while (count && *pos<cap-1) details[(*pos)++]=digits[--count];
        const char *suffix = " bytes";
        for (int i=0; suffix[i] && *pos<cap-1; i++) details[(*pos)++]=suffix[i];
        details[*pos]=0;
    }
    pClose(h);
}

static void reload(QueryDirectoryA_t pQuery, TextOutA_t pText, InvalidateRect_t pInv) {
    g_count = 0;
    g_hover_row = -1;
    g_scroll = 0;
    if(g_home){draw_rows(pText,pInv);return;}

    /* Always provide a parent-directory entry, even if the VFS omits it. */
    g_names[g_count][0] = '.';
    g_names[g_count][1] = '.';
    g_names[g_count][2] = 0;
    g_isdir[g_count++] = 1;

    for (DWORD i = 0; i < MAX_ENT - 1 && g_count < MAX_ENT; i++) {
        char name[256];
        name[0] = 0;
        DWORD t = pQuery(i, name, 256);
        /* QueryDirectoryA returns 0 at end, 1 for a file, and 2 for a directory. */
        if (t == 0) break;

        /* Avoid duplicate parent entries if the filesystem supplies one. */
        if (name[0] == '.' && name[1] == '.' && !name[2]) continue;

        int k = 0;
        for (; name[k] && k < 255; k++) g_names[g_count][k] = name[k];
        g_names[g_count][k] = 0;
        /* Trust the filesystem's entry type; names without extensions are files too. */
        if(g_filter_len&&!name_contains_ci(name,g_filter))continue;
        g_isdir[g_count] = (t == 2);
        g_count++;
    }
    if(g_sort_mode) {
        for(int a=1;a<g_count;a++) {
            char nm[256];int isd=g_isdir[a],z=0,b=a-1;
            while(g_names[a][z]&&z<255){nm[z]=g_names[a][z];z++;}nm[z]=0;
            while(b>=0) {
                int move=(g_sort_mode==1)?
                    (g_isdir[b]<isd||(g_isdir[b]==isd&&name_compare_ci(g_names[b],nm)>0)):
                    (name_compare_ci(g_names[b],nm)>0);
                if(!move)break;
                int q=0;while(g_names[b][q]&&q<255){g_names[b+1][q]=g_names[b][q];q++;}g_names[b+1][q]=0;
                g_isdir[b+1]=g_isdir[b];b--;
            }
            z=0;while(nm[z]){g_names[b+1][z]=nm[z];z++;}g_names[b+1][z]=0;g_isdir[b+1]=isd;
        }
    }

    /* TextOut lines: 0=path 1=hint 2..=entries => click row = line - 2. */
    draw_rows(pText, pInv);
}

void mainCRTStartup(void) {
    char k32[] = {'k','e','r','n','e','l','3','2','.','d','l','l',0};
    char u32[] = {'u','s','e','r','3','2','.','d','l','l',0};

    QueryDirectoryA_t pQuery = (QueryDirectoryA_t)gpa(k32, "QueryDirectoryA");
    CreateFileA_t pOpenFile = (CreateFileA_t)gpa(k32, "CreateFileA");
    GetFileSizeEx_t pGetFileSizeEx = (GetFileSizeEx_t)gpa(k32, "GetFileSizeEx");
    CloseHandle_t pCloseHandle = (CloseHandle_t)gpa(k32, "CloseHandle");
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
    GetWindowRect_t pGetWindowRect = (GetWindowRect_t)gpa(u32, "GetWindowRect");
    BeginRedrawBatch_t pBeginBatch = (BeginRedrawBatch_t)gpa(u32, "BeginRedrawBatch");
    EndRedrawBatch_t pEndBatch = (EndRedrawBatch_t)gpa(u32, "EndRedrawBatch");

    if (!pQuery || !pCreate || !pShow || !pText || !pGetMsg) {
        print("fileexplorer: missing API\n");
        for (;;) __asm__ volatile("pause");
    }

    path_init();
    print("fileexplorer: create window\n");
    g_win_y = 40;
    if (pBeginBatch) pBeginBatch();
    print("fileexplorer: startup stage=before-create\n");
    g_window = pCreate(0, "Explorer", "File Explorer", 0, 40, g_win_y, 760, 520, 0, 0, 0, 0);
    print(g_window ? "fileexplorer: startup stage=after-create hwnd-valid\n"
                   : "fileexplorer: startup stage=create-failed\n");
    if (!g_window) {
        if (pEndBatch) pEndBatch();
        print("fileexplorer: aborting startup after CreateWindowExA failure\n");
        for (;;) __asm__ volatile("pause");
    }
    print("fileexplorer: startup stage=before-show\n");
    if (pShow) pShow(g_window, 1);
    print("fileexplorer: startup stage=after-show\n");
    print("fileexplorer: startup stage=before-reload\n");
    reload(pQuery, pText, pInv);
    print("fileexplorer: startup stage=after-reload\n");
    if (pUpdate) pUpdate(g_window);
    print("fileexplorer: startup stage=after-update\n");
    if (pEndBatch) pEndBatch();
    print("fileexplorer: ready\n");

    MSG msg;
    for (;;) {
        if (!pGetMsg(&msg, 0, 0, 0)) continue;
        if (pTrans) pTrans(&msg);
        if (pDisp) pDisp(&msg);

        /* Mouse coordinates are screen-space. Never keep using the startup Y
         * after the user moves the window; ask USER32 for its live rectangle. */
        if (pGetWindowRect) {
            FE_RECT live_rect;
            if (pGetWindowRect(g_window, &live_rect)) g_win_y = live_rect.top;
        }

        /*
         * Mouse move: update a fixed-width hover marker. The marker occupies
         * a reserved character, so row text and controls never jump sideways.
         */
        if (msg.message == 0x0200 && !g_context_menu) {
            int line = (msg.y - (g_win_y + 44)) / 16;
            int row = line - 2;
            if (row < 0 || row >= VISIBLE_ROWS || g_scroll + row >= (g_home ? HOME_ITEM_COUNT : g_count)) row = -1;
            else row += g_scroll;
            if (row != g_hover_row) {
                int old_row = g_hover_row;
                g_hover_row = row;

                /* Update both backing text lines first, then repaint once.
                 * Two immediate invalidations made the intermediate state
                 * visible as a blank/flashing marker. */
                if (old_row >= g_scroll && old_row < g_scroll + VISIBLE_ROWS)
                    draw_visible_row(pText, 0, old_row - g_scroll);
                if (row >= g_scroll && row < g_scroll + VISIBLE_ROWS)
                    draw_visible_row(pText, 0, row - g_scroll);

                int first = old_row, last = row;
                if (first < 0 || first < g_scroll || first >= g_scroll + VISIBLE_ROWS) first = row;
                if (last < 0 || last < g_scroll || last >= g_scroll + VISIBLE_ROWS) last = old_row;
                if (first >= g_scroll && first < g_scroll + VISIBLE_ROWS &&
                    last >= g_scroll && last < g_scroll + VISIBLE_ROWS) {
                    if (first > last) { int tmp=first; first=last; last=tmp; }
                    FE_RECT dirty = { 0, 44 + (2 + first - g_scroll) * 16,
                                      760, 44 + (3 + last - g_scroll) * 16 };
                    if (pInv) pInv(g_window, &dirty, 0);
                } else if (row >= g_scroll && row < g_scroll + VISIBLE_ROWS) {
                    FE_RECT dirty = { 0, 44 + (2 + row - g_scroll) * 16,
                                      760, 44 + (3 + row - g_scroll) * 16 };
                    if (pInv) pInv(g_window, &dirty, 0);
                }
            }
        }

        /* Mouse wheel and arrow/Page keys scroll the visible directory rows. */
        if (msg.message == 0x020A && !g_context_menu) {
            int delta = (int)((msg.wParam >> 16) & 0xFFFF);
            if (delta & 0x8000) delta -= 0x10000;
            if (delta > 0) g_scroll -= 3;
            else if (delta < 0) g_scroll += 3;
            if (g_scroll < 0) g_scroll = 0;
            if (g_scroll > g_count - VISIBLE_ROWS) g_scroll = g_count - VISIBLE_ROWS;
            if (g_scroll < 0) g_scroll = 0;
            g_hover_row = -1;
            draw_rows(pText, pInv);
        }
        if(msg.message==0x0102) {
            int ch=(int)msg.wParam;
            if (g_context_menu) {
                if (ch==27) { g_context_menu=0; draw_rows(pText,pInv); }
            } else if(g_search_mode) {
                if(ch==8&&g_filter_len>0){g_filter[--g_filter_len]=0;if(!g_home)reload(pQuery,pText,pInv);}
                else if(ch==13){g_search_mode=0;draw_rows(pText,pInv);}
                else if(ch==27){g_search_mode=0;g_filter_len=0;g_filter[0]=0;reload(pQuery,pText,pInv);}
                else if(ch>=32&&ch<127&&g_filter_len<62){g_home=0;g_filter[g_filter_len++]=(char)ch;g_filter[g_filter_len]=0;reload(pQuery,pText,pInv);}
            } else if(ch=='/'||ch=='?') {g_search_mode=1;g_filter_len=0;g_filter[0]=0;g_home=0;draw_rows(pText,pInv);}
            else if(ch=='h'||ch=='H') {g_home=1;g_search_mode=0;g_filter_len=0;g_filter[0]=0;g_scroll=0;draw_rows(pText,pInv);}
            else if(ch=='s'||ch=='S') {g_sort_mode=(g_sort_mode+1)%3;if(!g_home)reload(pQuery,pText,pInv);}
        }
        if (msg.message == 0x0100 && g_context_menu) {
            if (msg.wParam == 0x1B) { g_context_menu=0; draw_rows(pText,pInv); }
            continue;
        }
        if (msg.message == 0x0100) {
            if (msg.wParam == 0x26 && g_scroll > 0) g_scroll--;
            if (msg.wParam == 0x28 && g_scroll < g_count - VISIBLE_ROWS) g_scroll++;
            if (msg.wParam == 0x21) g_scroll -= VISIBLE_ROWS;
            if (msg.wParam == 0x22) g_scroll += VISIBLE_ROWS;
            if (g_scroll < 0) g_scroll = 0;
            if (g_scroll > g_count - VISIBLE_ROWS) g_scroll = g_count - VISIBLE_ROWS;
            if (g_scroll < 0) g_scroll = 0;
            g_hover_row = -1;
            draw_rows(pText, pInv);
        }

        if (msg.message == 0x0204) {
            /* WM_RBUTTONDOWN: context menu for the selected directory entry. */
            int line = (msg.y - (g_win_y + 44)) / 16;
            int row = line - 2;
            if (!g_home && row >= 0 && row < VISIBLE_ROWS && g_scroll + row < g_count) {
                g_context_row = g_scroll + row;
                g_context_menu = 1;
                g_context_start_line = 2 + row;
                if (g_context_start_line + CONTEXT_ITEM_COUNT > 2 + VISIBLE_ROWS)
                    g_context_start_line = 2 + VISIBLE_ROWS - CONTEXT_ITEM_COUNT;
                if (g_context_start_line < 2) g_context_start_line = 2;
                g_hover_row = g_context_row;
                draw_rows(pText, pInv);
            }
        }

        if (msg.message == 0x0201) {
            if (g_context_menu) {
                int line = (msg.y - (g_win_y + 44)) / 16;
                int item = line - g_context_start_line;
                int selected = g_context_row;
                g_context_menu = 0;
                draw_rows(pText, pInv);
                if (item >= 0 && item < CONTEXT_ITEM_COUNT && selected >= 0 && selected < g_count) {
                    if (item == 5) { reload(pQuery, pText, pInv); continue; }
                    if (item == 6) continue;
                    if (item == 2 || item == 3 || item == 4) {
                        int protected_path = 0;
                        const char *prefix = "C:\\Windows\\System32";
                        int j=0, same=1;
                        while (prefix[j]) {
                            char a=g_path[j], b=prefix[j];
                            if (a>='a'&&a<='z')a-=32;
                            if (b>='a'&&b<='z')b-=32;
                            if (!a || a!=b) { same=0; break; }
                            j++;
                        }
                        if (same && (g_path[j]==0 || g_path[j]=='\\')) protected_path=1;
                        /* Also protect the System32 directory itself and its Windows parent.
                           This is only a UI guard; destructive VFS operations remain unavailable. */
                        if (!protected_path) {
                            const char *want="System32";
                            int k=0, eq=1;
                            while (want[k] || g_names[selected][k]) {
                                char a=g_names[selected][k], b=want[k];
                                if (a>='a'&&a<='z') a-=32;
                                if (b>='a'&&b<='z') b-=32;
                                if (a!=b) { eq=0; break; }
                                k++;
                            }
                            if (eq) {
                                const char *wp="C:\\Windows";
                                int q=0, peq=1;
                                while (wp[q] || g_path[q]) {
                                    char a=g_path[q], b=wp[q];
                                    if (a>='a'&&a<='z') a-=32;
                                    if (b>='a'&&b<='z') b-=32;
                                    if (a!=b) { peq=0; break; }
                                    q++;
                                }
                                if (peq) protected_path=1;
                            }
                            if (!protected_path && g_names[selected][0] &&
                                (g_names[selected][0]=='W'||g_names[selected][0]=='w')) {
                                const char *wn="Windows";
                                int q=0, weq=1;
                                while (wn[q] || g_names[selected][q]) {
                                    char a=g_names[selected][q], b=wn[q];
                                    if (a>='a'&&a<='z') a-=32;
                                    if (b>='a'&&b<='z') b-=32;
                                    if (a!=b) { weq=0; break; }
                                    q++;
                                }
                                if (weq) {
                                    const char *root="C:\\";
                                    int r=0, req=1;
                                    while (root[r] || g_path[r]) {
                                        char a=g_path[r], b=root[r];
                                        if (a>='a'&&a<='z') a-=32;
                                        if (b>='a'&&b<='z') b-=32;
                                        if (a!=b) { req=0; break; }
                                        r++;
                                    }
                                    if (req) protected_path=1;
                                }
                            }
                        }
                        if (protected_path && item==4) {
                            if (pMsgBox) pMsgBox(g_window, "Access denied: System32 is protected. No files were changed.", "Protected system folder", 0);
                        } else if (pMsgBox) {
                            pMsgBox(g_window, "This filesystem is currently read-only. The requested operation was not performed.", "File operation unavailable", 0);
                        }
                        continue;
                    }
                    if (item == 1 || (!g_isdir[selected] &&
                        !ends_with(g_names[selected], ".exe") &&
                        !ends_with(g_names[selected], ".msi"))) {
                        char details[512]; int d=0;
                        const char *label=g_isdir[selected]?"Folder":
                            ends_with(g_names[selected],".dll")?"DLL library (not directly executable)":
                            ends_with(g_names[selected],".msi")?"Windows Installer package":
                            ends_with(g_names[selected],".exe")?"Windows executable":"File";
                        const char *a="Name: "; while(a[d]){details[d]=a[d];d++;}
                        for(int j=0;g_names[selected][j]&&d<240;j++)details[d++]=g_names[selected][j];
                        const char *b="\nType: ";for(int j=0;b[j]&&d<260;j++)details[d++]=b[j];
                        for(int j=0;label[j]&&d<285;j++)details[d++]=label[j];
                        const char *c="\nLocation: ";for(int j=0;c[j]&&d<300;j++)details[d++]=c[j];
                        for(int j=0;g_path[j]&&d<470;j++)details[d++]=g_path[j];
                        details[d]=0;
                        if (!g_isdir[selected]) append_file_size(details,&d,512,g_names[selected],pOpenFile,pGetFileSizeEx,pCloseHandle);
                        if(pMsgBox)pMsgBox(g_window,details,"Properties",0);
                        continue;
                    }
                    if (item == 0) {
                        if (g_isdir[selected]) {
                            int ok = pChdir ? pChdir(g_names[selected]) : 0;
                            if (!ok) ok = (sy3(SYS_Chdir, (u64)g_names[selected], 0, 0) == 0);
                            if (ok) {
                                if (g_names[selected][0]=='.' && g_names[selected][1]=='.' && !g_names[selected][2]) {
                                    int n=0;while(g_path[n])n++;
                                    if(n>3){n--;while(n>2&&g_path[n]!='\\')n--;if(g_path[n]=='\\')n++;g_path[n]=0;if(n<=3)path_init();}
                                } else {
                                    int n=0;while(g_path[n])n++;
                                    if(n>0&&g_path[n-1]!='\\')g_path[n++]='\\';
                                    for(int k=0;g_names[selected][k]&&n<120;k++)g_path[n++]=g_names[selected][k];
                                    g_path[n]=0;
                                }
                                g_filter_len=0;g_filter[0]=0;reload(pQuery,pText,pInv);
                            } else if(pMsgBox) pMsgBox(g_window,"Could not open this folder.","File Explorer",0);
                        } else if (ends_with(g_names[selected], ".exe")) {
                            unsigned long long pid=sy3(SYS_CreateProcess,(u64)g_names[selected],0,0);
                            if ((!pid || (long long)pid<0) && pMsgBox) pMsgBox(g_window,"This executable could not be started.","Cannot open file",0);
                        } else if (ends_with(g_names[selected], ".msi")) {
                            static char msiexec_name[]="msiexec.exe";
                            unsigned long long pid=sy3(SYS_CreateProcess,(u64)msiexec_name,(u64)g_names[selected],0);
                            if ((!pid || (long long)pid<0) && pMsgBox) pMsgBox(g_window,"Windows Installer is not available.","Cannot open package",0);
                        }
                        continue;
                    }
                }
                continue;
            }
            /*
             * user32 draws line i at: wnd_y + 44 + i*16
             * lines 0,1 = path/hint; entries start at line 2
             * => entry_index = line - 2
             */
            int line = (msg.y - (g_win_y + 44)) / 16;
            int row = line - 2;
            int row_limit=g_home?HOME_ITEM_COUNT:g_count;
            if (row >= 0 && row < VISIBLE_ROWS && g_scroll + row < row_limit) {
                row += g_scroll;
                if(g_home) {
                    const char *target=0;
                    if(row==1)target="C:\\Users\\Desktop";
                    else if(row==2)target="C:\\Users\\Documents";
                    else if(row==3)target="C:\\Users\\Downloads";
                    else if(row==4)target="C:\\Users\\Pictures";
                    else if(row==5)target="C:\\";
                    else if(row==6)target="C:\\Windows";
                    else if(row==7)target="C:\\Windows\\System32";
                    else if(row==8)target="C:\\Users";
                    else if(row==9)target="C:\\Program Files";
                    if(row==0){g_home=0;reload(pQuery,pText,pInv);continue;}
                    if(target) {
                        int plen=0;while(g_path[plen])plen++;
                        while(plen>3) {
                            if(!pChdir||!pChdir(".."))break;
                            plen--;while(plen>3&&g_path[plen-1]!='\\')plen--;
                            g_path[plen]=0;
                        }
                        int ti=3,ok=1;
                        while(target[ti]&&ok) {
                            char component[128];int ci=0;
                            while(target[ti]&&target[ti]!='\\'&&ci<126)component[ci++]=target[ti++];
                            component[ci]=0;if(target[ti]=='\\')ti++;
                            if(ci&&pChdir&&!pChdir(component))ok=0;
                            if(ok&&ci){int n=0;while(g_path[n])n++;if(n>3&&g_path[n-1]!='\\')g_path[n++]='\\';for(int j=0;component[j]&&n<126;j++)g_path[n++]=component[j];g_path[n]=0;}
                        }
                        if(ok){g_home=0;g_filter_len=0;g_filter[0]=0;reload(pQuery,pText,pInv);}
                        else if(pMsgBox)pMsgBox(g_window,"That pinned folder is not available on this volume.","Quick access",0);
                    }
                    continue;
                }
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
                            print("fileexplorer: disk executable launch failed: ");
                            print(g_names[row]);
                            print("\n");
                            if (pMsgBox) pMsgBox(0,
                                "This EXE could not be loaded. Check the disk path and PE compatibility.",
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
                        char details[512];int d=0;
                        const char *label=ends_with(g_names[row],".dll")?"DLL library (not directly executable)":
                            ends_with(g_names[row],".msi")?"Windows Installer package":
                            ends_with(g_names[row],".exe")?"Windows executable":"File";
                        const char *a="Name: ";while(a[d]){details[d]=a[d];d++;}
                        for(int j=0;g_names[row][j]&&d<250;j++)details[d++]=g_names[row][j];
                        const char *b="\nType: ";for(int j=0;b[j]&&d<270;j++)details[d++]=b[j];
                        for(int j=0;label[j]&&d<290;j++)details[d++]=label[j];
                        const char *c="\nLocation: ";for(int j=0;c[j]&&d<300;j++)details[d++]=c[j];
                        for(int j=0;g_path[j]&&d<470;j++)details[d++]=g_path[j];
                        details[d]=0;
                        append_file_size(details,&d,512,g_names[row],pOpenFile,pGetFileSizeEx,pCloseHandle);
                        if(pMsgBox)pMsgBox(g_window,details,"Properties",0);
                        g_last_file_row=-1;
                    }
                }
            }
        }
    }
}
