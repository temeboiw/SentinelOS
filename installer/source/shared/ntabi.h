#pragma once
/*
 * SentinelOS NT-style system-call ABI.
 *
 * This is a stable, documented SentinelOS ABI inspired by NT conventions;
 * syscall numbers are NOT claimed to match undocumented Windows build-specific
 * kernel syscall numbers. Win32 APIs should be implemented above this layer.
 */
#include <stdint.h>

/* NTSTATUS is declared by shared/ntdef.h in the kernel build. Keep the
   same underlying type here so either include order is accepted. */
typedef long NTSTATUS;
typedef uint64_t NT_ULONG_PTR;
typedef uint64_t NT_SIZE_T;
typedef uint32_t NT_ULONG;
typedef int32_t  NT_LONG;
typedef void    *NT_HANDLE;
typedef void    *NT_PVOID;
typedef const void *NT_PCVOID;
typedef uint16_t NT_WCHAR;

#define NT_STATUS_SUCCESS                 ((NTSTATUS)0x00000000)
#define NT_STATUS_UNSUCCESSFUL            ((NTSTATUS)0xC0000001)
#define NT_STATUS_NOT_IMPLEMENTED         ((NTSTATUS)0xC0000002)
#define NT_STATUS_INFO_LENGTH_MISMATCH    ((NTSTATUS)0xC0000004)
#define NT_STATUS_ACCESS_VIOLATION        ((NTSTATUS)0xC0000005)
#define NT_STATUS_INVALID_PARAMETER       ((NTSTATUS)0xC000000D)
#define NT_STATUS_NO_MEMORY                ((NTSTATUS)0xC0000017)
#define NT_STATUS_INVALID_SYSTEM_SERVICE  ((NTSTATUS)0xC000001C)
#define NT_STATUS_INVALID_HANDLE          ((NTSTATUS)0xC0000008)
#define NT_STATUS_INVALID_ADDRESS         ((NTSTATUS)0xC0000141)

#define NT_CURRENT_PROCESS ((NT_HANDLE)(intptr_t)-1)

#define NT_PROCESS_BASIC_INFORMATION_CLASS 0u
#define NT_MEMORY_BASIC_INFORMATION_CLASS 0u

#define NT_MEM_COMMIT  0x00001000u
#define NT_MEM_RESERVE 0x00002000u
#define NT_MEM_RELEASE 0x00008000u
#define NT_PAGE_READWRITE 0x04u
#define NT_PAGE_READONLY  0x02u
#define NT_PAGE_EXECUTE_READ 0x20u
#define NT_MEM_PRIVATE 0x00020000u
#define NT_MEM_COMMITTED 0x00001000u

/* New ABI calls live in a separate range so existing SentinelOS programs
   using legacy syscall IDs 0..21 continue to work unchanged. */
enum {
    NT_SYS_QUERY_INFORMATION_PROCESS = 0x1000,
    NT_SYS_ALLOCATE_VIRTUAL_MEMORY   = 0x1001,
    NT_SYS_YIELD_EXECUTION            = 0x1002,
    NT_SYS_QUERY_VIRTUAL_MEMORY      = 0x1003
};

typedef struct {
    NTSTATUS ExitStatus;
    NT_PVOID PebBaseAddress;
    NT_ULONG_PTR AffinityMask;
    NT_LONG BasePriority;
    NT_ULONG_PTR UniqueProcessId;
    NT_ULONG_PTR InheritedFromUniqueProcessId;
} NT_PROCESS_BASIC_INFORMATION;

typedef struct {
    NT_PVOID BaseAddress;
    NT_PVOID AllocationBase;
    NT_ULONG AllocationProtect;
    uint16_t PartitionId;
    NT_SIZE_T RegionSize;
    NT_ULONG State;
    NT_ULONG Protect;
    NT_ULONG Type;
} NT_MEMORY_BASIC_INFORMATION;
