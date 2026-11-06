#ifndef _LOADED_IMAGE_H_
#define _LOADED_IMAGE_H_
#include <Uefi.h>
typedef struct { UINT32 Revision; VOID *ParentHandle; EFI_SYSTEM_TABLE *SystemTable; EFI_HANDLE DeviceHandle; VOID *FilePath; VOID *Reserved; } EFI_LOADED_IMAGE_PROTOCOL;
extern EFI_GUID gEfiLoadedImageProtocolGuid;
#endif
