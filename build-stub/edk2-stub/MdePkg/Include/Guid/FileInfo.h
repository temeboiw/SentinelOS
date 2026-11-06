#ifndef _FILE_INFO_H_
#define _FILE_INFO_H_
#include <Uefi.h>
typedef struct { UINT64 Size, FileSize, PhysSize, Create, LastAccess, Modification, Change; CHAR16 FileName[1]; } EFI_FILE_INFO;
extern EFI_GUID gEfiFileInfoGuid;
#endif
