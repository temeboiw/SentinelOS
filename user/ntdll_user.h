#pragma once
typedef unsigned long long u64;
typedef long long i64;
typedef int NTSTATUS;
#define STATUS_SUCCESS 0
#define STATUS_NO_MORE_FILES 1

enum {
    SYS_Exit = 0, SYS_Write = 1, SYS_GetPid = 2, SYS_Alloc = 3, SYS_CreateProcess = 4,
    SYS_OpenFile = 5, SYS_ReadFile = 6, SYS_QueryDir = 7, SYS_MapFramebuffer = 8,
    SYS_GetProcAddress = 9,
};

static inline u64 sy3(u64 n, u64 a, u64 b, u64 c) {
    u64 r;
    __asm__ volatile(
        "mov %[n], %%rax\nmov %[a], %%rdi\nmov %[b], %%rsi\nmov %[c], %%rdx\nint $0x80\nmov %%rax, %[r]"
        : [r] "=r"(r) : [n] "r"(n), [a] "r"(a), [b] "r"(b), [c] "r"(c)
        : "rax", "rdi", "rsi", "rdx", "memory", "cc");
    return r;
}
static inline void NtWriteConsole(const char *s) {
    u64 n = 0; while (s[n]) n++; sy3(SYS_Write, 1, (u64)s, n);
}
static inline void NtExit(u64 code) { sy3(SYS_Exit, code, 0, 0); }
static inline NTSTATUS NtCreateProcess(const char *name) {
    return (NTSTATUS)sy3(SYS_CreateProcess, (u64)name, 0, 0);
}
static inline NTSTATUS NtQueryDirectory(u64 index, char *name_out) {
    return (NTSTATUS)sy3(SYS_QueryDir, index, (u64)name_out, 0);
}
typedef unsigned int u32;
typedef struct { u64 va; u32 w, h, pitch; } FBInfo;
static inline u64 NtMapFramebuffer(FBInfo *out) { return sy3(SYS_MapFramebuffer, (u64)out, 0, 0); }
static inline u64 NtGetProcAddress(const char *dll, const char *fn) {
    return sy3(SYS_GetProcAddress, (u64)dll, (u64)fn, 0);
}
