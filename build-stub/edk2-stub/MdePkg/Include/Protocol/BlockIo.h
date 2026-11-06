#ifndef _BLOCK_IO_H_
#define _BLOCK_IO_H_
#include <Uefi.h>
typedef struct { UINT32 MediaId; BOOLEAN MediaPresent; UINT32 BlockSize; UINT64 LastBlock; } EFI_BLOCK_IO_MEDIA;
typedef struct EFI_BLOCK_IO_PROTOCOL EFI_BLOCK_IO_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_BLOCK_IO_READ_BLOCKS)(EFI_BLOCK_IO_PROTOCOL*, UINT32, UINT64, UINTN, VOID*);
typedef struct EFI_BLOCK_IO_PROTOCOL {
    EFI_BLOCK_IO_MEDIA *Media;
    EFI_BLOCK_IO_READ_BLOCKS ReadBlocks;
} EFI_BLOCK_IO_PROTOCOL;
extern EFI_GUID gEfiBlockIoProtocolGuid;
#endif
