/* gdi32.dll - small but functional GDI subset backed by user32's renderer. */
typedef unsigned long long u64;
typedef unsigned int DWORD;
typedef int BOOL;
typedef void *HDC;
typedef void *HGDIOBJ;
typedef const char *LPCSTR;
typedef struct { long left,top,right,bottom; } RECT;
typedef struct { long cx,cy; } SIZE;
#define E __attribute__((dllexport))
#define MAX_DC 24
#define MAX_OBJ 96
#define MAX_SAVE 8
#define OBJ_BRUSH 1
#define OBJ_PEN 2
#define OBJ_FONT 3
#define ROP_PATCOPY 0x00F00021u
#define ROP_BLACKNESS 0x00000042u
#define ROP_WHITENESS 0x00FF0062u

static u64 next_handle=0x100;
static u64 api_fill,api_line,api_text;
typedef BOOL (*GdiFillRect_t)(int,int,int,int,DWORD);
typedef BOOL (*GdiLine_t)(int,int,int,int,DWORD);
typedef BOOL (*GdiTextOut_t)(int,int,const char*,int,DWORD);
typedef struct {int used;u64 handle;int type;DWORD color;int width;} GdiObject;
typedef struct {int used;u64 handle;u64 brush,pen,font;DWORD text,bk;int bk_mode;int x,y;} GdiState;
typedef struct {u64 brush,pen,font;DWORD text,bk;int bk_mode;int x,y;} GdiSaved;
static GdiObject objects[MAX_OBJ];
static GdiState dcs[MAX_DC];
static GdiSaved saved[MAX_DC][MAX_SAVE];
static int saved_n[MAX_DC];

static u64 sy3(u64 n,u64 a,u64 b,u64 c) {
    u64 r;
    __asm__ volatile("mov %[n], %%rax\nmov %[a], %%rdi\nmov %[b], %%rsi\nmov %[c], %%rdx\nint $0x80\nmov %%rax, %[r]"
      :[r]"=r"(r):[n]"r"(n),[a]"r"(a),[b]"r"(b),[c]"r"(c):"rax","rdi","rsi","rdx","memory","cc");
    return r;
}
static void init_api(void) {
    if(api_fill&&api_line&&api_text)return;
    char dll[]="user32.dll";
    api_fill=sy3(9,(u64)dll,(u64)"GdiFillRect",0);
    api_line=sy3(9,(u64)dll,(u64)"GdiLine",0);
    api_text=sy3(9,(u64)dll,(u64)"GdiTextOut",0);
}
static GdiState *dc_get(HDC h) {
    u64 v=(u64)h;
    for(int i=0;i<MAX_DC;i++)if(dcs[i].used&&dcs[i].handle==v)return &dcs[i];
    if(v==0)return 0;
    /* HDC 1 is the conventional default window DC for this OS. */
    if(v==1) {
        for(int i=0;i<MAX_DC;i++)if(!dcs[i].used) {
            dcs[i].used=1;dcs[i].handle=1;dcs[i].text=0xFF000000;
            dcs[i].bk=0xFFFFFFFF;dcs[i].bk_mode=1;return &dcs[i];
        }
    }
    return 0;
}
static GdiState *dc_new(void) {
    for(int i=0;i<MAX_DC;i++)if(!dcs[i].used) {
        GdiState *d=&dcs[i];d->used=1;d->handle=++next_handle;
        d->brush=0;d->pen=0;d->font=0;d->text=0xFF000000;
        d->bk=0xFFFFFFFF;d->bk_mode=1;d->x=0;d->y=0;saved_n[i]=0;
        return d;
    }
    return 0;
}
static GdiObject *obj_get(HGDIOBJ h) {
    u64 v=(u64)h;for(int i=0;i<MAX_OBJ;i++)if(objects[i].used&&objects[i].handle==v)return &objects[i];return 0;
}
static HGDIOBJ obj_new(int type,DWORD color,int width) {
    for(int i=0;i<MAX_OBJ;i++)if(!objects[i].used) {
        objects[i].used=1;objects[i].handle=++next_handle;objects[i].type=type;
        objects[i].color=color;objects[i].width=width;return (HGDIOBJ)objects[i].handle;
    }
    return 0;
}
static DWORD obj_color(u64 h,DWORD fallback) {GdiObject *o=obj_get((HGDIOBJ)h);return o?o->color:fallback;}

E HDC CreateCompatibleDC(HDC d){(void)d;GdiState*s=dc_new();return s?(HDC)s->handle:0;}
E BOOL DeleteDC(HDC h){GdiState*d=dc_get(h);if(!d||d->handle==1)return 0;d->used=0;return 1;}
E HGDIOBJ SelectObject(HDC h,HGDIOBJ o) {
    GdiState*d=dc_get(h);GdiObject*g=obj_get(o);if(!d||!g)return 0;
    u64 *slot=g->type==OBJ_BRUSH?&d->brush:g->type==OBJ_PEN?&d->pen:&d->font;
    HGDIOBJ old=(HGDIOBJ)*slot;*slot=(u64)o;return old?old:(HGDIOBJ)1;
}
E BOOL DeleteObject(HGDIOBJ h){GdiObject*o=obj_get(h);if(!o)return 0;o->used=0;return 1;}
E HGDIOBJ CreateSolidBrush(DWORD c){return obj_new(OBJ_BRUSH,c,1);}
E HGDIOBJ CreatePen(int style,int width,DWORD c){(void)style;if(width<1)width=1;return obj_new(OBJ_PEN,c,width);}
E HGDIOBJ CreateFontA(int h,int w,int e,int o,int wt,DWORD i,DWORD u,DWORD st,DWORD cs,DWORD out,DWORD clip,DWORD q,DWORD p,const char*f) {
    (void)w;(void)e;(void)o;(void)wt;(void)i;(void)u;(void)st;(void)cs;(void)out;(void)clip;(void)q;(void)p;(void)f;
    return obj_new(OBJ_FONT,0xFF000000,h<0?-h:h);
}
E BOOL MoveToEx(HDC h,int x,int y,void*old) {
    GdiState*d=dc_get(h);if(!d)return 0;
    if(old){int *p=(int*)old;p[0]=d->x;p[1]=d->y;}d->x=x;d->y=y;return 1;
}
E BOOL LineTo(HDC h,int x,int y) {
    GdiState*d=dc_get(h);if(!d)return 0;init_api();
    if(!api_line)return 0;
    int ok=((GdiLine_t)api_line)(d->x,d->y,x,y,obj_color(d->pen,0xFF000000));
    d->x=x;d->y=y;return ok;
}
E BOOL Rectangle(HDC h,int l,int t,int r,int b) {
    GdiState*d=dc_get(h);if(!d||r<=l||b<=t)return 0;init_api();
    if(!api_line)return 0;DWORD c=obj_color(d->pen,0xFF000000);
    ((GdiLine_t)api_line)(l,t,r-1,t,c);((GdiLine_t)api_line)(r-1,t,r-1,b-1,c);
    ((GdiLine_t)api_line)(r-1,b-1,l,b-1,c);((GdiLine_t)api_line)(l,b-1,l,t,c);return 1;
}
E int FillRect(HDC h,const RECT*r,HGDIOBJ b) {
    if(!r||r->right<=r->left||r->bottom<=r->top)return 0;
    GdiState*d=dc_get(h);if(!d)return 0;init_api();if(!api_fill)return 0;
    DWORD c=obj_color((u64)b? (u64)b:d->brush,0xFFFFFFFF);
    return ((GdiFillRect_t)api_fill)((int)r->left,(int)r->top,(int)(r->right-r->left),(int)(r->bottom-r->top),c);
}
E BOOL PatBlt(HDC h,int x,int y,int w,int he,DWORD rop) {
    GdiState*d=dc_get(h);if(!d||w<=0||he<=0)return 0;init_api();if(!api_fill)return 0;
    DWORD c;
    if(rop==ROP_BLACKNESS)c=0xFF000000;
    else if(rop==ROP_WHITENESS)c=0xFFFFFFFF;
    else if(rop==ROP_PATCOPY)c=obj_color(d->brush,0xFFFFFFFF);
    else return 0;
    return ((GdiFillRect_t)api_fill)(x,y,w,he,c);
}
E BOOL BitBlt(HDC d,int x,int y,int w,int h,HDC s,int sx,int sy,DWORD rop) {
    (void)d;(void)x;(void)y;(void)w;(void)h;(void)s;(void)sx;(void)sy;(void)rop;
    return 0; /* Pixel-copy backing surfaces are not implemented yet. */
}
E BOOL TextOutA(HDC h,int x,int y,const char*s,int n) {
    GdiState*d=dc_get(h);if(!d||!s)return 0;init_api();if(!api_text)return 0;
    return ((GdiTextOut_t)api_text)(x,y,s,n,d->text);
}
E int SetBkMode(HDC h,int m){GdiState*d=dc_get(h);if(!d)return 0;int old=d->bk_mode;d->bk_mode=m;return old;}
E DWORD SetTextColor(HDC h,DWORD c){GdiState*d=dc_get(h);if(!d)return 0xFFFFFFFFu;DWORD old=d->text;d->text=c;return old;}
E DWORD SetBkColor(HDC h,DWORD c){GdiState*d=dc_get(h);if(!d)return 0xFFFFFFFFu;DWORD old=d->bk;d->bk=c;return old;}
E BOOL GetTextExtentPoint32A(HDC h,const char*s,int n,SIZE*z){(void)h;if(!s||!z)return 0;if(n<0){n=0;while(s[n])n++;}z->cx=n*8;z->cy=16;return 1;}
E int SaveDC(HDC h) {
    GdiState*d=dc_get(h);if(!d)return 0;int idx=(int)(d-dcs);
    if(saved_n[idx]>=MAX_SAVE)return 0;GdiSaved*s=&saved[idx][saved_n[idx]++];
    s->brush=d->brush;s->pen=d->pen;s->font=d->font;s->text=d->text;s->bk=d->bk;s->bk_mode=d->bk_mode;s->x=d->x;s->y=d->y;
    return saved_n[idx];
}
E BOOL RestoreDC(HDC h,int level) {
    GdiState*d=dc_get(h);if(!d)return 0;int idx=(int)(d-dcs);
    int slot=level<0?saved_n[idx]+level:level-1;if(slot<0||slot>=saved_n[idx])return 0;
    GdiSaved*s=&saved[idx][slot];d->brush=s->brush;d->pen=s->pen;d->font=s->font;d->text=s->text;d->bk=s->bk;d->bk_mode=s->bk_mode;d->x=s->x;d->y=s->y;saved_n[idx]=slot;return 1;
}
E int GetDeviceCaps(HDC h,int index){(void)h;return index==8?1024:index==10?768:96;}
E HDC CreateDCA(const char*a,const char*b,const char*c,const void*d){(void)a;(void)b;(void)c;(void)d;GdiState*s=dc_new();return s?(HDC)s->handle:0;}
