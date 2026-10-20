typedef unsigned long long u64;
typedef long long i64;
typedef unsigned int DWORD;
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
static DWORD k32_shutdown_level=0x280, k32_shutdown_flags=0;
static char k32_console_title[128]="SentinelOS";
static DWORD k32_priority_class=0x20u; static int k32_thread_priority=0;
typedef struct { HANDLE handle; DWORD flags; } K32_HANDLE_FLAGS; static K32_HANDLE_FLAGS k32_handle_flags[32];
static DWORD k32_console_mode=0x0007u; static BOOL k32_cursor_visible=1; static DWORD k32_cursor_size=25; static unsigned short k32_console_attr=7;

#define INVALID_HANDLE_VALUE ((HANDLE)(i64)-1)

enum {
    SYS_Exit=0, SYS_Write=1, SYS_Alloc=3, SYS_OpenFile=5, SYS_ReadFile=6,
    SYS_QueryDir=7, SYS_GetInput=10, SYS_CreateThread=13, SYS_Wait=14
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
__attribute__((dllexport)) BOOL CloseHandle(HANDLE h) { (void)h; return 1; }
__attribute__((dllexport)) DWORD QueryDirectoryA(DWORD index, LPSTR name, DWORD max) {
    (void)max; return (DWORD)sy3(SYS_QueryDir, index, (u64)name, 0);
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
    (void)path; (void)security; return 0;
}
__attribute__((dllexport)) BOOL RemoveDirectoryA(LPCSTR path) { (void)path; return 0; }
__attribute__((dllexport)) BOOL DeleteFileA(LPCSTR path) { (void)path; return 0; }
__attribute__((dllexport)) BOOL MoveFileA(LPCSTR from, LPCSTR to) { (void)from; (void)to; return 0; }
__attribute__((dllexport)) BOOL CopyFileA(LPCSTR from, LPCSTR to, BOOL fail_if_exists) {
    (void)from; (void)to; (void)fail_if_exists; return 0;
}
__attribute__((dllexport)) DWORD GetFileAttributesA(LPCSTR path) { (void)path; return 0xFFFFFFFFu; }
__attribute__((dllexport)) BOOL SetFileAttributesA(LPCSTR path, DWORD attrs) { (void)path; (void)attrs; return 0; }
__attribute__((dllexport)) DWORD GetFileSize(HANDLE file, DWORD *high) {
    (void)file; if (high) *high=0; return 0xFFFFFFFFu;
}
__attribute__((dllexport)) DWORD SetFilePointer(HANDLE file, LONG distance, LONG *high, DWORD method) {
    (void)file; (void)distance; (void)method; if (high) *high=0; return 0xFFFFFFFFu;
}
__attribute__((dllexport)) BOOL FlushFileBuffers(HANDLE file) { (void)file; return 0; }
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
__attribute__((dllexport)) HANDLE CreateEventA(void *security, BOOL manual, BOOL initial, LPCSTR name) {
    (void)security; (void)manual; (void)initial; (void)name; return (HANDLE)0;
}
__attribute__((dllexport)) BOOL SetEvent(HANDLE event) { (void)event; return 0; }
__attribute__((dllexport)) BOOL ResetEvent(HANDLE event) { (void)event; return 0; }
__attribute__((dllexport)) HANDLE CreateMutexA(void *security, BOOL owner, LPCSTR name) {
    (void)security; (void)owner; (void)name; return (HANDLE)0;
}
__attribute__((dllexport)) BOOL ReleaseMutex(HANDLE mutex) { (void)mutex; return 0; }
__attribute__((dllexport)) HANDLE CreateSemaphoreA(void *security, LONG initial, LONG maximum, LPCSTR name) {
    (void)security; (void)initial; (void)maximum; (void)name; return (HANDLE)0;
}
__attribute__((dllexport)) BOOL ReleaseSemaphore(HANDLE semaphore, LONG count, LONG *previous) {
    (void)semaphore; (void)count; if (previous) *previous=0; return 0;
}
__attribute__((dllexport)) DWORD WaitForMultipleObjects(DWORD count, const HANDLE *handles, BOOL wait_all, DWORD ms) {
    (void)count; (void)handles; (void)wait_all; (void)ms; return 0xFFFFFFFFu;
}
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
__attribute__((dllexport)) unsigned long long DeleteFileW(void) { return 0; }
__attribute__((dllexport)) unsigned long long MoveFileW(void) { return 0; }
__attribute__((dllexport)) unsigned long long CopyFileW(void) { return 0; }
__attribute__((dllexport)) unsigned long long CreateDirectoryW(void) { return 0; }
__attribute__((dllexport)) unsigned long long RemoveDirectoryW(void) { return 0; }
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
typedef struct { DWORD dwLowDateTime,dwHighDateTime; } K32_FILETIME;
__attribute__((dllexport)) LONG CompareFileTime(const K32_FILETIME *a,const K32_FILETIME *b){if(!a||!b)return 0;if(a->dwHighDateTime<b->dwHighDateTime)return -1;if(a->dwHighDateTime>b->dwHighDateTime)return 1;if(a->dwLowDateTime<b->dwLowDateTime)return -1;if(a->dwLowDateTime>b->dwLowDateTime)return 1;return 0;}
__attribute__((dllexport)) unsigned long long GetFileAttributesExA(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetFileInformationByHandle(void) { return 0; }
__attribute__((dllexport)) unsigned long long SetEndOfFile(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetOverlappedResult(void) { return 0; }
__attribute__((dllexport)) unsigned long long CancelIo(void) { return 0; }
__attribute__((dllexport)) unsigned long long CreatePipe(void) { return 0; }
__attribute__((dllexport)) unsigned long long PeekNamedPipe(void) { return 0; }
__attribute__((dllexport)) BOOL SetHandleInformation(HANDLE h,DWORD mask,DWORD flags){if(!h||h==INVALID_HANDLE_VALUE)return 0;for(int i=0;i<32;i++){if(k32_handle_flags[i].handle==h){k32_handle_flags[i].flags=(k32_handle_flags[i].flags&~mask)|(flags&mask);return 1;}}for(int i=0;i<32;i++)if(!k32_handle_flags[i].handle){k32_handle_flags[i].handle=h;k32_handle_flags[i].flags=flags&mask;return 1;}return 0;}
__attribute__((dllexport)) BOOL GetHandleInformation(HANDLE h,DWORD *flags){if(!h||h==INVALID_HANDLE_VALUE||!flags)return 0;for(int i=0;i<32;i++)if(k32_handle_flags[i].handle==h){*flags=k32_handle_flags[i].flags;return 1;}for(int i=0;i<32;i++)if(!k32_handle_flags[i].handle){k32_handle_flags[i].handle=h;k32_handle_flags[i].flags=0;*flags=0;return 1;}return 0;}
__attribute__((dllexport)) BOOL DuplicateHandle(HANDLE source_process,HANDLE source,HANDLE target_process,HANDLE *target,DWORD access,BOOL inherit,DWORD options){(void)access;(void)inherit;(void)options;if((source_process==GetCurrentProcess()||source_process==(HANDLE)1)&&(target_process==GetCurrentProcess()||target_process==(HANDLE)1)&&source&&target){*target=source;return 1;}k32_last_error=50;return 0;}
__attribute__((dllexport)) unsigned long long CreateProcessA(void) { return 0; }
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
__attribute__((dllexport)) BOOL GetComputerNameW(K32_LPWSTR out,DWORD *size){const char *name="SENTINELOS";DWORD n=10;if(!size)return 0;if(!out||*size<n+1){*size=n+1;k32_last_error=122;return 0;}k32_ansi_to_wide(out,*size,name);*size=n;return 1;}k32_ansi_to_wide(out,*size+1,name);*size=n;return 1;}
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
