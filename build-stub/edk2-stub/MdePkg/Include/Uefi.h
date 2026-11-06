#ifndef _UEFI_H_
#define _UEFI_H_

#define EFIAPI
#define NULL ((void *)0)
#define FALSE 0
#define TRUE 1
#define EfiLoaderData 3
#define ByProtocol 0x00000001

#define EFI_SUCCESS 0
#define EFI_ERROR(status) ((status) != 0)
#define EFI_LOAD_ERROR 1
#define EFI_INVALID_PARAMETER 2
#define EFI_UNSUPPORTED 3
#define EFI_NOT_FOUND 4
#define EFI_NO_MEDIA 5
#define EFI_DEVICE_ERROR 6
#define EFI_BUFFER_TOO_SMALL 7
#define EFI_OUT_OF_RESOURCES 8
#define EFI_ABORTED 9

#define EFI_IMAGE_DIRECTORY_ENTRY_EXPORT 0
#define EFI_IMAGE_DIRECTORY_ENTRY_IMPORT 1
#define EFI_IMAGE_DIRECTORY_ENTRY_BASERELOC 5
#define EFI_IMAGE_REL_BASED_DIR64 10

typedef signed char INT8;
typedef signed long long INTN;
typedef char CHAR8;
typedef unsigned char BOOLEAN;
typedef unsigned short CHAR16;
typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef unsigned int UINT32;
typedef unsigned long long UINTN;
typedef unsigned long long UINT64;
typedef long long INT64;
typedef UINTN EFI_STATUS;
typedef void VOID;
typedef VOID *EFI_HANDLE;
typedef UINT64 EFI_PHYSICAL_ADDRESS;
#define EFI_SIZE_TO_PAGES(size) (((size) + 0xFFF) >> 12)

typedef struct { UINT32 Data1; UINT16 Data2; UINT16 Data3; UINT8 Data4[8]; } EFI_GUID;
#define EFI_IMAGE_DOS_SIGNATURE 0x5A4D
#define EFI_IMAGE_NT_SIGNATURE 0x00004550
#define EFI_IMAGE_MACHINE_X86_64 0x8664
#define EFI_IMAGE_MACHINE_X64 EFI_IMAGE_MACHINE_X86_64
#define EFI_IMAGE_NT_OPTIONAL_HDR64_MAGIC 0x20b

typedef struct {
    UINT16 e_magic, e_cblp, e_cp, e_crlc, e_cparhdr, e_minalloc, e_maxalloc;
    UINT16 e_ss, e_sp, e_csum, e_ip, e_cs, e_lfarlc, e_ovno;
    UINT16 e_res[4], e_oemid, e_oeminfo;
    UINT16 e_res2[10];
    UINT32 e_lfanew;
} EFI_IMAGE_DOS_HEADER;

typedef struct { UINT32 VirtualAddress, Size; } EFI_IMAGE_DATA_DIRECTORY;

typedef struct {
    UINT16 Magic;
    UINT32 MajorLinkerVersion, MinorLinkerVersion;
    UINT32 SizeOfCode, SizeOfInitializedData, SizeOfUninitializedData;
    UINT32 AddressOfEntryPoint, BaseOfCode;
    UINT64 ImageBase;
    UINT32 SectionAlignment, FileAlignment;
    UINT16 MajorOS, MinorOS, MajorImage, MinorImage, MajorSubsys, MinorSubsys;
    UINT32 Win32Ver, SizeOfImage, SizeOfHeaders, CheckSum;
    UINT16 Subsystem, DllChar;
    UINT64 StackReserve, StackCommit, HeapReserve, HeapCommit;
    UINT32 LoaderFlags, NumRvaAndSizes;
    EFI_IMAGE_DATA_DIRECTORY DataDirectory[16];
} EFI_IMAGE_NT_OPTIONAL_HDR;

typedef struct {
    UINT16 Machine, NumberOfSections; UINT32 TimeDate, PtrToSym, NumSyms;
    UINT16 SizeOfOptionalHeader, Characteristics;
} EFI_IMAGE_FILE_HEADER;

typedef struct {
    UINT32 Signature;
    EFI_IMAGE_FILE_HEADER FileHeader;
    EFI_IMAGE_NT_OPTIONAL_HDR OptionalHeader;
} EFI_IMAGE_NT_HEADERS;
typedef EFI_IMAGE_NT_HEADERS EFI_IMAGE_NT_HEADERS64;

typedef struct {
    UINT8 Name[8];
    union {
        UINT32 PhysicalAddress;
        UINT32 VirtualSize;
    } Misc;
    UINT32 VirtualAddress;
    UINT32 SizeOfRawData;
    UINT32 PointerToRawData;
    UINT32 PointerToRelocations;
    UINT32 PointerToLinenumbers;
    UINT16 NumberOfRelocations;
    UINT16 NumberOfLinenumbers;
    UINT32 Characteristics;
} EFI_IMAGE_SECTION_HEADER;

typedef struct {
    UINT32 VirtualAddress;
    UINT32 SizeOfBlock;
} EFI_IMAGE_BASE_RELOCATION;

typedef struct { UINT8 Type; UINT8 Pad[7]; UINT64 PhysAddr, VirtAddr, NumPages, Attr; } EFI_MEMORY_DESCRIPTOR;

typedef struct { UINT32 Data1, Data2, Data3; UINT8 Data4[8]; } EFI_TABLE_HEADER;

struct EFI_RUNTIME_SERVICES;
struct EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER Hdr; CHAR16 *FirmwareVendor; UINT32 FirmwareRev;
    EFI_HANDLE ConsoleIn; VOID *ConIn; EFI_HANDLE ConsoleOut; VOID *ConOut;
    EFI_HANDLE StandardErrorHandle; VOID *StdErr;
    struct EFI_RUNTIME_SERVICES *RuntimeServices;
    struct EFI_BOOT_SERVICES *BootServices;
    UINTN NumTables; VOID *ConfigTable;
} EFI_SYSTEM_TABLE;

#define EFI_FILE_MODE_READ 1
#define EFI_FILE_MODE_WRITE 2

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(EFI_FILE_PROTOCOL*, EFI_FILE_PROTOCOL**, CHAR16*, UINT64, UINT64);
typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(EFI_FILE_PROTOCOL*);
typedef EFI_STATUS (EFIAPI *EFI_FILE_DELETE)(EFI_FILE_PROTOCOL*);
typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(EFI_FILE_PROTOCOL*, UINTN*, VOID*);
typedef EFI_STATUS (EFIAPI *EFI_FILE_WRITE)(EFI_FILE_PROTOCOL*, UINTN*, VOID*);
typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_POS)(EFI_FILE_PROTOCOL*, UINTN*);
typedef EFI_STATUS (EFIAPI *EFI_FILE_SET_POS)(EFI_FILE_PROTOCOL*, UINTN);
typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_INFO)(EFI_FILE_PROTOCOL*, VOID*, UINTN*, VOID*);
typedef EFI_STATUS (EFIAPI *EFI_FILE_SET_INFO)(EFI_FILE_PROTOCOL*, VOID*, UINTN, VOID*);
typedef EFI_STATUS (EFIAPI *EFI_FILE_FLUSH)(EFI_FILE_PROTOCOL*);

typedef struct EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_FILE_OPEN Open; EFI_FILE_CLOSE Close; EFI_FILE_DELETE Delete;
    EFI_FILE_READ Read; EFI_FILE_WRITE Write;
    EFI_FILE_GET_POS GetPosition; EFI_FILE_SET_POS SetPosition;
    EFI_FILE_GET_INFO GetInfo; EFI_FILE_SET_INFO SetInfo; EFI_FILE_FLUSH Flush;
} EFI_FILE_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(UINTN, UINTN, VOID **);
typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(VOID *);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(UINTN, UINTN, UINTN, EFI_PHYSICAL_ADDRESS *);
typedef EFI_STATUS (EFIAPI *EFI_FREE_PAGES)(EFI_PHYSICAL_ADDRESS, UINTN);
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(UINTN *, VOID *, UINTN *, UINTN *, UINT32 *);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_HANDLE_BUFFER)(UINT32, VOID*, VOID*, UINTN*, EFI_HANDLE **);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(VOID*, VOID*, VOID **);
typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(EFI_HANDLE, VOID*, VOID **);
typedef EFI_STATUS (EFIAPI *EFI_LOAD_IMAGE)(BOOLEAN, EFI_HANDLE, VOID*, VOID*, UINTN, EFI_HANDLE *);
typedef EFI_STATUS (EFIAPI *EFI_START_IMAGE)(EFI_HANDLE, UINTN*, CHAR16 **);
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE, UINTN);
typedef EFI_STATUS (EFIAPI *EFI_GET_VARIABLE)(CHAR16*, VOID*, UINT32*, UINTN*, VOID*);
typedef EFI_STATUS (EFIAPI *EFI_SET_VARIABLE)(CHAR16*, VOID*, UINT32, UINTN, VOID*);

#define AllocateAnyPages 0
#define AllocateMaxAddress 1
#define AllocateAddress 2

typedef struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;
    EFI_ALLOCATE_POOL AllocatePool; EFI_FREE_POOL FreePool;
    EFI_GET_MEMORY_MAP GetMemoryMap; EFI_ALLOCATE_PAGES AllocatePages; EFI_FREE_PAGES FreePages; VOID *GetNextMemoryMap;
    VOID *SetVirtualAddressMap, *ConvertPointer;
    EFI_LOCATE_HANDLE_BUFFER LocateHandleBuffer;
    VOID *LocateHandle;
    EFI_LOCATE_PROTOCOL LocateProtocol;
    VOID *InstallProtocolInterface, *ReinstallProtocolInterface, *UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL HandleProtocol;
    VOID *Reserved, *RegisterProtocolNotify, *OpenProtocol, *CloseProtocol;
    VOID *ProtocolsPerHandle, *GetProtocolIndex, *InstallConfigurationTable;
    EFI_LOAD_IMAGE LoadImage; EFI_START_IMAGE StartImage; EFI_EXIT_BOOT_SERVICES ExitBootServices;
} EFI_BOOT_SERVICES;

typedef struct EFI_RUNTIME_SERVICES {
    EFI_TABLE_HEADER Hdr;
    UINT64 (EFIAPI *GetTime)(VOID*, VOID*);
    VOID *SetTime, *GetWakeupTime, *SetWakeupTime, *SetVirtualAddressMap, *ConvertPointer;
    EFI_GET_VARIABLE GetVariable;
    EFI_SET_VARIABLE SetVariable;
    VOID *GetNextVariableName, *SetVariable2, *GetNextHighMonotonicCount, *ResetSystem;
} EFI_RUNTIME_SERVICES;

extern EFI_GUID gEfiLoadedImageProtocolGuid;
extern EFI_GUID gEfiDevicePathProtocolGuid;
extern EFI_GUID gEfiSimpleFileSystemProtocolGuid;
extern EFI_GUID gEfiGraphicsOutputProtocolGuid;
extern EFI_GUID gEfiEdidActiveProtocolGuid;
extern EFI_GUID gEfiEdidDiscoveredProtocolGuid;
extern EFI_GUID gEfiFileInfoGuid;

#endif
