/* SentinelOS File Explorer: path-aware navigation and Windows-style double-click. */
typedef unsigned long long u64;
typedef unsigned int DWORD;
typedef int BOOL;
typedef const char *LPCSTR;

enum { SYS_Write=1, SYS_GetProc=9, SYS_Chdir=12, SYS_CreateProcess=4 };

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

typedef void *(*CreateWindowExA_t)(DWORD,LPCSTR,LPCSTR,DWORD,int,int,int,int,void*,void*,void*,void*);
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
typedef u64 (*GetTickCount64_t)(void);
typedef BOOL (*GetWindowRect_t)(void *, int *);
typedef int (*GetScrollPosition_t)(void);

typedef struct {
    DWORD hwnd, message;
    unsigned long long wParam;
    long long lParam;
    DWORD time;
    int pt_x, pt_y;
} MSG;

#define MAX_ENT 27 /* 27 entries + two header lines fit without scrolling */
static char g_names[MAX_ENT][64];
static int g_isdir[MAX_ENT];
static int g_count;
static char g_path[128];
static int g_win_y = 40;
static int g_last_row = -1;
static u64 g_last_click_ms;

static void path_init(void) {
    g_path[0]='C'; g_path[1]=':'; g_path[2]='\\'; g_path[3]=0;
}
static int is_root(void) {
    return g_path[0]=='C' && g_path[1]==':' && g_path[2]=='\\' && g_path[3]==0;
}
static int ends_with(const char *n, const char *ext) {
    int i=0,j=0; while(n[i])i++; while(ext[j])j++;
    if(i<j)return 0;
    for(int k=0;k<j;k++) {
        char a=n[i-j+k],b=ext[k];
        if(a>='A'&&a<='Z')a+=32;
        if(b>='A'&&b<='Z')b+=32;
        if(a!=b)return 0;
    }
    return 1;
}
static void copy_text(char *dst, const char *src, int cap) {
    int i=0; if(cap<=0)return;
    while(src[i] && i<cap-1){dst[i]=src[i];i++;}
    dst[i]=0;
}

/* Always change directory with a full C:\\ path, avoiding relative-CWD bugs. */
static void make_target_path(int row, char *out, int cap) {
    copy_text(out,g_path,cap);
    if(g_names[row][0]=='.' && g_names[row][1]=='.' && !g_names[row][2]) {
        int n=0; while(out[n])n++;
        if(n>3) {
            int sep=-1;
            for(int j=3;j<n;j++) if(out[j]=='\\') sep=j;
            if(sep<=2) out[3]=0;
            else out[sep]=0;
        }
        return;
    }
    int n=0; while(out[n])n++;
    if(n>0 && out[n-1]!='\\' && n<cap-1)out[n++]='\\';
    for(int k=0;g_names[row][k] && n<cap-1;k++)out[n++]=g_names[row][k];
    out[n]=0;
}
static void reload(QueryDirectoryA_t pQuery, TextOutA_t pText, InvalidateRect_t pInv) {
    /* Clear TextOutA's persistent line buffer before painting a new directory. */
    pText(0, 0, -1, "", 0);
    pText(0, 0, -2, "", 0);
    g_count=0;
    /* ext4 listing omits dot entries; synthesize .. so users can navigate back. */
    if(!is_root()) {
        copy_text(g_names[g_count],"..",64);
        g_isdir[g_count++]=1;
    }
    for(DWORD i=0; i<MAX_ENT && g_count<MAX_ENT; i++) {
        char name[64]; name[0]=0;
        DWORD t=pQuery(i,name,64);
        if(!t)break;
        int isdir=(t==2);
        if(is_root() && !isdir)continue;
        copy_text(g_names[g_count],name,64);
        g_isdir[g_count]=isdir;
        g_count++;
    }
    pText(0,0,0,g_path,-1);
    pText(0,0,1,"Click folder to open; .. goes up; double-click .exe to run",-1);
    for(int i=0;i<g_count;i++) {
        char line[80]; int p=0;
        const char *tag=g_isdir[i]?"[DIR] ":ends_with(g_names[i],".exe")?"[EXE] ":ends_with(g_names[i],".dll")?"[DLL] ":"[FIL] ";
        for(int j=0;tag[j] && p<78;j++)line[p++]=tag[j];
        for(int j=0;g_names[i][j] && p<78;j++)line[p++]=g_names[i][j];
        line[p]=0;
        pText(0,0,2+i,line,-1);
    }
    for(int i=g_count;i<28;i++)pText(0,0,2+i," ",-1);
    if(pInv)pInv(0,0,1);
}
static void launch_exe(const char *path, MessageBoxA_t pMsgBox) {
    u64 pid=sy3(SYS_CreateProcess,(u64)path,0,0);
    if((long long)pid<0 && pMsgBox)
        pMsgBox(0,"The executable could not be loaded. It may be an unsupported PE, need unavailable Windows DLLs, or exceed loader limits.","Cannot run executable",0);
}

void mainCRTStartup(void) {
    char k32[]="kernel32.dll";
    char u32[]="user32.dll";
    QueryDirectoryA_t pQuery=(QueryDirectoryA_t)gpa(k32,"QueryDirectoryA");
    SetCurrentDirectoryA_t pChdir=(SetCurrentDirectoryA_t)gpa(k32,"SetCurrentDirectoryA");
    CreateWindowExA_t pCreate=(CreateWindowExA_t)gpa(u32,"CreateWindowExA");
    ShowWindow_t pShow=(ShowWindow_t)gpa(u32,"ShowWindow");
    UpdateWindow_t pUpdate=(UpdateWindow_t)gpa(u32,"UpdateWindow");
    TextOutA_t pText=(TextOutA_t)gpa(u32,"TextOutA");
    GetMessageA_t pGetMsg=(GetMessageA_t)gpa(u32,"GetMessageA");
    TranslateMessage_t pTrans=(TranslateMessage_t)gpa(u32,"TranslateMessage");
    DispatchMessageA_t pDisp=(DispatchMessageA_t)gpa(u32,"DispatchMessageA");
    MessageBoxA_t pMsgBox=(MessageBoxA_t)gpa(u32,"MessageBoxA");
    InvalidateRect_t pInv=(InvalidateRect_t)gpa(u32,"InvalidateRect");
    GetTickCount64_t pTick=(GetTickCount64_t)gpa(k32,"GetTickCount64");
    GetWindowRect_t pGetRect=(GetWindowRect_t)gpa(u32,"GetWindowRect");
    GetScrollPosition_t pGetScroll=(GetScrollPosition_t)gpa(u32,"GetScrollPosition");
    if(!pQuery || !pCreate || !pShow || !pText || !pGetMsg) {
        print("fileexplorer: required API missing\n");
        for(;;)__asm__ volatile("pause");
    }

    path_init();
    void *hwnd=pCreate(0,"Explorer","File Explorer",0,40,g_win_y,760,520,0,0,0,0);
    pShow(hwnd,1);
    reload(pQuery,pText,pInv);
    if(pUpdate)pUpdate(hwnd);
    MSG msg;
    for(;;) {
        if(!pGetMsg(&msg,0,0,0))continue;
        if(pTrans)pTrans(&msg);
        if(pDisp)pDisp(&msg);
        if(msg.message!=0x0201 && msg.message!=0x0203)continue;
        /* MSG layout matches user32: time, pt_x, pt_y (screen coordinates). */
        /*
         * Match the actual client-area row geometry used by user32:
         * text rows start at window_y + 44, each row is 16px high.
         * Reject clicks outside the Explorer client content instead of
         * accidentally mapping title-bar/edge clicks to row zero.
         */
        int rect[4]={40,g_win_y,800,g_win_y+520};
        if(pGetRect) (void)pGetRect(0,rect);
        int client_top=rect[1]+44;
        int client_bottom=rect[3]-8;
        if(msg.pt_x < rect[0]+14 || msg.pt_x >= rect[2]-16 ||
           msg.pt_y < client_top || msg.pt_y >= client_bottom) {
            g_last_row=-1;
            continue;
        }
        int content_line=(msg.pt_y-client_top)/16;
        /* TextOutA rows may scroll; map the visible row back to the data row. */
        int scroll=pGetScroll ? pGetScroll() : 0;
        int row=scroll+content_line-2;
        if(row<0 || row>=g_count) {g_last_row=-1;continue;}
        u64 now=pTick?pTick():msg.time;
        int dbl=(msg.message==0x0203) ||
                (row==g_last_row && now>=g_last_click_ms && now-g_last_click_ms<=1000);
        g_last_row=row; g_last_click_ms=now;
        if(g_isdir[row]) {
            if(!dbl) continue;
            char target[128]; make_target_path(row,target,sizeof target);
            int ok=pChdir ? pChdir(target) : 0;
            if(!ok)ok=(sy3(SYS_Chdir,(u64)target,0,0)==0);
            if(ok) {
                copy_text(g_path,target,sizeof g_path);
                if(is_root())path_init();
                g_last_row=-1;
                g_last_click_ms=0;
                reload(pQuery,pText,pInv);
            } else if(pMsgBox) {
                pMsgBox(0,target,"Cannot open folder",0);
            }
        } else if(ends_with(g_names[row],".exe")) {
            if(dbl) {
                char target[128];
                make_target_path(row,target,sizeof target);
                launch_exe(target,pMsgBox);
            }
        } else if(dbl && pMsgBox) {
            pMsgBox(0,g_names[row],"File",0);
        }
    }
}
