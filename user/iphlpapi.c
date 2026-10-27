typedef unsigned short WCHAR;
typedef unsigned int DWORD;
typedef unsigned long ULONG;
typedef unsigned long long ULONG64;
typedef unsigned int NET_IFINDEX;
typedef struct { ULONG64 Value; } NET_LUID;
#define ERROR_NO_DATA 232
#define ERROR_NOT_FOUND 1168
__attribute__((dllexport)) DWORD GetAdaptersAddresses(ULONG family,ULONG flags,void *reserved,void *addresses,ULONG *size){(void)family;(void)flags;(void)reserved;(void)addresses;if(size)*size=0;return ERROR_NO_DATA;}
__attribute__((dllexport)) DWORD ConvertInterfaceIndexToLuid(NET_IFINDEX index,NET_LUID *luid){(void)index;if(luid)luid->Value=0;return ERROR_NOT_FOUND;}
__attribute__((dllexport)) DWORD ConvertInterfaceLuidToNameW(const NET_LUID *luid,WCHAR *name,ULONG64 size){(void)luid;(void)name;(void)size;return ERROR_NOT_FOUND;}
