typedef long HRESULT;
typedef unsigned long DWORD;
typedef void *LPVOID;
#define E __attribute__((dllexport))
#define E_NOTIMPL ((HRESULT)0x80004001L)
E HRESULT CoInitialize(LPVOID p){(void)p;return 0;}
E HRESULT CoInitializeEx(LPVOID p,DWORD f){(void)p;(void)f;return 0;}
E void CoUninitialize(void){}
E HRESULT CoCreateInstance(const void*c,LPVOID o,DWORD x,const void*i,void**p){(void)c;(void)o;(void)x;(void)i;if(p)*p=0;return E_NOTIMPL;}
E HRESULT CoGetClassObject(const void*c,DWORD x,void*s,const void*i,void**p){(void)c;(void)x;(void)s;(void)i;if(p)*p=0;return E_NOTIMPL;}
E HRESULT CoInitializeSecurity(void*a,long b,void*c,void*d,DWORD e,DWORD f,void*g,DWORD h,void*i){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;return E_NOTIMPL;}
