#pragma once
// Public interface of hal.dll.  hal.c defines HAL_BUILD (exports); everyone else imports.
#include "ntdef.h"

#ifdef HAL_BUILD
#define HALAPI __declspec(dllexport)
#else
#define HALAPI __declspec(dllimport)
#endif

typedef enum _FIRMWARE_REENTRY {
    HalHaltRoutine, HalPowerDownRoutine, HalRestartRoutine, HalRebootRoutine
} FIRMWARE_REENTRY;

HALAPI BOOLEAN        HalInitSystem(ULONG Phase, PVOID LoaderBlock);
HALAPI VOID           HalDisplayString(PCSTR String);
HALAPI VOID           HalReturnToFirmware(FIRMWARE_REENTRY Routine);
HALAPI VOID           KeStallExecutionProcessor(ULONG Microseconds);
HALAPI LARGE_INTEGER  KeQueryPerformanceCounter(PLARGE_INTEGER PerformanceFrequency);
HALAPI BOOLEAN        HalStartClockInterrupt(ULONG Hz, ULONG Vector);
HALAPI VOID           HalEndSystemInterrupt(KIRQL NewIrql, ULONG Vector);

HALAPI UCHAR  READ_PORT_UCHAR(PUCHAR Port);
HALAPI USHORT READ_PORT_USHORT(PUSHORT Port);
HALAPI ULONG  READ_PORT_ULONG(PULONG Port);
HALAPI VOID   WRITE_PORT_UCHAR(PUCHAR Port, UCHAR Value);
HALAPI VOID   WRITE_PORT_USHORT(PUSHORT Port, USHORT Value);
HALAPI VOID   WRITE_PORT_ULONG(PULONG Port, ULONG Value);

