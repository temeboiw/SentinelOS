typedef unsigned short WCHAR;
typedef unsigned int DWORD;
typedef int BOOL;
typedef void *HANDLE;
__attribute__((dllexport)) BOOL GetUserProfileDirectoryW(HANDLE token,WCHAR *path,DWORD *size){
 (void)token;
 static const WCHAR profile[]={'C',':','\\','U','s','e','r','s','\\','P','u','b','l','i','c',0};
 DWORD need=(DWORD)(sizeof(profile)/sizeof(profile[0]));
 if(!size)return 0;
 if(!path||*size<need){*size=need;return 0;}
 for(DWORD i=0;i<need;i++)path[i]=profile[i];
 *size=need;return 1;
}
