/* shell32.dll - SentinelOS shell compatibility layer.
 * This is a native compatibility subset, not Microsoft's full shell32.
 * Process launch uses SentinelOS syscall 4 (boot-module PE loader).
 */
typedef unsigned long long u64;
typedef long long i64;
typedef unsigned int DWORD;
typedef int BOOL;
typedef long HRESULT;
typedef void *HANDLE;
typedef void *HWND;
typedef const char *LPCSTR;
typedef char *LPSTR;
#define E __attribute__((dllexport))
#define SE_ERR_FNF 2
#define SE_ERR_PNF 3
#define SE_ERR_ACCESSDENIED 5
#define SE_ERR_OOM 8
#define SE_ERR_NOASSOC 31
#define SEE_MASK_NOCLOSEPROCESS 0x00000040u
#define S_OK ((HRESULT)0)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define ERROR_NOT_SUPPORTED 50u

static u64 shell_sy3(u64 n,u64 a,u64 b,u64 c) {
    u64 r;
    __asm__ volatile(
        "mov %[n], %%rax\nmov %[a], %%rdi\nmov %[b], %%rsi\nmov %[c], %%rdx\nint $0x80\nmov %%rax, %[r]"
        : [r]"=r"(r):[n]"r"(n),[a]"r"(a),[b]"r"(b),[c]"r"(c)
        :"rax","rdi","rsi","rdx","memory","cc");
    return r;
}
static int shell_streqi(const char *a,const char *b) {
    if(!a||!b)return 0;
    while(*a&&*b) {
        char x=*a++,y=*b++;
        if(x>='A'&&x<='Z')x=(char)(x+32);
        if(y>='A'&&y<='Z')y=(char)(y+32);
        if(x!=y)return 0;
    }
    return *a==0&&*b==0;
}
static void shell_copy(char *d,const char *s,unsigned cap) {
    unsigned i=0;if(!d||!cap)return;
    if(s)while(s[i]&&i+1<cap){d[i]=s[i];i++;}
    d[i]=0;
}
static int shell_is_absolute(const char *p) {
    return p && (((p[0]=='C'||p[0]=='c')&&p[1]==':') || p[0]=='\\\\' || p[0]=='/');
}

/* Win32 ShellExecuteA: values > 32 mean success. */
E u64 ShellExecuteA(HWND hwnd,LPCSTR verb,LPCSTR file,LPCSTR params,LPCSTR dir,int show) {
    (void)hwnd;(void)params;(void)dir;(void)show;
    if(!file||!file[0])return SE_ERR_FNF;
    if(verb && !shell_streqi(verb,"open") && !shell_streqi(verb,"runas") &&
       !shell_streqi(verb,"")) return SE_ERR_NOASSOC;
    /* The current kernel loader launches registered boot modules by image name.
       It cannot yet load arbitrary PE files from disk. */
    const char *image=file;
    if(shell_is_absolute(file)) {
        const char *last=file;
        for(const char *p=file;*p;p++)if(*p=='\\\\'||*p=='/')last=p+1;
        image=last;
    }
    i64 pid=(i64)shell_sy3(4,(u64)image,0,0);
    if(pid<=0)return SE_ERR_FNF;
    return 33;
}

typedef struct {
    DWORD cbSize;
    DWORD fMask;
    HWND hwnd;
    LPCSTR lpVerb;
    LPCSTR lpFile;
    LPCSTR lpParameters;
    LPCSTR lpDirectory;
    int nShow;
    HANDLE hInstApp;
    void *lpIDList;
    LPCSTR lpClass;
    HANDLE hkeyClass;
    DWORD dwHotKey;
    HANDLE hIconOrMonitor;
    HANDLE hProcess;
} SHELLEXECUTEINFOA_LOCAL;

E BOOL ShellExecuteExA(SHELLEXECUTEINFOA_LOCAL *info) {
    if(!info || info->cbSize<sizeof(SHELLEXECUTEINFOA_LOCAL) || !info->lpFile)
        return 0;
    u64 result=ShellExecuteA(info->hwnd,info->lpVerb,info->lpFile,
                             info->lpParameters,info->lpDirectory,info->nShow);
    info->hInstApp=(HANDLE)result;
    if(info->hProcess)info->hProcess=0;
    return result>32;
}

/* CSIDL-compatible common folders for the current single-user system layout. */
E HRESULT SHGetFolderPathA(HWND hwnd,int csidl,HANDLE token,DWORD flags,LPSTR out) {
    (void)hwnd;(void)token;(void)flags;
    if(!out)return E_INVALIDARG;
    const char *p=0;
    switch(csidl & 0xFF) {
        case 0:  p="C:\\Users\\Public\\Desktop"; break;             /* Desktop */
        case 5:  p="C:\\Users\\Public\\Documents"; break;            /* Personal */
        case 6:  p="C:\\Windows\\Favorites"; break;
        case 16: p="C:\\Users\\Public\\Desktop"; break;             /* DesktopDirectory */
        case 20: p="C:\\Windows\\Fonts"; break;
        case 26: p="C:\\Users\\Public\\AppData\\Roaming"; break;
        case 28: p="C:\\Users\\Public\\AppData\\Local"; break;
        case 35: p="C:\\Users\\Public"; break;                        /* Profile */
        case 36: p="C:\\Windows\\System32"; break;
        case 37: p="C:\\Program Files"; break;
        case 38: p="C:\\Users\\Public\\Pictures"; break;
        case 39: p="C:\\Program Files\\Common Files"; break;
        case 40: p="C:\\ProgramData"; break;
        case 41: p="C:\\Windows"; break;
        case 42: p="C:\\Windows\\System32"; break;
        case 43: p="C:\\Program Files (x86)"; break;
        case 44: p="C:\\Program Files (x86)\\Common Files"; break;
        case 48: p="C:\\Users\\Public"; break;
        case 50: p="C:\\Users\\Public\\Documents"; break;
        case 26+0x1000: p="C:\\Users\\Public\\AppData\\Roaming"; break;
        default: out[0]=0; return (HRESULT)0x80070002L;
    }
    shell_copy(out,p,260);
    return S_OK;
}

/* A bounded, quote-aware CommandLineToArgvA parser. Returned storage is
 * process-local and remains valid until the next call to this function. */
static char shell_argv_text[4096];
static LPSTR shell_argv[64];
E LPSTR *CommandLineToArgvA(LPCSTR command,int *argc_out) {
    if(argc_out)*argc_out=0;
    if(!command)return 0;
    unsigned i=0,o=0,argc=0;
    while(command[i]==' '||command[i]=='\\t')i++;
    while(command[i] && argc<63) {
        shell_argv[argc++]=&shell_argv_text[o];
        int quoted=0;
        while(command[i]) {
            char c=command[i];
            if(c=='"') { quoted=!quoted; i++; continue; }
            if(!quoted && (c==' '||c=='\\t'))break;
            if(c=='\\\\') {
                unsigned bs=0;
                while(command[i]=='\\\\'){bs++;i++;}
                if(command[i]=='"') {
                    for(unsigned k=0;k<bs/2 && o<sizeof(shell_argv_text)-2;k++)shell_argv_text[o++]='\\\\';
                    if(bs&1) { if(o<sizeof(shell_argv_text)-2)shell_argv_text[o++]='"'; i++; }
                    else { quoted=!quoted; i++; }
                    continue;
                }
                while(bs-- && o<sizeof(shell_argv_text)-2)shell_argv_text[o++]='\\\\';
                continue;
            }
            if(o>=sizeof(shell_argv_text)-2) { if(argc_out)*argc_out=0;return 0; }
            shell_argv_text[o++]=c;i++;
        }
        shell_argv_text[o++]=0;
        while(command[i]==' '||command[i]=='\\t')i++;
    }
    shell_argv[argc]=0;
    if(argc_out)*argc_out=(int)argc;
    return shell_argv;
}
