#pragma once
#include "ntabi.h"

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
    SYS_WmRestore = 19, SYS_WmSetRect = 20, SYS_Yield = 21, SYS_InitializeGraphics = 32, SYS_FlushGraphics = 33, SYS_PresentLayer = 37, SYS_QueryInstallerDisk = 40, SYS_InstallerDiskIo = 41, SYS_ReadInstallerPayload = 42, SYS_QueryInstallerPayload = 43, SYS_VerifyInstallerPayload = 44, SYS_InstallerInstallImage = 45, SYS_GetTickCount = 24
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
typedef struct { u64 sectors; u32 sector_size, present; } InstallerDiskInfo;
static inline u64 NtQueryInstallerDisk(InstallerDiskInfo *out) { return sy3(SYS_QueryInstallerDisk, (u64)out, 0, 0); }
static inline i64 NtInstallerDiskIo(u64 write, u64 sector, void *buffer) { return (i64)sy3(SYS_InstallerDiskIo, write, sector, (u64)buffer); }
typedef struct {
    u32 size, version, backend, capabilities;
    u32 width, height, pitch;
    u32 pci_bus, pci_device, pci_function;
    unsigned short vendor_id, device_id;
    unsigned char pci_class, pci_subclass, pci_prog_if, initialized;
    u64 surface_va;
    u64 virgl_buffer_va;
    u32 virgl_buffer_resource_id, virgl_buffer_size;
} GfxBootInfo;
typedef struct { u32 x, y, w, h; } GfxRect;

#define GFX_CAP_FRAMEBUFFER (1u << 0)
#define GFX_CAP_PCI_SCAN    (1u << 1)
#define GFX_CAP_VIRTIO_GPU  (1u << 2)
#define GFX_CAP_ACCEL_2D    (1u << 3)
#define GFX_CAP_ACCEL_3D    (1u << 4)

static inline void NtWriteConsole(const char *s) {
    u64 n = 0;
    while (s[n]) n++;
    (void)sy3(SYS_Write, 1, (u64)s, n);
}
static inline void NtExit(u64 code) {
    (void)sy3(SYS_Exit, code, 0, 0);
}
static inline NTSTATUS NtCreateProcess(const char *name) {
    NTSTATUS status = (NTSTATUS)sy3(SYS_CreateProcess, (u64)name, 0, 0);
    if (status == (NTSTATUS)-2) {
        typedef int (*MessageBoxA_t)(void *, const char *, const char *, u32);
        static const char dll_name[] = "user32.dll";
        static const char api_name[] = "MessageBoxA";
        MessageBoxA_t box = (MessageBoxA_t)sy3(
            SYS_GetProcAddress, (u64)dll_name, (u64)api_name, 0);
        if (box)
            (void)box(0, "Cannot use this .exe in Win32 mode.",
                      "Unsupported executable", 0x10);
        else
            NtWriteConsole("Cannot use this .exe in Win32 mode.\n");
    }
    return status;
}
static inline NTSTATUS NtQueryDirectory(u64 index, char *name_out) {
    return (NTSTATUS)sy3(SYS_QueryDir, index, (u64)name_out, 0);
}
static inline u64 NtInitializeGraphics(GfxBootInfo *out) {
    if (!out) return 0;
    out->size = sizeof(*out);
    out->version = 1;
    return sy3(SYS_InitializeGraphics, (u64)out, 0, 0);
}
typedef struct { u64 pixels; u32 layer,x,y,w,h,width,height; } NT_PRESENT_LAYER;
static inline u64 NtPresentLayer(const NT_PRESENT_LAYER *layer) {
    return sy3(SYS_PresentLayer, (u64)layer, 0, 0);
}
static inline u64 NtFlushGraphics(u32 x, u32 y, u32 w, u32 h) {
    GfxRect rect = {x, y, w, h};
    return sy3(SYS_FlushGraphics, (u64)&rect, 0, 0);
}
static inline u64 NtMapFramebuffer(FBInfo *out) {
    return sy3(SYS_MapFramebuffer, (u64)out, 0, 0);
}
static inline u64 NtGetProcAddress(const char *dll, const char *fn) {
    return sy3(SYS_GetProcAddress, (u64)dll, (u64)fn, 0);
}

/* NT-style APIs implemented by the new, stable syscall range. */
/* image: 0=esp.img, 1=system.img; metadata/read requests are trusted-only. */
static inline i64 NtQueryInstallerPayload(u64 package, u64 *size_out) {
    return (i64)NtSyscall6(SYS_QueryInstallerPayload, package, (u64)size_out, 0, 0, 0, 0);
}
static inline i64 NtVerifyInstallerPayload(u64 package) {
    return (i64)NtSyscall6(SYS_VerifyInstallerPayload, package, 0, 0, 0, 0, 0);
}
static inline i64 NtInstallerInstallImage(u64 image, u64 offset, u64 bytes) {
    return (i64)NtSyscall6(SYS_InstallerInstallImage, image, offset, bytes, 0, 0, 0);
}
static inline i64 NtReadInstallerPayload(u64 package, u64 offset, void *buffer, u32 size) {
    return (i64)NtSyscall6(SYS_ReadInstallerPayload, package, offset, (u64)buffer, size, 0, 0);
}

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
