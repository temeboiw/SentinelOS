typedef unsigned long long u64;
typedef const char *LPCSTR;
typedef void *HWND;
typedef unsigned int DWORD;
#define E __attribute__((dllexport))
E u64 ShellExecuteA(HWND h,LPCSTR v,LPCSTR f,LPCSTR p,LPCSTR d,int s){(void)h;(void)v;(void)f;(void)p;(void)d;(void)s;return 31;}
E int ShellExecuteExA(void *i){(void)i;return 0;}
E int SHGetFolderPathA(HWND h,int f,void*t,DWORD flags,char*p){(void)h;(void)f;(void)t;(void)flags;if(p){p[0]='C';p[1]=':';p[2]='\\';p[3]=0;}return -1;}
E void *CommandLineToArgvA(const char*c,int*n){(void)c;if(n)*n=0;return 0;}
