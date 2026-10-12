#pragma once
// NT-flavoured basic types (unsigned long is 32-bit on the Windows ABI, same as real NT).
typedef void VOID, *PVOID;
typedef char CHAR, *PCHAR;
typedef const char *PCSTR;
typedef unsigned char UCHAR, *PUCHAR, BOOLEAN;
typedef unsigned short USHORT, *PUSHORT;
typedef unsigned long ULONG, *PULONG;
typedef long LONG;
typedef long NTSTATUS;
typedef UCHAR KIRQL;
typedef unsigned long long ULONGLONG;
typedef long long LONGLONG;
typedef union _LARGE_INTEGER {
    struct { ULONG LowPart; LONG HighPart; };
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

#ifndef NULL
#define NULL ((PVOID)0)
#endif
#define TRUE  1
#define FALSE 0
#define NTAPI
#define STATUS_SUCCESS      ((NTSTATUS)0x00000000L)
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)

