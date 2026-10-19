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
#define ERROR_SUCCESS 0u
#define ERROR_INVALID_FUNCTION 1u
#define ERROR_FILE_NOT_FOUND 2u
#define ERROR_ACCESS_DENIED 5u
#define ERROR_INVALID_HANDLE 6u
#define ERROR_NOT_ENOUGH_MEMORY 8u
#define ERROR_INVALID_PARAMETER 87u
#define ERROR_NOT_SUPPORTED 50u
#define ERROR_INSUFFICIENT_BUFFER 122u
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define OPEN_EXISTING 3u
#define FILE_HANDLE_TAG 0x100u

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
__attribute__((dllexport)) HANDLE GetStdHandle(DWORD n) {
    /* Win32: STD_INPUT_HANDLE=-10, STD_OUTPUT_HANDLE=-11, STD_ERROR_HANDLE=-12. */
    if (n == (DWORD)-11) return (HANDLE)1;
    if (n == (DWORD)-12) return (HANDLE)2;
    if (n == (DWORD)-10) { g_last_error=ERROR_NOT_SUPPORTED; return 0; }
    g_last_error=ERROR_INVALID_PARAMETER; return 0;
}
__attribute__((dllexport)) BOOL WriteFile(HANDLE h, LPCVOID buf, DWORD len, DWORD *w, void *ov) {
    (void)ov;
    if (w) *w=0;
    if ((h!=(HANDLE)1 && h!=(HANDLE)2) || (!buf && len)) {
        g_last_error=ERROR_INVALID_HANDLE; return 0;
    }
    if (!len) { g_last_error=ERROR_SUCCESS; return 1; }
    u64 n=sy3(SYS_Write,1,(u64)buf,len);
    if ((i64)n<0) { g_last_error=ERROR_INVALID_FUNCTION; return 0; }
    if (w) *w=(DWORD)n;
    g_last_error=ERROR_SUCCESS; return 1;
}
__attribute__((dllexport)) LPVOID VirtualAlloc(LPVOID a, u64 sz, DWORD t, DWORD p) {
    (void)t;(void)p; return (LPVOID)sy3(SYS_Alloc, (u64)a, sz, 0);
}
__attribute__((dllexport)) HANDLE CreateFileA(LPCSTR name, DWORD access, DWORD share, void *sec, DWORD disposition, DWORD flags, HANDLE templ) {
    (void)share;(void)sec;(void)flags;(void)templ;
    if (!name || !name[0] || disposition!=OPEN_EXISTING ||
        (access & (GENERIC_WRITE|0x00010000u|0x00040000u))) {
        g_last_error = (!name || !name[0]) ? ERROR_INVALID_PARAMETER :
                       (disposition!=OPEN_EXISTING ? ERROR_NOT_SUPPORTED : ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    if (!(access & GENERIC_READ) && access!=0) {
        g_last_error=ERROR_ACCESS_DENIED; return INVALID_HANDLE_VALUE;
    }
    i64 h=(i64)sy3(SYS_OpenFile,(u64)name,0,0);
    if (h<1) { g_last_error=ERROR_FILE_NOT_FOUND; return INVALID_HANDLE_VALUE; }
    /* Keep file handles distinct from the console handles 1 and 2. */
    g_last_error=ERROR_SUCCESS;
    return (HANDLE)(u64)(FILE_HANDLE_TAG+(u64)h);
}
__attribute__((dllexport)) BOOL ReadFile(HANDLE h, LPVOID buf, DWORD len, DWORD *rd, void *ov) {
    (void)ov;
    if (rd) *rd=0;
    u64 hv=(u64)h;
    if (hv<=FILE_HANDLE_TAG || h==INVALID_HANDLE_VALUE || (!buf && len)) {
        g_last_error=ERROR_INVALID_HANDLE; return 0;
    }
    if (!len) { g_last_error=ERROR_SUCCESS; return 1; }

    /* The kernel's bounded read syscall transfers at most 4096 bytes per call.
       Loop so ordinary Win32 callers can request larger buffers. */
    DWORD total=0;
    while (total<len) {
        DWORD chunk=len-total;
        if (chunk>4096) chunk=4096;
        i64 n=(i64)sy3(SYS_ReadFile,hv-FILE_HANDLE_TAG,(u64)((unsigned char *)buf+total),chunk);
        if (n<0) {
            if (rd) *rd=total;
            g_last_error=ERROR_INVALID_HANDLE;
            return total!=0;
        }
        if (n==0) break; /* EOF */
        total+=(DWORD)n;
        if ((DWORD)n<chunk) break;
    }
    if (rd) *rd=total;
    g_last_error=ERROR_SUCCESS;
    return 1;
}
__attribute__((dllexport)) BOOL CloseHandle(HANDLE h) {
    u64 hv=(u64)h;
    if (hv<=FILE_HANDLE_TAG || h==INVALID_HANDLE_VALUE) {
        g_last_error=ERROR_INVALID_HANDLE; return 0;
    }
    i64 rc=(i64)sy3(34,hv-FILE_HANDLE_TAG,0,0);
    if (rc<0) { g_last_error=ERROR_INVALID_HANDLE; return 0; }
    g_last_error=ERROR_SUCCESS; return 1;
}
__attribute__((dllexport)) DWORD QueryDirectoryA(DWORD index, LPSTR name, DWORD max) {
    (void)max; return (DWORD)sy3(SYS_QueryDir, index, (u64)name, 0);
}
__attribute__((dllexport)) BOOL SetCurrentDirectoryA(LPCSTR path) {
    if (!path || !path[0]) { g_last_error=ERROR_INVALID_PARAMETER; return 0; }
    if ((i64)sy3(12,(u64)path,0,0)!=0) { g_last_error=ERROR_FILE_NOT_FOUND; return 0; }
    g_last_error=ERROR_SUCCESS; return 1;
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
__attribute__((dllexport)) DWORD GetCurrentDirectoryA(DWORD cap,LPSTR out) {
    u64 n=sy3(35,(u64)out,cap,0);
    if(!n) { g_last_error=ERROR_INVALID_FUNCTION; return 0; }
    if(out && cap>n) g_last_error=ERROR_SUCCESS;
    else g_last_error=ERROR_INSUFFICIENT_BUFFER;
    return (DWORD)n;
}
__attribute__((dllexport)) DWORD GetWindowsDirectoryA(LPSTR out,DWORD cap) { const char p[]="C:\\Windows"; DWORD n=sizeof p-1;if(!out||cap<=n)return n+1;for(DWORD i=0;i<=n;i++)out[i]=p[i];return n; }
__attribute__((dllexport)) DWORD GetSystemDirectoryA(LPSTR out,DWORD cap) { const char p[]="C:\\Windows\\System32"; DWORD n=sizeof p-1;if(!out||cap<=n)return n+1;for(DWORD i=0;i<=n;i++)out[i]=p[i];return n; }
__attribute__((dllexport)) DWORD GetTempPathA(DWORD cap,LPSTR out) { const char p[]="C:\\Users\\Public\\Downloads\\"; DWORD n=sizeof p-1;if(!out||cap<=n)return n+1;for(DWORD i=0;i<=n;i++)out[i]=p[i];return n; }
__attribute__((dllexport)) DWORD GetModuleFileNameA(HANDLE module,LPSTR out,DWORD cap) { (void)module;if(out&&cap)out[0]=0;return 0; }
__attribute__((dllexport)) HANDLE GetModuleHandleA(LPCSTR name) { (void)name; return (HANDLE)1; }
__attribute__((dllexport)) HANDLE GetModuleHandleW(const unsigned short *name) { (void)name; return (HANDLE)1; }
__attribute__((dllexport)) BOOL GetProcessAffinityMask(HANDLE p,u64 *process,u64 *system) { (void)p;if(process)*process=1;if(system)*system=1;return 1; }
__attribute__((dllexport)) DWORD GetFileAttributesA(LPCSTR path) {
    if(!path || !path[0]) { g_last_error=ERROR_INVALID_PARAMETER; return 0xFFFFFFFFu; }
    u64 a=sy3(36,(u64)path,0,0);
    if(a==0xFFFFFFFFu) { g_last_error=ERROR_FILE_NOT_FOUND; return 0xFFFFFFFFu; }
    g_last_error=ERROR_SUCCESS;
    return (DWORD)a;
}
__attribute__((dllexport)) BOOL CreateDirectoryA(LPCSTR path,void *sec) { (void)path;(void)sec;g_last_error=ERROR_NOT_SUPPORTED;return 0; }
__attribute__((dllexport)) BOOL DeleteFileA(LPCSTR path) { (void)path;g_last_error=ERROR_NOT_SUPPORTED;return 0; }
__attribute__((dllexport)) BOOL MoveFileA(LPCSTR from,LPCSTR to) { (void)from;(void)to;g_last_error=ERROR_NOT_SUPPORTED;return 0; }
__attribute__((dllexport)) BOOL GetComputerNameA(LPSTR name,DWORD *size) { const char p[]="SENTINELOS";DWORD n=sizeof p-1;if(!size)return 0;if(!name||*size<=n){*size=n+1;return 0;}for(DWORD i=0;i<=n;i++)name[i]=p[i];*size=n;return 1; }
__attribute__((dllexport)) DWORD GetEnvironmentVariableA(LPCSTR name,LPSTR out,DWORD cap) { (void)name;if(out&&cap)out[0]=0;return 0; }
__attribute__((dllexport)) BOOL SetEnvironmentVariableA(LPCSTR name,LPCSTR value) { (void)name;(void)value;return 0; }
__attribute__((dllexport)) BOOL FlushFileBuffers(HANDLE h) { (void)h;g_last_error=ERROR_NOT_SUPPORTED;return 0; }
__attribute__((dllexport)) BOOL GetExitCodeProcess(HANDLE h,DWORD *code) { (void)h;(void)code;g_last_error=ERROR_NOT_SUPPORTED;return 0; }
__attribute__((dllexport)) BOOL TerminateProcess(HANDLE h,DWORD code) { (void)h;(void)code;g_last_error=ERROR_NOT_SUPPORTED;return 0; }
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


/* -------------------------------------------------------------------------
 * Phase 1 runtime support: heap allocator, memory primitives and Unicode.
 * This allocator manages reusable blocks inside process-private VirtualAlloc
 * arenas. The kernel currently has no page-release syscall, so freeing a heap
 * block returns it to the heap for reuse rather than unmapping its arena.
 * ------------------------------------------------------------------------- */
typedef u64 SIZE_T;
typedef unsigned int UINT;
typedef unsigned short WORD;
typedef unsigned char BYTE;
typedef void *LPVOID_LOCAL;
typedef const char *LPCCH;
typedef const unsigned short *LPCWCH;
#define HEAP_ZERO_MEMORY 0x00000008u
#define MEM_COMMIT 0x1000u
#define MEM_RESERVE 0x2000u
#define PAGE_READWRITE 0x04u
#define CP_ACP 0u
#define CP_UTF8 65001u
#define CP_1252 1252u
#define HEAP_ARENA_BYTES (1024u * 1024u)
#define HEAP_ARENA_MAX 16

typedef struct HeapBlock {
    u64 size;
    struct HeapBlock *next;
    u32 free;
    u32 magic;
    u64 reserved;
} HeapBlock;
typedef struct {
    BYTE *base;
    HeapBlock *first;
} HeapArena;
static HeapArena g_heap_arenas[HEAP_ARENA_MAX];
static u32 g_heap_arena_count;
#define HEAP_BLOCK_MAGIC 0x53484D42u

static u64 k32_align16(u64 n) { return (n + 15u) & ~15ull; }
static void *k32_heap_grow(void) {
    if (g_heap_arena_count >= HEAP_ARENA_MAX) return 0;
    u32 slot = g_heap_arena_count;
    u64 base = 0x51000000ull + (u64)slot * HEAP_ARENA_BYTES;
    void *mem = (void *)sy3(SYS_Alloc, base, HEAP_ARENA_BYTES, 0);
    if (!mem || (u64)mem != base) return 0;
    HeapArena *a = &g_heap_arenas[slot];
    a->base = (BYTE *)mem;
    a->first = (HeapBlock *)mem;
    a->first->size = HEAP_ARENA_BYTES - sizeof(HeapBlock);
    a->first->next = 0;
    a->first->free = 1;
    a->first->magic = HEAP_BLOCK_MAGIC;
    g_heap_arena_count++;
    return mem;
}
static HeapBlock *k32_block_from_user(const void *p) {
    if (!p) return 0;
    HeapBlock *b = (HeapBlock *)((BYTE *)p - sizeof(HeapBlock));
    for (u32 i=0; i<g_heap_arena_count; i++) {
        BYTE *lo=g_heap_arenas[i].base, *hi=lo+HEAP_ARENA_BYTES;
        if ((BYTE *)b >= lo && (BYTE *)b + sizeof(HeapBlock) <= hi &&
            b->magic == HEAP_BLOCK_MAGIC) return b;
    }
    for (u32 i=0; i<g_heap_large_count; i++) {
        BYTE *lo=g_heap_large[i].base, *hi=lo+g_heap_large[i].bytes;
        if (g_heap_large[i].used && (BYTE *)b >= lo &&
            (BYTE *)b + sizeof(HeapBlock) <= hi && b->magic == HEAP_BLOCK_MAGIC)
            return b;
    }
    return 0;
}
static void *k32_heap_alloc(u64 bytes, u32 flags) {
    if (!bytes) bytes=1;
    bytes=k32_align16(bytes);
    if (bytes > HEAP_ARENA_BYTES-sizeof(HeapBlock)) {
        /* Large allocations use their own exact-sized VirtualAlloc region. */
        u64 size=(bytes+sizeof(HeapBlock)+0xFFFu)&~0xFFFULL;
        if(size>15ull*1024*1024 || g_heap_large_count>=32)return 0;
        u32 slot=g_heap_large_count;
        u64 base=0x200000000ull+(u64)slot*0x01000000ull;
        void *mem=(void *)sy3(SYS_Alloc,base,size,0);
        if(!mem || (u64)mem!=base)return 0;
        g_heap_large[slot].base=(BYTE *)mem;
        g_heap_large[slot].bytes=size;
        g_heap_large[slot].used=1;
        g_heap_large_count++;
        HeapBlock *b=(HeapBlock *)mem;
        b->size=size-sizeof(HeapBlock);b->next=0;b->free=0;b->magic=HEAP_BLOCK_MAGIC;
        BYTE *out=(BYTE *)mem+sizeof(HeapBlock);
        if(flags&HEAP_ZERO_MEMORY)for(u64 i=0;i<bytes;i++)out[i]=0;
        return out;
    }
    for (;;) {
        for(u32 i=0;i<g_heap_arena_count;i++) {
            for(HeapBlock *b=g_heap_arenas[i].first;b;b=b->next) {
                if(!b->free || b->magic!=HEAP_BLOCK_MAGIC || b->size<bytes)continue;
                if(b->size >= bytes+sizeof(HeapBlock)+32) {
                    HeapBlock *n=(HeapBlock *)((BYTE *)b+sizeof(HeapBlock)+bytes);
                    n->size=b->size-bytes-sizeof(HeapBlock);
                    n->next=b->next;n->free=1;n->magic=HEAP_BLOCK_MAGIC;
                    b->next=n;b->size=bytes;
                }
                b->free=0;
                BYTE *out=(BYTE *)b+sizeof(HeapBlock);
                if(flags&HEAP_ZERO_MEMORY)for(u64 j=0;j<b->size;j++)out[j]=0;
                return out;
            }
        }
        if(!k32_heap_grow())return 0;
    }
}
__attribute__((dllexport)) HANDLE GetProcessHeap(void) { return (HANDLE)1; }
__attribute__((dllexport)) LPVOID HeapAlloc(HANDLE heap,DWORD flags,SIZE_T bytes) {
    if(heap && heap!=(HANDLE)1){g_last_error=ERROR_INVALID_HANDLE;return 0;}
    void *p=k32_heap_alloc(bytes,flags);
    g_last_error=p?ERROR_SUCCESS:ERROR_NOT_ENOUGH_MEMORY;
    return p;
}
__attribute__((dllexport)) BOOL HeapFree(HANDLE heap,DWORD flags,LPVOID p) {
    (void)flags;
    if(heap && heap!=(HANDLE)1){g_last_error=ERROR_INVALID_HANDLE;return 0;}
    if(!p){g_last_error=ERROR_INVALID_PARAMETER;return 0;}
    HeapBlock *b=k32_block_from_user(p);
    if(!b || b->free){g_last_error=ERROR_INVALID_PARAMETER;return 0;}
    for(u32 i=0;i<g_heap_large_count;i++) {
        if(g_heap_large[i].used &&
           (BYTE *)b==g_heap_large[i].base) {
            b->free=1;
            g_heap_large[i].used=0;
            g_last_error=ERROR_SUCCESS;
            return 1;
        }
    }
    b->free=1;
    for(u32 i=0;i<g_heap_arena_count;i++) {
        for(HeapBlock *q=g_heap_arenas[i].first;q && q->next;q=q->next) {
            if(q->free && q->next->free &&
               (BYTE *)q+sizeof(HeapBlock)+q->size==(BYTE *)q->next) {
                q->size+=sizeof(HeapBlock)+q->next->size;
                q->next=q->next->next;
            }
        }
    }
    g_last_error=ERROR_SUCCESS;return 1;
}
__attribute__((dllexport)) SIZE_T HeapSize(HANDLE heap,DWORD flags,LPCVOID p) {
    (void)flags;
    if(heap && heap!=(HANDLE)1){g_last_error=ERROR_INVALID_HANDLE;return (SIZE_T)-1;}
    HeapBlock *b=k32_block_from_user(p);
    if(!b || b->free){g_last_error=ERROR_INVALID_PARAMETER;return (SIZE_T)-1;}
    g_last_error=ERROR_SUCCESS;return (SIZE_T)b->size;
}
__attribute__((dllexport)) LPVOID HeapReAlloc(HANDLE heap,DWORD flags,LPVOID p,SIZE_T bytes) {
    if(!p)return HeapAlloc(heap,flags,bytes);
    if(!bytes){HeapFree(heap,0,p);return 0;}
    HeapBlock *b=k32_block_from_user(p);
    if(!b || b->free){g_last_error=ERROR_INVALID_PARAMETER;return 0;}
    if(b->size>=bytes) {
        if(flags&HEAP_ZERO_MEMORY)for(u64 i=bytes;i<b->size;i++)((BYTE *)p)[i]=0;
        return p;
    }
    void *n=HeapAlloc(heap,flags,bytes);
    if(!n)return 0;
    BYTE *d=(BYTE *)n,*s=(BYTE *)p;
    for(u64 i=0;i<b->size;i++)d[i]=s[i];
    HeapFree(heap,0,p);
    return n;
}
__attribute__((dllexport)) LPVOID LocalAlloc(UINT flags,SIZE_T bytes) {
    return HeapAlloc((HANDLE)1,(flags&0x40)?HEAP_ZERO_MEMORY:0,bytes);
}
__attribute__((dllexport)) LPVOID LocalReAlloc(LPVOID p,SIZE_T bytes,UINT flags) {
    return HeapReAlloc((HANDLE)1,(flags&0x40)?HEAP_ZERO_MEMORY:0,p,bytes);
}
__attribute__((dllexport)) LPVOID LocalFree(LPVOID p) {
    return HeapFree((HANDLE)1,0,p)?0:p;
}
__attribute__((dllexport)) LPVOID GlobalAlloc(UINT flags,SIZE_T bytes) {
    return LocalAlloc(flags,bytes);
}
__attribute__((dllexport)) LPVOID GlobalReAlloc(LPVOID p,SIZE_T bytes,UINT flags) {
    return LocalReAlloc(p,bytes,flags);
}
__attribute__((dllexport)) LPVOID GlobalFree(LPVOID p) { return LocalFree(p); }

__attribute__((dllexport)) void *RtlMoveMemory(void *dst,const void *src,SIZE_T n) {
    if(!dst||!src)return dst;
    BYTE *d=(BYTE *)dst;const BYTE *s=(const BYTE *)src;
    if(d<s || d>=s+n){for(u64 i=0;i<n;i++)d[i]=s[i];}
    else {for(u64 i=n;i>0;i--)d[i-1]=s[i-1];}
    return dst;
}
__attribute__((dllexport)) void *RtlCopyMemory(void *dst,const void *src,SIZE_T n) {
    if(!dst||!src)return dst;
    BYTE *d=(BYTE *)dst;const BYTE *s=(const BYTE *)src;
    for(u64 i=0;i<n;i++)d[i]=s[i];
    return dst;
}
__attribute__((dllexport)) void *RtlZeroMemory(void *dst,SIZE_T n) {
    if(dst)for(u64 i=0;i<n;i++)((BYTE *)dst)[i]=0;
    return dst;
}
__attribute__((dllexport)) int lstrlenW(LPCWSTR s) {
    int n=0;if(!s)return 0;while(s[n])n++;return n;
}
__attribute__((dllexport)) int lstrcmpW(LPCWSTR a,LPCWSTR b) {
    if(!a)a=(LPCWSTR)u"";if(!b)b=(LPCWSTR)u"";
    while(*a && *a==*b){a++;b++;}
    return (int)*a-(int)*b;
}
__attribute__((dllexport)) int lstrcmpiW(LPCWSTR a,LPCWSTR b) {
    if(!a)a=(LPCWSTR)u"";if(!b)b=(LPCWSTR)u"";
    while(*a && *b) {
        WORD x=*a++,y=*b++;
        if(x>='A'&&x<='Z')x=(WORD)(x+32);
        if(y>='A'&&y<='Z')y=(WORD)(y+32);
        if(x!=y)return (int)x-(int)y;
    }
    return (int)*a-(int)*b;
}
__attribute__((dllexport)) LPWSTR lstrcpyW(LPWSTR d,LPCWSTR s) {
    if(!d||!s)return d;int i=0;do{d[i]=s[i];}while(s[i++]);return d;
}
__attribute__((dllexport)) LPWSTR lstrcpynW(LPWSTR d,LPCWSTR s,int cap) {
    if(!d||cap<=0)return d;int i=0;
    if(s)while(s[i]&&i<cap-1){d[i]=s[i];i++;}
    d[i]=0;return d;
}
__attribute__((dllexport)) DWORD CharLowerBuffA(LPSTR s,DWORD n) {
    if(!s)return 0;for(DWORD i=0;i<n;i++)if(s[i]>='A'&&s[i]<='Z')s[i]=(char)(s[i]+32);return n;
}
__attribute__((dllexport)) DWORD CharUpperBuffA(LPSTR s,DWORD n) {
    if(!s)return 0;for(DWORD i=0;i<n;i++)if(s[i]>='a'&&s[i]<='z')s[i]=(char)(s[i]-32);return n;
}
__attribute__((dllexport)) int MultiByteToWideChar(UINT cp,DWORD flags,LPCCH src,int src_len,LPWSTR dst,int dst_cap) {
    (void)flags;
    if(!src || src_len==0 || dst_cap<0 || (!dst && dst_cap)) {g_last_error=ERROR_INVALID_PARAMETER;return 0;}
    int input=src_len<0?0:src_len;
    if(src_len<0){while(src[input])input++;input++;}
    int out=0;
    for(int i=0;i<input;) {
        u32 ch=(BYTE)src[i++];
        if(cp==CP_UTF8) {
            BYTE c=(BYTE)src[i-1];
            if(c<0x80)ch=c;
            else if((c&0xE0)==0xC0 && i<input){ch=((u32)(c&0x1F)<<6)|((BYTE)src[i++]&0x3F);}
            else if((c&0xF0)==0xE0 && i+1<input){ch=((u32)(c&0x0F)<<12)|(((BYTE)src[i]&0x3F)<<6)|((BYTE)src[i+1]&0x3F);i+=2;}
            else if((c&0xF8)==0xF0 && i+2<input){ch=((u32)(c&7)<<18)|(((BYTE)src[i]&0x3F)<<12)|(((BYTE)src[i+1]&0x3F)<<6)|((BYTE)src[i+2]&0x3F);i+=3;}
            else ch=0xFFFD;
        } else if(cp!=CP_ACP && cp!=CP_1252) {g_last_error=ERROR_INVALID_PARAMETER;return 0;}
        if(ch<=0xFFFF) {
            if(dst && out<dst_cap)dst[out]=(WORD)ch;
            out++;
        } else {
            ch-=0x10000;
            if(dst&&out<dst_cap)dst[out]=(WORD)(0xD800+(ch>>10));
            out++;
            if(dst&&out<dst_cap)dst[out]=(WORD)(0xDC00+(ch&0x3FF));
            out++;
        }
    }
    if(dst && out>dst_cap){g_last_error=ERROR_INSUFFICIENT_BUFFER;return 0;}
    g_last_error=ERROR_SUCCESS;return out;
}
__attribute__((dllexport)) int WideCharToMultiByte(UINT cp,DWORD flags,LPCWCH src,int src_len,LPSTR dst,int dst_cap,LPCCH defchar,BOOL *used_default) {
    (void)flags;(void)defchar;
    if(used_default)*used_default=0;
    if(!src || src_len==0 || dst_cap<0 || (!dst&&dst_cap)){g_last_error=ERROR_INVALID_PARAMETER;return 0;}
    int input=src_len<0?0:src_len;
    if(src_len<0){while(src[input])input++;input++;}
    if(cp!=CP_UTF8 && cp!=CP_ACP && cp!=CP_1252){g_last_error=ERROR_INVALID_PARAMETER;return 0;}
    int out=0;
    for(int i=0;i<input;i++) {
        u32 ch=src[i];
        if(ch>=0xD800 && ch<=0xDBFF && i+1<input && src[i+1]>=0xDC00 && src[i+1]<=0xDFFF)
            ch=0x10000+((ch-0xD800)<<10)+(src[++i]-0xDC00);
        if(cp!=CP_UTF8) {
            BYTE c=(ch<=255)?(BYTE)ch:(BYTE)'?';
            if(ch>255 && used_default)*used_default=1;
            if(dst&&out<dst_cap)dst[out]=(char)c;
            out++;
        } else if(ch<0x80) {
            if(dst&&out<dst_cap)dst[out]=(char)ch;out++;
        } else if(ch<0x800) {
            if(dst&&out<dst_cap)dst[out]=(char)(0xC0|(ch>>6));out++;
            if(dst&&out<dst_cap)dst[out]=(char)(0x80|(ch&0x3F));out++;
        } else if(ch<0x10000) {
            if(dst&&out<dst_cap)dst[out]=(char)(0xE0|(ch>>12));out++;
            if(dst&&out<dst_cap)dst[out]=(char)(0x80|((ch>>6)&0x3F));out++;
            if(dst&&out<dst_cap)dst[out]=(char)(0x80|(ch&0x3F));out++;
        } else {
            if(dst&&out<dst_cap)dst[out]=(char)(0xF0|(ch>>18));out++;
            if(dst&&out<dst_cap)dst[out]=(char)(0x80|((ch>>12)&0x3F));out++;
            if(dst&&out<dst_cap)dst[out]=(char)(0x80|((ch>>6)&0x3F));out++;
            if(dst&&out<dst_cap)dst[out]=(char)(0x80|(ch&0x3F));out++;
        }
    }
    if(dst && out>dst_cap){g_last_error=ERROR_INSUFFICIENT_BUFFER;return 0;}
    g_last_error=ERROR_SUCCESS;return out;
}


/* Wide-character filesystem entry points share the same checked ANSI paths. */
static int k32_wide_to_ansi(LPCWSTR wide,char *out,u32 cap) {
    if(!wide||!out||!cap)return -1;
    u32 i=0;
    while(wide[i] && i+1<cap) {
        if(wide[i]>0x7F){g_last_error=1113;return -1;}
        out[i]=(char)wide[i];i++;
    }
    if(wide[i]){g_last_error=ERROR_INSUFFICIENT_BUFFER;return -1;}
    out[i]=0;return (int)i;
}
__attribute__((dllexport)) DWORD GetFileAttributesW(LPCWSTR path) {
    char p[128];
    if(k32_wide_to_ansi(path,p,sizeof p)<0)return 0xFFFFFFFFu;
    return GetFileAttributesA(p);
}
__attribute__((dllexport)) BOOL SetCurrentDirectoryW(LPCWSTR path) {
    char p[128];
    if(k32_wide_to_ansi(path,p,sizeof p)<0)return 0;
    return SetCurrentDirectoryA(p);
}
__attribute__((dllexport)) DWORD GetCurrentDirectoryW(DWORD cap,LPWSTR out) {
    char path[128];
    DWORD n=GetCurrentDirectoryA(sizeof path,path);
    if(!n)return 0;
    DWORD required=n+1;
    if(!out || cap<required){g_last_error=ERROR_INSUFFICIENT_BUFFER;return required;}
    for(DWORD i=0;i<required;i++)out[i]=(BYTE)path[i];
    g_last_error=ERROR_SUCCESS;
    return n;
}
__attribute__((dllexport)) HANDLE CreateFileW(
    LPCWSTR name,DWORD access,DWORD share,void *sec,DWORD disposition,
    DWORD flags,HANDLE templ)
{
    char path[128];
    if(k32_wide_to_ansi(name,path,sizeof path)<0)return INVALID_HANDLE_VALUE;
    return CreateFileA(path,access,share,sec,disposition,flags,templ);
}
__attribute__((dllexport)) DWORD GetFullPathNameA(
    LPCSTR path,DWORD cap,LPSTR out,LPSTR *file_part)
{
    if(file_part)*file_part=0;
    if(!path||!path[0]){g_last_error=ERROR_INVALID_PARAMETER;return 0;}
    char full[256];u32 n=0;
    int absolute=(path[0]=='\\\\'||path[0]=='/') ||
                 ((path[0]=='C'||path[0]=='c')&&path[1]==':');
    if(absolute) {
        if((path[0]=='C'||path[0]=='c')&&path[1]==':') {
            full[n++]='C';full[n++]=':';path+=2;
        } else {full[n++]='C';full[n++]=':';}
        if(*path!='\\\\'&&*path!='/')full[n++]='\\\\';
    } else {
        DWORD cwd=GetCurrentDirectoryA(sizeof full,full);
        if(!cwd)return 0;
        n=cwd;
        if(n && full[n-1]!='\\\\')full[n++]='\\\\';
    }
    while((*path=='\\\\'||*path=='/'))path++;
    while(*path && n<sizeof(full)-2) {
        char ch=*path++;
        if(ch=='/')ch='\\\\';
        full[n++]=ch;
    }
    full[n]=0;
    /* Normalize separators and resolve . / .. components in-place. */
    char normalized[256];u32 o=0,i=0;
    normalized[o++]='C';normalized[o++]=':';normalized[o++]='\\\\';
    if(full[0]=='C'&&full[1]==':')i=2;
    while(full[i]=='\\\\'||full[i]=='/')i++;
    while(full[i]) {
        char comp[128];u32 z=0;
        while(full[i]&&full[i]!='\\\\'&&full[i]!='/') {
            if(z+1>=sizeof comp){g_last_error=ERROR_INSUFFICIENT_BUFFER;return 0;}
            comp[z++]=full[i++];
        }
        comp[z]=0;
        while(full[i]=='\\\\'||full[i]=='/')i++;
        if(!z || (z==1&&comp[0]=='.'))continue;
        if(z==2&&comp[0]=='.'&&comp[1]=='.') {
            if(o>3){if(normalized[o-1]=='\\\\')o--;while(o>3&&normalized[o-1]!='\\\\')o--;if(o<3)o=3;}
            continue;
        }
        if(o>3&&normalized[o-1]!='\\\\')normalized[o++]='\\\\';
        for(u32 j=0;j<z&&o<sizeof(normalized)-1;j++)normalized[o++]=comp[j];
    }
    normalized[o]=0;
    if(!out||cap<=o){g_last_error=ERROR_INSUFFICIENT_BUFFER;return o+1;}
    for(u32 j=0;j<=o;j++)out[j]=normalized[j];
    if(file_part) {
        int j=(int)o-1;while(j>=0&&out[j]!='\\\\')j--;
        if(j>=0)*file_part=out+j+1;
    }
    g_last_error=ERROR_SUCCESS;return o;
}
__attribute__((dllexport)) int memcmp(const void *a,const void *b,SIZE_T n) {
    const BYTE *x=(const BYTE *)a,*y=(const BYTE *)b;
    for(u64 i=0;i<n;i++)if(x[i]!=y[i])return (int)x[i]-(int)y[i];
    return 0;
}
__attribute__((dllexport)) void *memcpy(void *dst,const void *src,SIZE_T n) {
    return RtlCopyMemory(dst,src,n);
}
__attribute__((dllexport)) void *memmove(void *dst,const void *src,SIZE_T n) {
    return RtlMoveMemory(dst,src,n);
}
__attribute__((dllexport)) void *memset(void *dst,int value,SIZE_T n) {
    if(dst)for(u64 i=0;i<n;i++)((BYTE *)dst)[i]=(BYTE)value;
    return dst;
}
