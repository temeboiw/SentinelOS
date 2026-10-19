/* hello.cpp - non-destructive SentinelOS syscall/API smoke test.
 * Build with the same freestanding x86_64 user-image flags as hello.c.
 * Each test reports PASS/FAIL and the final passed/total count.
 */
typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef long long i64;

enum {
    SYS_Write=1, SYS_GetPid=2, SYS_Alloc=3, SYS_QueryDir=7,
    SYS_GetProcAddress=9, SYS_Yield=21, SYS_QueryNetworkConfig=22,
    SYS_GetTickCount64=33
};

static u64 call3(u64 n,u64 a,u64 b,u64 c) {
    u64 r;
    __asm__ volatile(
        "mov %[n], %%rax\n"
        "mov %[a], %%rdi\n"
        "mov %[b], %%rsi\n"
        "mov %[c], %%rdx\n"
        "int $0x80\n"
        "mov %%rax, %[r]"
        : [r]"=r"(r)
        : [n]"r"(n),[a]"r"(a),[b]"r"(b),[c]"r"(c)
        : "rax","rdi","rsi","rdx","memory","cc");
    return r;
}

static u64 g_passed=0, g_total=0;

static u64 text_len(const char *s) {
    u64 n=0; while(s[n])n++; return n;
}
static void raw_write(const char *s) {
    (void)call3(SYS_Write,1,(u64)s,text_len(s));
}
static void result(const char *name,int ok) {
    raw_write(ok ? "[PASS] " : "[FAIL] ");
    raw_write(name);
    raw_write("\n");
    g_total++;
    if(ok)g_passed++;
}
static void number(u64 n) {
    char b[24]; int i=0;
    if(!n){raw_write("0");return;}
    while(n && i<23){b[i++]='0'+(n%10);n/=10;}
    while(i) { char c[2]={b[--i],0}; raw_write(c); }
}
static void *get_proc(const char *dll,const char *name) {
    return (void *)call3(SYS_GetProcAddress,(u64)dll,(u64)name,0);
}

typedef struct { u64 va; u32 w,h,pitch; } FBInfo;
typedef struct {
    u32 size;
    u32 pid;
    u32 parent_pid;
    u32 state;
    char image_name[64];
} ProcessInfo;

void hello_main(void) {
    raw_write("\nSentinelOS API smoke tests\n");
    raw_write("==========================\n");

    /* 1. Console write: check the syscall's returned byte count. */
    const char *probe="hello.cpp: write syscall probe\n";
    u64 wrote=call3(SYS_Write,1,(u64)probe,text_len(probe));
    result("sys_write returns requested byte count",wrote==text_len(probe));

    /* 2. PID must be a valid non-zero process identifier. */
    u64 pid=call3(SYS_GetPid,0,0,0);
    result("sys_getpid returns non-zero PID",pid!=0 && pid!=(u64)-1);

    /* 3. Legacy allocator: reserve a small region and touch it. */
    u64 mem=call3(SYS_Alloc,0,4096,0);
    int alloc_ok=mem!=0 && mem!=(u64)-1;
    if(alloc_ok) {
        volatile unsigned char *p=(volatile unsigned char *)mem;
        p[0]=0x53; p[1]=0x4f; p[4095]=0x21;
        alloc_ok=(p[0]==0x53 && p[1]==0x4f && p[4095]==0x21);
    }
    result("sys_alloc returns writable memory",alloc_ok);

    /* 4. Directory query: root should contain at least one entry. */
    char entry[128]; for(int i=0;i<128;i++)entry[i]=0;
    u64 q=call3(SYS_QueryDir,0,(u64)entry,0);
    int q_ok=(q!=0 && q!=(u64)-1 && entry[0]!=0);
    result("sys_querydir returns first directory entry",q_ok);

    /* 5. Export lookup is safe and should resolve this core API. */
    void *query_api=get_proc("kernel32.dll","QueryDirectoryA");
    result("GetProcAddress finds QueryDirectoryA",query_api!=0 && query_api!=(void *)(u64)-1);

    /* 6. Tick counter should be callable and not return the error sentinel. */
    u64 tick1=call3(SYS_GetTickCount64,0,0,0);
    u64 tick2=call3(SYS_GetTickCount64,0,0,0);
    result("sys_gettickcount64 is callable",tick1!=(u64)-1 && tick2!=(u64)-1);

    /* 7. Yield must return instead of trapping or hanging. */
    u64 yielded=call3(SYS_Yield,0,0,0);
    result("sys_yield returns to caller",yielded!=(u64)-1);

    /* 8. Network configuration query is read-only; a missing NIC/config is
       reported as a failure rather than crashing the test program. */
    struct NetConfigProbe {
        u32 Version;
        u32 Flags;
        unsigned char MacAddress[6];
        unsigned char Reserved[2];
        u32 IPv4Address;
        u32 SubnetMask;
        u32 Gateway;
        u32 DnsServer;
        u64 RxPackets;
        u64 TxPackets;
        u32 RxHead, RxTail, RxDescStatus, RxDescErrors, RxControl;
        u32 RxGoodCount, RxMissedCount;
        u32 TxHead, TxTail, TxDescStatus;
    } net;
    volatile unsigned char *nb=(volatile unsigned char *)&net;
    for(u64 i=0;i<sizeof(net);i++)nb[i]=0;
    u64 net_rc=call3(SYS_QueryNetworkConfig,(u64)&net,0,0);
    result("sys_query_network_config succeeds",net_rc==0);

    raw_write("--------------------------\nResult: ");
    number(g_passed); raw_write("/"); number(g_total);
    raw_write(" calls passed\n");
    if(g_passed==g_total) raw_write("ALL TESTS PASSED\n");
    else raw_write("Some tests failed; check the kernel/API implementation.\n");

    for(;;) {
        (void)call3(SYS_Yield,0,0,0);
        __asm__ volatile("pause");
    }
}
