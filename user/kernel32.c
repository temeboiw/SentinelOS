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

#define INVALID_HANDLE_VALUE ((HANDLE)(i64)-1)
static DWORD k32_last_error;
static char k32_current_dir[260]="C:\\";
static char k32_dll_directory[260];
typedef struct { int used,refs; char name[64]; } K32_MODULE_HANDLE;
static K32_MODULE_HANDLE k32_modules[16];
static DWORD k32_console_cp_in=65001, k32_console_cp_out=65001;
static DWORD k32_error_mode;
static int k32_eqi(const char *a, const char *b);
static DWORD k32_shutdown_level=0x280, k32_shutdown_flags=0;
static char k32_console_title[128]="SentinelOS";
static DWORD k32_priority_class=0x20u; static int k32_thread_priority=0; static DWORD k32_thread_memory_priority=5; static DWORD k32_thread_power_control=1,k32_thread_power_state=0; static BOOL k32_process_boost_disabled,k32_thread_boost_disabled; static unsigned long long k32_working_set_min,k32_working_set_max;
typedef struct { HANDLE handle; DWORD flags; } K32_HANDLE_FLAGS; static K32_HANDLE_FLAGS k32_handle_flags[32];
typedef struct {
    unsigned char data[4096];
    unsigned int read_pos, write_pos, used;
    int read_open, write_open;
} K32_PIPE;
static K32_PIPE k32_pipes[16];
#define K32_PIPE_BASE 0x50500000ULL
static int k32_pipe_decode(HANDLE h, unsigned int *slot, unsigned int *side) {
    unsigned long long v=(unsigned long long)h;
    if(v<K32_PIPE_BASE+1 || v>K32_PIPE_BASE+32) return 0;
    unsigned long long d=v-K32_PIPE_BASE-1;
    if(slot)*slot=(unsigned int)(d/2);
    if(side)*side=(unsigned int)(d&1);
    return 1;
}
static HANDLE k32_pipe_handle(unsigned int slot,unsigned int side) {
    return (HANDLE)(K32_PIPE_BASE+1ULL+(unsigned long long)slot*2ULL+side);
}
static DWORD k32_console_mode=0x0007u; static BOOL k32_cursor_visible=1; static DWORD k32_cursor_size=25; static unsigned short k32_console_attr=7;
typedef struct { HANDLE handle; char path[260]; int used; } K32_OPEN_PATH;
static K32_OPEN_PATH k32_open_paths[32];
static void k32_track_path(HANDLE h,const char *path) {
    if(!h||h==INVALID_HANDLE_VALUE||!path)return;
    int slot=-1;
    for(int i=0;i<32;i++){if(k32_open_paths[i].used&&k32_open_paths[i].handle==h){slot=i;break;}if(!k32_open_paths[i].used&&slot<0)slot=i;}
    if(slot<0)return;
    K32_OPEN_PATH *p=&k32_open_paths[slot];p->handle=h;int j=0;
    while(path[j]&&j<259){p->path[j]=path[j];j++;}p->path[j]=0;p->used=1;
}
static K32_OPEN_PATH *k32_find_path(HANDLE h) {
    for(int i=0;i<32;i++)if(k32_open_paths[i].used&&k32_open_paths[i].handle==h)return &k32_open_paths[i];
    return 0;
}
static void k32_untrack_path(HANDLE h) {
    K32_OPEN_PATH *p=k32_find_path(h);if(p){p->used=0;p->path[0]=0;p->handle=0;}
}


enum {
    SYS_Exit=0, SYS_Write=1, SYS_Alloc=3, SYS_CreateProcess=4, SYS_OpenFile=5, SYS_ReadFile=6,
    SYS_QueryDir=7, SYS_GetProcAddress=9, SYS_GetInput=10, SYS_CreateThread=13, SYS_Wait=14, SYS_CloseFile=22, SYS_GetSystemTime=23, SYS_GetTickCount=24, SYS_QueryFileSize=25, SYS_SetFilePosition=26, SYS_FreeVirtualMemory=27, SYS_ProtectVirtualMemory=28, SYS_QueryVirtualMemory=29, SYS_QueryFileMetadata=30
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
    if(n==0xFFFFFFF6u)return (HANDLE)3;
    if(n==0xFFFFFFF5u)return (HANDLE)1;
    if(n==0xFFFFFFF4u)return (HANDLE)2;
    k32_last_error=87;return 0;
}
__attribute__((dllexport)) BOOL WriteFile(HANDLE h, LPCVOID buf, DWORD len, DWORD *w, void *ov) {
    (void)ov;
    unsigned int slot,side;
    if(k32_pipe_decode(h,&slot,&side)) {
        K32_PIPE *p=&k32_pipes[slot];
        if(side!=1 || !p->write_open || (!buf&&len)) { if(w)*w=0; k32_last_error=109; return 0; }
        if(!p->read_open) { if(w)*w=0; k32_last_error=109; return 0; }
        const unsigned char *src=(const unsigned char*)buf; DWORD n=0;
        while(n<len && p->used<sizeof(p->data)) {
            p->data[p->write_pos]=src[n++]; p->write_pos=(p->write_pos+1u)%sizeof(p->data); p->used++;
        }
        if(w)*w=n;
        k32_last_error=(n==len)?0: 232;
        return n!=0 || len==0;
    }
    if((h!=(HANDLE)1 && h!=(HANDLE)2) || (!buf&&len)) { if(w)*w=0; k32_last_error=6; return 0; }
    u64 n = sy3(SYS_Write, 1, (u64)buf, len);
    if (w) *w = (DWORD)n;
    if(n==(u64)-1){k32_last_error=5;return 0;}
    k32_last_error=0;return 1;
}
__attribute__((dllexport)) LPVOID VirtualAlloc(LPVOID a,u64 sz,DWORD t,DWORD p) {
    if(!sz||(t&0x3000u)!=0x3000u){k32_last_error=87;return 0;}
    if(p!=0x02u&&p!=0x04u&&p!=0x20u&&p!=0x40u){k32_last_error=50;return 0;}
    LPVOID result=(LPVOID)sy3(SYS_Alloc,(u64)a,sz,p);
    if(!result){k32_last_error=8;return 0;}
    k32_last_error=0;return result;
}
__attribute__((dllexport)) HANDLE CreateFileA(LPCSTR name, DWORD a, DWORD s, void *sec, DWORD d, DWORD f, HANDLE t) {
    (void)a;(void)s;(void)sec;(void)d;(void)f;(void)t;
    if(!name||!*name){k32_last_error=87;return INVALID_HANDLE_VALUE;}
    i64 h = (i64)sy3(SYS_OpenFile, (u64)name, 0, 0);
    if(h<0){k32_last_error=2;return INVALID_HANDLE_VALUE;}
    k32_track_path((HANDLE)h,name);k32_last_error=0;return (HANDLE)h;
}
__attribute__((dllexport)) BOOL ReadFile(HANDLE h, LPVOID buf, DWORD len, DWORD *rd, void *ov) {
    (void)ov;
    unsigned int slot,side;
    if(k32_pipe_decode(h,&slot,&side)) {
        K32_PIPE *p=&k32_pipes[slot];
        if(side!=0 || !p->read_open || (!buf&&len)) { if(rd)*rd=0; k32_last_error=6; return 0; }
        if(!p->used) { if(rd)*rd=0; k32_last_error=p->write_open?232:109; return 0; }
        unsigned char *dst=(unsigned char*)buf; DWORD n=0;
        while(n<len && p->used) {
            dst[n++]=p->data[p->read_pos]; p->read_pos=(p->read_pos+1u)%sizeof(p->data); p->used--;
        }
        if(rd)*rd=n;k32_last_error=0;return 1;
    }
    if(h==(HANDLE)3) {
        if(!buf&&len){if(rd)*rd=0;k32_last_error=87;return 0;}
        if(!len){if(rd)*rd=0;k32_last_error=0;return 1;}
        typedef struct { int type,x,y,btn; char key; } K32_INPUT_EVENT;
        K32_INPUT_EVENT ev;
        for(;;) {
            ev.type=ev.x=ev.y=ev.btn=0;ev.key=0;
            if(sy3(SYS_GetInput,(u64)&ev,0,0)) {
                if(ev.type==1&&ev.key){((char*)buf)[0]=ev.key;if(rd)*rd=1;k32_last_error=0;return 1;}
            }
            sy3(21,0,0,0);
        }
    }
    if(h==(HANDLE)1||h==(HANDLE)2){if(rd)*rd=0;k32_last_error=6;return 0;}
    i64 n = (i64)sy3(SYS_ReadFile, (u64)h, (u64)buf, len);
    if (n < 0) { if(rd)*rd=0; k32_last_error=6; return 0; }
    if (rd) *rd = (DWORD)n;
    k32_last_error=0;return 1;
}
__attribute__((dllexport)) BOOL CloseHandle(HANDLE h) {
    unsigned int slot,side;
    if(k32_pipe_decode(h,&slot,&side)) {
        K32_PIPE *p=&k32_pipes[slot];
        if(side==0) { if(!p->read_open){k32_last_error=6;return 0;} p->read_open=0; }
        else { if(!p->write_open){k32_last_error=6;return 0;} p->write_open=0; }
        if(!p->read_open&&!p->write_open){p->used=0;p->read_pos=p->write_pos=0;}
        k32_last_error=0;return 1;
    }
    if(h==(HANDLE)1||h==(HANDLE)2||h==(HANDLE)3){k32_last_error=6;return 0;}
    if(sy3(SYS_CloseFile, (u64)h, 0, 0) == 0){k32_last_error=6;return 0;}
    k32_untrack_path(h);k32_last_error=0;return 1;
}
__attribute__((dllexport)) DWORD QueryDirectoryA(DWORD index, LPSTR name, DWORD max) {
    return (DWORD)sy3(SYS_QueryDir, index, (u64)name, (u64)max);
}
__attribute__((dllexport)) DWORD GetFullPathNameA(LPCSTR path, DWORD cap, LPSTR out, LPSTR *part);
__attribute__((dllexport)) BOOL SetCurrentDirectoryA(LPCSTR path) {
    if(!path||!*path){k32_last_error=87;return 0;}
    if(sy3(12,(u64)path,0,0)!=0){k32_last_error=3;return 0;}
    char full[768];DWORD n=GetFullPathNameA(path,sizeof(full),full,0);
    if(!n||n>=sizeof(full)){k32_last_error=206;return 0;}
    if(n>=sizeof(k32_current_dir)){k32_last_error=206;return 0;}
    for(DWORD i=0;i<=n;i++)k32_current_dir[i]=full[i];
    k32_last_error=0;return 1;
}
__attribute__((dllexport)) DWORD GetLastError(void) { return k32_last_error; }
__attribute__((dllexport)) void SetLastError(DWORD e) { k32_last_error=e; }
__attribute__((dllexport)) u64 GetTickCount64(void) { u64 t=sy3(SYS_GetTickCount,0,0,0);return t==(u64)-1?0:t; }
__attribute__((dllexport)) void Sleep(DWORD ms) { if(!ms){sy3(21,0,0,0);return;}u64 start=sy3(SYS_GetTickCount,0,0,0);if(start==(u64)-1){sy3(21,0,0,0);return;}for(;;){u64 now=sy3(SYS_GetTickCount,0,0,0);if(now==(u64)-1||now-start>=ms)break;sy3(21,0,0,0);} }
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
__attribute__((dllexport)) HANDLE GetModuleHandleA(LPCSTR name){if(!name||k32_eqi(name,"sentinel.exe")||k32_eqi(name,"C:\\sentinel.exe"))return (HANDLE)1;for(int i=0;i<16;i++)if(k32_modules[i].used&&k32_eqi(k32_modules[i].name,name))return (HANDLE)&k32_modules[i];k32_last_error=126;return 0;}
__attribute__((dllexport)) LPSTR GetCommandLineA(void) { return k32_cmdline; }
__attribute__((dllexport)) DWORD GetModuleFileNameA(HANDLE module, LPSTR out, DWORD cap) {
    const char *s = "C:\\sentinel.exe"; DWORD i=0; (void)module;
    if (!out || !cap) return 0;
    while (s[i] && i+1<cap) { out[i]=s[i]; i++; } out[i]=0; return i;
}
__attribute__((dllexport)) void *GetProcAddress(HANDLE module, LPCSTR name) {
    if(!name||!*name){k32_last_error=87;return 0;}
    const char *dll="sentinel.exe";
    if(module&&(unsigned long long)module!=1){int found=0;for(int i=0;i<16;i++)if(module==(HANDLE)&k32_modules[i]&&k32_modules[i].used){dll=k32_modules[i].name;found=1;break;}if(!found){k32_last_error=126;return 0;}}
    void *address=(void*)sy3(SYS_GetProcAddress,(u64)dll,(u64)name,0);
    if(!address)k32_last_error=127;else k32_last_error=0;
    return address;
}
__attribute__((dllexport)) HANDLE LoadLibraryA(LPCSTR name) { if(!name||!*name){k32_last_error=87;return 0;}for(int i=0;i<16;i++)if(k32_modules[i].used&&k32_eqi(k32_modules[i].name,name)){k32_modules[i].refs++;return (HANDLE)&k32_modules[i];}for(int i=0;i<16;i++)if(!k32_modules[i].used){int j=0;while(name[j]&&j<63){k32_modules[i].name[j]=name[j];j++;}k32_modules[i].name[j]=0;k32_modules[i].refs=1;k32_modules[i].used=1;return (HANDLE)&k32_modules[i];}k32_last_error=8;return 0; }
__attribute__((dllexport)) BOOL FreeLibrary(HANDLE module) { for(int i=0;i<16;i++)if(module==(HANDLE)&k32_modules[i]&&k32_modules[i].used){if(k32_modules[i].refs>0)k32_modules[i].refs--;if(!k32_modules[i].refs)k32_modules[i].used=0;k32_last_error=0;return 1;}k32_last_error=126;return 0; }
__attribute__((dllexport)) BOOL VirtualFree(LPVOID address,u64 size,DWORD type) {
    if(!address||((unsigned long long)address&0xFFFULL)){k32_last_error=87;return 0;}
    if(type!=0x8000u){k32_last_error=50;return 0;}
    if(size!=0){k32_last_error=87;return 0;}
    if(!sy3(SYS_FreeVirtualMemory,(u64)address,0,type)){k32_last_error=87;return 0;}
    k32_last_error=0;return 1;
}
__attribute__((dllexport)) BOOL VirtualProtect(LPVOID address,u64 size,DWORD protect,DWORD *old) {
    if(!address||!size||!old){k32_last_error=87;return 0;}
    if(protect!=0x02u&&protect!=0x04u&&protect!=0x20u&&protect!=0x40u){k32_last_error=50;return 0;}
    u64 prior=sy3(SYS_ProtectVirtualMemory,(u64)address,size,protect);
    if(!prior){k32_last_error=87;return 0;}
    *old=(DWORD)prior;k32_last_error=0;return 1;
}
__attribute__((dllexport)) K32_SIZE_T VirtualQuery(LPCVOID address,void *info,K32_SIZE_T len) {
    if(!info||len<48){k32_last_error=87;return 0;}
    u64 n=sy3(SYS_QueryVirtualMemory,(u64)address,(u64)info,len);
    if(!n){k32_last_error=87;return 0;}
    k32_last_error=0;return n;
}
__attribute__((dllexport)) DWORD GetCurrentDirectoryA(DWORD cap, LPSTR out) {
    DWORD n=0;while(k32_current_dir[n])n++;
    if(!out||!cap)return n+1;
    if(cap<=n){DWORD i=0;while(i+1<cap){out[i]=k32_current_dir[i];i++;}out[i]=0;k32_last_error=122;return n+1;}
    for(DWORD i=0;i<=n;i++)out[i]=k32_current_dir[i];k32_last_error=0;return n;
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
    if(!file||file==INVALID_HANDLE_VALUE){k32_last_error=6;return 0xFFFFFFFFu;}
    u64 size=sy3(SYS_QueryFileSize,(u64)file,0,0);if((i64)size<0){k32_last_error=6;return 0xFFFFFFFFu;}
    if(high)*high=(DWORD)(size>>32);k32_last_error=0;return (DWORD)size;
}
__attribute__((dllexport)) DWORD SetFilePointer(HANDLE file, LONG distance, LONG *high, DWORD method) {
    if(!file||file==INVALID_HANDLE_VALUE){k32_last_error=6;return 0xFFFFFFFFu;}
    long long offset;
    if(high)offset=(long long)(((unsigned long long)(unsigned int)*high<<32)|(unsigned int)distance);else offset=(int)distance;
    u64 pos=sy3(SYS_SetFilePosition,(u64)file,(u64)offset,method);
    if((i64)pos<0){k32_last_error=87;return 0xFFFFFFFFu;}
    if(high)*high=(LONG)(pos>>32);k32_last_error=0;return (DWORD)pos;
}
__attribute__((dllexport)) BOOL FlushFileBuffers(HANDLE file) { if(!file||file==INVALID_HANDLE_VALUE){k32_last_error=6;return 0;} k32_last_error=50; return 0; }
__attribute__((dllexport)) DWORD GetTempPathA(DWORD cap, LPSTR out) {
    const char *s="C:\\TEMP\\"; DWORD i=0; if (!out || !cap) return 0;
    while (s[i] && i+1<cap) { out[i]=s[i]; i++; } out[i]=0; return i+1;
}
__attribute__((dllexport)) DWORD GetFullPathNameA(LPCSTR path, DWORD cap, LPSTR out, LPSTR *part) {
    char input[512], full[768]; DWORD n=0,f=0,pos=0; DWORD starts[128],depth=0;
    if(part)*part=0;
    if(!path||!*path){k32_last_error=87;return 0;}
    while(path[n]&&n+1<sizeof(input)){input[n]=path[n];n++;}
    if(path[n]){k32_last_error=206;return 0;}input[n]=0;
    for(DWORD i=0;i<n;i++)if(input[i]=='/')input[i]='\\';
    int has_drive=((input[0]>='A'&&input[0]<='Z')||(input[0]>='a'&&input[0]<='z'))&&input[1]==':';
    if(!has_drive && input[0]!='\\') {
        char combined[512];DWORD c=0;
        while(k32_current_dir[c]&&c+2<sizeof(combined)){combined[c]=k32_current_dir[c];c++;}
        if(c>3&&combined[c-1]!='\\')combined[c++]='\\';
        for(DWORD j=0;input[j]&&c+1<sizeof(combined);j++)combined[c++]=input[j];
        combined[c]=0;DWORD j=0;while(combined[j]&&j+1<sizeof(input)){input[j]=combined[j];j++;}input[j]=0;n=j;
        has_drive=((input[0]>='A'&&input[0]<='Z')||(input[0]>='a'&&input[0]<='z'))&&input[1]==':';
    }
    if(has_drive){full[f++]=input[0];full[f++]=':';full[f++]='\\';pos=(input[2]=='\\')?3:2;}
    else {full[f++]='C';full[f++]=':';full[f++]='\\';pos=(input[0]=='\\')?1:0;}
    while(input[pos]=='\\')pos++;
    while(input[pos]){
        DWORD begin=pos;while(input[pos]&&input[pos]!='\\')pos++;DWORD len=pos-begin;
        if(len==1&&input[begin]=='.'){}
        else if(len==2&&input[begin]=='.'&&input[begin+1]=='.'){
            if(depth){f=starts[--depth];if(f<3)f=3;}
        } else if(len){
            if(f>3&&full[f-1]!='\\')full[f++]='\\';
            if(depth<128)starts[depth++]=f>3?(full[f-1]=='\\'?f-1:f):3;
            for(DWORD j=0;j<len&&f+1<sizeof(full);j++)full[f++]=input[begin+j];
        }
        while(input[pos]=='\\')pos++;
    }
    if(f==2)full[f++]='\\';full[f]=0;
    DWORD last=0;for(DWORD i=0;i<f;i++)if(full[i]=='\\')last=i+1;
    if(!out||!cap){k32_last_error=0;return f+1;}
    if(cap<=f){DWORD i=0;while(i+1<cap){out[i]=full[i];i++;}out[i]=0;k32_last_error=122;return f+1;}
    for(DWORD i=0;i<=f;i++)out[i]=full[i];
    if(part)*part=out+last;
    k32_last_error=0;return f;
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
    if (!out) return 0; out->QuadPart=(long long)(GetTickCount64()*10000ULL); return 1;
}
__attribute__((dllexport)) BOOL QueryPerformanceFrequency(K32_LARGE_INTEGER *out) {
    if (!out) return 0; out->QuadPart=10000000LL; return 1;
}
__attribute__((dllexport)) DWORD GetTickCount(void) { return (DWORD)GetTickCount64(); }
__attribute__((dllexport)) BOOL GetExitCodeProcess(HANDLE process, DWORD *code) {
    (void)process; if (!code) return 0; *code=259; return 1;
}
__attribute__((dllexport)) BOOL TerminateProcess(HANDLE process, DWORD code) {
    if(process!=GetCurrentProcess()&&process!=(HANDLE)1){k32_last_error=50;return 0;}
    sy3(SYS_Exit,code,0,0);for(;;)__asm__ volatile("hlt");
}
typedef struct { void *ptr; K32_SIZE_T size; int freed; } K32_MEMENTRY;
static K32_MEMENTRY k32_mem_table[128];
static void k32_mem_register(void *p,K32_SIZE_T size){if(!p)return;int free_slot=-1;for(int i=0;i<128;i++){if(k32_mem_table[i].ptr==p){k32_mem_table[i].size=size;k32_mem_table[i].freed=0;return;}if((!k32_mem_table[i].ptr||k32_mem_table[i].freed)&&free_slot<0)free_slot=i;}if(free_slot>=0){k32_mem_table[free_slot].ptr=p;k32_mem_table[free_slot].size=size;k32_mem_table[free_slot].freed=0;}}
static K32_MEMENTRY *k32_mem_entry(const void *p){for(int i=0;i<128;i++)if(k32_mem_table[i].ptr==p&&!k32_mem_table[i].freed)return &k32_mem_table[i];return 0;}
static K32_SIZE_T k32_mem_size(void *p){K32_MEMENTRY *e=k32_mem_entry(p);return e?e->size:0;}
__attribute__((dllexport)) HANDLE GetProcessHeap(void) { return (HANDLE)&k32_heap_tag; }
__attribute__((dllexport)) LPVOID HeapAlloc(HANDLE heap, DWORD flags, K32_SIZE_T size) {
    if(heap!=GetProcessHeap()||size==0){k32_last_error=87;return 0;}
    void *p=VirtualAlloc((LPVOID)0,size,0x3000,4);if(!p){k32_last_error=8;return 0;}
    k32_mem_register(p,size);if(flags&0x8u){unsigned char *b=p;for(K32_SIZE_T i=0;i<size;i++)b[i]=0;}k32_last_error=0;return p;
}
__attribute__((dllexport)) BOOL HeapFree(HANDLE heap,DWORD flags,LPVOID mem) {
    (void)flags;if(heap!=GetProcessHeap()||!mem){k32_last_error=87;return 0;}
    K32_MEMENTRY *e=k32_mem_entry(mem);if(!e){k32_last_error=87;return 0;}
    if(!VirtualFree(mem,0,0x8000u))return 0;
    e->freed=1;k32_last_error=0;return 1;
}
__attribute__((dllexport)) LPVOID HeapReAlloc(HANDLE heap, DWORD flags, LPVOID mem, K32_SIZE_T size) {
    if(heap!=GetProcessHeap()||!mem||!size){k32_last_error=87;return 0;}
    K32_MEMENTRY *e=k32_mem_entry(mem);if(!e){k32_last_error=87;return 0;}
    K32_SIZE_T old=e->size;void *p=VirtualAlloc((LPVOID)0,size,0x3000,4);if(!p){k32_last_error=8;return 0;}
    K32_SIZE_T n=old<size?old:size;unsigned char *d=p,*s=mem;for(K32_SIZE_T i=0;i<n;i++)d[i]=s[i];
    if((flags&0x8u)&&size>old)for(K32_SIZE_T i=old;i<size;i++)d[i]=0;
    if(!VirtualFree(mem,0,0x8000u)){VirtualFree(p,0,0x8000u);k32_last_error=8;return 0;}
    e->freed=1;k32_mem_register(p,size);k32_last_error=0;return p;
}
__attribute__((dllexport)) K32_SIZE_T HeapSize(HANDLE heap, DWORD flags, LPCVOID mem) {
    (void)flags;if(heap!=GetProcessHeap()||!mem){k32_last_error=87;return (K32_SIZE_T)-1;}
    K32_MEMENTRY *e=k32_mem_entry(mem);if(!e){k32_last_error=87;return (K32_SIZE_T)-1;}k32_last_error=0;return e->size;
}
__attribute__((dllexport)) LPVOID LocalAlloc(DWORD flags,K32_SIZE_T size){if(!size){k32_last_error=87;return 0;}void *p=VirtualAlloc(0,size,0x3000,4);if(!p){k32_last_error=8;return 0;}k32_mem_register(p,size);if(flags&0x40u){unsigned char *b=p;for(K32_SIZE_T i=0;i<size;i++)b[i]=0;}k32_last_error=0;return p;}
__attribute__((dllexport)) LPVOID GlobalAlloc(DWORD flags,K32_SIZE_T size){if(!size){k32_last_error=87;return 0;}void *p=VirtualAlloc(0,size,0x3000,4);if(!p){k32_last_error=8;return 0;}k32_mem_register(p,size);if(flags&0x40u){unsigned char *b=p;for(K32_SIZE_T i=0;i<size;i++)b[i]=0;}k32_last_error=0;return p;}
__attribute__((dllexport)) BOOL GetConsoleMode(HANDLE h,DWORD *mode){if((h!=(HANDLE)1&&h!=(HANDLE)2&&h!=(HANDLE)3)||!mode){k32_last_error=6;return 0;}*mode=k32_console_mode;k32_last_error=0;return 1;}
__attribute__((dllexport)) BOOL SetConsoleMode(HANDLE h,DWORD mode){if(h!=(HANDLE)1&&h!=(HANDLE)2&&h!=(HANDLE)3){k32_last_error=6;return 0;}k32_console_mode=mode;k32_last_error=0;return 1;}
__attribute__((dllexport)) BOOL ReadConsoleA(HANDLE h, LPVOID buf, DWORD len, DWORD *read, void *reserved) {
    (void)reserved;if(read)*read=0;
    if(h!=(HANDLE)3||(!buf&&len)){k32_last_error=6;return 0;}
    if(!len){k32_last_error=0;return 1;}
    typedef struct { int type,x,y,btn; char key; } K32_INPUT_EVENT;
    char *out=(char*)buf;DWORD n=0;
    while(n<len){
        K32_INPUT_EVENT ev={0};
        if(!sy3(SYS_GetInput,(u64)&ev,0,0)){sy3(21,0,0,0);continue;}
        if(ev.type!=1||!ev.key)continue;
        if(ev.key=='\r'||ev.key=='\n'){
            if(n<len)out[n++]='\r';
            if(n<len)out[n++]='\n';
            const char newline[]="\r\n";DWORD ignored=0;WriteFile((HANDLE)1,newline,2,&ignored,0);break;
        }
        if(ev.key=='\b'||ev.key==127){if(n){n--;const char erase[]="\b \b";DWORD ignored=0;WriteFile((HANDLE)1,erase,3,&ignored,0);}continue;}
        out[n++]=ev.key;DWORD ignored=0;WriteFile((HANDLE)1,&ev.key,1,&ignored,0);
    }
    if(read)*read=n;k32_last_error=0;return 1;
}
__attribute__((dllexport)) BOOL WriteConsoleA(HANDLE h, LPCVOID buf, DWORD len, DWORD *written, void *reserved) {
    (void)reserved; return WriteFile(h,buf,len,written,0);
}
__attribute__((dllexport)) void GetSystemTime(K32_SYSTEMTIME *t) { if(!t)return; if(sy3(SYS_GetSystemTime,(u64)t,0,0)!=(u64)-1){return;} t->wYear=1601;t->wMonth=1;t->wDayOfWeek=1;t->wDay=1;t->wHour=t->wMinute=t->wSecond=t->wMilliseconds=0;k32_last_error=50; }
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
__attribute__((dllexport)) DWORD GetFileType(HANDLE h){if(!h||h==INVALID_HANDLE_VALUE)return 0;if(h==(HANDLE)1||h==(HANDLE)2||h==(HANDLE)3)return 2;unsigned int slot,side;if(k32_pipe_decode(h,&slot,&side))return 3;DWORD high=0,low=GetFileSize(h,&high);if(low==0xFFFFFFFFu&&GetLastError()!=0)return 0;return 1;}


/* Win32 compatibility exports. Implementations are limited by SentinelOS kernel services. */
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
__attribute__((dllexport)) DWORD GetTempFileNameA(LPCSTR path,LPCSTR prefix,UINT unique,LPSTR out) {
    if(!out){k32_last_error=87;return 0;}
    if(!unique){k32_last_error=50;out[0]=0;return 0;}
    char dir[260];if(!path||!*path){DWORD n=GetTempPathA(sizeof(dir),dir);if(!n||n>=sizeof(dir)){k32_last_error=3;return 0;}}else{DWORD i=0;while(path[i]&&i+2<sizeof(dir)){dir[i]=path[i];i++;}dir[i]=0;if(path[i]){k32_last_error=206;return 0;}if(i&&dir[i-1]!='\\'&&dir[i-1]!='/'){dir[i++]='\\';dir[i]=0;}}
    DWORD p=0;while(dir[p]&&p+1<sizeof(dir))p++;if(p+1>=sizeof(dir)){k32_last_error=206;return 0;}
    const char *pre=prefix&&*prefix?prefix:"TMP";for(int j=0;j<3&&pre[j]&&p+1<sizeof(dir);j++)dir[p++]=pre[j];
    static const char hex[]="0123456789ABCDEF";for(int j=0;j<4;j++)dir[p++]=hex[(unique>>(j*4))&15u];dir[p]=0;
    DWORD i=0;while(dir[i]&&i+1<260){out[i]=dir[i];i++;}out[i]=0;if(dir[i]){k32_last_error=206;return 0;}k32_last_error=0;return unique;
}
__attribute__((dllexport)) DWORD GetTempFileNameW(K32_LPCWSTR path,K32_LPCWSTR prefix,UINT unique,K32_LPWSTR out) {
    char p[260],pre[64],name[260];if(!out){k32_last_error=87;return 0;}k32_wide_to_ansi(p,sizeof(p),path);k32_wide_to_ansi(pre,sizeof(pre),prefix);DWORD id=GetTempFileNameA(path?p:0,prefix?pre:0,unique,name);if(!id){out[0]=0;return 0;}k32_ansi_to_wide(out,260,name);return id;
}
__attribute__((dllexport)) DWORD GetShortPathNameA(LPCSTR path,LPSTR out,DWORD cap){return GetFullPathNameA(path,cap,out,0);}
__attribute__((dllexport)) DWORD GetLongPathNameA(LPCSTR path,LPSTR out,DWORD cap){return GetFullPathNameA(path,cap,out,0);}
__attribute__((dllexport)) BOOL GetDiskFreeSpaceA(LPCSTR root,DWORD *sectorsPerCluster,DWORD *bytesPerSector,DWORD *freeClusters,DWORD *totalClusters) { (void)sectorsPerCluster;(void)bytesPerSector;(void)freeClusters;(void)totalClusters;if(!root){k32_last_error=87;return 0;}k32_last_error=50;return 0; }
__attribute__((dllexport)) BOOL GetDiskFreeSpaceExA(LPCSTR path,unsigned long long *available,unsigned long long *total,unsigned long long *freeBytes) { (void)available;(void)total;(void)freeBytes;if(!path){k32_last_error=87;return 0;}k32_last_error=50;return 0; }
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
__attribute__((dllexport)) BOOL GetVolumeInformationA(LPCSTR root,LPSTR label,DWORD labelCap,DWORD *serial,DWORD *maxComponent,DWORD *flags,LPSTR fsName,DWORD fsCap) { (void)label;(void)labelCap;(void)serial;(void)maxComponent;(void)flags;(void)fsName;(void)fsCap;if(!root){k32_last_error=87;return 0;}k32_last_error=50;return 0; }
typedef struct { unsigned long long creation_time,access_time,write_time,size; DWORD inode,mode; WORD links,reserved; } K32_FILE_METADATA;
static BOOL k32_query_file_metadata(HANDLE file,K32_FILE_METADATA *meta) {
    if(!file||file==INVALID_HANDLE_VALUE||!meta)return 0;
    return sy3(SYS_QueryFileMetadata,(u64)file,(u64)meta,sizeof(*meta))!=0;
}
static void k32_store_filetime(K32_FILETIME *out,unsigned long long ticks) {
    if(!out)return;out->dwLowDateTime=(DWORD)ticks;out->dwHighDateTime=(DWORD)(ticks>>32);
}
__attribute__((dllexport)) BOOL GetFileTime(HANDLE file,K32_FILETIME *created,K32_FILETIME *accessed,K32_FILETIME *written) {
    K32_FILE_METADATA meta;if(!k32_query_file_metadata(file,&meta)){k32_last_error=6;return 0;}
    k32_store_filetime(created,meta.creation_time);k32_store_filetime(accessed,meta.access_time);k32_store_filetime(written,meta.write_time);
    k32_last_error=0;return 1;
}
__attribute__((dllexport)) BOOL SetFileTime(HANDLE file,const K32_FILETIME *created,const K32_FILETIME *accessed,const K32_FILETIME *written) { (void)created;(void)accessed;(void)written;if(!file||file==INVALID_HANDLE_VALUE){k32_last_error=6;return 0;}k32_last_error=50;return 0; }
__attribute__((dllexport)) BOOL SystemTimeToFileTime(const K32_SYSTEMTIME *st,K32_FILETIME *ft);
__attribute__((dllexport)) void GetSystemTimeAsFileTime(K32_FILETIME *ft) { if(!ft)return;K32_SYSTEMTIME st;GetSystemTime(&st);SystemTimeToFileTime(&st,ft); }
static int k32_leap(int y){return (y%4==0&&y%100!=0)||y%400==0;}
__attribute__((dllexport)) BOOL SystemTimeToFileTime(const K32_SYSTEMTIME *st,K32_FILETIME *ft){static const int mdays[12]={31,28,31,30,31,30,31,31,30,31,30,31};if(!st||!ft||st->wYear<1601||st->wMonth<1||st->wMonth>12||st->wHour>23||st->wMinute>59||st->wSecond>59||st->wMilliseconds>999)return 0;int max=mdays[st->wMonth-1]+(st->wMonth==2&&k32_leap(st->wYear));if(st->wDay<1||st->wDay>max)return 0;unsigned long long days=0;for(int y=1601;y<st->wYear;y++)days+=k32_leap(y)?366:365;for(int m=1;m<st->wMonth;m++)days+=mdays[m-1]+(m==2&&k32_leap(st->wYear));days+=st->wDay-1;unsigned long long ticks=days*864000000000ULL+((unsigned long long)st->wHour*3600+st->wMinute*60+st->wSecond)*10000000ULL+(unsigned long long)st->wMilliseconds*10000ULL;ft->dwLowDateTime=(DWORD)ticks;ft->dwHighDateTime=(DWORD)(ticks>>32);return 1;}
__attribute__((dllexport)) BOOL FileTimeToSystemTime(const K32_FILETIME *ft,K32_SYSTEMTIME *st){if(!ft||!st)return 0;unsigned long long ticks=((unsigned long long)ft->dwHighDateTime<<32)|ft->dwLowDateTime;unsigned long long days=ticks/864000000000ULL,rem=ticks%864000000000ULL;int year=1601;while(days>=(unsigned long long)(k32_leap(year)?366:365)){days-=k32_leap(year)?366:365;if(++year>9999)return 0;}static const int mdays[12]={31,28,31,30,31,30,31,31,30,31,30,31};int month=1;while(month<=12){int d=mdays[month-1]+(month==2&&k32_leap(year));if(days<(unsigned long long)d)break;days-=d;month++;}st->wYear=year;st->wMonth=month;st->wDay=(unsigned short)days+1;st->wDayOfWeek=(unsigned short)((((ticks/864000000000ULL)+1)%7));st->wHour=rem/36000000000ULL;rem%=36000000000ULL;st->wMinute=rem/600000000ULL;rem%=600000000ULL;st->wSecond=rem/10000000ULL;st->wMilliseconds=(rem%10000000ULL)/10000ULL;return 1;}
__attribute__((dllexport)) LONG CompareFileTime(const K32_FILETIME *a,const K32_FILETIME *b){if(!a||!b)return 0;if(a->dwHighDateTime<b->dwHighDateTime)return -1;if(a->dwHighDateTime>b->dwHighDateTime)return 1;if(a->dwLowDateTime<b->dwLowDateTime)return -1;if(a->dwLowDateTime>b->dwLowDateTime)return 1;return 0;}
typedef struct { K32_FILETIME ftCreationTime,ftLastAccessTime,ftLastWriteTime; DWORD dwFileAttributes,nFileSizeHigh,nFileSizeLow; } K32_FILE_ATTRIBUTE_DATA;
__attribute__((dllexport)) BOOL GetFileAttributesExA(LPCSTR path,int level,void *out) {
    if(!path||!*path||!out||level!=0){k32_last_error=87;return 0;}
    K32_FILE_ATTRIBUTE_DATA *d=(K32_FILE_ATTRIBUTE_DATA*)out;
    DWORD attr=GetFileAttributesA(path);if(attr==0xFFFFFFFFu)return 0;
    d->dwFileAttributes=attr;d->nFileSizeHigh=d->nFileSizeLow=0;
    d->ftCreationTime.dwLowDateTime=d->ftCreationTime.dwHighDateTime=0;
    d->ftLastAccessTime.dwLowDateTime=d->ftLastAccessTime.dwHighDateTime=0;
    d->ftLastWriteTime.dwLowDateTime=d->ftLastWriteTime.dwHighDateTime=0;
    if(!(attr&0x10u)) {
        HANDLE h=CreateFileA(path,0,0,0,3,0,0);if(h==INVALID_HANDLE_VALUE){k32_last_error=2;return 0;}
        K32_FILE_METADATA meta;BOOL ok=k32_query_file_metadata(h,&meta);CloseHandle(h);if(!ok){k32_last_error=6;return 0;}
        k32_store_filetime(&d->ftCreationTime,meta.creation_time);k32_store_filetime(&d->ftLastAccessTime,meta.access_time);k32_store_filetime(&d->ftLastWriteTime,meta.write_time);
        d->nFileSizeLow=(DWORD)meta.size;d->nFileSizeHigh=(DWORD)(meta.size>>32);
    }
    k32_last_error=0;return 1;
}
typedef struct { DWORD dwFileAttributes; K32_FILETIME ftCreationTime,ftLastAccessTime,ftLastWriteTime; DWORD dwVolumeSerialNumber,nFileSizeHigh,nFileSizeLow,nNumberOfLinks,nFileIndexHigh,nFileIndexLow; } K32_BY_HANDLE_FILE_INFORMATION;
__attribute__((dllexport)) BOOL GetFileInformationByHandle(HANDLE file,void *info) {
    if(!file||file==INVALID_HANDLE_VALUE||!info){k32_last_error=6;return 0;}
    unsigned int slot,side;if(k32_pipe_decode(file,&slot,&side)){k32_last_error=50;return 0;}
    K32_FILE_METADATA meta;if(!k32_query_file_metadata(file,&meta)){k32_last_error=6;return 0;}
    K32_BY_HANDLE_FILE_INFORMATION *p=(K32_BY_HANDLE_FILE_INFORMATION*)info;
    for(unsigned int i=0;i<sizeof(*p)/sizeof(DWORD);i++)((DWORD*)p)[i]=0;
    p->dwFileAttributes=((meta.mode&0xF000u)==0x4000u)?0x10u:0x80u;
    k32_store_filetime(&p->ftCreationTime,meta.creation_time);k32_store_filetime(&p->ftLastAccessTime,meta.access_time);k32_store_filetime(&p->ftLastWriteTime,meta.write_time);
    p->nFileSizeHigh=(DWORD)(meta.size>>32);p->nFileSizeLow=(DWORD)meta.size;p->nNumberOfLinks=meta.links;
    p->nFileIndexLow=meta.inode;k32_last_error=0;return 1;
}
__attribute__((dllexport)) BOOL SetEndOfFile(HANDLE file) { if(!file||file==INVALID_HANDLE_VALUE){k32_last_error=6;return 0;}k32_last_error=50;return 0; }
__attribute__((dllexport)) BOOL GetOverlappedResult(HANDLE file, void *overlapped, DWORD *transferred, BOOL wait) { (void)file; (void)overlapped; (void)wait; if (transferred) *transferred=0; k32_last_error=997; return 0; }
__attribute__((dllexport)) BOOL CancelIo(HANDLE file) { if(!file||file==INVALID_HANDLE_VALUE){k32_last_error=6;return 0;}k32_last_error=50;return 0; }
__attribute__((dllexport)) BOOL CreatePipe(HANDLE *readPipe,HANDLE *writePipe,void *security,DWORD size) {
    (void)security;
    if(!readPipe||!writePipe){k32_last_error=87;return 0;}
    for(unsigned int i=0;i<16;i++) if(!k32_pipes[i].read_open&&!k32_pipes[i].write_open) {
        K32_PIPE *p=&k32_pipes[i];p->read_pos=p->write_pos=p->used=0;p->read_open=1;p->write_open=1;
        *readPipe=k32_pipe_handle(i,0);*writePipe=k32_pipe_handle(i,1);
        (void)size;k32_last_error=0;return 1;
    }
    *readPipe=0;*writePipe=0;k32_last_error=8;return 0;
}
__attribute__((dllexport)) BOOL PeekNamedPipe(HANDLE pipe,void *buffer,DWORD cap,DWORD *read,DWORD *available,DWORD *left) {
    unsigned int slot,side;
    if(!k32_pipe_decode(pipe,&slot,&side)||side!=0||!k32_pipes[slot].read_open){if(read)*read=0;if(available)*available=0;if(left)*left=0;k32_last_error=6;return 0;}
    K32_PIPE *p=&k32_pipes[slot];DWORD n=cap<p->used?cap:p->used;
    if(buffer&&n) { unsigned char *dst=(unsigned char*)buffer;for(DWORD i=0;i<n;i++)dst[i]=p->data[(p->read_pos+i)%sizeof(p->data)]; }
    if(read)*read=n;if(available)*available=p->used;if(left)*left=0;k32_last_error=0;return 1;
}
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
__attribute__((dllexport)) BOOL GetProcessTimes(HANDLE process,K32_FILETIME *created,K32_FILETIME *exitTime,K32_FILETIME *kernel,K32_FILETIME *user) { (void)created;(void)exitTime;(void)kernel;(void)user;if(process!=GetCurrentProcess()&&process!=(HANDLE)1){k32_last_error=87;return 0;}k32_last_error=50;return 0; }
__attribute__((dllexport)) BOOL GetThreadTimes(HANDLE thread,K32_FILETIME *created,K32_FILETIME *exitTime,K32_FILETIME *kernel,K32_FILETIME *user) { (void)created;(void)exitTime;(void)kernel;(void)user;if(thread!=GetCurrentThread()&&thread!=(HANDLE)1){k32_last_error=87;return 0;}k32_last_error=50;return 0; }
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


/* Additional Win32 compatibility APIs. Unsupported kernel services return explicit failure. */
__attribute__((dllexport)) DWORD GetOEMCP(void) { return 437u; }
__attribute__((dllexport)) DWORD GetConsoleCP(void){return k32_console_cp_in;}
__attribute__((dllexport)) DWORD GetConsoleOutputCP(void){return k32_console_cp_out;}
__attribute__((dllexport)) BOOL SetConsoleCP(DWORD cp){if(!cp){k32_last_error=87;return 0;}k32_console_cp_in=cp;return 1;}
__attribute__((dllexport)) BOOL SetConsoleOutputCP(DWORD cp){if(!cp){k32_last_error=87;return 0;}k32_console_cp_out=cp;return 1;}
__attribute__((dllexport)) BOOL GetFileSizeEx(HANDLE file,K32_LARGE_INTEGER *size) {
    if(!file||file==INVALID_HANDLE_VALUE||!size){k32_last_error=6;return 0;}
    DWORD high=0,low=GetFileSize(file,&high);if(low==0xFFFFFFFFu&&GetLastError()!=0)return 0;
    size->QuadPart=((unsigned long long)high<<32)|low;k32_last_error=0;return 1;
}
__attribute__((dllexport)) BOOL GetFileInformationByHandleEx(HANDLE file,int infoClass,void *info,DWORD size) {
    if(!file||file==INVALID_HANDLE_VALUE||!info){k32_last_error=6;return 0;}
    if(infoClass==0) {
        typedef struct { long long CreationTime,LastAccessTime,LastWriteTime,ChangeTime; DWORD FileAttributes; } BASIC_INFO;
        if(size<sizeof(BASIC_INFO)){k32_last_error=122;return 0;}
        BASIC_INFO *p=(BASIC_INFO*)info;p->CreationTime=p->LastAccessTime=p->LastWriteTime=p->ChangeTime=0;p->FileAttributes=0x80u;k32_last_error=0;return 1;
    }
    if(infoClass==1) {
        typedef struct { long long AllocationSize,EndOfFile; DWORD NumberOfLinks; unsigned char DeletePending,Directory; } STANDARD_INFO;
        if(size<sizeof(STANDARD_INFO)){k32_last_error=122;return 0;}
        DWORD high=0,low=GetFileSize(file,&high);if(low==0xFFFFFFFFu&&GetLastError()!=0)return 0;
        STANDARD_INFO *p=(STANDARD_INFO*)info;p->EndOfFile=(long long)(((unsigned long long)high<<32)|low);p->AllocationSize=(p->EndOfFile+4095LL)&~4095LL;p->NumberOfLinks=1;p->DeletePending=0;p->Directory=0;k32_last_error=0;return 1;
    }
    if(infoClass==2) {
        K32_OPEN_PATH *path=k32_find_path(file);if(!path){k32_last_error=50;return 0;}
        DWORD n=0;while(path->path[n])n++;if(size<sizeof(DWORD)+(n+1u)*sizeof(K32_WCHAR)){k32_last_error=122;return 0;}
        struct { DWORD FileNameLength; K32_WCHAR FileName[1]; } *p=info;p->FileNameLength=n*sizeof(K32_WCHAR);
        for(DWORD i=0;i<=n;i++)p->FileName[i]=(unsigned char)path->path[i];k32_last_error=0;return 1;
    }
    k32_last_error=50;return 0;
}
__attribute__((dllexport)) DWORD GetFinalPathNameByHandleA(HANDLE file,LPSTR out,DWORD cap,DWORD flags) {
    if(!file||file==INVALID_HANDLE_VALUE||!out||!cap){k32_last_error=87;return 0;}
    unsigned int volume=flags&0xFu;if(volume!=0&&volume!=4){out[0]=0;k32_last_error=50;return 0;}
    K32_OPEN_PATH *p=k32_find_path(file);if(!p){out[0]=0;k32_last_error=6;return 0;}
    char full[768];DWORD n=GetFullPathNameA(p->path,sizeof(full),full,0);if(!n||n>=sizeof(full)){out[0]=0;return 0;}
    DWORD prefix=volume==4?0:0; (void)prefix;
    if(cap<=n){DWORD i=0;while(i+1<cap){out[i]=full[i];i++;}out[i]=0;k32_last_error=122;return n+1;}
    for(DWORD i=0;i<=n;i++)out[i]=full[i];k32_last_error=0;return n;
}
__attribute__((dllexport)) DWORD GetFinalPathNameByHandleW(HANDLE file,K32_LPWSTR out,DWORD cap,DWORD flags) {
    char path[768];DWORD n=GetFinalPathNameByHandleA(file,path,sizeof(path),flags);if(!n){if(out&&cap)out[0]=0;return 0;}
    DWORD i=0;while(path[i])i++;if(!out||!cap)return i+1;
    if(cap<=i){k32_ansi_to_wide(out,cap,path);k32_last_error=122;return i+1;}
    k32_ansi_to_wide(out,cap,path);k32_last_error=0;return i;
}
__attribute__((dllexport)) BOOL GetVolumePathNameA(LPCSTR path,LPSTR out,DWORD cap){if(!path||!out||cap<4||!((path[0]=='C'||path[0]=='c')&&path[1]==':'&&(path[2]=='\\'||path[2]=='/'||path[2]==0))){k32_last_error=3;return 0;}out[0]='C';out[1]=':';out[2]='\\';out[3]=0;return 1;}
__attribute__((dllexport)) BOOL GetVolumePathNameW(K32_LPCWSTR path,K32_LPWSTR out,DWORD cap){if(!path||!out||cap<4||!((path[0]=='C'||path[0]=='c')&&path[1]==':'&&(path[2]=='\\'||path[2]=='/'||path[2]==0))){k32_last_error=3;return 0;}out[0]='C';out[1]=':';out[2]='\\';out[3]=0;return 1;}
__attribute__((dllexport)) BOOL GetVolumePathNamesForVolumeNameA(LPCSTR volume,LPSTR paths,DWORD cap,DWORD *required) { const char *known="\\\\?\\Volume{00000000-0000-0000-0000-000000000001}\\";if(!volume||!k32_eqi(volume,known)){if(required)*required=0;k32_last_error=2;return 0;}if(required)*required=5;if(!paths||cap<5){if(paths&&cap)paths[0]=0;k32_last_error=234;return 0;}paths[0]='C';paths[1]=':';paths[2]='\\';paths[3]=0;paths[4]=0;k32_last_error=0;return 1; }
__attribute__((dllexport)) BOOL GetVolumePathNamesForVolumeNameW(K32_LPCWSTR volume,K32_LPWSTR paths,DWORD cap,DWORD *required) { char v[128],p[8];if(!volume){k32_last_error=87;return 0;}k32_wide_to_ansi(v,sizeof(v),volume);if(!GetVolumePathNamesForVolumeNameA(v,p,sizeof(p),required))return 0;if(!paths||cap<5){if(required)*required=5;if(paths&&cap)paths[0]=0;k32_last_error=234;return 0;}for(int i=0;i<5;i++)paths[i]=(unsigned char)p[i];return 1; }
__attribute__((dllexport)) DWORD GetDriveTypeW(K32_LPCWSTR root) {
    if (!root || ((root[0]=='C' || root[0]=='c') && root[1]==':' && (root[2]=='\\' || root[2]=='/' || root[2]==0))) return 3;
    return 1;
}
__attribute__((dllexport)) DWORD GetLogicalDriveStringsW(DWORD cap, K32_LPWSTR out) {
    if (!out || !cap) return 4;
    if (cap<5) { out[0]=0; return 4; }
    out[0]='C'; out[1]=':'; out[2]='\\'; out[3]=0; out[4]=0; return 4;
}
__attribute__((dllexport)) BOOL GetVolumeInformationW(K32_LPCWSTR root,K32_LPWSTR label,DWORD labelCap,DWORD *serial,DWORD *maxComponent,DWORD *flags,K32_LPWSTR fsName,DWORD fsCap) { (void)label;(void)labelCap;(void)fsName;(void)fsCap;char p[260];if(!root){k32_last_error=87;return 0;}k32_wide_to_ansi(p,sizeof(p),root);return GetVolumeInformationA(p,0,0,serial,maxComponent,flags,0,0); }
__attribute__((dllexport)) BOOL GetDiskFreeSpaceW(K32_LPCWSTR root,DWORD *spc,DWORD *bps,DWORD *freeClusters,DWORD *totalClusters) { char p[260];if(!root){k32_last_error=87;return 0;}k32_wide_to_ansi(p,sizeof(p),root);return GetDiskFreeSpaceA(p,spc,bps,freeClusters,totalClusters); }
__attribute__((dllexport)) BOOL GetDiskFreeSpaceExW(K32_LPCWSTR path,unsigned long long *available,unsigned long long *total,unsigned long long *freeBytes) { char p[260];if(!path){k32_last_error=87;return 0;}k32_wide_to_ansi(p,sizeof(p),path);return GetDiskFreeSpaceExA(p,available,total,freeBytes); }
__attribute__((dllexport)) BOOL GetFileAttributesExW(K32_LPCWSTR path,int level,void *data) { char p[260];if(!path){k32_last_error=87;return 0;}k32_wide_to_ansi(p,sizeof(p),path);return GetFileAttributesExA(p,level,data); }
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
__attribute__((dllexport)) BOOL GetSystemTimeAdjustment(DWORD *adjustment,DWORD *increment,BOOL *disabled) { if(!adjustment||!increment||!disabled){k32_last_error=87;return 0;}*adjustment=0;*increment=10000;*disabled=1;return 1; }
__attribute__((dllexport)) void GetSystemTimePreciseAsFileTime(K32_FILETIME *ft) { GetSystemTimeAsFileTime(ft); }
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
__attribute__((dllexport)) HANDLE GetConsoleWindow(void){return 0;}
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
typedef struct { int used; char exe[64],source[64],target[128]; } K32_ALIAS;
static K32_ALIAS k32_aliases[32];
static const char *k32_alias_exe(LPCSTR exe){return exe&&*exe?exe:"sentinel.exe";}
static int k32_alias_find(LPCSTR source,LPCSTR exe){const char *e=k32_alias_exe(exe);for(int i=0;i<32;i++)if(k32_aliases[i].used&&k32_eqi(k32_aliases[i].exe,e)&&k32_eqi(k32_aliases[i].source,source))return i;return -1;}
__attribute__((dllexport)) void AddConsoleAliasA(LPCSTR source,LPCSTR target,LPCSTR exe){
 if(!source||!*source){k32_last_error=87;return;}int i=k32_alias_find(source,exe);
 if(!target){if(i>=0)k32_aliases[i].used=0;return;}
 if(i<0){for(i=0;i<32;i++)if(!k32_aliases[i].used)break;if(i==32){k32_last_error=8;return;}}
 K32_ALIAS *a=&k32_aliases[i];a->used=1;const char *e=k32_alias_exe(exe);int j=0;while(e[j]&&j<63){a->exe[j]=e[j];j++;}a->exe[j]=0;
 j=0;while(source[j]&&j<63){a->source[j]=source[j];j++;}a->source[j]=0;
 j=0;while(target[j]&&j<127){a->target[j]=target[j];j++;}a->target[j]=0;k32_last_error=0;
}
__attribute__((dllexport)) void AddConsoleAliasW(K32_LPCWSTR source,K32_LPCWSTR target,K32_LPCWSTR exe){char a[64],b[128],e[64];if(!source){k32_last_error=87;return;}k32_wide_to_ansi(a,sizeof(a),source);if(target)k32_wide_to_ansi(b,sizeof(b),target);if(exe)k32_wide_to_ansi(e,sizeof(e),exe);else e[0]=0;AddConsoleAliasA(a,target?b:0,e);}
static DWORD k32_alias_list_a(LPSTR out,DWORD cap,LPCSTR exe){
 DWORD used=0;for(int i=0;i<32;i++)if(k32_aliases[i].used&&k32_eqi(k32_aliases[i].exe,k32_alias_exe(exe))){
  DWORD n=0;while(k32_aliases[i].source[n])n++;DWORD t=0;while(k32_aliases[i].target[t])t++;DWORD need=n+1+t+1;
  if(out&&used+need+1<=cap){DWORD p=used;for(DWORD j=0;j<n;j++)out[p++]=k32_aliases[i].source[j];out[p++]='=';for(DWORD j=0;j<t;j++)out[p++]=k32_aliases[i].target[j];out[p]=0;}
  used+=need;
 }
 if(out&&cap){if(used<cap)out[used]=0;else out[cap-1]=0;if(used+1<cap)out[used+1]=0;}return used;
}
__attribute__((dllexport)) DWORD GetConsoleAliasesA(LPSTR buffer,DWORD cap,LPCSTR exe){return k32_alias_list_a(buffer,cap,exe);}
__attribute__((dllexport)) DWORD GetConsoleAliasesW(K32_LPWSTR buffer,DWORD cap,K32_LPCWSTR exe){char e[64],a[4096];if(exe)k32_wide_to_ansi(e,sizeof(e),exe);else e[0]=0;DWORD n=k32_alias_list_a(a,sizeof(a),e);if(!buffer||!cap)return n;DWORD i=0;while(i<n&&i+1<cap){buffer[i]=(unsigned char)a[i];i++;}if(i<cap)buffer[i]=0;if(i+1<cap)buffer[i+1]=0;return n;}
__attribute__((dllexport)) DWORD GetConsoleAliasesLengthA(LPCSTR exe){return k32_alias_list_a(0,0,exe);}
__attribute__((dllexport)) DWORD GetConsoleAliasesLengthW(K32_LPCWSTR exe){char e[64];if(exe)k32_wide_to_ansi(e,sizeof(e),exe);else e[0]=0;return k32_alias_list_a(0,0,e);}
__attribute__((dllexport)) DWORD GetConsoleAliasA(LPCSTR source,LPSTR target,DWORD cap,LPCSTR exe){if(!source||!target||!cap){k32_last_error=87;return 0;}int i=k32_alias_find(source,exe);if(i<0){target[0]=0;k32_last_error=2;return 0;}DWORD n=0;while(k32_aliases[i].target[n])n++;if(n+1>cap){target[0]=0;k32_last_error=122;return 0;}for(DWORD j=0;j<=n;j++)target[j]=k32_aliases[i].target[j];k32_last_error=0;return n;}
__attribute__((dllexport)) DWORD GetConsoleAliasW(K32_LPCWSTR source,K32_LPWSTR target,DWORD cap,K32_LPCWSTR exe){char a[64],e[64],b[128];if(!source||!target||!cap){k32_last_error=87;return 0;}k32_wide_to_ansi(a,sizeof(a),source);if(exe)k32_wide_to_ansi(e,sizeof(e),exe);else e[0]=0;DWORD n=GetConsoleAliasA(a,b,sizeof(b),e);if(!n){target[0]=0;return 0;}if(n+1>cap){target[0]=0;k32_last_error=122;return 0;}k32_ansi_to_wide(target,cap,b);return n;}
__attribute__((dllexport)) DWORD GetConsoleAliasExesA(LPSTR out,DWORD cap){DWORD used=0;for(int i=0;i<32;i++)if(k32_aliases[i].used){int seen=0;for(int j=0;j<i;j++)if(k32_aliases[j].used&&k32_eqi(k32_aliases[j].exe,k32_aliases[i].exe))seen=1;if(seen)continue;DWORD n=0;while(k32_aliases[i].exe[n])n++;if(out&&used+n+2<=cap){for(DWORD j=0;j<n;j++)out[used+j]=k32_aliases[i].exe[j];out[used+n]=0;}used+=n+1;}if(out&&cap){if(used<cap)out[used]=0;else out[cap-1]=0;if(used+1<cap)out[used+1]=0;}return used;}
__attribute__((dllexport)) DWORD GetConsoleAliasExesW(K32_LPWSTR out,DWORD cap){char a[2048];DWORD n=GetConsoleAliasExesA(a,sizeof(a));if(!out||!cap)return n;DWORD i=0;while(i<n&&i+1<cap){out[i]=(unsigned char)a[i];i++;}if(i<cap)out[i]=0;if(i+1<cap)out[i+1]=0;return n;}
__attribute__((dllexport)) DWORD GetConsoleAliasExesLengthA(void){return GetConsoleAliasExesA(0,0);}
__attribute__((dllexport)) DWORD GetConsoleAliasExesLengthW(void){return GetConsoleAliasExesLengthA();}
__attribute__((dllexport)) void GetConsoleCommandHistoryA(LPSTR buffer,DWORD cap,LPCSTR exe){(void)exe;if(buffer&&cap){buffer[0]=0;if(cap>1)buffer[1]=0;}}
__attribute__((dllexport)) void GetConsoleCommandHistoryW(K32_LPWSTR buffer,DWORD cap,K32_LPCWSTR exe){(void)exe;if(buffer&&cap){buffer[0]=0;if(cap>1)buffer[1]=0;}}
__attribute__((dllexport)) DWORD GetConsoleInputExeNameA(DWORD cap,LPSTR out){const char *n="sentinel.exe";DWORD len=11;if(!out||cap<=len)return len+1;for(DWORD i=0;i<=len;i++)out[i]=n[i];return len;}
__attribute__((dllexport)) DWORD GetConsoleInputExeNameW(DWORD cap,K32_LPWSTR out){const char *n="sentinel.exe";DWORD len=11;if(!out||cap<=len)return len+1;k32_ansi_to_wide(out,cap,n);return len;}
__attribute__((dllexport)) DWORD GetConsoleOutputExeNameA(DWORD cap,LPSTR out){return GetConsoleInputExeNameA(cap,out);}
__attribute__((dllexport)) DWORD GetConsoleOutputExeNameW(DWORD cap,K32_LPWSTR out){return GetConsoleInputExeNameW(cap,out);}
__attribute__((dllexport)) BOOL CreateHardLinkA(LPCSTR newPath,LPCSTR existing,void *security) { (void)security;if(!newPath||!*newPath||!existing||!*existing){k32_last_error=87;return 0;}k32_last_error=50;return 0; }
__attribute__((dllexport)) BOOL CreateHardLinkW(K32_LPCWSTR newPath,K32_LPCWSTR existing,void *security) { (void)security;if(!newPath||!existing){k32_last_error=87;return 0;}k32_last_error=50;return 0; }
__attribute__((dllexport)) BOOL CreateSymbolicLinkA(LPCSTR linkPath,LPCSTR target,DWORD flags) { (void)flags;if(!linkPath||!*linkPath||!target||!*target){k32_last_error=87;return 0;}k32_last_error=50;return 0; }
__attribute__((dllexport)) BOOL CreateSymbolicLinkW(K32_LPCWSTR linkPath,K32_LPCWSTR target,DWORD flags) { (void)flags;if(!linkPath||!target){k32_last_error=87;return 0;}k32_last_error=50;return 0; }
__attribute__((dllexport)) BOOL SetFileValidData(HANDLE file,long long length) { (void)length;if(!file||file==INVALID_HANDLE_VALUE){k32_last_error=6;return 0;}k32_last_error=50;return 0; }
__attribute__((dllexport)) BOOL LockFile(HANDLE file,DWORD offsetLow,DWORD offsetHigh,DWORD bytesLow,DWORD bytesHigh) { (void)offsetLow;(void)offsetHigh;(void)bytesLow;(void)bytesHigh;if(!file||file==INVALID_HANDLE_VALUE){k32_last_error=6;return 0;}k32_last_error=50;return 0; }
__attribute__((dllexport)) BOOL UnlockFile(HANDLE file,DWORD offsetLow,DWORD offsetHigh,DWORD bytesLow,DWORD bytesHigh) { (void)offsetLow;(void)offsetHigh;(void)bytesLow;(void)bytesHigh;if(!file||file==INVALID_HANDLE_VALUE){k32_last_error=6;return 0;}k32_last_error=50;return 0; }
__attribute__((dllexport)) BOOL LockFileEx(HANDLE file,DWORD flags,DWORD reserved,DWORD bytesLow,DWORD bytesHigh,void *overlapped) { (void)flags;(void)reserved;(void)bytesLow;(void)bytesHigh;if(!file||file==INVALID_HANDLE_VALUE||!overlapped){k32_last_error=6;return 0;}k32_last_error=50;return 0; }
__attribute__((dllexport)) BOOL UnlockFileEx(HANDLE file,DWORD reserved,DWORD bytesLow,DWORD bytesHigh,void *overlapped) { (void)reserved;(void)bytesLow;(void)bytesHigh;if(!file||file==INVALID_HANDLE_VALUE||!overlapped){k32_last_error=6;return 0;}k32_last_error=50;return 0; }
__attribute__((dllexport)) DWORD GetCompressedFileSizeA(LPCSTR path,DWORD *high) {
    if(!path||!*path){k32_last_error=87;return 0xFFFFFFFFu;}
    HANDLE h=CreateFileA(path,0,0,0,3,0,0);if(h==INVALID_HANDLE_VALUE)return 0xFFFFFFFFu;
    DWORD hi=0,lo=GetFileSize(h,&hi);DWORD err=GetLastError();CloseHandle(h);
    if(lo==0xFFFFFFFFu&&err!=0){k32_last_error=err;return 0xFFFFFFFFu;}
    if(high)*high=hi;k32_last_error=0;return lo;
}
__attribute__((dllexport)) DWORD GetCompressedFileSizeW(K32_LPCWSTR path,DWORD *high) { char p[260];if(!path){k32_last_error=87;return 0xFFFFFFFFu;}k32_wide_to_ansi(p,sizeof(p),path);return GetCompressedFileSizeA(p,high); }
__attribute__((dllexport)) DWORD SearchPathA(LPCSTR path,LPCSTR file,LPCSTR ext,DWORD cap,LPSTR out,LPSTR *part){(void)path;if(!file)return 0;char joined[512];DWORD i=0;while(file[i]&&i+1<sizeof(joined)){joined[i]=file[i];i++;}joined[i]=0;if(i==0||!out||!cap)return 0;if(ext&&i+1<sizeof(joined)){int has=0;for(DWORD j=0;j<i;j++)if(joined[j]=='.')has=1;if(!has){DWORD j=0;while(ext[j]&&i+1<sizeof(joined))joined[i++]=ext[j++];joined[i]=0;}}DWORD n=GetFullPathNameA(joined,cap,out,part);return n;}
__attribute__((dllexport)) DWORD SearchPathW(K32_LPCWSTR path,K32_LPCWSTR file,K32_LPCWSTR ext,DWORD cap,K32_LPWSTR out,K32_LPWSTR *part){(void)path;(void)part;char f[512],e[64],b[512];k32_wide_to_ansi(f,sizeof(f),file);k32_wide_to_ansi(e,sizeof(e),ext);DWORD n=SearchPathA(0,f,ext?e:0,sizeof(b),b,0);if(out&&cap)k32_ansi_to_wide(out,cap,b);return n;}
__attribute__((dllexport)) DWORD GetModuleFileNameExA(HANDLE process,HANDLE module,LPSTR out,DWORD cap){if(process!=(HANDLE)(i64)-1&&process!=(HANDLE)1)return 0;return GetModuleFileNameA(module,out,cap);}
__attribute__((dllexport)) DWORD GetModuleFileNameExW(HANDLE process,HANDLE module,K32_LPWSTR out,DWORD cap){if(process!=(HANDLE)(i64)-1&&process!=(HANDLE)1)return 0;return GetModuleFileNameW(module,out,cap);}
__attribute__((dllexport)) BOOL HeapValidate(HANDLE heap,DWORD flags,LPCVOID mem) { (void)flags;if(heap!=GetProcessHeap())return 0;if(!mem)return 1;return k32_mem_size((void*)mem)!=0; }
__attribute__((dllexport)) K32_SIZE_T HeapCompact(HANDLE heap,DWORD flags) { (void)flags;if(heap!=GetProcessHeap())return 0;k32_last_error=0;return 0; }
__attribute__((dllexport)) DWORD GetProcessHeaps(DWORD count,HANDLE *heaps) { if(!count||!heaps)return 0;heaps[0]=GetProcessHeap();return 1; }
static LPVOID k32_realloc_tracked(LPVOID mem,K32_SIZE_T size,DWORD flags){
    if(!mem)return size?LocalAlloc(flags,size):0;
    if(!size){k32_last_error=87;return 0;}
    K32_MEMENTRY *e=k32_mem_entry(mem);if(!e){k32_last_error=87;return 0;}
    K32_SIZE_T old=e->size;LPVOID p=VirtualAlloc(0,size,0x3000,4);if(!p){k32_last_error=8;return 0;}
    K32_SIZE_T n=old<size?old:size;unsigned char *d=p,*s=mem;for(K32_SIZE_T i=0;i<n;i++)d[i]=s[i];
    if((flags&0x40u)&&size>old)for(K32_SIZE_T i=old;i<size;i++)d[i]=0;
    if(!VirtualFree(mem,0,0x8000u)){VirtualFree(p,0,0x8000u);k32_last_error=8;return 0;}
    e->freed=1;k32_mem_register(p,size);k32_last_error=0;return p;
}
__attribute__((dllexport)) LPVOID LocalFree(LPVOID mem){if(!mem)return 0;K32_MEMENTRY *e=k32_mem_entry(mem);if(!e){k32_last_error=87;return mem;}if(!VirtualFree(mem,0,0x8000u))return mem;e->freed=1;k32_last_error=0;return 0;}
__attribute__((dllexport)) LPVOID LocalReAlloc(LPVOID mem,K32_SIZE_T size,DWORD flags){return k32_realloc_tracked(mem,size,flags);}
__attribute__((dllexport)) K32_SIZE_T LocalSize(HANDLE mem){return k32_mem_size(mem);}
__attribute__((dllexport)) LPVOID GlobalFree(LPVOID mem){if(!mem)return 0;K32_MEMENTRY *e=k32_mem_entry(mem);if(!e){k32_last_error=87;return mem;}if(!VirtualFree(mem,0,0x8000u))return mem;e->freed=1;k32_last_error=0;return 0;}
__attribute__((dllexport)) LPVOID GlobalReAlloc(LPVOID mem,K32_SIZE_T size,DWORD flags){return k32_realloc_tracked(mem,size,flags);}
__attribute__((dllexport)) K32_SIZE_T GlobalSize(HANDLE mem){return k32_mem_size(mem);}
__attribute__((dllexport)) LPVOID GlobalLock(HANDLE mem){return k32_mem_size(mem)?mem:0;}
__attribute__((dllexport)) BOOL GlobalUnlock(HANDLE mem){if(!k32_mem_size(mem)){k32_last_error=87;return 0;}k32_last_error=0;return 1;}
__attribute__((dllexport)) HANDLE GlobalHandle(LPCVOID mem){return k32_mem_size((void*)mem)?(HANDLE)mem:0;}

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

__attribute__((dllexport)) BOOL GetProcessPriorityBoost(HANDLE process,BOOL *disabled){if(!disabled||(process!=GetCurrentProcess()&&process!=(HANDLE)1)){k32_last_error=87;return 0;}*disabled=k32_process_boost_disabled;k32_last_error=0;return 1;}

__attribute__((dllexport)) BOOL SetProcessPriorityBoost(HANDLE process,BOOL disable){if(process!=GetCurrentProcess()&&process!=(HANDLE)1){k32_last_error=87;return 0;}k32_process_boost_disabled=disable!=0;k32_last_error=0;return 1;}

__attribute__((dllexport)) BOOL GetThreadPriorityBoost(HANDLE thread,BOOL *disabled){if(!disabled||(thread!=GetCurrentThread()&&thread!=(HANDLE)1)){k32_last_error=87;return 0;}*disabled=k32_thread_boost_disabled;k32_last_error=0;return 1;}

__attribute__((dllexport)) BOOL SetThreadPriorityBoost(HANDLE thread,BOOL disable){if(thread!=GetCurrentThread()&&thread!=(HANDLE)1){k32_last_error=87;return 0;}k32_thread_boost_disabled=disable!=0;k32_last_error=0;return 1;}

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

typedef struct { DWORD Version,ControlMask,StateMask; } K32_THREAD_POWER_THROTTLING_STATE;
__attribute__((dllexport)) BOOL GetThreadInformation(HANDLE thread,int info_class,void *buffer,DWORD size) {
    if(thread!=GetCurrentThread()&&thread!=(HANDLE)1){k32_last_error=87;return 0;}
    if(!buffer){k32_last_error=87;return 0;}
    if(info_class==0) {
        if(size<sizeof(DWORD)){k32_last_error=122;return 0;}
        *(DWORD*)buffer=k32_thread_memory_priority;k32_last_error=0;return 1;
    }
    if(info_class==1) {
        if(size<sizeof(K32_THREAD_POWER_THROTTLING_STATE)){k32_last_error=122;return 0;}
        K32_THREAD_POWER_THROTTLING_STATE *p=(K32_THREAD_POWER_THROTTLING_STATE*)buffer;
        p->Version=1;p->ControlMask=k32_thread_power_control;p->StateMask=k32_thread_power_state;k32_last_error=0;return 1;
    }
    k32_last_error=50;return 0;
}
__attribute__((dllexport)) BOOL SetThreadInformation(HANDLE thread,int info_class,const void *buffer,DWORD size) {
    if(thread!=GetCurrentThread()&&thread!=(HANDLE)1){k32_last_error=87;return 0;}
    if(!buffer){k32_last_error=87;return 0;}
    if(info_class==0) {
        if(size<sizeof(DWORD)){k32_last_error=122;return 0;}
        DWORD priority=*(const DWORD*)buffer;if(priority<1||priority>5){k32_last_error=87;return 0;}
        k32_thread_memory_priority=priority;k32_last_error=0;return 1;
    }
    if(info_class==1) {
        if(size<sizeof(K32_THREAD_POWER_THROTTLING_STATE)){k32_last_error=122;return 0;}
        const K32_THREAD_POWER_THROTTLING_STATE *p=(const K32_THREAD_POWER_THROTTLING_STATE*)buffer;
        if(p->Version!=1||(p->ControlMask&~1u)||(p->StateMask&~1u)){k32_last_error=87;return 0;}
        k32_thread_power_state=(k32_thread_power_state&~p->ControlMask)|(p->StateMask&p->ControlMask);k32_thread_power_control|=p->ControlMask;k32_last_error=0;return 1;
    }
    k32_last_error=50;return 0;
}

__attribute__((dllexport)) BOOL GetProcessWorkingSetSize(HANDLE process,K32_SIZE_T *min,K32_SIZE_T *max){if(!min||!max||(process!=GetCurrentProcess()&&process!=(HANDLE)1)){k32_last_error=87;return 0;}*min=k32_working_set_min;*max=k32_working_set_max;k32_last_error=0;return 1;}

__attribute__((dllexport)) BOOL SetProcessWorkingSetSize(HANDLE process,K32_SIZE_T min,K32_SIZE_T max){if(process!=GetCurrentProcess()&&process!=(HANDLE)1){k32_last_error=87;return 0;}if(min!=~(K32_SIZE_T)0&&max!=~(K32_SIZE_T)0&&min>max){k32_last_error=87;return 0;}k32_working_set_min=min;k32_working_set_max=max;k32_last_error=0;return 1;}

__attribute__((dllexport)) BOOL EmptyWorkingSet(HANDLE process){return process==GetCurrentProcess()||process==(HANDLE)1;}

__attribute__((dllexport)) BOOL GetProcessMitigationPolicy(HANDLE process,int policy,void *buffer,K32_SIZE_T size){(void)policy;if((process!=GetCurrentProcess()&&process!=(HANDLE)1)||!buffer||!size)return 0;for(K32_SIZE_T i=0;i<size;i++)((unsigned char*)buffer)[i]=0;return 1;}

__attribute__((dllexport)) BOOL SetProcessMitigationPolicy(int policy,const void *buffer,K32_SIZE_T size) { (void)policy;(void)buffer;(void)size;k32_last_error=50;return 0; }

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

__attribute__((dllexport)) HANDLE LoadLibraryW(K32_LPCWSTR name){
    char narrow[260]; DWORD i=0;
    if(!name||!name[0]){k32_last_error=87;return 0;}
    while(name[i]&&i+1<sizeof(narrow)){narrow[i]=name[i]<128?(char)name[i]:'?';i++;}
    if(name[i]){k32_last_error=206;return 0;}
    narrow[i]=0;
    return LoadLibraryA(narrow);
}

__attribute__((dllexport)) void FreeLibraryAndExitThread(HANDLE module,DWORD code){FreeLibrary(module);ExitProcess(code);}

__attribute__((dllexport)) DWORD GetDllDirectoryA(DWORD cap,LPSTR out){DWORD n=0;while(k32_dll_directory[n])n++;if(!out||!cap)return n?n+1:0;if(cap<=n){DWORD i=0;while(i+1<cap){out[i]=k32_dll_directory[i];i++;}out[i]=0;k32_last_error=122;return n+1;}for(DWORD i=0;i<=n;i++)out[i]=k32_dll_directory[i];k32_last_error=0;return n;}
__attribute__((dllexport)) DWORD GetDllDirectoryW(DWORD cap,K32_LPWSTR out){DWORD n=0;while(k32_dll_directory[n])n++;if(!out||!cap)return n?n+1:0;if(cap<=n){k32_ansi_to_wide(out,cap,k32_dll_directory);k32_last_error=122;return n+1;}k32_ansi_to_wide(out,cap,k32_dll_directory);k32_last_error=0;return n;}
__attribute__((dllexport)) BOOL SetDllDirectoryA(LPCSTR path){DWORD i=0;if(path)while(path[i]&&i+1<sizeof(k32_dll_directory)){k32_dll_directory[i]=path[i];i++;}if(path&&path[i]){k32_last_error=206;return 0;}k32_dll_directory[i]=0;k32_last_error=0;return 1;}
__attribute__((dllexport)) BOOL SetDllDirectoryW(K32_LPCWSTR path){char narrow[260];if(path){DWORD i=0;while(path[i]&&i+1<sizeof(narrow)){narrow[i]=path[i]<128?(char)path[i]:'?';i++;}if(path[i]){k32_last_error=206;return 0;}narrow[i]=0;}else narrow[0]=0;return SetDllDirectoryA(path?narrow:0);}

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

__attribute__((dllexport)) BOOL VerifyVersionInfoA(const void *v,DWORD type,unsigned long long mask){
    typedef struct { DWORD size,major,minor,build,platform; } K32_VERSION_PREFIX;
    const K32_VERSION_PREFIX *want=(const K32_VERSION_PREFIX*)v;
    if(!want||want->size<sizeof(K32_VERSION_PREFIX)){k32_last_error=87;return 0;}
    const DWORD actual[4]={1,0,1,2};
    const DWORD requested[4]={want->major,want->minor,want->build,want->platform};
    const DWORD flags[4]={0x2u,0x1u,0x4u,0x8u};
    const unsigned int shifts[4]={0,3,21,24};
    for(unsigned int i=0;i<4;i++){
        if(!(type&flags[i]))continue;
        unsigned int op=(unsigned int)((mask>>shifts[i])&7u);
        int ok=0;
        switch(op){
            case 1: ok=actual[i]==requested[i];break;
            case 2: ok=actual[i]>requested[i];break;
            case 3: ok=actual[i]>=requested[i];break;
            case 4: ok=actual[i]<requested[i];break;
            case 5: ok=actual[i]<=requested[i];break;
            case 6: ok=(actual[i]&requested[i])==requested[i];break;
            case 7: ok=(actual[i]&requested[i])!=0;break;
            default: k32_last_error=87;return 0;
        }
        if(!ok){k32_last_error=1150;return 0;}
    }
    k32_last_error=0;return 1;
}

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

__attribute__((dllexport)) void DebugBreak(void){sy3(SYS_Exit,0x80000003u,0,0);for(;;)__asm__ volatile("hlt");}

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
