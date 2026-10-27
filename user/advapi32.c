typedef unsigned char BYTE;
typedef unsigned short WCHAR;
typedef unsigned int DWORD;
typedef unsigned int UINT;
typedef int BOOL;
typedef long LONG;
typedef void *HANDLE;
typedef void *HKEY;
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_INVALID_HANDLE 6
#define ERROR_INSUFFICIENT_BUFFER 122
#define ERROR_NO_TOKEN 1008
#define ERROR_CALL_NOT_IMPLEMENTED 120
#define ERROR_NOT_SUPPORTED 50
#define ERROR_SUCCESS 0
__attribute__((dllexport)) LONG RegCloseKey(HKEY key){(void)key;return ERROR_INVALID_HANDLE;}
__attribute__((dllexport)) LONG RegOpenKeyExW(HKEY key,const WCHAR *sub,DWORD opt,DWORD access,HKEY *out){(void)key;(void)sub;(void)opt;(void)access;if(out)*out=0;return ERROR_FILE_NOT_FOUND;}
__attribute__((dllexport)) LONG RegGetValueW(HKEY key,const WCHAR *sub,const WCHAR *name,DWORD flags,DWORD *type,void *data,DWORD *size){(void)key;(void)sub;(void)name;(void)flags;(void)data;if(type)*type=0;if(size)*size=0;return ERROR_FILE_NOT_FOUND;}
__attribute__((dllexport)) LONG RegQueryValueExW(HKEY key,const WCHAR *name,DWORD *reserved,DWORD *type,BYTE *data,DWORD *size){(void)key;(void)name;(void)reserved;(void)data;if(type)*type=0;if(size)*size=0;return ERROR_FILE_NOT_FOUND;}
__attribute__((dllexport)) BOOL GetUserNameW(WCHAR *name,DWORD *size){static const WCHAR user[]={ 'S','e','n','t','i','n','e','l',0 };if(!size)return 0;if(!name||*size<sizeof(user)/sizeof(user[0])){*size=sizeof(user)/sizeof(user[0]);return 0;}for(unsigned i=0;i<sizeof(user)/sizeof(user[0]);i++)name[i]=user[i];*size=sizeof(user)/sizeof(user[0])-1;return 1;}
__attribute__((dllexport)) BOOL OpenProcessToken(HANDLE process,DWORD access,HANDLE *token){(void)process;(void)access;if(token)*token=0;return 0;}
__attribute__((dllexport)) BOOL OpenThreadToken(HANDLE thread,DWORD access,BOOL self,HANDLE *token){(void)thread;(void)access;(void)self;if(token)*token=0;return 0;}
__attribute__((dllexport)) BOOL GetTokenInformation(HANDLE token,int cls,void *info,DWORD length,DWORD *needed){(void)token;(void)cls;(void)info;(void)length;if(needed)*needed=0;return 0;}
__attribute__((dllexport)) BOOL ConvertSidToStringSidA(void *sid,char **text){(void)sid;if(text)*text=0;return 0;}
__attribute__((dllexport)) BOOL AllocateAndInitializeSid(void *authority,BYTE count,DWORD a,DWORD b,DWORD c,DWORD d,DWORD e,DWORD f,DWORD g,DWORD h,void **sid){(void)authority;(void)count;(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;if(sid)*sid=0;return 0;}
__attribute__((dllexport)) void *FreeSid(void *sid){(void)sid;return 0;}
__attribute__((dllexport)) DWORD SetEntriesInAclA(DWORD count,void *entries,void *old_acl,void **new_acl){(void)count;(void)entries;(void)old_acl;if(new_acl)*new_acl=0;return ERROR_CALL_NOT_IMPLEMENTED;}
__attribute__((dllexport)) DWORD GetSecurityInfo(HANDLE object,int type,DWORD info,void **owner,void **group,void **dacl,void **sacl,void **descriptor){(void)object;(void)type;(void)info;if(owner)*owner=0;if(group)*group=0;if(dacl)*dacl=0;if(sacl)*sacl=0;if(descriptor)*descriptor=0;return ERROR_NOT_SUPPORTED;}
__attribute__((dllexport)) DWORD SetSecurityInfo(HANDLE object,int type,DWORD info,void *owner,void *group,void *dacl,void *sacl){(void)object;(void)type;(void)info;(void)owner;(void)group;(void)dacl;(void)sacl;return ERROR_NOT_SUPPORTED;}
__attribute__((dllexport)) BOOL SystemFunction036(void *buffer,ULONG length){if(!buffer&&length)return 0;unsigned int a,b,c,d;__asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(1),"c"(0));if(!(c&(1u<<30)))return 0;BYTE *p=(BYTE *)buffer;for(ULONG i=0;i<length;){unsigned long long v;unsigned char ok;__asm__ volatile("rdrand %0; setc %1":"=r"(v),"=qm"(ok));if(!ok)return 0;for(unsigned j=0;j<8&&i<length;j++,i++)p[i]=(BYTE)(v>>(j*8));}return 1;}
typedef unsigned long ULONG;
