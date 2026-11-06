#ifndef _DEVICE_PATH_H_
#define _DEVICE_PATH_H_
#include <Uefi.h>
typedef struct { UINT8 Type, SubType; UINT8 Length[2]; } EFI_DEVICE_PATH_PROTOCOL;
extern EFI_GUID gEfiDevicePathProtocolGuid;
#endif
