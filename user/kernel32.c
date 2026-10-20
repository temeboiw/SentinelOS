typedef unsigned long long u64;
typedef long long i64;
typedef unsigned int DWORD;
typedef int BOOL;
typedef void *HANDLE;
typedef void *LPVOID;
typedef const char *LPCSTR;
typedef const void *LPCVOID;
typedef char *LPSTR;
static DWORD k32_last_error;

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


/* Compatibility export placeholders: signatures and semantics still need implementation. */
__attribute__((dllexport)) unsigned long long GetCommandLineW(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetModuleHandleW(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetModuleFileNameW(void) { return 0; }
__attribute__((dllexport)) unsigned long long CreateFileW(void) { return 0; }
__attribute__((dllexport)) unsigned long long DeleteFileW(void) { return 0; }
__attribute__((dllexport)) unsigned long long MoveFileW(void) { return 0; }
__attribute__((dllexport)) unsigned long long CopyFileW(void) { return 0; }
__attribute__((dllexport)) unsigned long long CreateDirectoryW(void) { return 0; }
__attribute__((dllexport)) unsigned long long RemoveDirectoryW(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetCurrentDirectoryW(void) { return 0; }
__attribute__((dllexport)) unsigned long long SetCurrentDirectoryW(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetFullPathNameW(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetTempPathW(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetEnvironmentVariableW(void) { return 0; }
__attribute__((dllexport)) unsigned long long SetEnvironmentVariableW(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetFileAttributesW(void) { return 0; }
__attribute__((dllexport)) unsigned long long SetFileAttributesW(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetTempFileNameA(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetTempFileNameW(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetShortPathNameA(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetLongPathNameA(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetDiskFreeSpaceA(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetDiskFreeSpaceExA(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetDriveTypeA(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetLogicalDrives(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetLogicalDriveStringsA(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetVolumeInformationA(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetFileTime(void) { return 0; }
__attribute__((dllexport)) unsigned long long SetFileTime(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetSystemTimeAsFileTime(void) { return 0; }
__attribute__((dllexport)) unsigned long long SystemTimeToFileTime(void) { return 0; }
__attribute__((dllexport)) unsigned long long FileTimeToSystemTime(void) { return 0; }
__attribute__((dllexport)) unsigned long long CompareFileTime(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetFileAttributesExA(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetFileInformationByHandle(void) { return 0; }
__attribute__((dllexport)) unsigned long long SetEndOfFile(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetOverlappedResult(void) { return 0; }
__attribute__((dllexport)) unsigned long long CancelIo(void) { return 0; }
__attribute__((dllexport)) unsigned long long CreatePipe(void) { return 0; }
__attribute__((dllexport)) unsigned long long PeekNamedPipe(void) { return 0; }
__attribute__((dllexport)) unsigned long long SetHandleInformation(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetHandleInformation(void) { return 0; }
__attribute__((dllexport)) unsigned long long DuplicateHandle(void) { return 0; }
__attribute__((dllexport)) unsigned long long CreateProcessA(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetStartupInfoA(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetEnvironmentStringsA(void) { return 0; }
__attribute__((dllexport)) unsigned long long FreeEnvironmentStringsA(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetProcessTimes(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetThreadTimes(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetPriorityClass(void) { return 0; }
__attribute__((dllexport)) unsigned long long SetPriorityClass(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetThreadPriority(void) { return 0; }
__attribute__((dllexport)) unsigned long long SetThreadPriority(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetCurrentProcessorNumber(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetNativeSystemInfo(void) { return 0; }
__attribute__((dllexport)) unsigned long long IsProcessorFeaturePresent(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetSystemDirectoryA(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetWindowsDirectoryA(void) { return 0; }
__attribute__((dllexport)) unsigned long long GetUserDefaultLCID(void) { return 0; }
__attribute__((dllexport)) DWORD GetACP(void) { return 1252u; }


/* Additional Win32 entry points. These ABI placeholders are intentionally conservative;
   replace with OS-backed implementations as the corresponding subsystems mature. */
__attribute__((dllexport)) DWORD GetOEMCP(void) { return 437u; }
__attribute__((dllexport)) unsigned long long GetConsoleCP(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleOutputCP(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long SetConsoleCP(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long SetConsoleOutputCP(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetFileSizeEx(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetFileInformationByHandleEx(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetFinalPathNameByHandleA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetFinalPathNameByHandleW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetVolumePathNameA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetVolumePathNameW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetVolumePathNamesForVolumeNameA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetVolumePathNamesForVolumeNameW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetDriveTypeW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetLogicalDriveStringsW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetVolumeInformationW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetDiskFreeSpaceW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetDiskFreeSpaceExW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetFileAttributesExW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetTempPath2A(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetTempPath2W(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetShortPathNameW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetLongPathNameW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetSystemDirectoryW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetWindowsDirectoryW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetComputerNameW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetUserNameA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetUserNameW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetComputerNameExA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetComputerNameExW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetEnvironmentStringsW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long FreeEnvironmentStringsW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long ExpandEnvironmentStringsA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long ExpandEnvironmentStringsW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetModuleHandleExA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetModuleHandleExW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetStartupInfoW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) DWORD GetProcessId(HANDLE process) { return process==GetCurrentProcess() ? GetCurrentProcessId() : 0; }
__attribute__((dllexport)) DWORD GetThreadId(HANDLE thread) { if (thread==GetCurrentThread()) return GetCurrentThreadId(); return (u64)thread>0 && (u64)thread<0xFFFFFFFFULL ? (DWORD)(u64)thread : 0; }
__attribute__((dllexport)) DWORD GetProcessIdOfThread(HANDLE thread) { return GetThreadId(thread) ? GetCurrentProcessId() : 0; }
__attribute__((dllexport)) unsigned long long OpenProcess(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long OpenThread(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetCurrentProcessToken(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetCurrentThreadToken(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetProcessVersion(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetProcessShutdownParameters(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long SetProcessShutdownParameters(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long SetErrorMode(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetErrorMode(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetSystemTimeAdjustment(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetSystemTimePreciseAsFileTime(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetThreadLocale(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long SetThreadLocale(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetUserDefaultLangID(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetThreadUILanguage(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetUserDefaultUILanguage(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetSystemDefaultUILanguage(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetSystemDefaultLangID(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetSystemDefaultLCID(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleTitleA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleTitleW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long SetConsoleTitleW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleScreenBufferInfo(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleScreenBufferInfoEx(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long SetConsoleScreenBufferInfoEx(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleCursorInfo(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long SetConsoleCursorInfo(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleWindow(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleProcessList(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetNumberOfConsoleInputEvents(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetNumberOfConsoleMouseButtons(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetLargestConsoleWindowSize(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleFontSize(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetCurrentConsoleFont(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetCurrentConsoleFontEx(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long SetCurrentConsoleFontEx(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleHistoryInfo(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long SetConsoleHistoryInfo(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
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
__attribute__((dllexport)) unsigned long long GetConsoleInputExeNameA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleInputExeNameW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleOutputExeNameA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetConsoleOutputExeNameW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
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
__attribute__((dllexport)) unsigned long long SearchPathA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long SearchPathW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetModuleFileNameExA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetModuleFileNameExW(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long HeapValidate(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long HeapCompact(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetProcessHeaps(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long LocalReAlloc(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long LocalSize(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GlobalReAlloc(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GlobalSize(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GlobalLock(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GlobalUnlock(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GlobalHandle(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
__attribute__((dllexport)) unsigned long long GetBinaryTypeA(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
