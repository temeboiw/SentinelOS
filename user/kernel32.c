typedef unsigned long long u64;
typedef long long i64;
typedef unsigned int DWORD;
typedef int BOOL;
typedef void *HANDLE;
typedef void *LPVOID;
typedef const char *LPCSTR;
typedef const void *LPCVOID;
typedef char *LPSTR;

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
__attribute__((dllexport)) DWORD GetLastError(void) { return 0; }
__attribute__((dllexport)) void SetLastError(DWORD e) { (void)e; }
__attribute__((dllexport)) u64 GetTickCount64(void) { return sy3(33,0,0,0); }
__attribute__((dllexport)) void Sleep(DWORD ms) { u64 start=sy3(33,0,0,0); while(sy3(33,0,0,0)-start<ms) __asm__ volatile("pause"); }
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


/* Common process, clock, path and environment APIs used by Win32 programs.
   Unsupported filesystem mutations deliberately fail instead of pretending. */
typedef struct { DWORD low, high; } FILETIME;
typedef struct { i64 quad; } LARGE_INTEGER;
__attribute__((dllexport)) DWORD GetCurrentProcessId(void) { return (DWORD)sy3(2,0,0,0); }
__attribute__((dllexport)) DWORD GetCurrentThreadId(void) { return 1; }
__attribute__((dllexport)) HANDLE GetCurrentProcess(void) { return (HANDLE)(i64)-1; }
__attribute__((dllexport)) HANDLE GetCurrentThread(void) { return (HANDLE)(i64)-2; }
__attribute__((dllexport)) BOOL IsDebuggerPresent(void) { return 0; }
__attribute__((dllexport)) BOOL QueryPerformanceCounter(LARGE_INTEGER *v) { if(!v)return 0; v->quad=(i64)sy3(33,0,0,0)*10000; return 1; }
__attribute__((dllexport)) BOOL QueryPerformanceFrequency(LARGE_INTEGER *v) { if(!v)return 0; v->quad=10000000; return 1; }
__attribute__((dllexport)) void GetSystemTimeAsFileTime(FILETIME *v) { if(v){v->low=0;v->high=0;} }
__attribute__((dllexport)) DWORD GetCurrentDirectoryA(DWORD cap,LPSTR out) { const char p[]="C:\\"; DWORD n=sizeof p-1;if(!out||cap<=n)return n+1;for(DWORD i=0;i<=n;i++)out[i]=p[i];return n; }
__attribute__((dllexport)) DWORD GetWindowsDirectoryA(LPSTR out,DWORD cap) { const char p[]="C:\\Windows"; DWORD n=sizeof p-1;if(!out||cap<=n)return n+1;for(DWORD i=0;i<=n;i++)out[i]=p[i];return n; }
__attribute__((dllexport)) DWORD GetSystemDirectoryA(LPSTR out,DWORD cap) { const char p[]="C:\\Windows\\System32"; DWORD n=sizeof p-1;if(!out||cap<=n)return n+1;for(DWORD i=0;i<=n;i++)out[i]=p[i];return n; }
__attribute__((dllexport)) DWORD GetTempPathA(DWORD cap,LPSTR out) { const char p[]="C:\\Users\\Public\\Downloads\\"; DWORD n=sizeof p-1;if(!out||cap<=n)return n+1;for(DWORD i=0;i<=n;i++)out[i]=p[i];return n; }
__attribute__((dllexport)) DWORD GetModuleFileNameA(HANDLE module,LPSTR out,DWORD cap) { (void)module;if(out&&cap)out[0]=0;return 0; }
__attribute__((dllexport)) HANDLE GetModuleHandleA(LPCSTR name) { (void)name; return (HANDLE)1; }
__attribute__((dllexport)) HANDLE GetModuleHandleW(const unsigned short *name) { (void)name; return (HANDLE)1; }
__attribute__((dllexport)) BOOL GetProcessAffinityMask(HANDLE p,u64 *process,u64 *system) { (void)p;if(process)*process=1;if(system)*system=1;return 1; }
__attribute__((dllexport)) DWORD GetFileAttributesA(LPCSTR path) { (void)path; return 0xFFFFFFFFu; }
__attribute__((dllexport)) BOOL CreateDirectoryA(LPCSTR path,void *sec) { (void)path;(void)sec;return 0; }
__attribute__((dllexport)) BOOL DeleteFileA(LPCSTR path) { (void)path;return 0; }
__attribute__((dllexport)) BOOL MoveFileA(LPCSTR from,LPCSTR to) { (void)from;(void)to;return 0; }
__attribute__((dllexport)) BOOL GetComputerNameA(LPSTR name,DWORD *size) { const char p[]="SENTINELOS";DWORD n=sizeof p-1;if(!size)return 0;if(!name||*size<=n){*size=n+1;return 0;}for(DWORD i=0;i<=n;i++)name[i]=p[i];*size=n;return 1; }
__attribute__((dllexport)) DWORD GetEnvironmentVariableA(LPCSTR name,LPSTR out,DWORD cap) { (void)name;if(out&&cap)out[0]=0;return 0; }
__attribute__((dllexport)) BOOL SetEnvironmentVariableA(LPCSTR name,LPCSTR value) { (void)name;(void)value;return 0; }
__attribute__((dllexport)) BOOL FlushFileBuffers(HANDLE h) { (void)h;return 1; }
__attribute__((dllexport)) BOOL GetExitCodeProcess(HANDLE h,DWORD *code) { (void)h;if(code)*code=259;return 1; }
__attribute__((dllexport)) BOOL TerminateProcess(HANDLE h,DWORD code) { (void)h;sy3(SYS_Exit,code,0,0);return 1; }
__attribute__((dllexport)) BOOL ProcessIdToSessionId(DWORD pid,DWORD *sid) { (void)pid;if(sid)*sid=0;return 1; }
