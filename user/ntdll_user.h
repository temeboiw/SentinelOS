#pragma once
#include "ntabi.h"
#include "../shared/netabi.h"

/* Legacy SentinelOS syscall numbers are kept stable for existing binaries. */
typedef unsigned long long u64;
typedef long long i64;
typedef unsigned int u32;

enum {
    SYS_Exit = 0, SYS_Write = 1, SYS_GetPid = 2, SYS_Alloc = 3,
    SYS_CreateProcess = 4, SYS_OpenFile = 5, SYS_ReadFile = 6,
    SYS_QueryDir = 7, SYS_MapFramebuffer = 8, SYS_GetProcAddress = 9,
    SYS_GetInput = 10, SYS_Shutdown = 11, SYS_SetCurrentDirectory = 12,
    SYS_CreateThread = 13, SYS_Wait = 14, SYS_WmRegister = 15,
    SYS_WmSetMin = 16, SYS_WmUnregister = 17, SYS_WmQuery = 18,
    SYS_WmRestore = 19, SYS_WmSetRect = 20, SYS_Yield = 21
};

/* Legacy three-argument transport. Do not change: existing user images use it. */
static inline u64 sy3(u64 n, u64 a, u64 b, u64 c) {
    u64 r;
    __asm__ volatile(
        "mov %[n], %%rax\nmov %[a], %%rdi\nmov %[b], %%rsi\nmov %[c], %%rdx\nint $0x80\nmov %%rax, %[r]"
        : [r] "=r"(r) : [n] "r"(n), [a] "r"(a), [b] "r"(b), [c] "r"(c)
        : "rax", "rdi", "rsi", "rdx", "memory", "cc");
    return r;
}

/*
 * Six-argument NT-style syscall transport:
 * RAX=service, arguments in RDI, RSI, RDX, RCX, R8, R9; result in RAX.
 * This transport is SentinelOS-specific; it is not Windows' private syscall
 * instruction/number table. NT API contracts live above the transport.
 */
static inline u64 NtSyscall6(u64 service, u64 a1, u64 a2, u64 a3,
                             u64 a4, u64 a5, u64 a6) {
    register u64 raxv __asm__("rax") = service;
    register u64 rdiv __asm__("rdi") = a1;
    register u64 rsiv __asm__("rsi") = a2;
    register u64 rdxv __asm__("rdx") = a3;
    register u64 rcxv __asm__("rcx") = a4;
    register u64 r8v  __asm__("r8")  = a5;
    register u64 r9v  __asm__("r9")  = a6;
    __asm__ volatile(
        "int $0x80"
        : "+a"(raxv), "+D"(rdiv), "+S"(rsiv), "+d"(rdxv),
          "+c"(rcxv), "+r"(r8v), "+r"(r9v)
        :
        : "r10", "r11", "memory", "cc");
    return raxv;
}

typedef struct { u64 va; u32 w, h, pitch; } FBInfo;

static inline void NtWriteConsole(const char *s) {
    u64 n = 0;
    while (s[n]) n++;
    (void)sy3(SYS_Write, 1, (u64)s, n);
}
static inline void NtExit(u64 code) {
    (void)sy3(SYS_Exit, code, 0, 0);
}
static inline NTSTATUS NtCreateProcess(const char *name) {
    return (NTSTATUS)sy3(SYS_CreateProcess, (u64)name, 0, 0);
}
static inline NTSTATUS NtQueryDirectory(u64 index, char *name_out) {
    return (NTSTATUS)sy3(SYS_QueryDir, index, (u64)name_out, 0);
}
static inline u64 NtMapFramebuffer(FBInfo *out) {
    return sy3(SYS_MapFramebuffer, (u64)out, 0, 0);
}
static inline u64 NtGetProcAddress(const char *dll, const char *fn) {
    return sy3(SYS_GetProcAddress, (u64)dll, (u64)fn, 0);
}

/* SentinelOS network interface query (legacy syscall ID 22). */
static inline int NtQueryNetworkConfig(SENTINELOS_NET_CONFIG *out) {
    return (int)sy3(22, (u64)out, 0, 0);
}

/* NT-style APIs implemented by the new, stable syscall range. */
static inline NTSTATUS NtQueryInformationProcess(
    NT_HANDLE process, NT_ULONG information_class, NT_PVOID information,
    NT_ULONG information_length, NT_ULONG *return_length)
{
    return (NTSTATUS)NtSyscall6(NT_SYS_QUERY_INFORMATION_PROCESS,
        (u64)process, information_class, (u64)information,
        information_length, (u64)return_length, 0);
}

static inline NTSTATUS NtAllocateVirtualMemory(
    NT_HANDLE process, NT_PVOID *base_address, NT_ULONG_PTR zero_bits,
    NT_SIZE_T *region_size, NT_ULONG allocation_type, NT_ULONG protect)
{
    return (NTSTATUS)NtSyscall6(NT_SYS_ALLOCATE_VIRTUAL_MEMORY,
        (u64)process, (u64)base_address, zero_bits, (u64)region_size,
        allocation_type, protect);
}

static inline NTSTATUS NtYieldExecution(void) {
    return (NTSTATUS)NtSyscall6(NT_SYS_YIELD_EXECUTION, 0, 0, 0, 0, 0, 0);
}

static inline NTSTATUS NtQueryVirtualMemory(
    NT_HANDLE process, NT_PVOID base_address, NT_ULONG information_class,
    NT_PVOID information, NT_SIZE_T information_length, NT_ULONG *return_length)
{
    return (NTSTATUS)NtSyscall6(NT_SYS_QUERY_VIRTUAL_MEMORY,
        (u64)process, (u64)base_address, information_class,
        (u64)information, information_length, (u64)return_length);
}
