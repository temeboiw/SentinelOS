typedef unsigned long long u64;
typedef unsigned int DWORD;
typedef int BOOL;
typedef void *HDC;
typedef void *HGDIOBJ;
typedef struct { long left,top,right,bottom; } RECT;
typedef struct { long cx,cy; } SIZE;
#define E __attribute__((dllexport))
static u64 next_handle=0x100;
E HDC CreateCompatibleDC(HDC d){(void)d;return (HDC)(next_handle+=4);}
E BOOL DeleteDC(HDC d){(void)d;return 1;}
E HGDIOBJ SelectObject(HDC d,HGDIOBJ o){(void)d;return o?o:(HGDIOBJ)1;}
E BOOL DeleteObject(HGDIOBJ o){(void)o;return 1;}
E HGDIOBJ CreateSolidBrush(DWORD c){(void)c;return (HGDIOBJ)(next_handle+=4);}
E HGDIOBJ CreatePen(int s,int w,DWORD c){(void)s;(void)w;(void)c;return (HGDIOBJ)(next_handle+=4);}
E HGDIOBJ CreateFontA(int h,int w,int e,int o,int wt,DWORD i,DWORD u,DWORD st,DWORD cs,DWORD out,DWORD clip,DWORD q,DWORD p,const char*f){(void)h;(void)w;(void)e;(void)o;(void)wt;(void)i;(void)u;(void)st;(void)cs;(void)out;(void)clip;(void)q;(void)p;(void)f;return (HGDIOBJ)(next_handle+=4);}
E BOOL MoveToEx(HDC d,int x,int y,void*p){(void)d;(void)x;(void)y;(void)p;return 1;}
E BOOL LineTo(HDC d,int x,int y){(void)d;(void)x;(void)y;return 1;}
E BOOL Rectangle(HDC d,int l,int t,int r,int b){(void)d;(void)l;(void)t;(void)r;(void)b;return 1;}
E int FillRect(HDC d,const RECT*r,HGDIOBJ b){(void)d;(void)r;(void)b;return 1;}
E BOOL PatBlt(HDC d,int x,int y,int w,int h,DWORD rop){(void)d;(void)x;(void)y;(void)w;(void)h;(void)rop;return 1;}
E BOOL BitBlt(HDC d,int x,int y,int w,int h,HDC s,int sx,int sy,DWORD rop){(void)d;(void)x;(void)y;(void)w;(void)h;(void)s;(void)sx;(void)sy;(void)rop;return 1;}
E BOOL TextOutA(HDC d,int x,int y,const char*s,int n){(void)d;(void)x;(void)y;(void)s;(void)n;return 1;}
E int SetBkMode(HDC d,int m){(void)d;return m;}
E DWORD SetTextColor(HDC d,DWORD c){(void)d;return c;}
E DWORD SetBkColor(HDC d,DWORD c){(void)d;return c;}
E BOOL GetTextExtentPoint32A(HDC d,const char*s,int n,SIZE*z){(void)d;(void)s;if(!z)return 0;z->cx=n*8;z->cy=16;return 1;}
E int SaveDC(HDC d){(void)d;return 1;}
E BOOL RestoreDC(HDC d,int n){(void)d;(void)n;return 1;}
E int GetDeviceCaps(HDC d,int i){(void)d;return i==8?1024:i==10?768:96;}
E HDC CreateDCA(const char*a,const char*b,const char*c,const void*d){(void)a;(void)b;(void)c;(void)d;return (HDC)(next_handle+=4);}
