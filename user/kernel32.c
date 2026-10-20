typedef unsigned long long u64;
typedef long long i64;
typedef unsigned int DWORD;
typedef unsigned short WORD;
typedef unsigned char BYTE;
typedef unsigned int UINT;
typedef int BOOL;
typedef void *HANDLE;
typedef void *LPVOID;
typedef const char *LPCSTR;
typedef const void *LPCVOID;
typedef char *LPSTR;
typedef long LONG;
typedef unsigned short K32_WCHAR;
typedef const K32_WCHAR *K32_LPCWSTR;
typedef K32_WCHAR *K32_LPWSTR;
static DWORD k32_last_error;
static DWORD k32_console_cp_in=65001, k32_console_cp_out=65001;
static DWORD k32_error_mode;
static int k32_eqi(const char *a, const char *b);
static DWORD k32_shutdown_level=0x280, k32_shutdown_flags=0;
static char k32_console_title[128]="SentinelOS";
static DWORD k32_priority_class=0x20u; static int k32_thread_priority=0;
typedef struct { HANDLE handle; DWORD flags; } K32_HANDLE_FLAGS; static K32_HANDLE_FLAGS k32_handle_flags[32];
static DWORD k32_console_mode=0x0007u; static BOOL k32_cursor_visible=1; static DWORD k32_cursor_size=25; static unsigned short k32_console_attr=7;

#define INVALID_HANDLE_VALUE ((HANDLE)(i64)-1)

enum {
    SYS_Exit=0, SYS_Write=1, SYS_Alloc=3, SYS_CreateProcess=4, SYS_OpenFile=5, SYS_ReadFile=6,
    SYS_QueryDir=7, SYS_GetInput=10, SYS_CreateThread=13, SYS_Wait=14, SYS_CloseFile=22
};

static u64 sy3(u64 n, u64 a, u64 b, u64 c) {
    u64 r;
    __asm__ volatile(
        "mov %[n], %%rax\nmov %[a], %%rdi\nmov %[b], %%rsi\nmov %[c], %%rdx\nint $0x80\nmov %%rax, %[r]"
        : [r]"=r"(r) : [n]"r"(n),[a]"r"(a),[b]"r"(b),[c]"r"(c)
        : "rax","rdi","rsi","rdx","memory","cc");
    return r;
}

__attribute__((dllexport)) void ExitProcess(DWORD code) {
    sy3(SYS_Exit, code, 0, 0);
    for(;;) __asm__ volatile("hlt");
}
__attribute__((dllexport)) HANDLE GetStdHandle(DWORD n) { (void)n; return (HANDLE)1; }
__attribute__((dllexport)) BOOL WriteFile(HANDLE h, LPCVOID buf, DWORD len, DWORD *w, void *ov) {
    (void)h;(void)ov;
    u64 n = sy3(SYS_Write, 1, (u64)buf, len);
    if (w) *w = (DWORD)n;
    return n != (u64)-1;
}
__attribute__((dllexport)) LPVOID VirtualAlloc(LPVOID a, u64 sz, DWORD t, DWORD p) {
    (void)t;(void)p; return (LPVOID)sy3(SYS_Alloc, (u64)a, sz, 0);
}
__attribute__((dllexport)) HANDLE CreateFileA(LPCSTR name, DWORD a, DWORD s, void *sec, DWORD d, DWORD f, HANDLE t) {
    (void)a;(void)s;(void)sec;(void)d;(void)f;(void)t;
    i64 h = (i64)sy3(SYS_OpenFile, (u64)name, 0, 0);
    return h < 0 ? INVALID_HANDLE_VALUE : (HANDLE)h;
}
__attribute__((dllexport)) BOOL ReadFile(HANDLE h, LPVOID buf, DWORD len, DWORD *rd, void *ov) {
    (void)ov;
    i64 n = (i64)sy3(SYS_ReadFile, (u64)h, (u64)buf, len);
    if (n < 0) return 0;
    if (rd) *rd = (DWORD)n;
    return 1;
}
__attribute__((dllexport)) BOOL CloseHandle(HANDLE h) { return sy3(SYS_CloseFile, (u64)h, 0, 0) != 0; }
__attribute__((dllexport)) DWORD QueryDirectoryA(DWORD index, LPSTR name, DWORD max) {
    return (DWORD)sy3(SYS_QueryDir, index, (u64)name, (u64)max);
}
__attribute__((dllexport)) BOOL SetCurrentDirectoryA(LPCSTR path) {
    return sy3(12, (u64)path, 0, 0) == 0;
}
__attribute__((dllexport)) DWORD GetLastError(void) { return k32_last_error; }
__attribute__((dllexport)) void SetLastError(DWORD e) { k32_last_error=e; }
__attribute__((dllexport)) u64 GetTickCount64(void) { return 0; }
__attribute__((dllexport)) void Sleep(DWORD ms) { (void)ms; }
__attribute__((dllexport)) BOOL PeekInput(void *out) {
    return (BOOL)sy3(SYS_GetInput, (u64)out, 0, 0);
}
__attribute__((dllexport)) int ExitWindowsEx(unsigned int flags, unsigned int reason) {
    (void)reason;
    return (int)sy3(11, (flags & 2) ? 1 : 0, 0, 0);
}
__attribute__((dllexport)) HANDLE CreateThread(
    void *sa, u64 stack, void *start, void *param, DWORD flags, DWORD *id)
{
    (void)sa;(void)stack;(void)flags;
    u64 h = sy3(SYS_CreateThread, (u64)start, (u64)param, 0);
    if (h == 0 || h == (u64)-1) return 0;
    if (id) *id = (DWORD)h;
    return (HANDLE)h;
}
__attribute__((dllexport)) DWORD WaitForSingleObject(HANDLE h, DWORD ms) {
    return (DWORD)sy3(SYS_Wait, (u64)h, (u64)ms, 0);
}


/* -------------------------------------------------------------------------
 * Additional Win32 compatibility exports.
 *
 * These exports intentionally provide a small, documented compatibility
 * subset. APIs requiring kernel services not present in SentinelOS return a
 * safe failure/default rather than pretending the service succeeded.
 * ------------------------------------------------------------------------- */
typedef unsigned char K32_BYTE;
typedef unsigned short K32_WORD;
typedef unsigned long long K32_SIZE_T;
typedef long K32_LONG;
typedef struct { K32_WORD wYear,wMonth,wDayOfWeek,wDay,wHour,wMinute,wSecond,wMilliseconds; } K32_SYSTEMTIME;
typedef struct { K32_LONG dwOemId; K32_LONG dwPageSize; void *lpMinimumApplicationAddress; void *lpMaximumApplicationAddress; K32_SIZE_T dwActiveProcessorMask; K32_LONG dwNumberOfProcessors; K32_LONG dwProcessorType; K32_LONG dwAllocationGranularity; K32_WORD wProcessorLevel; K32_WORD wProcessorRevision; } K32_SYSTEM_INFO;
typedef struct { K32_SIZE_T QuadPart; } K32_LARGE_INTEGER;
typedef struct { DWORD dwLowDateTime,dwHighDateTime; } K32_FILETIME;
static DWORD k32_heap_tag;
static char k32_cmdline[] = "sentinel.exe";
static K32_WCHAR k32_cmdline_w[] = {'s','e','n','t','i','n','e','l','.','e','x','e',0};
static void k32_wide_to_ansi(char *out, DWORD cap, K32_LPCWSTR in) {
    DWORD i=0; if (!out || !cap) return;
    if (in) while (in[i] && i+1<cap) { out[i]=in[i]<128?(char)in[i]:'?'; i++; }
    out[i]=0;
}
static DWORD k32_ansi_to_wide(K32_LPWSTR out, DWORD cap, const char *in) {
    DWORD i=0; if (!out || !cap) return 0;
    if (in) while (in[i] && i+1<cap) { out[i]=(unsigned char)in[i]; i++; }
    out[i]=0; return i;
}
static DWORD k32_copy_wide_path(const char *src, K32_LPWSTR out, DWORD cap) {
    DWORD n=0; while (src && src[n]) n++;
    if (!out || !cap) return n+1;
    k32_ansi_to_wide(out,cap,src);
    return n+1>cap ? n+1 : n;
}

__attribute__((dllexport)) DWORD GetCurrentProcessId(void) { return (DWORD)sy3(2,0,0,0); }
__attribute__((dllexport)) DWORD GetCurrentThreadId(void) { return 1; }
__attribute__((dllexport)) HANDLE GetCurrentProcess(void) { return (HANDLE)(i64)-1; }
__attribute__((dllexport)) HANDLE GetCurrentThread(void) { return (HANDLE)(i64)-2; }
__attribute__((dllexport)) HANDLE GetModuleHandleA(LPCSTR name){if(!name||k32_eqi(name,"sentinel.exe")||k32_eqi(name,"C:\\sentinel.exe"))return (HANDLE)1;k32_last_error=126;return 0;}
__attribute__((dllexport)) LPSTR GetCommandLineA(void) { return k32_cmdline; }
__attribute__((dllexport)) DWORD GetModuleFileNameA(HANDLE module, LPSTR out, DWORD cap) {
    const char *s = "C:\\sentinel.exe"; DWORD i=0; (void)module;
    if (!out || !cap) return 0;
    while (s[i] && i+1<cap) { out[i]=s[i]; i++; } out[i]=0; return i;
}
__attribute__((dllexport)) void *GetProcAddress(HANDLE module, LPCSTR name) {
    (void)module; (void)name; return (void *)0;
}
__attribute__((dllexport)) HANDLE LoadLibraryA(LPCSTR name) { (void)name; return (HANDLE)0; }
__attribute__((dllexport)) BOOL FreeLibrary(HANDLE module) { (void)module; return 0; }
__attribute__((dllexport)) BOOL VirtualFree(LPVOID address, u64 size, DWORD type) {
    (void)address; (void)size; (void)type; return 0;
}
__attribute__((dllexport)) BOOL VirtualProtect(LPVOID address, u64 size, DWORD protect, DWORD *old) {
    (void)address; (void)size; (void)protect; if (old) *old=0; return 0;
}
__attribute__((dllexport)) K32_SIZE_T VirtualQuery(LPCVOID address, void *info, K32_SIZE_T len) {
    (void)address; (void)info; (void)len; return 0;
}
__attribute__((dllexport)) DWORD GetCurrentDirectoryA(DWORD cap, LPSTR out) {
    const char *s="C:\\"; DWORD i=0; if (!out || !cap) return 0;
    while (s[i] && i+1<cap) { out[i]=s[i]; i++; } out[i]=0; return i;
}
__attribute__((dllexport)) BOOL CreateDirectoryA(LPCSTR path, void *security) {
    (void)security;
    if(!path||!*path){k32_last_error=87;return 0;}
    k32_last_error=50; return 0;
}
__attribute__((dllexport)) BOOL RemoveDirectoryA(LPCSTR path) { if(!path||!*path){k32_last_error=87;return 0;} k32_last_error=50; return 0; }
__attribute__((dllexport)) BOOL DeleteFileA(LPCSTR path) { if(!path||!*path){k32_last_error=87;return 0;} k32_last_error=50; return 0; }
__attribute__((dllexport)) BOOL MoveFileA(LPCSTR from, LPCSTR to) { if(!from||!*from||!to||!*to){k32_last_error=87;return 0;} k32_last_error=50; return 0; }
__attribute__((dllexport)) BOOL CopyFileA(LPCSTR from, LPCSTR to, BOOL fail_if_exists) {
    (void)fail_if_exists;
    if(!from||!*from||!to||!*to){k32_last_error=87;return 0;}
    k32_last_error=50; return 0;
}
__attribute__((dllexport)) DWORD GetFileAttributesA(LPCSTR path) {
    if(!path||!*path){k32_last_error=87;return 0xFFFFFFFFu;}
    const char *p=path;
    if((p[0]=='C'||p[0]=='c')&&p[1]==':')p+=2;
    while(*p=='\\'||*p=='/')p++;
    if(!*p){k32_last_error=0;return 0x10u;}
    for(const char *q=p;*q;q++)if(*q=='\\'||*q=='/'){k32_last_error=3;return 0xFFFFFFFFu;}
    char entry[256];
    for(DWORD i=0;i<256;i++){
        DWORD type=QueryDirectoryA(i,entry,sizeof(entry));
        if(!type)break;
        if(k32_eqi(entry,p)){k32_last_error=0;return type==2?0x10u:0x80u;}
    }
    k32_last_error=2;
    return 0xFFFFFFFFu;
}
__attribute__((dllexport)) BOOL SetFileAttributesA(LPCSTR path, DWORD attrs) { (void)attrs; if(!path||!*path){k32_last_error=87;return 0;} k32_last_error=50; return 0; }
__attribute__((dllexport)) DWORD GetFileSize(HANDLE file, DWORD *high) {
    (void)file; if (high) *high=0; return 0xFFFFFFFFu;
}
__attribute__((dllexport)) DWORD SetFilePointer(HANDLE file, LONG distance, LONG *high, DWORD method) {
    (void)file; (void)distance; (void)method; if (high) *high=0; return 0xFFFFFFFFu;
}
__attribute__((dllexport)) BOOL FlushFileBuffers(HANDLE file) { if(!file||file==INVALID_HANDLE_VALUE){k32_last_error=6;return 0;} k32_last_error=50; return 0; }
__attribute__((dllexport)) DWORD GetTempPathA(DWORD cap, LPSTR out) {
    const char *s="C:\\TEMP\\"; DWORD i=0; if (!out || !cap) return 0;
    while (s[i] && i+1<cap) { out[i]=s[i]; i++; } out[i]=0; return i+1;
}
__attribute__((dllexport)) DWORD GetFullPathNameA(LPCSTR path, DWORD cap, LPSTR out, LPSTR *part) {
    DWORD i=0; (void)part; if (!path || !out || !cap) return 0;
    while (path[i] && i+1<cap) { out[i]=path[i]; i++; } out[i]=0;
    return path[i] ? i+1 : i;
}
typedef struct { char name[32]; char value[128]; int used; } K32_ENV_ENTRY;
static K32_ENV_ENTRY k32_env[16]; static int k32_env_init;
static int k32_eqi(const char *a,const char *b){if(!a||!b)return 0;while(*a&&*b){char x=*a++,y=*b++;if(x>='a'&&x<='z')x-=32;if(y>='a'&&y<='z')y-=32;if(x!=y)return 0;}return *a==0&&*b==0;}
static void k32_env_boot(void){if(k32_env_init)return;k32_env_init=1;const char *ns[3]={"PATH","TEMP","OS"},*vs[3]={"C:\\;C:\\SYSTEM32","C:\\TEMP","SentinelOS"};for(int i=0;i<3;i++){int j=0;while(ns[i][j]&&j<31){k32_env[i].name[j]=ns[i][j];j++;}k32_env[i].name[j]=0;j=0;while(vs[i][j]&&j<127){k32_env[i].value[j]=vs[i][j];j++;}k32_env[i].value[j]=0;k32_env[i].used=1;}}
__attribute__((dllexport)) DWORD GetEnvironmentVariableA(LPCSTR name, LPSTR out, DWORD cap){k32_env_boot();if(!name||!*name){k32_last_error=87;return 0;}for(int i=0;i<16;i++)if(k32_env[i].used&&k32_eqi(k32_env[i].name,name)){DWORD n=0;while(k32_env[i].value[n])n++;if(!out||!cap)return n;if(cap<=n){k32_last_error=122;return n+1;}for(DWORD j=0;j<=n;j++)out[j]=k32_env[i].value[j];k32_last_error=0;return n;}if(out&&cap)out[0]=0;k32_last_error=203;return 0;}
__attribute__((dllexport)) BOOL SetEnvironmentVariableA(LPCSTR name,LPCSTR value){k32_env_boot();if(!name||!*name||name[0]=='='){k32_last_error=87;return 0;}int slot=-1;for(int i=0;i<16;i++){if(k32_env[i].used&&k32_eqi(k32_env[i].name,name)){slot=i;break;}if(!k32_env[i].used&&slot<0)slot=i;}if(!value){if(slot>=0)k32_env[slot].used=0;k32_last_error=0;return 1;}if(slot<0){k32_last_error=8;return 0;}int n=0;while(name[n]&&n<31)n++;if(name[n]){k32_last_error=87;return 0;}for(int j=0;j<n;j++)k32_env[slot].name[j]=name[j];k32_env[slot].name[n]=0;n=0;while(value[n]&&n<127)n++;if(value[n]){k32_last_error=87;return 0;}for(int j=0;j<n;j++)k32_env[slot].value[j]=value[j];k32_env[slot].value[n]=0;k32_env[slot].used=1;k32_last_error=0;return 1;}
__attribute__((dllexport)) BOOL GetComputerNameA(LPSTR out, DWORD *size) {
    const char *s="SENTINELOS"; DWORD i=0;
    if (!out || !size || *size < 10) { if (size) *size=10; return 0; }
    while (s[i]) { out[i]=s[i]; i++; } out[i]=0; *size=i; return 1;
}
__attribute__((dllexport)) void GetSystemInfo(K32_SYSTEM_INFO *s) {
    if (!s) return;
    s->dwOemId=0; s->dwPageSize=4096; s->lpMinimumApplicationAddress=(void *)0x10000;
    s->lpMaximumApplicationAddress=(void *)0x7FFFFFFFFFFFULL; s->dwActiveProcessorMask=1;
    s->dwNumberOfProcessors=1; s->dwProcessorType=8664; s->dwAllocationGranularity=65536;
    s->wProcessorLevel=6; s->wProcessorRevision=0;
}
__attribute__((dllexport)) DWORD GetVersion(void) { return 0x00000001u; }
__attribute__((dllexport)) BOOL QueryPerformanceCounter(K32_LARGE_INTEGER *out) {
    if (!out) return 0; out->QuadPart=0; return 1;
}
__attribute__((dllexport)) BOOL QueryPerformanceFrequency(K32_LARGE_INTEGER *out) {
    if (!out) return 0; out->QuadPart=1000; return 1;
}
__attribute__((dllexport)) DWORD GetTickCount(void) { return 0; }
__attribute__((dllexport)) BOOL GetExitCodeProcess(HANDLE process, DWORD *code) {
    (void)process; if (!code) return 0; *code=259; return 1;
}
__attribute__((dllexport)) BOOL TerminateProcess(HANDLE process, DWORD code) {
    (void)process; (void)code; return 0;
}
__attribute__((dllexport)) HANDLE GetProcessHeap(void) { return (HANDLE)&k32_heap_tag; }
__attribute__((dllexport)) LPVOID HeapAlloc(HANDLE heap, DWORD flags, K32_SIZE_T size) {
    (void)heap; (void)flags; return VirtualAlloc((LPVOID)0,size,0x3000,4);
}
__attribute__((dllexport)) BOOL HeapFree(HANDLE heap, DWORD flags, LPVOID mem) {
    (void)heap; (void)flags; (void)mem; return 0;
}
__attribute__((dllexport)) LPVOID HeapReAlloc(HANDLE heap, DWORD flags, LPVOID mem, K32_SIZE_T size) {
    (void)heap; (void)flags; (void)mem; (void)size; return (LPVOID)0;
}
__attribute__((dllexport)) K32_SIZE_T HeapSize(HANDLE heap, DWORD flags, LPCVOID mem) {
    (void)heap; (void)flags; (void)mem; return (K32_SIZE_T)-1;
}
typedef struct { void *ptr; K32_SIZE_T size; } K32_MEMENTRY;
static K32_MEMENTRY k32_mem_table[64];
static void k32_mem_register(void *p,K32_SIZE_T size){if(!p)return;int free_slot=-1;for(int i=0;i<64;i++){if(k32_mem_table[i].ptr==p){k32_mem_table[i].size=size;return;}if(!k32_mem_table[i].ptr&&free_slot<0)free_slot=i;}if(free_slot>=0){k32_mem_table[free_slot].ptr=p;k32_mem_table[free_slot].size=size;}}
static K32_SIZE_T k32_mem_size(void *p){for(int i=0;i<64;i++)if(k32_mem_table[i].ptr==p)return k32_mem_table[i].size;return 0;}
__attribute__((dllexport)) LPVOID LocalAlloc(DWORD flags,K32_SIZE_T size){(void)flags;void *p=VirtualAlloc((LPVOID)0,size,0x3000,4);k32_mem_register(p,size);return p;}
__attribute__((dllexport)) LPVOID LocalFree(LPVOID mem){(void)mem;return mem;}
__attribute__((dllexport)) LPVOID GlobalAlloc(DWORD flags,K32_SIZE_T size){(void)flags;void *p=VirtualAlloc((LPVOID)0,size,0x3000,4);k32_mem_register(p,size);return p;}
__attribute__((dllexport)) LPVOID GlobalFree(LPVOID mem){(void)mem;return mem;}
__attribute__((dllexport)) BOOL GetConsoleMode(HANDLE h,DWORD *mode){if(!h||!mode){k32_last_error=6;return 0;}*mode=k32_console_mode;return 1;}
__attribute__((dllexport)) BOOL SetConsoleMode(HANDLE h,DWORD mode){if(!h){k32_last_error=6;return 0;}k32_console_mode=mode;return 1;}
__attribute__((dllexport)) BOOL ReadConsoleA(HANDLE h, LPVOID buf, DWORD len, DWORD *read, void *reserved) {
    (void)h; (void)buf; (void)len; (void)reserved; if (read) *read=0; return 0;
}
__attribute__((dllexport)) BOOL WriteConsoleA(HANDLE h, LPCVOID buf, DWORD len, DWORD *written, void *reserved) {
    (void)reserved; return WriteFile(h,buf,len,written,0);
}
__attribute__((dllexport)) void GetSystemTime(K32_SYSTEMTIME *t) {
    if (!t) return; t->wYear=2026; t->wMonth=1; t->wDayOfWeek=0; t->wDay=1;
    t->wHour=0; t->wMinute=0; t->wSecond=0; t->wMilliseconds=0;
}
__attribute__((dllexport)) void GetLocalTime(K32_SYSTEMTIME *t) { GetSystemTime(t); }
typedef struct { int used, type, signaled, manual, owner; LONG count, maximum; char name[64]; } K32_SYNC_OBJECT;
static K32_SYNC_OBJECT k32_sync[32];
static HANDLE k32_sync_handle(int i) { return (HANDLE)(unsigned long long)(0x534E0000u+(unsigned)i+1u); }
static K32_SYNC_OBJECT *k32_sync_get(HANDLE h) { unsigned long long v=(unsigned long long)h; if(v<0x534E0001ULL||v>0x534E0020ULL)return 0; K32_SYNC_OBJECT *o=&k32_sync[(unsigned)(v-0x534E0001ULL)]; return o->used?o:0; }
static HANDLE k32_sync_create(int type,int manual,int initial,LONG count,LONG maximum,LPCSTR name) {
 if(name&&*name){for(int i=0;i<32;i++)if(k32_sync[i].used&&k32_sync[i].type==type&&k32_eqi(k32_sync[i].name,name))return k32_sync_handle(i);}
 for(int i=0;i<32;i++)if(!k32_sync[i].used){K32_SYNC_OBJECT *o=&k32_sync[i];o->used=1;o->type=type;o->manual=manual;o->signaled=initial;o->count=count;o->maximum=maximum;o->owner=0;int j=0;if(name)while(name[j]&&j<63){o->name[j]=name[j];j++;}o->name[j]=0;return k32_sync_handle(i);}k32_last_error=8;return 0; }
__attribute__((dllexport)) HANDLE CreateEventA(void *security,BOOL manual,BOOL initial,LPCSTR name){(void)security;return k32_sync_create(1,manual!=0,initial!=0,0,0,name);}
__attribute__((dllexport)) BOOL SetEvent(HANDLE h){K32_SYNC_OBJECT *o=k32_sync_get(h);if(!o||o->type!=1){k32_last_error=6;return 0;}o->signaled=1;return 1;}
__attribute__((dllexport)) BOOL ResetEvent(HANDLE h){K32_SYNC_OBJECT *o=k32_sync_get(h);if(!o||o->type!=1){k32_last_error=6;return 0;}o->signaled=0;return 1;}
__attribute__((dllexport)) HANDLE CreateMutexA(void *security,BOOL owner,LPCSTR name){(void)security;HANDLE h=k32_sync_create(2,1,owner!=0,0,0,name);K32_SYNC_OBJECT *o=k32_sync_get(h);if(o&&owner)o->owner=(int)GetCurrentThreadId();return h;}
__attribute__((dllexport)) BOOL ReleaseMutex(HANDLE h){K32_SYNC_OBJECT *o=k32_sync_get(h);if(!o||o->type!=2||o->owner!=(int)GetCurrentThreadId()){k32_last_error=288;return 0;}o->owner=0;o->signaled=0;return 1;}
__attribute__((dllexport)) HANDLE CreateSemaphoreA(void *security,LONG initial,LONG maximum,LPCSTR name){(void)security;if(initial<0||maximum<=0||initial>maximum){k32_last_error=87;return 0;}return k32_sync_create(3,1,initial>0,initial,maximum,name);}
__attribute__((dllexport)) BOOL ReleaseSemaphore(HANDLE h,LONG count,LONG *previous){K32_SYNC_OBJECT *o=k32_sync_get(h);if(!o||o->type!=3||count<=0||o->count>o->maximum-count){k32_last_error=87;return 0;}if(previous)*previous=o->count;o->count+=count;o->signaled=o->count>0;return 1;}
static int k32_sync_ready(K32_SYNC_OBJECT *o){if(!o)return 0;if(o->type==3)return o->count>0;if(o->type==2)return o->owner==0||o->owner==(int)GetCurrentThreadId();return o->signaled;}
static void k32_sync_consume(K32_SYNC_OBJECT *o){if(!o)return;if(o->type==3){if(o->count>0)o->count--;o->signaled=o->count>0;}else if(o->type==1&&!o->manual)o->signaled=0;else if(o->type==2){o->owner=(int)GetCurrentThreadId();o->signaled=1;}}
__attribute__((dllexport)) DWORD WaitForMultipleObjects(DWORD count,const HANDLE *handles,BOOL wait_all,DWORD ms){if(!count||count>64||!handles){k32_last_error=87;return 0xFFFFFFFFu;}for(DWORD i=0;i<count;i++)if(!k32_sync_get(handles[i])&&handles[i]!=(HANDLE)1&&handles[i]!=(HANDLE)2){k32_last_error=6;return 0xFFFFFFFFu;}if(wait_all){for(DWORD i=0;i<count;i++)if(!k32_sync_ready(k32_sync_get(handles[i])))return 0x102u;for(DWORD i=0;i<count;i++)k32_sync_consume(k32_sync_get(handles[i]));return 0;}for(DWORD i=0;i<count;i++)if(k32_sync_ready(k32_sync_get(handles[i]))){k32_sync_consume(k32_sync_get(handles[i]));return 0x102u+i;}return 0x102u;}
__attribute__((dllexport)) DWORD GetFileType(HANDLE h){if(!h||h==INVALID_HANDLE_VALUE)return 0;if(h==(HANDLE)1||h==(HANDLE)2)return 2;return 1;}


/* Compatibility export placeholders: signatures and semantics still need implementation. */
__attribute__((dllexport)) K32_LPWSTR GetCommandLineW(void) { return (K32_LPWSTR)k32_cmdline_w; }
__attribute__((dllexport)) HANDLE GetModuleHandleW(K32_LPCWSTR name){char n[256];if(!name)return GetModuleHandleA(0);k32_wide_to_ansi(n,sizeof(n),name);return GetModuleHandleA(n);}
__attribute__((dllexport)) DWORD GetModuleFileNameW(HANDLE module, K32_LPWSTR out, DWORD cap) {
    char narrow[512]; DWORD n=GetModuleFileNameA(module,narrow,sizeof(narrow));
    if (!out || !cap) return 0;
    DWORD copied=k32_ansi_to_wide(out,cap,narrow);
    return copied+1<n ? cap : copied;
}
__attribute__((dllexport)) HANDLE CreateFileW(K32_LPCWSTR name, DWORD access, DWORD share, void *security, DWORD creation, DWORD flags, HANDLE template_file) {
    char narrow[512]; if (!name) return INVALID_HANDLE_VALUE;
    k32_wide_to_ansi(narrow,sizeof(narrow),name);
    return CreateFileA(narrow,access,share,security,creation,flags,template_file);
}
__attribute__((dllexport)) BOOL DeleteFileW(K32_LPCWSTR path){char p[512];if(!path){k32_last_error=87;return 0;}k32_wide_to_ansi(p,sizeof(p),path);return DeleteFileA(p);}
__attribute__((dllexport)) BOOL MoveFileW(K32_LPCWSTR from,K32_LPCWSTR to){char a[512],b[512];if(!from||!to){k32_last_error=87;return 0;}k32_wide_to_ansi(a,sizeof(a),from);k32_wide_to_ansi(b,sizeof(b),to);return MoveFileA(a,b);}
__attribute__((dllexport)) BOOL CopyFileW(K32_LPCWSTR from,K32_LPCWSTR to,BOOL fail_if_exists){char a[512],b[512];if(!from||!to){k32_last_error=87;return 0;}k32_wide_to_ansi(a,sizeof(a),from);k32_wide_to_ansi(b,sizeof(b),to);return CopyFileA(a,b,fail_if_exists);}
__attribute__((dllexport)) BOOL CreateDirectoryW(K32_LPCWSTR path,void *security){char p[512];if(!path){k32_last_error=87;return 0;}k32_wide_to_ansi(p,sizeof(p),path);return CreateDirectoryA(p,security);}
__attribute__((dllexport)) BOOL RemoveDirectoryW(K32_LPCWSTR path){char p[512];if(!path){k32_last_error=87;return 0;}k32_wide_to_ansi(p,sizeof(p),path);return RemoveDirectoryA(p);}
__attribute__((dllexport)) DWORD GetCurrentDirectoryW(DWORD cap, K32_LPWSTR out) {
    char narrow[512]; DWORD n=GetCurrentDirectoryA(sizeof(narrow),narrow);
    if (!out || !cap) return n+1;
    if (n+1>cap) { k32_ansi_to_wide(out,cap,narrow); return n+1; }
    return k32_ansi_to_wide(out,cap,narrow);
}
__attribute__((dllexport)) BOOL SetCurrentDirectoryW(K32_LPCWSTR path) {
    char narrow[512]; if (!path) return 0;
    k32_wide_to_ansi(narrow,sizeof(narrow),path); return SetCurrentDirectoryA(narrow);
}
__attribute__((dllexport)) DWORD GetFullPathNameW(K32_LPCWSTR path, DWORD cap, K32_LPWSTR out, K32_LPWSTR *part) {
    char in[512], resolved[512]; if (!path) return 0;
    k32_wide_to_ansi(in,sizeof(in),path);
    DWORD n=GetFullPathNameA(in,sizeof(resolved),resolved,0);
    if (!out || !cap) return n+1;
    if (n+1>cap) { k32_ansi_to_wide(out,cap,resolved); if (part) *part=0; return n+1; }
    k32_ansi_to_wide(out,cap,resolved);
    if (part) { DWORD i=0,last=0; while (resolved[i]) { if (resolved[i]=='\\' || resolved[i]=='/') last=i+1; i++; } *part=out+last; }
    return n;
}
__attribute__((dllexport)) DWORD GetTempPathW(DWORD cap, K32_LPWSTR out) {
    char narrow[260]; DWORD n=GetTempPathA(sizeof(narrow),narrow);
    if (!out || !cap) return n;
    if (n>cap) { k32_ansi_to_wide(out,cap,narrow); return n; }
    k32_ansi_to_wide(out,cap,narrow); return n;
}
__attribute__((dllexport)) DWORD GetEnvironmentVariableW(K32_LPCWSTR name,K32_LPWSTR out,DWORD cap){char n[128],v[256];k32_wide_to_ansi(n,sizeof(n),name);DWORD z=GetEnvironmentVariableA(n,v,sizeof(v));if(!z){if(out&&cap)out[0]=0;return 0;}DWORD len=0;while(v[len])len++;if(!out||!cap)return len;if(cap<=len){k32_ansi_to_wide(out,cap,v);k32_last_error=122;return len+1;}k32_ansi_to_wide(out,cap,v);return len;}
__attribute__((dllexport)) BOOL SetEnvironmentVariableW(K32_LPCWSTR name,K32_LPCWSTR value){char n[128],v[256];if(!name)return 0;k32_wide_to_ansi(n,sizeof(n),name);if(!value)return SetEnvironmentVariableA(n,0);k32_wide_to_ansi(v,sizeof(v),value);return SetEnvironmentVariableA(n,v);}
__attribute__((dllexport)) DWORD GetFileAttributesW(K32_LPCWSTR path){char p[512];k32_wide_to_ansi(p,sizeof(p),path);return GetFileAttributesA(p);}
__attribute__((dllexport)) BOOL SetFileAttributesW(K32_LPCWSTR path,DWORD attrs){char p[512];k32_wide_to_ansi(p,sizeof(p),path);return SetFileAttributesA(p,attrs);}
__attribute__((dllexport)) unsigned long long GetTempFileNameA(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetTempFileNameW(void) { return 0; }
__attribute__((dllexport)) DWORD GetShortPathNameA(LPCSTR path,LPSTR out,DWORD cap){return GetFullPathNameA(path,cap,out,0);}
__attribute__((dllexport)) DWORD GetLongPathNameA(LPCSTR path,LPSTR out,DWORD cap){return GetFullPathNameA(path,cap,out,0);}
__attribute__((dllexport)) unsigned long long GetDiskFreeSpaceA(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetDiskFreeSpaceExA(void) { return 0; }
__attribute__((dllexport)) DWORD GetDriveTypeA(LPCSTR root) {
    if (!root || ((root[0]=='C' || root[0]=='c') && root[1]==':' && (root[2]=='\\' || root[2]=='/' || root[2]==0))) return 3;
    return 1;
}
__attribute__((dllexport)) DWORD GetLogicalDrives(void) { return 1u<<2; }
__attribute__((dllexport)) DWORD GetLogicalDriveStringsA(DWORD cap, LPSTR out) {
    if (!out || !cap) return 4;
    if (cap<5) { out[0]=0; return 4; }
    out[0]='C'; out[1]=':'; out[2]='\\'; out[3]=0; out[4]=0; return 4;
}
__attribute__((dllexport)) unsigned long long GetVolumeInformationA(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetFileTime(void) { return 0; }
__attribute__((dllexport)) unsigned long long SetFileTime(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetSystemTimeAsFileTime(void) { return 0; }
static int k32_leap(int y){return (y%4==0&&y%100!=0)||y%400==0;}
__attribute__((dllexport)) BOOL SystemTimeToFileTime(const K32_SYSTEMTIME *st,K32_FILETIME *ft){static const int mdays[12]={31,28,31,30,31,30,31,31,30,31,30,31};if(!st||!ft||st->wYear<1601||st->wMonth<1||st->wMonth>12||st->wHour>23||st->wMinute>59||st->wSecond>59||st->wMilliseconds>999)return 0;int max=mdays[st->wMonth-1]+(st->wMonth==2&&k32_leap(st->wYear));if(st->wDay<1||st->wDay>max)return 0;unsigned long long days=0;for(int y=1601;y<st->wYear;y++)days+=k32_leap(y)?366:365;for(int m=1;m<st->wMonth;m++)days+=mdays[m-1]+(m==2&&k32_leap(st->wYear));days+=st->wDay-1;unsigned long long ticks=days*864000000000ULL+((unsigned long long)st->wHour*3600+st->wMinute*60+st->wSecond)*10000000ULL+(unsigned long long)st->wMilliseconds*10000ULL;ft->dwLowDateTime=(DWORD)ticks;ft->dwHighDateTime=(DWORD)(ticks>>32);return 1;}
__attribute__((dllexport)) BOOL FileTimeToSystemTime(const K32_FILETIME *ft,K32_SYSTEMTIME *st){if(!ft||!st)return 0;unsigned long long ticks=((unsigned long long)ft->dwHighDateTime<<32)|ft->dwLowDateTime;unsigned long long days=ticks/864000000000ULL,rem=ticks%864000000000ULL;int year=1601;while(days>=(unsigned long long)(k32_leap(year)?366:365)){days-=k32_leap(year)?366:365;if(++year>9999)return 0;}static const int mdays[12]={31,28,31,30,31,30,31,31,30,31,30,31};int month=1;while(month<=12){int d=mdays[month-1]+(month==2&&k32_leap(year));if(days<(unsigned long long)d)break;days-=d;month++;}st->wYear=year;st->wMonth=month;st->wDay=(unsigned short)days+1;st->wDayOfWeek=(unsigned short)((((ticks/864000000000ULL)+1)%7));st->wHour=rem/36000000000ULL;rem%=36000000000ULL;st->wMinute=rem/600000000ULL;rem%=600000000ULL;st->wSecond=rem/10000000ULL;st->wMilliseconds=(rem%10000000ULL)/10000ULL;return 1;}
__attribute__((dllexport)) LONG CompareFileTime(const K32_FILETIME *a,const K32_FILETIME *b){if(!a||!b)return 0;if(a->dwHighDateTime<b->dwHighDateTime)return -1;if(a->dwHighDateTime>b->dwHighDateTime)return 1;if(a->dwLowDateTime<b->dwLowDateTime)return -1;if(a->dwLowDateTime>b->dwLowDateTime)return 1;return 0;}
__attribute__((dllexport)) unsigned long long GetFileAttributesExA(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetFileInformationByHandle(void) { return 0; }
__attribute__((dllexport)) unsigned long long SetEndOfFile(void) { return 0; }
__attribute__((dllexport)) BOOL GetOverlappedResult(HANDLE file, void *overlapped, DWORD *transferred, BOOL wait) { (void)file; (void)overlapped; (void)wait; if (transferred) *transferred=0; k32_last_error=997; return 0; }
__attribute__((dllexport)) unsigned long long CancelIo(void) { return 0; }
__attribute__((dllexport)) unsigned long long CreatePipe(void) { return 0; }
__attribute__((dllexport)) unsigned long long PeekNamedPipe(void) { return 0; }
__attribute__((dllexport)) BOOL SetHandleInformation(HANDLE h,DWORD mask,DWORD flags){if(!h||h==INVALID_HANDLE_VALUE)return 0;for(int i=0;i<32;i++){if(k32_handle_flags[i].handle==h){k32_handle_flags[i].flags=(k32_handle_flags[i].flags&~mask)|(flags&mask);return 1;}}for(int i=0;i<32;i++)if(!k32_handle_flags[i].handle){k32_handle_flags[i].handle=h;k32_handle_flags[i].flags=flags&mask;return 1;}return 0;}
__attribute__((dllexport)) BOOL GetHandleInformation(HANDLE h,DWORD *flags){if(!h||h==INVALID_HANDLE_VALUE||!flags)return 0;for(int i=0;i<32;i++)if(k32_handle_flags[i].handle==h){*flags=k32_handle_flags[i].flags;return 1;}for(int i=0;i<32;i++)if(!k32_handle_flags[i].handle){k32_handle_flags[i].handle=h;k32_handle_flags[i].flags=0;*flags=0;return 1;}return 0;}
__attribute__((dllexport)) BOOL DuplicateHandle(HANDLE source_process,HANDLE source,HANDLE target_process,HANDLE *target,DWORD access,BOOL inherit,DWORD options){(void)access;(void)inherit;(void)options;if((source_process==GetCurrentProcess()||source_process==(HANDLE)1)&&(target_process==GetCurrentProcess()||target_process==(HANDLE)1)&&source&&target){*target=source;return 1;}k32_last_error=50;return 0;}
typedef struct {HANDLE hProcess;HANDLE hThread;DWORD dwProcessId;DWORD dwThreadId;} K32_PROCESS_INFORMATION;
static const char *k32_module_basename(const char *s){const char *base=s;if(!s)return 0;for(;*s;s++)if(*s=='\\'||*s=='/')base=s+1;return base;}
static BOOL k32_create_process_name(const char *name,K32_PROCESS_INFORMATION *pi){if(!name||!*name){k32_last_error=87;return 0;}const char *base=k32_module_basename(name);i64 pid=(i64)sy3(SYS_CreateProcess,(u64)base,0,0);if(pid<0){k32_last_error=2;return 0;}if(pi){pi->hProcess=(HANDLE)(unsigned long long)pid;pi->hThread=0;pi->dwProcessId=(DWORD)pid;pi->dwThreadId=0;}k32_last_error=0;return 1;}
__attribute__((dllexport)) BOOL CreateProcessA(LPCSTR app,LPSTR cmd,void *process_security,void *thread_security,BOOL inherit,DWORD flags,void *environment,LPCSTR cwd,void *startup,void *process_info){
 (void)process_security;(void)thread_security;(void)inherit;(void)flags;(void)environment;(void)cwd;(void)startup;char name[256];const char *src=app;
 if(!src||!*src){if(!cmd||!*cmd){k32_last_error=87;return 0;}src=cmd;while(*src==' '||*src=='\\t')src++;int i=0;while(src[i]&&src[i]!=' '&&src[i]!='\\t'&&i<255){name[i]=src[i];i++;}name[i]=0;src=name;}return k32_create_process_name(src,(K32_PROCESS_INFORMATION*)process_info);
}
typedef struct { DWORD cb; char *lpReserved,*lpDesktop,*lpTitle; DWORD dwX,dwY,dwXSize,dwYSize,dwXCountChars,dwYCountChars,dwFillAttribute,dwFlags; unsigned short wShowWindow,cbReserved2; unsigned char *lpReserved2; HANDLE hStdInput,hStdOutput,hStdError; } K32_STARTUPINFOA;
__attribute__((dllexport)) void GetStartupInfoA(K32_STARTUPINFOA *si){if(!si)return;si->cb=sizeof(*si);si->lpReserved=si->lpDesktop=si->lpTitle=0;si->dwX=si->dwY=si->dwXSize=si->dwYSize=0;si->dwXCountChars=80;si->dwYCountChars=25;si->dwFillAttribute=7;si->dwFlags=0;si->wShowWindow=1;si->cbReserved2=0;si->lpReserved2=0;si->hStdInput=(HANDLE)0;si->hStdOutput=(HANDLE)1;si->hStdError=(HANDLE)2;}
static char k32_env_block_a[16*162]; static K32_WCHAR k32_env_block_w[16*162];
static void k32_build_env_block(void){k32_env_boot();unsigned int p=0;for(int i=0;i<16;i++)if(k32_env[i].used){for(unsigned int j=0;k32_env[i].name[j]&&p+1<sizeof(k32_env_block_a);j++)k32_env_block_a[p++]=k32_env[i].name[j];if(p+1<sizeof(k32_env_block_a))k32_env_block_a[p++]='=';for(unsigned int j=0;k32_env[i].value[j]&&p+1<sizeof(k32_env_block_a);j++)k32_env_block_a[p++]=k32_env[i].value[j];if(p+1<sizeof(k32_env_block_a))k32_env_block_a[p++]=0;}k32_env_block_a[p]=0;for(unsigned int i=0;i<=p;i++)k32_env_block_w[i]=(unsigned char)k32_env_block_a[i];}
__attribute__((dllexport)) LPSTR GetEnvironmentStringsA(void){k32_build_env_block();return k32_env_block_a;}
__attribute__((dllexport)) BOOL FreeEnvironmentStringsA(LPCSTR p){if(p!=k32_env_block_a){k32_last_error=87;return 0;}return 1;}
__attribute__((dllexport)) unsigned long long GetProcessTimes(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetThreadTimes(void) { return 0; }
__attribute__((dllexport)) DWORD GetPriorityClass(HANDLE process){if(process!=GetCurrentProcess()&&process!=(HANDLE)1){k32_last_error=87;return 0;}return k32_priority_class;}
__attribute__((dllexport)) BOOL SetPriorityClass(HANDLE process,DWORD priority){if((process!=GetCurrentProcess()&&process!=(HANDLE)1)||(priority!=0x40u&&priority!=0x4000u&&priority!=0x8000u&&priority!=0x80u&&priority!=0x20u&&priority!=0x100u)){k32_last_error=87;return 0;}k32_priority_class=priority;return 1;}
__attribute__((dllexport)) int GetThreadPriority(HANDLE thread){if(thread!=GetCurrentThread()&&thread!=(HANDLE)1){k32_last_error=87;return 0x7FFFFFFF;}return k32_thread_priority;}
__attribute__((dllexport)) BOOL SetThreadPriority(HANDLE thread,int priority){if((thread!=GetCurrentThread()&&thread!=(HANDLE)1)||priority< -15||priority>15){k32_last_error=87;return 0;}k32_thread_priority=priority;return 1;}
__attribute__((dllexport)) DWORD GetCurrentProcessorNumber(void){return 0;}
__attribute__((dllexport)) void GetNativeSystemInfo(K32_SYSTEM_INFO *info){GetSystemInfo(info);}
__attribute__((dllexport)) BOOL IsProcessorFeaturePresent(DWORD feature){switch(feature){case 0:return 1;case 1:return 1;case 2:return 1;case 3:return 1;case 10:return 1;case 23:return 1;default:return 0;}}
__attribute__((dllexport)) DWORD GetSystemDirectoryA(LPSTR out, DWORD cap) {
    const char *p="C:\\System32"; DWORD i=0; if (!out || !cap) return 11;
    while (p[i] && i+1<cap) { out[i]=p[i]; i++; } out[i]=0; return p[i] ? 12 : i;
}
__attribute__((dllexport)) DWORD GetWindowsDirectoryA(LPSTR out, DWORD cap) {
    const char *p="C:\\Windows"; DWORD i=0; if (!out || !cap) return 10;
    while (p[i] && i+1<cap) { out[i]=p[i]; i++; } out[i]=0; return p[i] ? 11 : i;
}
__attribute__((dllexport)) DWORD GetUserDefaultLCID(void){return 0x0409;}
__attribute__((dllexport)) DWORD GetACP(void) { return 1252u; }


/* Additional Win32 entry points. These ABI placeholders are intentionally conservative;
   replace with OS-backed implementations as the corresponding subsystems mature. */
__attribute__((dllexport)) DWORD GetOEMCP(void) { return 437u; }
__attribute__((dllexport)) DWORD GetConsoleCP(void){return k32_console_cp_in;}
__attribute__((dllexport)) DWORD GetConsoleOutputCP(void){return k32_console_cp_out;}
__attribute__((dllexport)) BOOL SetConsoleCP(DWORD cp){if(!cp){k32_last_error=87;return 0;}k32_console_cp_in=cp;return 1;}
__attribute__((dllexport)) BOOL SetConsoleOutputCP(DWORD cp){if(!cp){k32_last_error=87;return 0;}k32_console_cp_out=cp;return 1;}
__attribute__((dllexport)) unsigned long long GetFileSizeEx(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetFileInformationByHandleEx(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetFinalPathNameByHandleA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetFinalPathNameByHandleW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) BOOL GetVolumePathNameA(LPCSTR path,LPSTR out,DWORD cap){if(!path||!out||cap<4||!((path[0]=='C'||path[0]=='c')&&path[1]==':'&&(path[2]=='\\'||path[2]=='/'||path[2]==0))){k32_last_error=3;return 0;}out[0]='C';out[1]=':';out[2]='\\';out[3]=0;return 1;}
__attribute__((dllexport)) BOOL GetVolumePathNameW(K32_LPCWSTR path,K32_LPWSTR out,DWORD cap){if(!path||!out||cap<4||!((path[0]=='C'||path[0]=='c')&&path[1]==':'&&(path[2]=='\\'||path[2]=='/'||path[2]==0))){k32_last_error=3;return 0;}out[0]='C';out[1]=':';out[2]='\\';out[3]=0;return 1;}
__attribute__((dllexport)) unsigned long long GetVolumePathNamesForVolumeNameA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetVolumePathNamesForVolumeNameW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) DWORD GetDriveTypeW(K32_LPCWSTR root) {
    if (!root || ((root[0]=='C' || root[0]=='c') && root[1]==':' && (root[2]=='\\' || root[2]=='/' || root[2]==0))) return 3;
    return 1;
}
__attribute__((dllexport)) DWORD GetLogicalDriveStringsW(DWORD cap, K32_LPWSTR out) {
    if (!out || !cap) return 4;
    if (cap<5) { out[0]=0; return 4; }
    out[0]='C'; out[1]=':'; out[2]='\\'; out[3]=0; out[4]=0; return 4;
}
__attribute__((dllexport)) unsigned long long GetVolumeInformationW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetDiskFreeSpaceW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetDiskFreeSpaceExW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetFileAttributesExW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) DWORD GetTempPath2A(DWORD cap,LPSTR out){return GetTempPathA(cap,out);}
__attribute__((dllexport)) DWORD GetTempPath2W(DWORD cap,K32_LPWSTR out){return GetTempPathW(cap,out);}
__attribute__((dllexport)) DWORD GetShortPathNameW(K32_LPCWSTR path,K32_LPWSTR out,DWORD cap){char a[512],b[512];k32_wide_to_ansi(a,sizeof(a),path);DWORD n=GetShortPathNameA(a,b,sizeof(b));if(out&&cap)k32_ansi_to_wide(out,cap,b);return n;}
__attribute__((dllexport)) DWORD GetLongPathNameW(K32_LPCWSTR path,K32_LPWSTR out,DWORD cap){char a[512],b[512];k32_wide_to_ansi(a,sizeof(a),path);DWORD n=GetLongPathNameA(a,b,sizeof(b));if(out&&cap)k32_ansi_to_wide(out,cap,b);return n;}
__attribute__((dllexport)) DWORD GetSystemDirectoryW(K32_LPWSTR out, DWORD cap) { return k32_copy_wide_path("C:\\System32",out,cap); }
__attribute__((dllexport)) DWORD GetWindowsDirectoryW(K32_LPWSTR out, DWORD cap) { return k32_copy_wide_path("C:\\Windows",out,cap); }
__attribute__((dllexport)) BOOL GetComputerNameW(K32_LPWSTR out,DWORD *size){const char *name="SENTINELOS";DWORD n=10;if(!size){k32_last_error=87;return 0;}if(!out||*size<n+1){*size=n+1;k32_last_error=122;return 0;}k32_ansi_to_wide(out,*size,name);*size=n;return 1;}
__attribute__((dllexport)) BOOL GetUserNameA(LPSTR out,DWORD *size){const char *name="user";DWORD n=4;if(!size)return 0;if(!out||*size<n+1){*size=n+1;k32_last_error=122;return 0;}for(DWORD i=0;i<=n;i++)out[i]=name[i];*size=n+1;return 1;}
__attribute__((dllexport)) BOOL GetUserNameW(K32_LPWSTR out,DWORD *size){const char *name="user";DWORD n=4;if(!size)return 0;if(!out||*size<n+1){*size=n+1;k32_last_error=122;return 0;}k32_ansi_to_wide(out,*size,name);*size=n+1;return 1;}
__attribute__((dllexport)) BOOL GetComputerNameExA(int type,LPSTR out,DWORD *size){if(type!=0&&type!=1&&type!=4&&type!=5){if(size)*size=0;k32_last_error=50;return 0;}return GetComputerNameA(out,size);}
__attribute__((dllexport)) BOOL GetComputerNameExW(int type,K32_LPWSTR out,DWORD *size){if(type!=0&&type!=1&&type!=4&&type!=5){if(size)*size=0;k32_last_error=50;return 0;}return GetComputerNameW(out,size);}
__attribute__((dllexport)) K32_LPWSTR GetEnvironmentStringsW(void){k32_build_env_block();return k32_env_block_w;}
__attribute__((dllexport)) BOOL FreeEnvironmentStringsW(K32_LPCWSTR p){if(p!=k32_env_block_w){k32_last_error=87;return 0;}return 1;}
__attribute__((dllexport)) DWORD ExpandEnvironmentStringsA(LPCSTR src,LPSTR dst,DWORD cap){if(!src){k32_last_error=87;return 0;}DWORD n=0;const char *p=src;while(*p){if(*p=='%'){const char *e=p+1;while(*e&&*e!='%')e++;if(*e=='%'&&e>p+1){char key[64],val[256];unsigned int z=(unsigned int)(e-p-1);if(z<sizeof(key)){for(unsigned int j=0;j<z;j++)key[j]=p[j+1];key[z]=0;DWORD got=GetEnvironmentVariableA(key,val,sizeof(val));if(got&&got<sizeof(val)){for(DWORD j=0;val[j];j++){if(dst&&n+1<cap)dst[n]=val[j];n++;}p=e+1;continue;}}}}if(dst&&n+1<cap)dst[n]=*p;n++;p++;}if(dst&&cap)dst[n<cap?n:cap-1]=0;return n+1;}
__attribute__((dllexport)) DWORD ExpandEnvironmentStringsW(K32_LPCWSTR src,K32_LPWSTR dst,DWORD cap){char a[512],b[1024];k32_wide_to_ansi(a,sizeof(a),src);DWORD n=ExpandEnvironmentStringsA(a,b,sizeof(b));if(dst&&cap)k32_ansi_to_wide(dst,cap,b);return n;}
__attribute__((dllexport)) BOOL GetModuleHandleExA(DWORD flags,LPCSTR name,HANDLE *module){if(!module)return 0;if((flags&4u)!=0){*module=(HANDLE)1;return 1;}if(!name||k32_eqi(name,"sentinel.exe")||k32_eqi(name,"C:\\sentinel.exe")){*module=(HANDLE)1;return 1;}*module=0;k32_last_error=126;return 0;}
__attribute__((dllexport)) BOOL GetModuleHandleExW(DWORD flags,K32_LPCWSTR name,HANDLE *module){char n[256];if(!name)return GetModuleHandleExA(flags,0,module);k32_wide_to_ansi(n,sizeof(n),name);return GetModuleHandleExA(flags,n,module);}
typedef struct { DWORD cb; K32_LPWSTR lpReserved,lpDesktop,lpTitle; DWORD dwX,dwY,dwXSize,dwYSize,dwXCountChars,dwYCountChars,dwFillAttribute,dwFlags; unsigned short wShowWindow,cbReserved2; unsigned char *lpReserved2; HANDLE hStdInput,hStdOutput,hStdError; } K32_STARTUPINFOW;
__attribute__((dllexport)) void GetStartupInfoW(K32_STARTUPINFOW *si){if(!si)return;si->cb=sizeof(*si);si->lpReserved=si->lpDesktop=si->lpTitle=0;si->dwX=si->dwY=si->dwXSize=si->dwYSize=0;si->dwXCountChars=80;si->dwYCountChars=25;si->dwFillAttribute=7;si->dwFlags=0;si->wShowWindow=1;si->cbReserved2=0;si->lpReserved2=0;si->hStdInput=(HANDLE)0;si->hStdOutput=(HANDLE)1;si->hStdError=(HANDLE)2;}
__attribute__((dllexport)) DWORD GetProcessId(HANDLE process) { return process==GetCurrentProcess() ? GetCurrentProcessId() : 0; }
__attribute__((dllexport)) DWORD GetThreadId(HANDLE thread) { if (thread==GetCurrentThread()) return GetCurrentThreadId(); return (u64)thread>0 && (u64)thread<0xFFFFFFFFULL ? (DWORD)(u64)thread : 0; }
__attribute__((dllexport)) DWORD GetProcessIdOfThread(HANDLE thread) { return GetThreadId(thread) ? GetCurrentProcessId() : 0; }
__attribute__((dllexport)) HANDLE OpenProcess(DWORD access,BOOL inherit,DWORD pid){(void)access;(void)inherit;if(pid==GetCurrentProcessId())return GetCurrentProcess();k32_last_error=87;return 0;}
__attribute__((dllexport)) HANDLE OpenThread(DWORD access,BOOL inherit,DWORD tid){(void)access;(void)inherit;if(tid==GetCurrentThreadId())return GetCurrentThread();k32_last_error=87;return 0;}
__attribute__((dllexport)) HANDLE GetCurrentProcessToken(void){return GetCurrentProcess();}
__attribute__((dllexport)) HANDLE GetCurrentThreadToken(void){return GetCurrentThread();}
__attribute__((dllexport)) DWORD GetProcessVersion(DWORD pid){if(pid && pid!=GetCurrentProcessId())return 0;return 0x00010000u;}
__attribute__((dllexport)) BOOL GetProcessShutdownParameters(DWORD *level,DWORD *flags){if(!level||!flags){k32_last_error=87;return 0;}*level=k32_shutdown_level;*flags=k32_shutdown_flags;return 1;}
__attribute__((dllexport)) BOOL SetProcessShutdownParameters(DWORD level,DWORD flags){if(level>0x3FFu){k32_last_error=87;return 0;}k32_shutdown_level=level;k32_shutdown_flags=flags;return 1;}
__attribute__((dllexport)) DWORD SetErrorMode(DWORD mode){DWORD old=k32_error_mode;k32_error_mode=mode;return old;}
__attribute__((dllexport)) DWORD GetErrorMode(void){return k32_error_mode;}
__attribute__((dllexport)) unsigned long long GetSystemTimeAdjustment(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetSystemTimePreciseAsFileTime(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) DWORD GetThreadLocale(void){return 0x0409;}
__attribute__((dllexport)) BOOL SetThreadLocale(DWORD locale){return locale==0x0409;}
__attribute__((dllexport)) unsigned short GetUserDefaultLangID(void){return 0x0409;}
__attribute__((dllexport)) unsigned short GetThreadUILanguage(void){return 0x0409;}
__attribute__((dllexport)) unsigned short GetUserDefaultUILanguage(void){return 0x0409;}
__attribute__((dllexport)) unsigned short GetSystemDefaultUILanguage(void){return 0x0409;}
__attribute__((dllexport)) unsigned short GetSystemDefaultLangID(void){return 0x0409;}
__attribute__((dllexport)) DWORD GetSystemDefaultLCID(void){return 0x0409;}
__attribute__((dllexport)) DWORD GetConsoleTitleA(LPSTR out,DWORD cap){DWORD n=0;while(k32_console_title[n])n++;if(!out||!cap)return n;if(cap<=n){for(DWORD i=0;i+1<cap;i++)out[i]=k32_console_title[i];out[cap-1]=0;return 0;}for(DWORD i=0;i<=n;i++)out[i]=k32_console_title[i];return n;}
__attribute__((dllexport)) DWORD GetConsoleTitleW(K32_LPWSTR out,DWORD cap){DWORD n=0;while(k32_console_title[n])n++;if(!out||!cap)return n;k32_ansi_to_wide(out,cap,k32_console_title);return cap<=n?0:n;}
__attribute__((dllexport)) BOOL SetConsoleTitleW(K32_LPCWSTR title){if(!title){k32_last_error=87;return 0;}k32_wide_to_ansi(k32_console_title,sizeof(k32_console_title),title);return 1;}
typedef struct { short X,Y; } K32_COORD;
typedef struct { short Left,Top,Right,Bottom; } K32_SMALL_RECT;
typedef struct { K32_COORD dwSize,dwCursorPosition; unsigned short wAttributes; K32_SMALL_RECT srWindow; K32_COORD dwMaximumWindowSize; } K32_CONSOLE_SCREEN_BUFFER_INFO;
__attribute__((dllexport)) BOOL GetConsoleScreenBufferInfo(HANDLE h,K32_CONSOLE_SCREEN_BUFFER_INFO *info){if(!h||!info)return 0;info->dwSize.X=80;info->dwSize.Y=25;info->dwCursorPosition.X=0;info->dwCursorPosition.Y=0;info->wAttributes=k32_console_attr;info->srWindow.Left=0;info->srWindow.Top=0;info->srWindow.Right=79;info->srWindow.Bottom=24;info->dwMaximumWindowSize.X=80;info->dwMaximumWindowSize.Y=25;return 1;}
typedef struct { DWORD cbSize; K32_COORD dwSize,dwCursorPosition; unsigned short wAttributes; K32_SMALL_RECT srWindow; K32_COORD dwMaximumWindowSize; unsigned short wPopupAttributes; BOOL bFullscreenSupported; unsigned int ColorTable[16]; } K32_CONSOLE_SCREEN_BUFFER_INFOEX;
__attribute__((dllexport)) BOOL GetConsoleScreenBufferInfoEx(HANDLE h,K32_CONSOLE_SCREEN_BUFFER_INFOEX *info){if(!h||!info||info->cbSize<sizeof(*info))return 0;info->dwSize.X=80;info->dwSize.Y=25;info->dwCursorPosition.X=0;info->dwCursorPosition.Y=0;info->wAttributes=k32_console_attr;info->srWindow.Left=0;info->srWindow.Top=0;info->srWindow.Right=79;info->srWindow.Bottom=24;info->dwMaximumWindowSize.X=80;info->dwMaximumWindowSize.Y=25;info->wPopupAttributes=0xF5;info->bFullscreenSupported=0;for(int i=0;i<16;i++)info->ColorTable[i]=(unsigned int)i*0x111111u;return 1;}
__attribute__((dllexport)) BOOL SetConsoleScreenBufferInfoEx(HANDLE h,const K32_CONSOLE_SCREEN_BUFFER_INFOEX *info){if(!h||!info||info->cbSize<sizeof(*info))return 0;k32_console_attr=info->wAttributes;return 1;}
typedef struct { DWORD dwSize; BOOL bVisible; } K32_CONSOLE_CURSOR_INFO;
__attribute__((dllexport)) BOOL GetConsoleCursorInfo(HANDLE h,K32_CONSOLE_CURSOR_INFO *info){if(!h||!info)return 0;info->dwSize=k32_cursor_size;info->bVisible=k32_cursor_visible;return 1;}
__attribute__((dllexport)) BOOL SetConsoleCursorInfo(HANDLE h,const K32_CONSOLE_CURSOR_INFO *info){if(!h||!info||info->dwSize<1||info->dwSize>100)return 0;k32_cursor_size=info->dwSize;k32_cursor_visible=info->bVisible!=0;return 1;}
__attribute__((dllexport)) HANDLE GetConsoleWindow(void){return (HANDLE)1;}
__attribute__((dllexport)) DWORD GetConsoleProcessList(DWORD *list,DWORD cap){if(!list||!cap)return 1;list[0]=GetCurrentProcessId();return 1;}
__attribute__((dllexport)) BOOL GetNumberOfConsoleInputEvents(HANDLE h,DWORD *count){if(!h||!count)return 0;*count=0;return 1;}
__attribute__((dllexport)) BOOL GetNumberOfConsoleMouseButtons(DWORD *count){if(!count)return 0;*count=3;return 1;}
__attribute__((dllexport)) unsigned long long GetLargestConsoleWindowSize(HANDLE h){(void)h;return ((unsigned long long)25<<16)|80;}
__attribute__((dllexport)) K32_COORD GetConsoleFontSize(HANDLE h,DWORD index){(void)index;K32_COORD c={8,16};if(!h){c.X=0;c.Y=0;}return c;}
typedef struct { DWORD nFont; K32_COORD dwFontSize; } K32_CONSOLE_FONT_INFO;
__attribute__((dllexport)) BOOL GetCurrentConsoleFont(HANDLE h,BOOL max,K32_CONSOLE_FONT_INFO *info){(void)max;if(!h||!info)return 0;info->nFont=0;info->dwFontSize.X=8;info->dwFontSize.Y=16;return 1;}
typedef struct { DWORD cbSize,nFont; K32_COORD dwFontSize; unsigned int FontFamily,FontWeight; K32_WCHAR FaceName[32]; } K32_CONSOLE_FONT_INFOEX;
__attribute__((dllexport)) BOOL GetCurrentConsoleFontEx(HANDLE h,BOOL max,K32_CONSOLE_FONT_INFOEX *info){(void)max;if(!h||!info||info->cbSize<sizeof(*info))return 0;info->nFont=0;info->dwFontSize.X=8;info->dwFontSize.Y=16;info->FontFamily=54;info->FontWeight=400;const char *name="Terminal";for(int i=0;i<32;i++)info->FaceName[i]=(unsigned char)(name[i]?name[i]:0);return 1;}
__attribute__((dllexport)) BOOL SetCurrentConsoleFontEx(HANDLE h,BOOL max,const K32_CONSOLE_FONT_INFOEX *info){(void)max;return h&&info&&info->cbSize>=sizeof(*info)&&info->dwFontSize.X>0&&info->dwFontSize.Y>0;}
typedef struct { DWORD cbSize,HistoryBufferSize,NumberOfHistoryBuffers; BOOL dwFlags; } K32_CONSOLE_HISTORY_INFO;
__attribute__((dllexport)) BOOL GetConsoleHistoryInfo(K32_CONSOLE_HISTORY_INFO *info){if(!info||info->cbSize<sizeof(*info))return 0;info->HistoryBufferSize=50;info->NumberOfHistoryBuffers=4;info->dwFlags=0;return 1;}
__attribute__((dllexport)) BOOL SetConsoleHistoryInfo(const K32_CONSOLE_HISTORY_INFO *info){return info&&info->cbSize>=sizeof(*info)&&info->HistoryBufferSize>0&&info->NumberOfHistoryBuffers>0;}
__attribute__((dllexport)) unsigned long long GetConsoleCommandHistoryA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleCommandHistoryW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleAliasesA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleAliasesW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleAliasesLengthA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleAliasesLengthW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleAliasA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleAliasW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleAliasExesA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleAliasExesW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleAliasExesLengthA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleAliasExesLengthW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) DWORD GetConsoleInputExeNameA(DWORD cap,LPSTR out){const char *n="sentinel.exe";DWORD len=11;if(!out||cap<=len)return len+1;for(DWORD i=0;i<=len;i++)out[i]=n[i];return len;}
__attribute__((dllexport)) DWORD GetConsoleInputExeNameW(DWORD cap,K32_LPWSTR out){const char *n="sentinel.exe";DWORD len=11;if(!out||cap<=len)return len+1;k32_ansi_to_wide(out,cap,n);return len;}
__attribute__((dllexport)) DWORD GetConsoleOutputExeNameA(DWORD cap,LPSTR out){return GetConsoleInputExeNameA(cap,out);}
__attribute__((dllexport)) DWORD GetConsoleOutputExeNameW(DWORD cap,K32_LPWSTR out){return GetConsoleInputExeNameW(cap,out);}
__attribute__((dllexport)) unsigned long long CreateHardLinkA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long CreateHardLinkW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long CreateSymbolicLinkA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long CreateSymbolicLinkW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long SetFileValidData(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long LockFile(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long UnlockFile(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long LockFileEx(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long UnlockFileEx(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetCompressedFileSizeA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetCompressedFileSizeW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) DWORD SearchPathA(LPCSTR path,LPCSTR file,LPCSTR ext,DWORD cap,LPSTR out,LPSTR *part){(void)path;if(!file)return 0;char joined[512];DWORD i=0;while(file[i]&&i+1<sizeof(joined)){joined[i]=file[i];i++;}joined[i]=0;if(i==0||!out||!cap)return 0;if(ext&&i+1<sizeof(joined)){int has=0;for(DWORD j=0;j<i;j++)if(joined[j]=='.')has=1;if(!has){DWORD j=0;while(ext[j]&&i+1<sizeof(joined))joined[i++]=ext[j++];joined[i]=0;}}DWORD n=GetFullPathNameA(joined,cap,out,part);return n;}
__attribute__((dllexport)) DWORD SearchPathW(K32_LPCWSTR path,K32_LPCWSTR file,K32_LPCWSTR ext,DWORD cap,K32_LPWSTR out,K32_LPWSTR *part){(void)path;(void)part;char f[512],e[64],b[512];k32_wide_to_ansi(f,sizeof(f),file);k32_wide_to_ansi(e,sizeof(e),ext);DWORD n=SearchPathA(0,f,ext?e:0,sizeof(b),b,0);if(out&&cap)k32_ansi_to_wide(out,cap,b);return n;}
__attribute__((dllexport)) DWORD GetModuleFileNameExA(HANDLE process,HANDLE module,LPSTR out,DWORD cap){if(process!=(HANDLE)(i64)-1&&process!=(HANDLE)1)return 0;return GetModuleFileNameA(module,out,cap);}
__attribute__((dllexport)) DWORD GetModuleFileNameExW(HANDLE process,HANDLE module,K32_LPWSTR out,DWORD cap){if(process!=(HANDLE)(i64)-1&&process!=(HANDLE)1)return 0;return GetModuleFileNameW(module,out,cap);}
__attribute__((dllexport)) unsigned long long HeapValidate(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long HeapCompact(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetProcessHeaps(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long LocalReAlloc(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) K32_SIZE_T LocalSize(HANDLE mem){return k32_mem_size(mem);}
__attribute__((dllexport)) unsigned long long GlobalReAlloc(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) K32_SIZE_T GlobalSize(HANDLE mem){return k32_mem_size(mem);}
__attribute__((dllexport)) LPVOID GlobalLock(HANDLE mem){return mem;}
__attribute__((dllexport)) BOOL GlobalUnlock(HANDLE mem){return mem!=0&&k32_mem_size(mem)!=0;}
__attribute__((dllexport)) HANDLE GlobalHandle(LPCVOID mem){return (HANDLE)mem;}
__attribute__((dllexport)) BOOL GetBinaryTypeA(LPCSTR file,DWORD *type){if(!file||!type)return 0;DWORD n=0;while(file[n])n++;if(n<4||file[n-4]!='.'||((file[n-3]|32)!='e')||((file[n-2]|32)!='x')||((file[n-1]|32)!='e'))return 0;*type=0;return 1;}

__attribute__((dllexport)) BOOL GetBinaryTypeW(K32_LPCWSTR file,DWORD *type){char p[512];if(!file)return 0;k32_wide_to_ansi(p,sizeof(p),file);return GetBinaryTypeA(p,type);}

__attribute__((dllexport)) int lstrlenA(LPCSTR s){int n=0;if(s)while(s[n])n++;return n;}

__attribute__((dllexport)) int lstrlenW(K32_LPCWSTR s){int n=0;if(s)while(s[n])n++;return n;}

__attribute__((dllexport)) int lstrcmpA(LPCSTR a,LPCSTR c){if(!a)a="";if(!c)c="";while(*a&&*a==*c){a++;c++;}return (unsigned char)*a-(unsigned char)*c;}

__attribute__((dllexport)) int lstrcmpiA(LPCSTR a,LPCSTR c){if(!a)a="";if(!c)c="";while(*a&&(((*a>='A'&&*a<='Z')?*a+32:*a)==((*c>='A'&&*c<='Z')?*c+32:*c))){a++;c++;}return (unsigned char)((*a>='A'&&*a<='Z')?*a+32:*a)-(unsigned char)((*c>='A'&&*c<='Z')?*c+32:*c);}

__attribute__((dllexport)) int lstrcmpW(K32_LPCWSTR a,K32_LPCWSTR c){if(!a||!c)return a?1:c?-1:0;while(*a&&*a==*c){a++;c++;}return (int)*a-(int)*c;}

__attribute__((dllexport)) int lstrcmpiW(K32_LPCWSTR a,K32_LPCWSTR c){if(!a||!c)return a?1:c?-1:0;while(*a){unsigned short x=*a,y=*c;if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return (int)x-(int)y;if(!*c)return 1;a++;c++;}return *c?-1:0;}

__attribute__((dllexport)) LPSTR lstrcpyA(LPSTR d,LPCSTR s){if(!d)return d;int i=0;if(s)while((d[i]=s[i])!=0)i++;else d[0]=0;return d;}

__attribute__((dllexport)) LPSTR lstrcpynA(LPSTR d,LPCSTR s,int n){if(!d||n<=0)return d;int i=0;if(s)while(i<n-1&&s[i]){d[i]=s[i];i++;}d[i]=0;return d;}

__attribute__((dllexport)) LPSTR lstrcatA(LPSTR d,LPCSTR s){if(!d)return d;int i=0;while(d[i])i++;if(s)for(int j=0;s[j];j++)d[i++]=s[j];d[i]=0;return d;}

__attribute__((dllexport)) LPSTR CharLowerA(LPSTR s){unsigned long long v=(unsigned long long)s;if(v<=255){unsigned char c=(unsigned char)v;return (LPSTR)(unsigned long long)(c>='A'&&c<='Z'?c+32:c);}if(s)for(int i=0;s[i];i++)if(s[i]>='A'&&s[i]<='Z')s[i]+=32;return s;}

__attribute__((dllexport)) LPSTR CharUpperA(LPSTR s){unsigned long long v=(unsigned long long)s;if(v<=255){unsigned char c=(unsigned char)v;return (LPSTR)(unsigned long long)(c>='a'&&c<='z'?c-32:c);}if(s)for(int i=0;s[i];i++)if(s[i]>='a'&&s[i]<='z')s[i]-=32;return s;}

__attribute__((dllexport)) DWORD CharLowerBuffA(LPSTR s,DWORD n){if(!s)return 0;for(DWORD i=0;i<n&&s[i];i++)if(s[i]>='A'&&s[i]<='Z')s[i]+=32;return n;}

__attribute__((dllexport)) DWORD CharUpperBuffA(LPSTR s,DWORD n){if(!s)return 0;for(DWORD i=0;i<n&&s[i];i++)if(s[i]>='a'&&s[i]<='z')s[i]-=32;return n;}

__attribute__((dllexport)) K32_LPWSTR CharLowerW(K32_LPWSTR s){unsigned long long v=(unsigned long long)s;if(v<=255){unsigned short c=(unsigned short)v;return (K32_LPWSTR)(unsigned long long)(c>='A'&&c<='Z'?c+32:c);}if(s)for(int i=0;s[i];i++)if(s[i]>='A'&&s[i]<='Z')s[i]+=32;return s;}

__attribute__((dllexport)) K32_LPWSTR CharUpperW(K32_LPWSTR s){unsigned long long v=(unsigned long long)s;if(v<=255){unsigned short c=(unsigned short)v;return (K32_LPWSTR)(unsigned long long)(c>='a'&&c<='z'?c-32:c);}if(s)for(int i=0;s[i];i++)if(s[i]>='a'&&s[i]<='z')s[i]-=32;return s;}

__attribute__((dllexport)) DWORD CharLowerBuffW(K32_LPWSTR s,DWORD n){if(!s)return 0;for(DWORD i=0;i<n&&s[i];i++)if(s[i]>='A'&&s[i]<='Z')s[i]+=32;return n;}

__attribute__((dllexport)) DWORD CharUpperBuffW(K32_LPWSTR s,DWORD n){if(!s)return 0;for(DWORD i=0;i<n&&s[i];i++)if(s[i]>='a'&&s[i]<='z')s[i]-=32;return n;}

__attribute__((dllexport)) BOOL IsCharAlphaA(char c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z');}

__attribute__((dllexport)) BOOL IsCharAlphaNumericA(char c){return IsCharAlphaA(c)||(c>='0'&&c<='9');}

__attribute__((dllexport)) BOOL IsCharUpperA(char c){return c>='A'&&c<='Z';}

__attribute__((dllexport)) BOOL IsCharLowerA(char c){return c>='a'&&c<='z';}

__attribute__((dllexport)) BOOL IsCharAlphaW(K32_WCHAR c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z');}

__attribute__((dllexport)) BOOL IsCharAlphaNumericW(K32_WCHAR c){return IsCharAlphaW(c)||(c>='0'&&c<='9');}

__attribute__((dllexport)) BOOL IsCharUpperW(K32_WCHAR c){return c>='A'&&c<='Z';}

__attribute__((dllexport)) BOOL IsCharLowerW(K32_WCHAR c){return c>='a'&&c<='z';}

__attribute__((dllexport)) LPSTR CharNextA(LPCSTR s){return (LPSTR)(s&&*s?s+1:s);}

__attribute__((dllexport)) LPSTR CharPrevA(LPCSTR start,LPCSTR cur){return (LPSTR)(start&&cur>start?cur-1:start);}

__attribute__((dllexport)) K32_LPWSTR CharNextW(K32_LPCWSTR s){return (K32_LPWSTR)(s&&*s?s+1:s);}

__attribute__((dllexport)) K32_LPWSTR CharPrevW(K32_LPCWSTR start,K32_LPCWSTR cur){return (K32_LPWSTR)(start&&cur>start?cur-1:start);}

__attribute__((dllexport)) LPVOID EncodePointer(LPVOID p){return p;}

__attribute__((dllexport)) LPVOID DecodePointer(LPVOID p){return p;}

__attribute__((dllexport)) BOOL IsBadReadPtr(LPCVOID p,K32_SIZE_T n){(void)n;return p==0;}

__attribute__((dllexport)) BOOL IsBadWritePtr(LPVOID p,K32_SIZE_T n){(void)n;return p==0;}

__attribute__((dllexport)) BOOL IsBadCodePtr(void *p){return p==0;}

__attribute__((dllexport)) K32_SIZE_T IsBadStringPtrA(LPCSTR s,K32_SIZE_T max){if(!s)return max?1:0;for(K32_SIZE_T i=0;i<max;i++)if(!s[i])return 0;return max;}

__attribute__((dllexport)) K32_SIZE_T IsBadStringPtrW(K32_LPCWSTR s,K32_SIZE_T max){if(!s)return max?1:0;for(K32_SIZE_T i=0;i<max;i++)if(!s[i])return 0;return max;}

__attribute__((dllexport)) BOOL GetStringTypeA(DWORD locale,DWORD type,LPCSTR src,int len,unsigned short *out){(void)locale;if(!src||!out||len<0)return 0;for(int i=0;i<len;i++){unsigned short v=0;char c=src[i];if(c>='A'&&c<='Z')v|=1;if(c>='a'&&c<='z')v|=2;if(c>='0'&&c<='9')v|=4;if(c==' '||c=='\t')v|=8;if(c>=32&&c<127)v|=0x10;out[i]=type==1?v:(unsigned short)c;}return 1;}

__attribute__((dllexport)) BOOL GetStringTypeW(DWORD locale,DWORD type,K32_LPCWSTR src,int len,unsigned short *out){(void)locale;if(!src||!out||len<0)return 0;for(int i=0;i<len;i++){unsigned short c=src[i],v=0;if(c>='A'&&c<='Z')v|=1;if(c>='a'&&c<='z')v|=2;if(c>='0'&&c<='9')v|=4;if(c==' '||c=='\t')v|=8;if(c>=32&&c<127)v|=0x10;out[i]=type==1?v:c;}return 1;}

__attribute__((dllexport)) int CompareStringA(DWORD locale,DWORD flags,LPCSTR a,int na,LPCSTR c,int nc){(void)locale;if(!a||!c)return 0;int i=0;while((na<0?a[i]!=0:i<na)&&(nc<0?c[i]!=0:i<nc)){unsigned char x=a[i],y=c[i];if(flags&1){if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;}if(x<y)return 1;if(x>y)return 3;i++;}int ea=na<0?!a[i]:i>=na,ec=nc<0?!c[i]:i>=nc;return ea?(ec?2:1):3;}

__attribute__((dllexport)) int CompareStringW(DWORD locale,DWORD flags,K32_LPCWSTR a,int na,K32_LPCWSTR c,int nc){(void)locale;if(!a||!c)return 0;int i=0;while((na<0?a[i]!=0:i<na)&&(nc<0?c[i]!=0:i<nc)){unsigned short x=a[i],y=c[i];if(flags&1){if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;}if(x<y)return 1;if(x>y)return 3;i++;}int ea=na<0?!a[i]:i>=na,ec=nc<0?!c[i]:i>=nc;return ea?(ec?2:1):3;}

__attribute__((dllexport)) K32_LPWSTR lstrcpyW(K32_LPWSTR d,K32_LPCWSTR s){if(!d)return d;int i=0;if(s)while((d[i]=s[i])!=0)i++;else d[0]=0;return d;}

__attribute__((dllexport)) K32_LPWSTR lstrcpynW(K32_LPWSTR d,K32_LPCWSTR s,int n){if(!d||n<=0)return d;int i=0;if(s)while(i<n-1&&s[i]){d[i]=s[i];i++;}d[i]=0;return d;}

__attribute__((dllexport)) K32_LPWSTR lstrcatW(K32_LPWSTR d,K32_LPCWSTR s){if(!d)return d;int i=0;while(d[i])i++;if(s)for(int j=0;s[j];j++)d[i++]=s[j];d[i]=0;return d;}

__attribute__((dllexport)) BOOL CharToOemA(LPCSTR src,LPSTR dst){if(!src||!dst)return 0;int i=0;do{dst[i]=src[i];}while(src[i++]);return 1;}

__attribute__((dllexport)) BOOL OemToCharA(LPCSTR src,LPSTR dst){return CharToOemA(src,dst);}

__attribute__((dllexport)) BOOL CharToOemW(K32_LPCWSTR src,LPSTR dst){if(!src||!dst)return 0;int i=0;while(src[i]){dst[i]=src[i]<256?(char)src[i]:'?';i++;}dst[i]=0;return 1;}

__attribute__((dllexport)) BOOL OemToCharW(LPCSTR src,K32_LPWSTR dst){if(!src||!dst)return 0;int i=0;while(src[i]){dst[i]=(unsigned char)src[i];i++;}dst[i]=0;return 1;}

__attribute__((dllexport)) BOOL CharToOemBuffA(LPCSTR src,LPSTR dst,DWORD n){if(!src||!dst)return 0;for(DWORD i=0;i<n;i++)dst[i]=src[i];return 1;}

__attribute__((dllexport)) BOOL OemToCharBuffA(LPCSTR src,LPSTR dst,DWORD n){return CharToOemBuffA(src,dst,n);}

__attribute__((dllexport)) BOOL CharToOemBuffW(K32_LPCWSTR src,LPSTR dst,DWORD n){if(!src||!dst)return 0;for(DWORD i=0;i<n;i++)dst[i]=src[i]<256?(char)src[i]:'?';return 1;}

__attribute__((dllexport)) BOOL OemToCharBuffW(LPCSTR src,K32_LPWSTR dst,DWORD n){if(!src||!dst)return 0;for(DWORD i=0;i<n;i++)dst[i]=(unsigned char)src[i];return 1;}

__attribute__((dllexport)) int MulDiv(int a,int b,int d){if(!d)return -1;long long v=(long long)a*b;v+=((v<0)^(d<0))?-(long long)(d<0?-d:d)/2:(long long)(d<0?-d:d)/2;return (int)(v/d);}

__attribute__((dllexport)) DWORD GetCurrentTime(void){return GetTickCount();}

typedef struct { DWORD MaxCharSize; unsigned char DefaultChar[2],LeadByte[12]; } K32_CPINFO;
__attribute__((dllexport)) BOOL GetCPInfo(DWORD cp,K32_CPINFO *info){if(!info||(cp!=0&&cp!=65001&&cp!=1252&&cp!=437&&cp!=1200)){k32_last_error=87;return 0;}info->MaxCharSize=cp==65001?4:(cp==1200?2:1);info->DefaultChar[0]='?';info->DefaultChar[1]=0;for(int i=0;i<12;i++)info->LeadByte[i]=0;return 1;}

typedef struct { DWORD MaxCharSize,DefaultChar[1],LeadByte[6]; char CodePageName[260]; } K32_CPINFOEXA;
__attribute__((dllexport)) BOOL GetCPInfoExA(DWORD cp,DWORD flags,K32_CPINFOEXA *info){(void)flags;if(!info)return 0;K32_CPINFO base;if(!GetCPInfo(cp,&base))return 0;info->MaxCharSize=base.MaxCharSize;info->DefaultChar[0]='?';for(int i=0;i<6;i++)info->LeadByte[i]=0;const char *n=cp==65001?"Unicode (UTF-8)":cp==1252?"Western European (Windows)":cp==437?"OEM United States":"Unicode";int i=0;while(n[i]&&i<259){info->CodePageName[i]=n[i];i++;}info->CodePageName[i]=0;return 1;}

typedef struct { DWORD MaxCharSize,DefaultChar[1],LeadByte[6]; K32_WCHAR CodePageName[260]; } K32_CPINFOEXW;
__attribute__((dllexport)) BOOL GetCPInfoExW(DWORD cp,DWORD flags,K32_CPINFOEXW *info){if(!info)return 0;K32_CPINFOEXA a;if(!GetCPInfoExA(cp,flags,&a))return 0;info->MaxCharSize=a.MaxCharSize;info->DefaultChar[0]='?';for(int i=0;i<6;i++)info->LeadByte[i]=0;int i=0;while(a.CodePageName[i]&&i<259){info->CodePageName[i]=(unsigned char)a.CodePageName[i];i++;}info->CodePageName[i]=0;return 1;}

__attribute__((dllexport)) BOOL GetProcessAffinityMask(HANDLE process,K32_SIZE_T *process_mask,K32_SIZE_T *system_mask){if(!process_mask||!system_mask||(process!=GetCurrentProcess()&&process!=(HANDLE)1)){k32_last_error=87;return 0;}*process_mask=1;*system_mask=1;return 1;}

__attribute__((dllexport)) BOOL SetProcessAffinityMask(HANDLE process,K32_SIZE_T mask){if((process!=GetCurrentProcess()&&process!=(HANDLE)1)||mask!=1){k32_last_error=87;return 0;}return 1;}

__attribute__((dllexport)) K32_SIZE_T GetThreadAffinityMask(HANDLE thread){if(thread!=GetCurrentThread()&&thread!=(HANDLE)1){k32_last_error=87;return 0;}return 1;}

__attribute__((dllexport)) K32_SIZE_T SetThreadAffinityMask(HANDLE thread,K32_SIZE_T mask){if((thread!=GetCurrentThread()&&thread!=(HANDLE)1)||mask!=1){k32_last_error=87;return 0;}return 1;}

__attribute__((dllexport)) BOOL GetProcessPriorityBoost(HANDLE process,BOOL *disabled){if(!disabled||(process!=GetCurrentProcess()&&process!=(HANDLE)1))return 0;*disabled=0;return 1;}

__attribute__((dllexport)) BOOL SetProcessPriorityBoost(HANDLE process,BOOL disable){(void)disable;return process==GetCurrentProcess()||process==(HANDLE)1;}

__attribute__((dllexport)) BOOL GetThreadPriorityBoost(HANDLE thread,BOOL *disabled){if(!disabled||(thread!=GetCurrentThread()&&thread!=(HANDLE)1))return 0;*disabled=0;return 1;}

__attribute__((dllexport)) BOOL SetThreadPriorityBoost(HANDLE thread,BOOL disable){(void)disable;return thread==GetCurrentThread()||thread==(HANDLE)1;}

typedef struct { unsigned short Group,Number,Reserved; } K32_PROCESSOR_NUMBER;
__attribute__((dllexport)) void GetCurrentProcessorNumberEx(K32_PROCESSOR_NUMBER *number){if(number){number->Group=0;number->Number=0;number->Reserved=0;}}

__attribute__((dllexport)) DWORD SetThreadIdealProcessor(HANDLE thread,DWORD processor){if(thread!=GetCurrentThread()&&thread!=(HANDLE)1)return 0xFFFFFFFFu;return processor==0?0:0xFFFFFFFFu;}

__attribute__((dllexport)) BOOL GetThreadIdealProcessorEx(HANDLE thread,K32_PROCESSOR_NUMBER *number){if(!number||(thread!=GetCurrentThread()&&thread!=(HANDLE)1))return 0;number->Group=0;number->Number=0;number->Reserved=0;return 1;}

__attribute__((dllexport)) BOOL SetThreadIdealProcessorEx(HANDLE thread,const K32_PROCESSOR_NUMBER *number,K32_PROCESSOR_NUMBER *previous){if(!number||(thread!=GetCurrentThread()&&thread!=(HANDLE)1)||number->Group||number->Number)return 0;if(previous){previous->Group=0;previous->Number=0;previous->Reserved=0;}return 1;}

__attribute__((dllexport)) BOOL IsWow64Process(HANDLE process,BOOL *wow64){if(!wow64||(process!=GetCurrentProcess()&&process!=(HANDLE)1))return 0;*wow64=0;return 1;}

__attribute__((dllexport)) BOOL IsWow64Process2(HANDLE process,unsigned short *process_machine,unsigned short *native_machine){if(!process_machine||!native_machine||(process!=GetCurrentProcess()&&process!=(HANDLE)1))return 0;*process_machine=0;*native_machine=0x8664;return 1;}

__attribute__((dllexport)) BOOL GetProcessHandleCount(HANDLE process,DWORD *count){if(!count||(process!=GetCurrentProcess()&&process!=(HANDLE)1))return 0;*count=1;return 1;}

__attribute__((dllexport)) BOOL GetThreadIOPendingFlag(HANDLE thread,BOOL *pending){if(!pending||(thread!=GetCurrentThread()&&thread!=(HANDLE)1))return 0;*pending=0;return 1;}

typedef struct { unsigned long long ReadOperationCount,WriteOperationCount,OtherOperationCount,ReadTransferCount,WriteTransferCount,OtherTransferCount; } K32_IO_COUNTERS;
__attribute__((dllexport)) BOOL GetProcessIoCounters(HANDLE process,K32_IO_COUNTERS *c){if(!c||(process!=GetCurrentProcess()&&process!=(HANDLE)1))return 0;for(int i=0;i<6;i++)((unsigned long long*)c)[i]=0;return 1;}

__attribute__((dllexport)) BOOL GetThreadDescription(HANDLE thread,K32_LPWSTR *description){if(!description||(thread!=GetCurrentThread()&&thread!=(HANDLE)1))return 0;static K32_WCHAR name[]={'S','e','n','t','i','n','e','l','O','S',' ','t','h','r','e','a','d',0};*description=name;return 1;}

__attribute__((dllexport)) BOOL SetThreadDescription(HANDLE thread,K32_LPCWSTR description){return (thread==GetCurrentThread()||thread==(HANDLE)1)&&description!=0;}

__attribute__((dllexport)) LONG GetCurrentPackageId(DWORD *length,void *buffer){(void)buffer;if(length)*length=0;return 15700;}

__attribute__((dllexport)) LONG GetCurrentPackageFullName(DWORD *length,K32_LPWSTR name){if(length)*length=0;(void)name;return 15700;}

__attribute__((dllexport)) LONG GetCurrentPackageFamilyName(DWORD *length,K32_LPWSTR name){if(length)*length=0;(void)name;return 15700;}

__attribute__((dllexport)) LONG GetCurrentApplicationUserModelId(DWORD *length,K32_LPWSTR id){if(length)*length=0;(void)id;return 15700;}

__attribute__((dllexport)) BOOL GetSystemTimeAdjustmentPrecise(unsigned long long *adjustment,unsigned long long *increment,BOOL *disabled){if(!adjustment||!increment||!disabled)return 0;*adjustment=10000000;*increment=10000000;*disabled=1;return 1;}

__attribute__((dllexport)) BOOL GetThreadInformation(HANDLE thread,int info_class,void *buffer,DWORD size){(void)info_class;if(thread!=GetCurrentThread()&&thread!=(HANDLE)1)return 0;if(!buffer||size==0)return 0;return 0;}

__attribute__((dllexport)) BOOL SetThreadInformation(HANDLE thread,int info_class,const void *buffer,DWORD size){(void)info_class;(void)buffer;(void)size;return thread==GetCurrentThread()||thread==(HANDLE)1?0:0;}

__attribute__((dllexport)) BOOL GetProcessWorkingSetSize(HANDLE process,K32_SIZE_T *min,K32_SIZE_T *max){if(!min||!max||(process!=GetCurrentProcess()&&process!=(HANDLE)1))return 0;*min=0;*max=0;return 1;}

__attribute__((dllexport)) BOOL SetProcessWorkingSetSize(HANDLE process,K32_SIZE_T min,K32_SIZE_T max){(void)min;(void)max;return process==GetCurrentProcess()||process==(HANDLE)1;}

__attribute__((dllexport)) BOOL EmptyWorkingSet(HANDLE process){return process==GetCurrentProcess()||process==(HANDLE)1;}

__attribute__((dllexport)) BOOL GetProcessMitigationPolicy(HANDLE process,int policy,void *buffer,K32_SIZE_T size){(void)policy;if((process!=GetCurrentProcess()&&process!=(HANDLE)1)||!buffer||!size)return 0;for(K32_SIZE_T i=0;i<size;i++)((unsigned char*)buffer)[i]=0;return 1;}

__attribute__((dllexport)) BOOL SetProcessMitigationPolicy(int policy,const void *buffer,K32_SIZE_T size){(void)policy;(void)buffer;(void)size;return 0;}

__attribute__((dllexport)) BOOL GetThreadLocaleName(K32_LPWSTR name,int cap){if(!name||cap<9)return 0;const char *s="en-US";int i=0;while(s[i]){name[i]=(unsigned char)s[i];i++;}name[i]=0;return 1;}

__attribute__((dllexport)) int GetUserDefaultGeoName(K32_LPWSTR name,int cap){if(!name||cap<3)return 3;name[0]='F';name[1]='I';name[2]=0;return 3;}

__attribute__((dllexport)) int GetGeoInfoA(int geo,int type,LPSTR out,int cap,DWORD lang){(void)geo;(void)type;(void)lang;const char *s="Finland";if(!out||cap<8)return 8;for(int i=0;i<=7;i++)out[i]=s[i];return 8;}

__attribute__((dllexport)) int GetGeoInfoW(int geo,int type,K32_LPWSTR out,int cap,DWORD lang){(void)geo;(void)type;(void)lang;const char *s="Finland";if(!out||cap<8)return 8;for(int i=0;i<=7;i++)out[i]=(unsigned char)s[i];return 8;}

__attribute__((dllexport)) DWORD GetTimeZoneInformation(void *info){if(info){for(int i=0;i<64;i++)((unsigned char*)info)[i]=0;}return 0;}

__attribute__((dllexport)) DWORD GetDynamicTimeZoneInformation(void *info){if(info){for(int i=0;i<128;i++)((unsigned char*)info)[i]=0;}return 0;}

__attribute__((dllexport)) BOOL SystemTimeToTzSpecificLocalTime(const void *zone,const K32_SYSTEMTIME *utc,K32_SYSTEMTIME *local){(void)zone;if(!utc||!local)return 0;*local=*utc;return 1;}

__attribute__((dllexport)) BOOL TzSpecificLocalTimeToSystemTime(const void *zone,const K32_SYSTEMTIME *local,K32_SYSTEMTIME *utc){(void)zone;if(!local||!utc)return 0;*utc=*local;return 1;}

__attribute__((dllexport)) int MultiByteToWideChar(DWORD cp,DWORD flags,LPCSTR src,int len,K32_LPWSTR out,int cap){(void)flags;if(!src)return 0;int n=len<0?lstrlenA(src)+1:len;if(!out||cap==0)return n;if(cap<n){k32_last_error=122;return 0;}for(int i=0;i<n;i++)out[i]=(unsigned char)src[i];return n;}

__attribute__((dllexport)) int WideCharToMultiByte(DWORD cp,DWORD flags,K32_LPCWSTR src,int len,LPSTR out,int cap,LPCSTR defchar,BOOL *used){(void)cp;(void)flags;if(!src)return 0;int n=len<0?lstrlenW(src)+1:len;if(!out||cap==0)return n;if(cap<n){k32_last_error=122;return 0;}BOOL bad=0;for(int i=0;i<n;i++){if(src[i]>255){out[i]=defchar&&defchar[0]?defchar[0]:'?';bad=1;}else out[i]=(char)src[i];}if(used)*used=bad;return n;}

__attribute__((dllexport)) int LCMapStringA(DWORD locale,DWORD flags,LPCSTR src,int len,LPSTR dst,int cap){(void)locale;if(!src)return 0;int n=len<0?lstrlenA(src)+1:len;if(!dst||cap<n)return n;for(int i=0;i<n;i++){char c=src[i];if(flags&0x100u&&c>='a'&&c<='z')c-=32;if(flags&0x200u&&c>='A'&&c<='Z')c+=32;dst[i]=c;}return n;}

__attribute__((dllexport)) int LCMapStringW(DWORD locale,DWORD flags,K32_LPCWSTR src,int len,K32_LPWSTR dst,int cap){(void)locale;if(!src)return 0;int n=len<0?lstrlenW(src)+1:len;if(!dst||cap<n)return n;for(int i=0;i<n;i++){unsigned short c=src[i];if(flags&0x100u&&c>='a'&&c<='z')c-=32;if(flags&0x200u&&c>='A'&&c<='Z')c+=32;dst[i]=c;}return n;}

__attribute__((dllexport)) int LCMapStringEx(const K32_WCHAR *locale,DWORD flags,K32_LPCWSTR src,int len,K32_LPWSTR dst,int cap,void *version,void *reserved,LPVOID sort_handle){(void)locale;(void)version;(void)reserved;(void)sort_handle;return LCMapStringW(0,flags,src,len,dst,cap);}

__attribute__((dllexport)) int GetLocaleInfoA(DWORD locale,DWORD type,LPSTR out,int cap){(void)locale;const char *s=(type&0xFFFF)==0x1004?"English":(type&0xFFFF)==0x5A?"US":(type&0xFFFF)==0x1F?"Sunday":(type&0xFFFF)==0x1003?"en-US":"English (United States)";int n=lstrlenA(s)+1;if(!out||cap<n)return n;for(int i=0;i<n;i++)out[i]=s[i];return n;}

__attribute__((dllexport)) int GetLocaleInfoW(DWORD locale,DWORD type,K32_LPWSTR out,int cap){(void)locale;const char *s=(type&0xFFFF)==0x1004?"English":(type&0xFFFF)==0x5A?"US":(type&0xFFFF)==0x1F?"Sunday":(type&0xFFFF)==0x1003?"en-US":"English (United States)";int n=lstrlenA(s)+1;if(!out||cap<n)return n;for(int i=0;i<n;i++)out[i]=(unsigned char)s[i];return n;}

__attribute__((dllexport)) int GetLocaleInfoEx(K32_LPCWSTR locale,DWORD type,K32_LPWSTR out,int cap){(void)locale;return GetLocaleInfoW(0,type,out,cap);}

__attribute__((dllexport)) int GetDateFormatA(DWORD locale,DWORD flags,const K32_SYSTEMTIME *st,LPCSTR format,LPSTR out,int cap){(void)locale;(void)flags;(void)format;K32_SYSTEMTIME t;if(st)t=*st;else GetLocalTime(&t);char tmp[16];int n=0;int vals[3]={t.wMonth,t.wDay,t.wYear};for(int j=0;j<3;j++){if(j)tmp[n++]='/';if(j==2){tmp[n++]=(char)('0'+(vals[j]/1000)%10);tmp[n++]=(char)('0'+(vals[j]/100)%10);tmp[n++]=(char)('0'+(vals[j]/10)%10);tmp[n++]=(char)('0'+vals[j]%10);}else{if(vals[j]>=10)tmp[n++]=(char)('0'+vals[j]/10);tmp[n++]=(char)('0'+vals[j]%10);}}tmp[n]=0;if(!out||cap<n+1)return n+1;for(int i=0;i<=n;i++)out[i]=tmp[i];return n+1;}

__attribute__((dllexport)) int GetDateFormatW(DWORD locale,DWORD flags,const K32_SYSTEMTIME *st,K32_LPCWSTR format,K32_LPWSTR out,int cap){(void)format;char tmp[32];int n=GetDateFormatA(locale,flags,st,0,tmp,sizeof(tmp));if(!out||cap<n)return n;for(int i=0;i<n;i++)out[i]=(unsigned char)tmp[i];return n;}

__attribute__((dllexport)) int GetTimeFormatA(DWORD locale,DWORD flags,const K32_SYSTEMTIME *st,LPCSTR format,LPSTR out,int cap){(void)locale;(void)flags;(void)format;K32_SYSTEMTIME t;if(st)t=*st;else GetLocalTime(&t);char tmp[16];int n=0;int v[3]={t.wHour,t.wMinute,t.wSecond};for(int j=0;j<3;j++){if(j)tmp[n++]=':';tmp[n++]=(char)('0'+(v[j]/10)%10);tmp[n++]=(char)('0'+v[j]%10);}tmp[n]=0;if(!out||cap<n+1)return n+1;for(int i=0;i<=n;i++)out[i]=tmp[i];return n+1;}

__attribute__((dllexport)) int GetTimeFormatW(DWORD locale,DWORD flags,const K32_SYSTEMTIME *st,K32_LPCWSTR format,K32_LPWSTR out,int cap){(void)format;char tmp[32];int n=GetTimeFormatA(locale,flags,st,0,tmp,sizeof(tmp));if(!out||cap<n)return n;for(int i=0;i<n;i++)out[i]=(unsigned char)tmp[i];return n;}

__attribute__((dllexport)) int GetNumberFormatA(DWORD locale,DWORD flags,LPCSTR value,const void *fmt,LPSTR out,int cap){(void)locale;(void)flags;(void)fmt;if(!value)return 0;int n=lstrlenA(value)+1;if(!out||cap<n)return n;for(int i=0;i<n;i++)out[i]=value[i];return n;}

__attribute__((dllexport)) int GetNumberFormatW(DWORD locale,DWORD flags,K32_LPCWSTR value,const void *fmt,K32_LPWSTR out,int cap){(void)locale;(void)flags;(void)fmt;if(!value)return 0;int n=lstrlenW(value)+1;if(!out||cap<n)return n;for(int i=0;i<n;i++)out[i]=value[i];return n;}

__attribute__((dllexport)) int GetCurrencyFormatA(DWORD locale,DWORD flags,LPCSTR value,const void *fmt,LPSTR out,int cap){return GetNumberFormatA(locale,flags,value,fmt,out,cap);}

__attribute__((dllexport)) int GetCurrencyFormatW(DWORD locale,DWORD flags,K32_LPCWSTR value,const void *fmt,K32_LPWSTR out,int cap){return GetNumberFormatW(locale,flags,value,fmt,out,cap);}

__attribute__((dllexport)) int GetCalendarInfoA(DWORD locale,DWORD calendar,DWORD type,LPCSTR out,int cap,DWORD *value){(void)locale;(void)calendar;if(value)*value=1;if(out&&cap>0){const char *s=(type&0xFFFF)==0x2?"Gregorian":"1";int i=0;while(s[i]&&i+1<cap){((char*)out)[i]=s[i];i++;}((char*)out)[i]=0;return i+1;}return 1;}

__attribute__((dllexport)) int GetCalendarInfoW(DWORD locale,DWORD calendar,DWORD type,K32_LPWSTR out,int cap,DWORD *value){(void)locale;(void)calendar;if(value)*value=1;if(out&&cap>0){const char *s=(type&0xFFFF)==0x2?"Gregorian":"1";int i=0;while(s[i]&&i+1<cap){out[i]=(unsigned char)s[i];i++;}out[i]=0;return i+1;}return 1;}

__attribute__((dllexport)) DWORD GetActiveProcessorCount(WORD group){return group==0||group==0xFFFFu?1:0;}

__attribute__((dllexport)) DWORD GetMaximumProcessorCount(WORD group){return group==0||group==0xFFFFu?1:0;}

__attribute__((dllexport)) WORD GetActiveProcessorGroupCount(void){return 1;}

__attribute__((dllexport)) BOOL GetNumaHighestNodeNumber(DWORD *highest){if(!highest)return 0;*highest=0;return 1;}

__attribute__((dllexport)) BOOL GetNumaProcessorNode(BYTE processor,BYTE *node){if(!node||processor>0)return 0;*node=0;return 1;}

__attribute__((dllexport)) BOOL GetNumaAvailableMemoryNode(BYTE node,unsigned long long *bytes){if(!bytes||node)return 0;*bytes=0;return 1;}

__attribute__((dllexport)) BOOL GetNumaAvailableMemoryNodeEx(WORD node,unsigned long long *bytes){if(!bytes||node)return 0;*bytes=0;return 1;}

__attribute__((dllexport)) BOOL GetNumaNodeProcessorMask(BYTE node,unsigned long long *mask){if(!mask||node)return 0;*mask=1;return 1;}

__attribute__((dllexport)) BOOL GetNumaProximityNode(DWORD proximity,BYTE *node){if(!node||proximity)return 0;*node=0;return 1;}

__attribute__((dllexport)) BOOL GetNumaProximityNodeEx(DWORD proximity,WORD *node){if(!node||proximity)return 0;*node=0;return 1;}

__attribute__((dllexport)) HANDLE CreateFile2(K32_LPCWSTR name,DWORD access,DWORD share,DWORD disposition,void *params){(void)access;(void)share;(void)disposition;(void)params;char p[512];k32_wide_to_ansi(p,sizeof(p),name);return CreateFileA(p,0,0,0,0,0,0);}

__attribute__((dllexport)) BOOL MoveFileExA(LPCSTR from,LPCSTR to,DWORD flags){(void)flags;return MoveFileA(from,to);}

__attribute__((dllexport)) BOOL MoveFileExW(K32_LPCWSTR from,K32_LPCWSTR to,DWORD flags){char a[512],c[512];k32_wide_to_ansi(a,sizeof(a),from);k32_wide_to_ansi(c,sizeof(c),to);return MoveFileExA(a,c,flags);}

__attribute__((dllexport)) BOOL CopyFileExA(LPCSTR from,LPCSTR to,void *progress,void *data,BOOL *cancel,DWORD flags){(void)progress;(void)data;(void)cancel;(void)flags;return CopyFileA(from,to,0);}

__attribute__((dllexport)) BOOL CopyFileExW(K32_LPCWSTR from,K32_LPCWSTR to,void *progress,void *data,BOOL *cancel,DWORD flags){char a[512],c[512];k32_wide_to_ansi(a,sizeof(a),from);k32_wide_to_ansi(c,sizeof(c),to);return CopyFileExA(a,c,progress,data,cancel,flags);}

__attribute__((dllexport)) BOOL ReplaceFileA(LPCSTR replaced,LPCSTR replacement,LPCSTR backup,DWORD flags,void *exclude,void *reserved){(void)backup;(void)flags;(void)exclude;(void)reserved;return MoveFileA(replacement,replaced);}

__attribute__((dllexport)) BOOL ReplaceFileW(K32_LPCWSTR replaced,K32_LPCWSTR replacement,K32_LPCWSTR backup,DWORD flags,void *exclude,void *reserved){char a[512],c[512],d[512];k32_wide_to_ansi(a,sizeof(a),replaced);k32_wide_to_ansi(c,sizeof(c),replacement);k32_wide_to_ansi(d,sizeof(d),backup);return ReplaceFileA(a,c,backup?d:0,flags,exclude,reserved);}

__attribute__((dllexport)) BOOL CreateDirectoryExA(LPCSTR template_path,LPCSTR path,void *security){(void)template_path;(void)security;return CreateDirectoryA(path,0);}

__attribute__((dllexport)) BOOL CreateDirectoryExW(K32_LPCWSTR template_path,K32_LPCWSTR path,void *security){char a[512],c[512];k32_wide_to_ansi(a,sizeof(a),template_path);k32_wide_to_ansi(c,sizeof(c),path);return CreateDirectoryExA(a,c,security);}

__attribute__((dllexport)) BOOL CancelIoEx(HANDLE file,void *overlapped){(void)file;(void)overlapped;k32_last_error=50;return 0;}

__attribute__((dllexport)) BOOL GetOverlappedResultEx(HANDLE file,void *ov,DWORD *transferred,DWORD ms,BOOL alertable){(void)ms;(void)alertable;return GetOverlappedResult(file,ov,transferred,0);}

__attribute__((dllexport)) DWORD WaitForSingleObjectEx(HANDLE h,DWORD ms,BOOL alertable){(void)alertable;return WaitForSingleObject(h,ms);}

__attribute__((dllexport)) DWORD WaitForMultipleObjectsEx(DWORD count,const HANDLE *handles,BOOL wait_all,DWORD ms,BOOL alertable){(void)alertable;return WaitForMultipleObjects(count,handles,wait_all,ms);}

__attribute__((dllexport)) DWORD SleepEx(DWORD ms,BOOL alertable){(void)alertable;Sleep(ms);return 0;}

__attribute__((dllexport)) DWORD SignalObjectAndWait(HANDLE signal,HANDLE wait,DWORD ms,BOOL alertable){(void)signal;(void)alertable;return WaitForSingleObject(wait,ms);}

__attribute__((dllexport)) HANDLE CreateEventW(void *security,BOOL manual,BOOL initial,K32_LPCWSTR name){char n[128];k32_wide_to_ansi(n,sizeof(n),name);return CreateEventA(security,manual,initial,name?n:0);}

__attribute__((dllexport)) HANDLE CreateMutexW(void *security,BOOL owner,K32_LPCWSTR name){char n[128];k32_wide_to_ansi(n,sizeof(n),name);return CreateMutexA(security,owner,name?n:0);}

__attribute__((dllexport)) HANDLE CreateSemaphoreW(void *security,LONG initial,LONG maximum,K32_LPCWSTR name){char n[128];k32_wide_to_ansi(n,sizeof(n),name);return CreateSemaphoreA(security,initial,maximum,name?n:0);}

__attribute__((dllexport)) HANDLE OpenEventA(DWORD access,BOOL inherit,LPCSTR name){(void)access;(void)inherit;(void)name;k32_last_error=2;return 0;}

__attribute__((dllexport)) HANDLE OpenEventW(DWORD access,BOOL inherit,K32_LPCWSTR name){char n[128];k32_wide_to_ansi(n,sizeof(n),name);return OpenEventA(access,inherit,n);}

__attribute__((dllexport)) HANDLE OpenMutexA(DWORD access,BOOL inherit,LPCSTR name){(void)access;(void)inherit;(void)name;k32_last_error=2;return 0;}

__attribute__((dllexport)) HANDLE OpenMutexW(DWORD access,BOOL inherit,K32_LPCWSTR name){char n[128];k32_wide_to_ansi(n,sizeof(n),name);return OpenMutexA(access,inherit,n);}

__attribute__((dllexport)) HANDLE OpenSemaphoreA(DWORD access,BOOL inherit,LPCSTR name){(void)access;(void)inherit;(void)name;k32_last_error=2;return 0;}

__attribute__((dllexport)) HANDLE OpenSemaphoreW(DWORD access,BOOL inherit,K32_LPCWSTR name){char n[128];k32_wide_to_ansi(n,sizeof(n),name);return OpenSemaphoreA(access,inherit,n);}

__attribute__((dllexport)) BOOL PulseEvent(HANDLE event){return SetEvent(event);}

__attribute__((dllexport)) HANDLE CreateWaitableTimerA(void *security,BOOL manual,LPCSTR name){(void)security;(void)manual;(void)name;k32_last_error=50;return 0;}

__attribute__((dllexport)) HANDLE CreateWaitableTimerW(void *security,BOOL manual,K32_LPCWSTR name){char n[128];k32_wide_to_ansi(n,sizeof(n),name);return CreateWaitableTimerA(security,manual,n);}

__attribute__((dllexport)) BOOL SetWaitableTimer(HANDLE timer,const K32_LARGE_INTEGER *due,LONG period,void *completion,void *arg,BOOL resume){(void)timer;(void)due;(void)period;(void)completion;(void)arg;(void)resume;k32_last_error=50;return 0;}

__attribute__((dllexport)) BOOL CancelWaitableTimer(HANDLE timer){(void)timer;k32_last_error=50;return 0;}

__attribute__((dllexport)) HANDLE LoadLibraryW(K32_LPCWSTR name){(void)name;k32_last_error=126;return 0;}

__attribute__((dllexport)) void FreeLibraryAndExitThread(HANDLE module,DWORD code){FreeLibrary(module);ExitProcess(code);}

__attribute__((dllexport)) DWORD GetDllDirectoryA(DWORD cap,LPSTR out){(void)cap;if(out)out[0]=0;return 0;}

__attribute__((dllexport)) DWORD GetDllDirectoryW(DWORD cap,K32_LPWSTR out){if(out&&cap)out[0]=0;return 0;}

__attribute__((dllexport)) BOOL SetDllDirectoryA(LPCSTR path){return !path||path[0]==0;}

__attribute__((dllexport)) BOOL SetDllDirectoryW(K32_LPCWSTR path){return !path||path[0]==0;}

__attribute__((dllexport)) DWORD GetSystemWindowsDirectoryA(LPSTR out,DWORD cap){return GetWindowsDirectoryA(out,cap);}

__attribute__((dllexport)) DWORD GetSystemWindowsDirectoryW(K32_LPWSTR out,DWORD cap){return GetWindowsDirectoryW(out,cap);}

__attribute__((dllexport)) DWORD GetSystemWow64DirectoryA(LPSTR out,DWORD cap){(void)out;(void)cap;k32_last_error=3;return 0;}

__attribute__((dllexport)) DWORD GetSystemWow64DirectoryW(K32_LPWSTR out,DWORD cap){(void)out;(void)cap;k32_last_error=3;return 0;}

__attribute__((dllexport)) DWORD GetSystemWow64Directory2A(LPSTR out,DWORD cap,WORD machine){(void)machine;return GetSystemWow64DirectoryA(out,cap);}

__attribute__((dllexport)) DWORD GetSystemWow64Directory2W(K32_LPWSTR out,DWORD cap,WORD machine){(void)machine;return GetSystemWow64DirectoryW(out,cap);}

__attribute__((dllexport)) BOOL CreateProcessW(K32_LPCWSTR app,K32_LPWSTR cmd,void *pa,void *ta,BOOL inherit,DWORD flags,void *env,K32_LPCWSTR cwd,void *startup,void *process_info){char a[256],c[512];if(app)k32_wide_to_ansi(a,sizeof(a),app);else a[0]=0;if(cmd)k32_wide_to_ansi(c,sizeof(c),cmd);else c[0]=0;return CreateProcessA(app?a:0,cmd?c:0,pa,ta,inherit,flags,env,0,startup,process_info);}

__attribute__((dllexport)) unsigned int WinExec(LPCSTR command,unsigned int show){(void)command;(void)show;k32_last_error=50;return 31;}

typedef struct { DWORD dwOSVersionInfoSize,dwMajorVersion,dwMinorVersion,dwBuildNumber,dwPlatformId; char szCSDVersion[128]; } K32_OSVERSIONINFOA;
__attribute__((dllexport)) BOOL GetVersionExA(K32_OSVERSIONINFOA *v){if(!v||v->dwOSVersionInfoSize<sizeof(DWORD)*5){k32_last_error=87;return 0;}v->dwMajorVersion=1;v->dwMinorVersion=0;v->dwBuildNumber=1;v->dwPlatformId=2;v->szCSDVersion[0]=0;return 1;}

typedef struct { DWORD dwOSVersionInfoSize,dwMajorVersion,dwMinorVersion,dwBuildNumber,dwPlatformId; K32_WCHAR szCSDVersion[128]; } K32_OSVERSIONINFOW;
__attribute__((dllexport)) BOOL GetVersionExW(K32_OSVERSIONINFOW *v){if(!v||v->dwOSVersionInfoSize<sizeof(DWORD)*5){k32_last_error=87;return 0;}v->dwMajorVersion=1;v->dwMinorVersion=0;v->dwBuildNumber=1;v->dwPlatformId=2;v->szCSDVersion[0]=0;return 1;}

__attribute__((dllexport)) BOOL VerifyVersionInfoA(const void *v,DWORD type,unsigned long long mask){(void)v;(void)type;(void)mask;k32_last_error=50;return 0;}

__attribute__((dllexport)) BOOL VerifyVersionInfoW(const void *v,DWORD type,unsigned long long mask){return VerifyVersionInfoA(v,type,mask);}

__attribute__((dllexport)) BOOL GetSystemTimes(K32_FILETIME *idle,K32_FILETIME *kernel,K32_FILETIME *user){if(!idle||!kernel||!user)return 0;idle->dwLowDateTime=idle->dwHighDateTime=0;kernel->dwLowDateTime=kernel->dwHighDateTime=0;user->dwLowDateTime=user->dwHighDateTime=0;return 1;}

__attribute__((dllexport)) DWORD GlobalFlags(HANDLE mem){return k32_mem_size(mem)?0:0x8000u;}

__attribute__((dllexport)) DWORD LocalFlags(HANDLE mem){return k32_mem_size(mem)?0:0x8000u;}

__attribute__((dllexport)) HANDLE HeapCreate(DWORD options,K32_SIZE_T initial,K32_SIZE_T maximum){(void)options;(void)initial;(void)maximum;return (HANDLE)&k32_heap_tag;}

__attribute__((dllexport)) BOOL HeapDestroy(HANDLE heap){return heap==(HANDLE)&k32_heap_tag;}

__attribute__((dllexport)) BOOL HeapLock(HANDLE heap){return heap==(HANDLE)&k32_heap_tag;}

__attribute__((dllexport)) BOOL HeapUnlock(HANDLE heap){return heap==(HANDLE)&k32_heap_tag;}

__attribute__((dllexport)) BOOL HeapSetInformation(HANDLE heap,int info,void *data,K32_SIZE_T size){(void)info;(void)data;(void)size;return heap==(HANDLE)&k32_heap_tag;}

__attribute__((dllexport)) BOOL HeapQueryInformation(HANDLE heap,int info,void *data,K32_SIZE_T size,K32_SIZE_T *needed){(void)info;if(heap!=(HANDLE)&k32_heap_tag||!data||!size){if(needed)*needed=0;return 0;}if(needed)*needed=0;return 1;}

typedef struct { DWORD dwLength; DWORD dwMemoryLoad; K32_SIZE_T ullTotalPhys,ullAvailPhys,ullTotalPageFile,ullAvailPageFile,ullTotalVirtual,ullAvailVirtual,ullAvailExtendedVirtual; } K32_MEMORYSTATUSEX;
__attribute__((dllexport)) BOOL GlobalMemoryStatusEx(K32_MEMORYSTATUSEX *m){if(!m||m->dwLength<sizeof(*m))return 0;m->dwMemoryLoad=0;m->ullTotalPhys=256ull*1024*1024;m->ullAvailPhys=128ull*1024*1024;m->ullTotalPageFile=m->ullTotalPhys;m->ullAvailPageFile=m->ullAvailPhys;m->ullTotalVirtual=m->ullTotalPhys;m->ullAvailVirtual=m->ullAvailPhys;m->ullAvailExtendedVirtual=0;return 1;}

typedef struct { DWORD dwLength,dwMemoryLoad; K32_SIZE_T dwTotalPhys,dwAvailPhys,dwTotalPageFile,dwAvailPageFile,dwTotalVirtual,dwAvailVirtual; } K32_MEMORYSTATUS;
__attribute__((dllexport)) void GlobalMemoryStatus(K32_MEMORYSTATUS *m){if(!m)return;m->dwLength=sizeof(*m);m->dwMemoryLoad=50;m->dwTotalPhys=256ull*1024*1024;m->dwAvailPhys=128ull*1024*1024;m->dwTotalPageFile=m->dwTotalPhys;m->dwAvailPageFile=m->dwAvailPhys;m->dwTotalVirtual=m->dwTotalPhys;m->dwAvailVirtual=m->dwAvailPhys;}

__attribute__((dllexport)) BOOL GetPhysicallyInstalledSystemMemory(unsigned long long *kb){if(!kb)return 0;*kb=262144;return 1;}

typedef struct { unsigned long long ProcessorMask; DWORD Relationship; unsigned char Reserved[32]; } K32_LOGICAL_PROCESSOR_INFO;
__attribute__((dllexport)) BOOL GetLogicalProcessorInformation(K32_LOGICAL_PROCESSOR_INFO *buffer,DWORD *length){if(!length)return 0;if(!buffer||*length<sizeof(K32_LOGICAL_PROCESSOR_INFO)){*length=sizeof(K32_LOGICAL_PROCESSOR_INFO);k32_last_error=122;return 0;}buffer->ProcessorMask=1;buffer->Relationship=0;for(int i=0;i<32;i++)buffer->Reserved[i]=0;*length=sizeof(*buffer);return 1;}

__attribute__((dllexport)) BOOL GetLogicalProcessorInformationEx(DWORD relationship,void *buffer,DWORD *length){(void)relationship;(void)buffer;if(length)*length=0;k32_last_error=50;return 0;}

__attribute__((dllexport)) BOOL GetProcessDefaultCpuSets(HANDLE process,DWORD *ids,DWORD cap,DWORD *count){(void)ids;(void)cap;if(count)*count=0;return process==GetCurrentProcess()||process==(HANDLE)1;}

__attribute__((dllexport)) BOOL SetProcessDefaultCpuSets(HANDLE process,const DWORD *ids,DWORD count){(void)ids;return (process==GetCurrentProcess()||process==(HANDLE)1)&&count==0;}

__attribute__((dllexport)) BOOL GetThreadSelectedCpuSets(HANDLE thread,DWORD *ids,DWORD cap,DWORD *count){(void)ids;(void)cap;if(count)*count=0;return thread==GetCurrentThread()||thread==(HANDLE)1;}

__attribute__((dllexport)) BOOL SetThreadSelectedCpuSets(HANDLE thread,const DWORD *ids,DWORD count){(void)ids;return (thread==GetCurrentThread()||thread==(HANDLE)1)&&count==0;}

__attribute__((dllexport)) BOOL GetProcessGroupAffinity(HANDLE process,WORD *count,WORD *groups){if(process!=GetCurrentProcess()&&process!=(HANDLE)1)return 0;if(!count)return 0;if(!groups||*count<1){*count=1;k32_last_error=122;return 0;}groups[0]=0;*count=1;return 1;}

__attribute__((dllexport)) BOOL GetThreadGroupAffinity(HANDLE thread,void *group){if((thread!=GetCurrentThread()&&thread!=(HANDLE)1)||!group)return 0;((WORD*)group)[0]=1;((WORD*)group)[1]=1;return 1;}

__attribute__((dllexport)) BOOL SetThreadGroupAffinity(HANDLE thread,const void *group,void *previous){if((thread!=GetCurrentThread()&&thread!=(HANDLE)1)||!group||((const WORD*)group)[0]!=1||((const WORD*)group)[1]!=1)return 0;if(previous){((WORD*)previous)[0]=1;((WORD*)previous)[1]=1;}return 1;}

__attribute__((dllexport)) BOOL GetNumaNodeProcessorMaskEx(WORD node,void *mask){if(!mask||node)return 0;((unsigned long long*)mask)[0]=1;((unsigned long long*)mask)[1]=0;return 1;}

__attribute__((dllexport)) DWORD GetProcessVersionEx(DWORD pid){return GetProcessVersion(pid);}

__attribute__((dllexport)) DWORD GetSystemDefaultLCIDEx(void){return GetSystemDefaultLCID();}

__attribute__((dllexport)) DWORD GetUserDefaultLCIDEx(void){return GetUserDefaultLCID();}

__attribute__((dllexport)) DWORD GetSystemDirectory(LPSTR out,DWORD cap){return GetSystemDirectoryA(out,cap);}

__attribute__((dllexport)) DWORD GetWindowsDirectory(LPSTR out,DWORD cap){return GetWindowsDirectoryA(out,cap);}

__attribute__((dllexport)) DWORD GetModuleFileName(HANDLE module,LPSTR out,DWORD cap){return GetModuleFileNameA(module,out,cap);}

__attribute__((dllexport)) HANDLE GetModuleHandle(LPCSTR name){return GetModuleHandleA(name);}

__attribute__((dllexport)) LPSTR GetCommandLine(void){return GetCommandLineA();}

__attribute__((dllexport)) DWORD GetTempPath(DWORD cap,LPSTR out){return GetTempPathA(cap,out);}

__attribute__((dllexport)) DWORD GetCurrentDirectory(DWORD cap,LPSTR out){return GetCurrentDirectoryA(cap,out);}

__attribute__((dllexport)) BOOL SetCurrentDirectory(LPCSTR path){return SetCurrentDirectoryA(path);}

__attribute__((dllexport)) DWORD GetEnvironmentVariable(LPCSTR name,LPSTR out,DWORD cap){return GetEnvironmentVariableA(name,out,cap);}

__attribute__((dllexport)) BOOL SetEnvironmentVariable(LPCSTR name,LPCSTR value){return SetEnvironmentVariableA(name,value);}

__attribute__((dllexport)) DWORD GetFileAttributes(LPCSTR path){return GetFileAttributesA(path);}

__attribute__((dllexport)) BOOL SetFileAttributes(LPCSTR path,DWORD attr){return SetFileAttributesA(path,attr);}

__attribute__((dllexport)) K32_SIZE_T GetLargePageMinimum(void){return 0;}

__attribute__((dllexport)) BOOL GetSystemRegistryQuota(DWORD *allowed,DWORD *used){if(allowed)*allowed=0;if(used)*used=0;k32_last_error=50;return 0;}

__attribute__((dllexport)) BOOL GetVolumeNameForVolumeMountPointA(LPCSTR path,LPSTR out,DWORD cap){if(!path||!out||cap<50||!(path[0]=='C'||path[0]=='c')||path[1]!=':'){k32_last_error=3;return 0;}const char *s="\\\\?\\Volume{00000000-0000-0000-0000-000000000001}\\";
int i=0;while(s[i]&&i+1<cap){out[i]=s[i];i++;}out[i]=0;return s[i]==0;}

__attribute__((dllexport)) BOOL GetVolumeNameForVolumeMountPointW(K32_LPCWSTR path,K32_LPWSTR out,DWORD cap){if(!path||!out||cap<50||path[1]!=':'){k32_last_error=3;return 0;}const char *s="\\\\?\\Volume{00000000-0000-0000-0000-000000000001}\\";
int i=0;while(s[i]&&i+1<cap){out[i]=(unsigned char)s[i];i++;}out[i]=0;return s[i]==0;}

__attribute__((dllexport)) BOOL SetVolumeMountPointA(LPCSTR mount,LPCSTR volume){(void)mount;(void)volume;k32_last_error=50;return 0;}

__attribute__((dllexport)) BOOL SetVolumeMountPointW(K32_LPCWSTR mount,K32_LPCWSTR volume){(void)mount;(void)volume;k32_last_error=50;return 0;}

__attribute__((dllexport)) BOOL DeleteVolumeMountPointA(LPCSTR mount){(void)mount;k32_last_error=50;return 0;}

__attribute__((dllexport)) BOOL DeleteVolumeMountPointW(K32_LPCWSTR mount){(void)mount;k32_last_error=50;return 0;}

__attribute__((dllexport)) HANDLE FindFirstVolumeA(LPSTR name,DWORD cap){if(!name||cap<50){k32_last_error=122;return INVALID_HANDLE_VALUE;}if(!GetVolumeNameForVolumeMountPointA("C:\\",name,cap))return INVALID_HANDLE_VALUE;return (HANDLE)1;}

__attribute__((dllexport)) HANDLE FindFirstVolumeW(K32_LPWSTR name,DWORD cap){if(!name||cap<50){k32_last_error=122;return INVALID_HANDLE_VALUE;}if(!GetVolumeNameForVolumeMountPointW((K32_LPCWSTR)0,name,cap))return INVALID_HANDLE_VALUE;return (HANDLE)1;}

__attribute__((dllexport)) BOOL FindNextVolumeA(HANDLE find,LPSTR name,DWORD cap){(void)find;(void)name;(void)cap;k32_last_error=18;return 0;}

__attribute__((dllexport)) BOOL FindNextVolumeW(HANDLE find,K32_LPWSTR name,DWORD cap){(void)find;(void)name;(void)cap;k32_last_error=18;return 0;}

__attribute__((dllexport)) BOOL FindVolumeClose(HANDLE find){return find==(HANDLE)1;}

__attribute__((dllexport)) HANDLE FindFirstVolumeMountPointA(LPCSTR volume,LPSTR mount,DWORD cap){(void)volume;(void)mount;(void)cap;k32_last_error=18;return INVALID_HANDLE_VALUE;}

__attribute__((dllexport)) HANDLE FindFirstVolumeMountPointW(K32_LPCWSTR volume,K32_LPWSTR mount,DWORD cap){(void)volume;(void)mount;(void)cap;k32_last_error=18;return INVALID_HANDLE_VALUE;}

__attribute__((dllexport)) BOOL FindNextVolumeMountPointA(HANDLE find,LPSTR mount,DWORD cap){(void)find;(void)mount;(void)cap;k32_last_error=18;return 0;}

__attribute__((dllexport)) BOOL FindNextVolumeMountPointW(HANDLE find,K32_LPWSTR mount,DWORD cap){(void)find;(void)mount;(void)cap;k32_last_error=18;return 0;}

__attribute__((dllexport)) BOOL FindVolumeMountPointClose(HANDLE find){return find==(HANDLE)1;}

__attribute__((dllexport)) BOOL SetVolumeLabelA(LPCSTR root,LPCSTR label){(void)root;(void)label;k32_last_error=50;return 0;}

__attribute__((dllexport)) BOOL SetVolumeLabelW(K32_LPCWSTR root,K32_LPCWSTR label){(void)root;(void)label;k32_last_error=50;return 0;}

__attribute__((dllexport)) BOOL GetVolumeInformationByHandleW(HANDLE file,K32_LPWSTR name,DWORD cap,DWORD *serial,DWORD *max_component,DWORD *flags,K32_LPWSTR fs,DWORD fs_cap){(void)file;if(name&&cap)name[0]=0;if(fs&&fs_cap)fs[0]=0;if(serial)*serial=0;if(max_component)*max_component=255;if(flags)*flags=0;k32_last_error=50;return 0;}

__attribute__((dllexport)) BOOL GetFirmwareType(DWORD *type){if(!type)return 0;*type=2;return 1;}

__attribute__((dllexport)) DWORD GetFirmwareEnvironmentVariableA(LPCSTR name,LPCSTR guid,LPVOID buffer,DWORD size){(void)name;(void)guid;(void)buffer;(void)size;k32_last_error=1;return 0;}

__attribute__((dllexport)) DWORD GetFirmwareEnvironmentVariableW(K32_LPCWSTR name,K32_LPCWSTR guid,LPVOID buffer,DWORD size){(void)name;(void)guid;(void)buffer;(void)size;k32_last_error=1;return 0;}

__attribute__((dllexport)) BOOL SetFirmwareEnvironmentVariableA(LPCSTR name,LPCSTR guid,LPVOID buffer,DWORD size){(void)name;(void)guid;(void)buffer;(void)size;k32_last_error=1;return 0;}

__attribute__((dllexport)) BOOL SetFirmwareEnvironmentVariableW(K32_LPCWSTR name,K32_LPCWSTR guid,LPVOID buffer,DWORD size){(void)name;(void)guid;(void)buffer;(void)size;k32_last_error=1;return 0;}

__attribute__((dllexport)) BOOL GetSystemPreferredUILanguages(DWORD flags,DWORD *count,K32_LPWSTR languages,DWORD *length){(void)flags;if(count)*count=1;if(length)*length=languages?6:6;if(languages&&length&&*length>=6){const char *s="en-US";for(int i=0;i<5;i++)languages[i]=s[i];languages[5]=0;}return 1;}

__attribute__((dllexport)) BOOL GetUserPreferredUILanguages(DWORD flags,DWORD *count,K32_LPWSTR languages,DWORD *length){return GetSystemPreferredUILanguages(flags,count,languages,length);}

__attribute__((dllexport)) BOOL GetThreadPreferredUILanguages(DWORD flags,DWORD *count,K32_LPWSTR languages,DWORD *length){return GetSystemPreferredUILanguages(flags,count,languages,length);}

__attribute__((dllexport)) BOOL GetProcessPreferredUILanguages(DWORD flags,DWORD *count,K32_LPWSTR languages,DWORD *length){return GetSystemPreferredUILanguages(flags,count,languages,length);}

__attribute__((dllexport)) BOOL SetThreadPreferredUILanguages(DWORD flags,K32_LPCWSTR languages,DWORD *count){(void)flags;(void)languages;if(count)*count=1;return 1;}

__attribute__((dllexport)) BOOL SetProcessPreferredUILanguages(DWORD flags,K32_LPCWSTR languages,DWORD *count){return SetThreadPreferredUILanguages(flags,languages,count);}

__attribute__((dllexport)) BOOL GetProcessMemoryInfo(HANDLE process,void *counters,DWORD size){if((process!=GetCurrentProcess()&&process!=(HANDLE)1)||!counters||size<16)return 0;for(DWORD i=0;i<size;i++)((unsigned char*)counters)[i]=0;return 1;}

__attribute__((dllexport)) BOOL GetProcessWorkingSetSizeEx(HANDLE process,K32_SIZE_T *min,K32_SIZE_T *max,DWORD *flags){if(!GetProcessWorkingSetSize(process,min,max))return 0;if(flags)*flags=0;return 1;}

__attribute__((dllexport)) BOOL SetProcessWorkingSetSizeEx(HANDLE process,K32_SIZE_T min,K32_SIZE_T max,DWORD flags){(void)flags;return SetProcessWorkingSetSize(process,min,max);}

__attribute__((dllexport)) BOOL GetProcessMemoryInfoEx(HANDLE process,void *counters,DWORD size){return GetProcessMemoryInfo(process,counters,size);}

__attribute__((dllexport)) DWORD GetSystemWow64Directory(LPSTR out,DWORD cap){return GetSystemWow64DirectoryA(out,cap);}

__attribute__((dllexport)) DWORD GetSystemWindowsDirectory(LPSTR out,DWORD cap){return GetSystemWindowsDirectoryA(out,cap);}

__attribute__((dllexport)) DWORD GetDllDirectory(DWORD cap,LPSTR out){return GetDllDirectoryA(cap,out);}

__attribute__((dllexport)) BOOL SetDllDirectory(LPCSTR path){return SetDllDirectoryA(path);}

__attribute__((dllexport)) HANDLE CreateFileTransactedA(LPCSTR name,DWORD access,DWORD share,void *security,DWORD disposition,DWORD flags,HANDLE template_file,HANDLE transaction,void *mini,void *extended){(void)name;(void)access;(void)share;(void)security;(void)disposition;(void)flags;(void)template_file;(void)transaction;(void)mini;(void)extended;k32_last_error=50;return INVALID_HANDLE_VALUE;}

__attribute__((dllexport)) HANDLE CreateFileTransactedW(K32_LPCWSTR name,DWORD access,DWORD share,void *security,DWORD disposition,DWORD flags,HANDLE template_file,HANDLE transaction,void *mini,void *extended){(void)name;(void)access;(void)share;(void)security;(void)disposition;(void)flags;(void)template_file;(void)transaction;(void)mini;(void)extended;k32_last_error=50;return INVALID_HANDLE_VALUE;}

__attribute__((dllexport)) BOOL DeleteFileTransactedA(LPCSTR path,HANDLE transaction){(void)path;(void)transaction;k32_last_error=50;return 0;}

__attribute__((dllexport)) BOOL DeleteFileTransactedW(K32_LPCWSTR path,HANDLE transaction){(void)path;(void)transaction;k32_last_error=50;return 0;}

__attribute__((dllexport)) BOOL MoveFileTransactedA(LPCSTR from,LPCSTR to,void *progress,void *data,DWORD flags,HANDLE transaction){(void)from;(void)to;(void)progress;(void)data;(void)flags;(void)transaction;k32_last_error=50;return 0;}

__attribute__((dllexport)) BOOL MoveFileTransactedW(K32_LPCWSTR from,K32_LPCWSTR to,void *progress,void *data,DWORD flags,HANDLE transaction){(void)from;(void)to;(void)progress;(void)data;(void)flags;(void)transaction;k32_last_error=50;return 0;}

__attribute__((dllexport)) BOOL CopyFileTransactedA(LPCSTR from,LPCSTR to,void *progress,void *data,BOOL *cancel,DWORD flags,HANDLE transaction){(void)from;(void)to;(void)progress;(void)data;(void)cancel;(void)flags;(void)transaction;k32_last_error=50;return 0;}

__attribute__((dllexport)) BOOL CopyFileTransactedW(K32_LPCWSTR from,K32_LPCWSTR to,void *progress,void *data,BOOL *cancel,DWORD flags,HANDLE transaction){(void)from;(void)to;(void)progress;(void)data;(void)cancel;(void)flags;(void)transaction;k32_last_error=50;return 0;}

__attribute__((dllexport)) BOOL CreateDirectoryTransactedA(LPCSTR template_path,LPCSTR path,void *security,HANDLE transaction){(void)template_path;(void)path;(void)security;(void)transaction;k32_last_error=50;return 0;}

__attribute__((dllexport)) BOOL CreateDirectoryTransactedW(K32_LPCWSTR template_path,K32_LPCWSTR path,void *security,HANDLE transaction){(void)template_path;(void)path;(void)security;(void)transaction;k32_last_error=50;return 0;}

__attribute__((dllexport)) BOOL RemoveDirectoryTransactedA(LPCSTR path,HANDLE transaction){(void)path;(void)transaction;k32_last_error=50;return 0;}

__attribute__((dllexport)) BOOL RemoveDirectoryTransactedW(K32_LPCWSTR path,HANDLE transaction){(void)path;(void)transaction;k32_last_error=50;return 0;}

__attribute__((dllexport)) BOOL IsDebuggerPresent(void){return 0;}

__attribute__((dllexport)) BOOL CheckRemoteDebuggerPresent(HANDLE process,BOOL *present){if(!present||(process!=GetCurrentProcess()&&process!=(HANDLE)1))return 0;*present=0;return 1;}

__attribute__((dllexport)) void OutputDebugStringA(LPCSTR text){if(text){DWORD n=(DWORD)lstrlenA(text),written;WriteFile(GetStdHandle((DWORD)-11),text,n,&written,0);}}

__attribute__((dllexport)) void OutputDebugStringW(K32_LPCWSTR text){if(text){char b[512];k32_wide_to_ansi(b,sizeof(b),text);DWORD written;WriteFile(GetStdHandle((DWORD)-11),b,(DWORD)lstrlenA(b),&written,0);}}

__attribute__((dllexport)) void DebugBreak(void){}

__attribute__((dllexport)) DWORD GetThreadErrorMode(void){return k32_error_mode;}

__attribute__((dllexport)) BOOL SetThreadErrorMode(DWORD mode,DWORD *old){if(old)*old=k32_error_mode;k32_error_mode=mode;return 1;}

__attribute__((dllexport)) BOOL SetThreadStackGuarantee(DWORD *size){if(!size)return 0;if(*size<4096)*size=4096;return 1;}

__attribute__((dllexport)) BOOL GetThreadStackGuarantee(DWORD *size){if(!size)return 0;*size=4096;return 1;}

__attribute__((dllexport)) BOOL GetProcessDefaultLayout(DWORD *layout){if(!layout)return 0;*layout=0;return 1;}

__attribute__((dllexport)) BOOL SetProcessDefaultLayout(DWORD layout){return layout==0;}

typedef struct { BYTE ACLineStatus,BatteryFlag,BatteryLifePercent,Reserved1; DWORD BatteryLifeTime,BatteryFullLifeTime; } K32_SYSTEM_POWER_STATUS;
__attribute__((dllexport)) BOOL GetSystemPowerStatus(K32_SYSTEM_POWER_STATUS *s){if(!s)return 0;s->ACLineStatus=1;s->BatteryFlag=128;s->BatteryLifePercent=255;s->Reserved1=0;s->BatteryLifeTime=0xFFFFFFFFu;s->BatteryFullLifeTime=0xFFFFFFFFu;return 1;}

__attribute__((dllexport)) BOOL SetSystemPowerState(BOOL suspend,BOOL force){(void)suspend;(void)force;k32_last_error=50;return 0;}

__attribute__((dllexport)) int GetSystemDefaultLocaleName(K32_LPWSTR name,int cap){if(!name||cap<6)return 6;const char *s="en-US";for(int i=0;i<5;i++)name[i]=s[i];name[5]=0;return 6;}

__attribute__((dllexport)) int GetUserDefaultLocaleName(K32_LPWSTR name,int cap){return GetSystemDefaultLocaleName(name,cap);}

__attribute__((dllexport)) int GetLocaleInfoByName(K32_LPCWSTR locale,DWORD type,K32_LPWSTR out,int cap){(void)locale;return GetLocaleInfoW(0,type,out,cap);}

__attribute__((dllexport)) int GetCalendarInfoEx(K32_LPCWSTR locale,DWORD calendar,DWORD reserved,DWORD type,K32_LPWSTR out,int cap,DWORD *value){(void)locale;(void)reserved;return GetCalendarInfoW(0,calendar,type,out,cap,value);}

__attribute__((dllexport)) int GetDateFormatEx(K32_LPCWSTR locale,DWORD flags,const K32_SYSTEMTIME *st,K32_LPCWSTR format,K32_LPWSTR out,int cap, K32_LPCWSTR calendar){(void)locale;(void)calendar;return GetDateFormatW(0,flags,st,format,out,cap);}

__attribute__((dllexport)) int GetTimeFormatEx(K32_LPCWSTR locale,DWORD flags,const K32_SYSTEMTIME *st,K32_LPCWSTR format,K32_LPWSTR out,int cap){(void)locale;return GetTimeFormatW(0,flags,st,format,out,cap);}

__attribute__((dllexport)) int GetNumberFormatEx(K32_LPCWSTR locale,DWORD flags,K32_LPCWSTR value,const void *fmt,K32_LPWSTR out,int cap){(void)locale;return GetNumberFormatW(0,flags,value,fmt,out,cap);}

__attribute__((dllexport)) int GetCurrencyFormatEx(K32_LPCWSTR locale,DWORD flags,K32_LPCWSTR value,const void *fmt,K32_LPWSTR out,int cap){(void)locale;return GetCurrencyFormatW(0,flags,value,fmt,out,cap);}

__attribute__((dllexport)) int GetDurationFormat(DWORD locale,DWORD flags,const K32_SYSTEMTIME *st,K32_SIZE_T duration,K32_LPCWSTR format,K32_LPWSTR out,int cap){(void)locale;(void)flags;(void)st;(void)duration;(void)format;if(out&&cap)out[0]=0;return 1;}

__attribute__((dllexport)) int GetDurationFormatEx(K32_LPCWSTR locale,DWORD flags,const K32_SYSTEMTIME *st,K32_SIZE_T duration,K32_LPCWSTR format,K32_LPWSTR out,int cap){(void)locale;return GetDurationFormat(0,flags,st,duration,format,out,cap);}

__attribute__((dllexport)) int FindNLSString(DWORD locale,DWORD flags,K32_LPCWSTR src,int src_len,K32_LPCWSTR value,int value_len,int *found){(void)locale;(void)flags;if(found)*found=-1;if(!src||!value)return -1;int sl=src_len<0?lstrlenW(src):src_len,vl=value_len<0?lstrlenW(value):value_len;if(vl==0){if(found)*found=0;return 0;}for(int i=0;i+vl<=sl;i++){int j=0;while(j<vl&&src[i+j]==value[j])j++;if(j==vl){if(found)*found=i;return i;}}return -1;}

__attribute__((dllexport)) int FindNLSStringEx(K32_LPCWSTR locale,DWORD flags,K32_LPCWSTR src,int src_len,K32_LPCWSTR value,int value_len,int *found,void *version,void *reserved,LPVOID handle){(void)locale;(void)version;(void)reserved;(void)handle;return FindNLSString(0,flags,src,src_len,value,value_len,found);}

__attribute__((dllexport)) int CompareStringEx(K32_LPCWSTR locale,DWORD flags,K32_LPCWSTR a,int na,K32_LPCWSTR c,int nc,void *version,void *reserved,LPVOID handle){(void)locale;(void)version;(void)reserved;(void)handle;return CompareStringW(0,flags,a,na,c,nc);}

__attribute__((dllexport)) BOOL SetFileInformationByHandle(HANDLE file,int cls,const void *info,DWORD size){(void)file;(void)cls;(void)info;(void)size;k32_last_error=50;return 0;}

__attribute__((dllexport)) DWORD GetFileAttributesTransactedA(LPCSTR path,void *data,HANDLE transaction){(void)path;(void)data;(void)transaction;k32_last_error=50;return 0xFFFFFFFFu;}

__attribute__((dllexport)) DWORD GetFileAttributesTransactedW(K32_LPCWSTR path,void *data,HANDLE transaction){(void)path;(void)data;(void)transaction;k32_last_error=50;return 0xFFFFFFFFu;}

__attribute__((dllexport)) BOOL SetFileAttributesTransactedA(LPCSTR path,DWORD attr,HANDLE transaction){(void)path;(void)attr;(void)transaction;k32_last_error=50;return 0;}

__attribute__((dllexport)) BOOL SetFileAttributesTransactedW(K32_LPCWSTR path,DWORD attr,HANDLE transaction){(void)path;(void)attr;(void)transaction;k32_last_error=50;return 0;}
