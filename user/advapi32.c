typedef unsigned long long u64;
typedef unsigned int DWORD;
typedef long LONG;
typedef LONG LSTATUS;
typedef void *HKEY;
typedef const char *LPCSTR;
typedef char *LPSTR;
#define E __attribute__((dllexport))
#define ERROR_SUCCESS 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_ACCESS_DENIED 5L
E LSTATUS RegOpenKeyExA(HKEY k,LPCSTR s,DWORD o,DWORD a,HKEY*out){(void)k;(void)s;(void)o;(void)a;if(out)*out=0;return ERROR_FILE_NOT_FOUND;}
E LSTATUS RegCreateKeyExA(HKEY k,LPCSTR s,DWORD r,LPSTR c,DWORD o,DWORD a,void*sec,HKEY*out,DWORD*d){(void)k;(void)s;(void)r;(void)c;(void)o;(void)a;(void)sec;if(out)*out=0;if(d)*d=0;return ERROR_ACCESS_DENIED;}
E LSTATUS RegQueryValueExA(HKEY k,LPCSTR n,DWORD*r,DWORD*t,unsigned char*d,DWORD*z){(void)k;(void)n;(void)r;(void)d;if(t)*t=1;if(z)*z=0;return ERROR_FILE_NOT_FOUND;}
E LSTATUS RegSetValueExA(HKEY k,LPCSTR n,DWORD r,DWORD t,const unsigned char*d,DWORD z){(void)k;(void)n;(void)r;(void)t;(void)d;(void)z;return ERROR_ACCESS_DENIED;}
E LSTATUS RegCloseKey(HKEY k){(void)k;return ERROR_SUCCESS;}
E LSTATUS RegDeleteKeyA(HKEY k,LPCSTR s){(void)k;(void)s;return ERROR_ACCESS_DENIED;}
E LSTATUS RegDeleteValueA(HKEY k,LPCSTR s){(void)k;(void)s;return ERROR_ACCESS_DENIED;}
E LSTATUS RegEnumKeyExA(HKEY k,DWORD i,LPSTR n,DWORD*l,DWORD*r,LPSTR c,DWORD*cl,void*t){(void)k;(void)i;(void)n;(void)l;(void)r;(void)c;(void)cl;(void)t;return ERROR_FILE_NOT_FOUND;}
E LSTATUS RegEnumValueA(HKEY k,DWORD i,LPSTR n,DWORD*l,DWORD*r,DWORD*t,unsigned char*d,DWORD*z){(void)k;(void)i;(void)n;(void)l;(void)r;(void)t;(void)d;(void)z;return ERROR_FILE_NOT_FOUND;}
E int OpenProcessToken(void*p,DWORD a,void**t){(void)p;(void)a;if(t)*t=0;return 0;}
E int LookupPrivilegeValueA(LPCSTR s,LPCSTR n,void*l){(void)s;(void)n;(void)l;return 0;}
E int AdjustTokenPrivileges(void*t,int d,void*n,DWORD l,void*p,DWORD*r){(void)t;(void)d;(void)n;(void)l;(void)p;(void)r;return 0;}
E int GetUserNameA(LPSTR n,DWORD*s){(void)n;if(s)*s=0;return 0;}
E int GetComputerNameA(LPSTR n,DWORD*s){(void)n;if(s)*s=0;return 0;}
