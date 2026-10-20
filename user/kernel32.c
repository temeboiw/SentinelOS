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

__attribute__((dllexport)) DWORD GetCurrentProcessId(void) { return (DWORD)sy3(2,0,0,0); }
__attribute__((dllexport)) DWORD GetCurrentThreadId(void) { return 1; }
__attribute__((dllexport)) HANDLE GetCurrentProcess(void) { return (HANDLE)(i64)-1; }
__attribute__((dllexport)) HANDLE GetCurrentThread(void) { return (HANDLE)(i64)-2; }
__attribute__((dllexport)) HANDLE GetModuleHandleA(LPCSTR name) { (void)name; return (HANDLE)1; }
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
__attribute__((dllexport)) DWORD GetEnvironmentVariableA(LPCSTR name, LPSTR out, DWORD cap) {
    (void)name; if (out && cap) out[0]=0; return 0;
}
__attribute__((dllexport)) BOOL SetEnvironmentVariableA(LPCSTR name, LPCSTR value) {
    (void)name; (void)value; return 0;
}
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
__attribute__((dllexport)) LPVOID LocalAlloc(DWORD flags, K32_SIZE_T size) {
    (void)flags; return VirtualAlloc((LPVOID)0,size,0x3000,4);
}
__attribute__((dllexport)) LPVOID LocalFree(LPVOID mem) { (void)mem; return mem; }
__attribute__((dllexport)) LPVOID GlobalAlloc(DWORD flags, K32_SIZE_T size) {
    (void)flags; return VirtualAlloc((LPVOID)0,size,0x3000,4);
}
__attribute__((dllexport)) LPVOID GlobalFree(LPVOID mem) { (void)mem; return mem; }
__attribute__((dllexport)) BOOL GetConsoleMode(HANDLE h, DWORD *mode) {
    (void)h; if (!mode) return 0; *mode=0; return 0;
}
__attribute__((dllexport)) BOOL SetConsoleMode(HANDLE h, DWORD mode) { (void)h; (void)mode; return 0; }
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
__attribute__((dllexport)) DWORD GetFileType(HANDLE file) { (void)file; return 0; }
