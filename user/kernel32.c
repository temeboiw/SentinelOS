typedef unsigned long long u64;
typedef long long i64;
typedef unsigned int DWORD;
typedef int BOOL;
typedef void *HANDLE;
typedef void *LPVOID;
typedef const char *LPCSTR;
typedef const void *LPCVOID;
typedef char *LPSTR;
typedef unsigned short WCHAR;
typedef WCHAR *LPWSTR;
typedef const WCHAR *LPCWSTR;

static DWORD g_last_error;

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
__attribute__((dllexport)) DWORD GetLastError(void) { return g_last_error; }
__attribute__((dllexport)) void SetLastError(DWORD e) { g_last_error=e; }
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


/* Additional Win32 compatibility exports. These are small, real implementations,
   not placeholders that claim success for unsupported kernel operations. */
typedef struct {
    DWORD cb;
    LPSTR lpReserved;
    LPSTR lpDesktop;
    LPSTR lpTitle;
    DWORD dwX, dwY, dwXSize, dwYSize;
    DWORD dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
    unsigned short wShowWindow, cbReserved2;
    unsigned char *lpReserved2;
    HANDLE hStdInput, hStdOutput, hStdError;
} STARTUPINFOA_LOCAL;

typedef struct {
    HANDLE hProcess;
    HANDLE hThread;
    DWORD dwProcessId;
    DWORD dwThreadId;
} PROCESS_INFORMATION_LOCAL;

typedef struct {
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    char szCSDVersion[128];
} OSVERSIONINFOA_LOCAL;

typedef OSVERSIONINFOA_LOCAL *LPOSVERSIONINFOA_LOCAL;

__attribute__((dllexport)) DWORD GetTickCount(void) {
    return (DWORD)sy3(33,0,0,0);
}
__attribute__((dllexport)) int lstrlenA(LPCSTR s) {
    int n=0; if(!s)return 0; while(s[n])n++; return n;
}
__attribute__((dllexport)) int lstrcmpA(LPCSTR a,LPCSTR b) {
    if(!a)a=""; if(!b)b="";
    while(*a && *a==*b){a++;b++;}
    return (int)(unsigned char)*a-(int)(unsigned char)*b;
}
__attribute__((dllexport)) int lstrcmpiA(LPCSTR a,LPCSTR b) {
    if(!a)a=""; if(!b)b="";
    while(*a && *b) {
        char x=*a++,y=*b++;
        if(x>='A'&&x<='Z')x=(char)(x+('a'-'A'));
        if(y>='A'&&y<='Z')y=(char)(y+('a'-'A'));
        if(x!=y)return (int)(unsigned char)x-(int)(unsigned char)y;
    }
    return (int)(unsigned char)*a-(int)(unsigned char)*b;
}
__attribute__((dllexport)) LPSTR lstrcpyA(LPSTR dst,LPCSTR src) {
    if(!dst||!src)return dst;
    int i=0; do {dst[i]=src[i];} while(src[i++]); return dst;
}
__attribute__((dllexport)) LPSTR lstrcpynA(LPSTR dst,LPCSTR src,int cap) {
    if(!dst||cap<=0)return dst;
    int i=0;
    if(src)while(src[i] && i<cap-1){dst[i]=src[i];i++;}
    dst[i]=0; return dst;
}
__attribute__((dllexport)) DWORD GetVersion(void) {
    /* Version 10.0, build 1; packed in the legacy Win32 GetVersion format. */
    return 10u | (0u<<8) | (1u<<16);
}
__attribute__((dllexport)) BOOL GetVersionExA(LPOSVERSIONINFOA_LOCAL v) {
    if(!v || v->dwOSVersionInfoSize < sizeof(OSVERSIONINFOA_LOCAL)) {
        g_last_error=87; /* ERROR_INVALID_PARAMETER */
        return 0;
    }
    v->dwMajorVersion=10;
    v->dwMinorVersion=0;
    v->dwBuildNumber=1;
    v->dwPlatformId=2; /* VER_PLATFORM_WIN32_NT */
    v->szCSDVersion[0]=0;
    g_last_error=0;
    return 1;
}
__attribute__((dllexport)) BOOL CreateProcessA(
    LPCSTR application, LPSTR command_line, void *process_attributes,
    void *thread_attributes, BOOL inherit_handles, DWORD creation_flags,
    void *environment, LPCSTR current_directory,
    STARTUPINFOA_LOCAL *startup, PROCESS_INFORMATION_LOCAL *information)
{
    (void)command_line;(void)process_attributes;(void)thread_attributes;
    (void)inherit_handles;(void)creation_flags;(void)environment;
    (void)current_directory;(void)startup;
    const char *image=application;
    if(!image || !image[0]) {
        g_last_error=87;
        return 0;
    }
    i64 pid=(i64)sy3(4,(u64)image,0,0);
    if(pid<=0) {
        g_last_error=2; /* ERROR_FILE_NOT_FOUND / image could not be loaded */
        return 0;
    }
    if(information) {
        information->hProcess=(HANDLE)pid;
        information->hThread=0;
        information->dwProcessId=(DWORD)pid;
        information->dwThreadId=0;
    }
    g_last_error=0;
    return 1;
}
__attribute__((dllexport)) BOOL CreateProcessW(
    LPCWSTR application, LPWSTR command_line, void *process_attributes,
    void *thread_attributes, BOOL inherit_handles, DWORD creation_flags,
    void *environment, LPCWSTR current_directory,
    void *startup, PROCESS_INFORMATION_LOCAL *information)
{
    (void)command_line;(void)process_attributes;(void)thread_attributes;
    (void)inherit_handles;(void)creation_flags;(void)environment;
    (void)current_directory;(void)startup;
    if(!application) { g_last_error=87; return 0; }
    char path[128]; int i=0;
    while(application[i] && i<(int)sizeof(path)-1) {
        WCHAR ch=application[i];
        if(ch>127) { g_last_error=1113; return 0; }
        path[i]=(char)ch; i++;
    }
    path[i]=0;
    return CreateProcessA(path,0,0,0,0,0,0,0,0,information);
}
__attribute__((dllexport)) DWORD GetProcessId(HANDLE process) {
    if(!process || process==(HANDLE)(i64)-1) return (DWORD)sy3(2,0,0,0);
    return (DWORD)(u64)process;
}
__attribute__((dllexport)) DWORD GetCurrentProcessIdCompat(void) {
    return (DWORD)sy3(2,0,0,0);
}
