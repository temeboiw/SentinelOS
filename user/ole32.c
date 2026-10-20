/* ole32.dll - COM initialization, GUID helpers, and task-memory allocation.
 * COM activation is not implemented: no COM server loader/class registry exists.
 */
typedef unsigned long long u64;
typedef unsigned int DWORD;
typedef unsigned short WORD;
typedef unsigned short WCHAR;
typedef long HRESULT;
typedef void *LPVOID;
typedef const WCHAR *LPCOLESTR;
typedef WCHAR *LPOLESTR;
#define E __attribute__((dllexport))
#define S_OK ((HRESULT)0)
#define S_FALSE ((HRESULT)1)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define RPC_E_CHANGED_MODE ((HRESULT)0x80010106L)
#define RPC_E_TOO_LATE ((HRESULT)0x80010119L)
#define HEAP_BYTES (256u*1024u)
#define GUID_HEAP_MAGIC 0x434F4D48u

typedef struct { DWORD Data1; WORD Data2,Data3; unsigned char Data4[8]; } GUID_LOCAL;
typedef struct HeapBlock {u64 size;struct HeapBlock*next;DWORD free,magic;} HeapBlock;
typedef struct {DWORD cb;void *vtable;} IMalloc_LOCAL;

static unsigned char task_heap[HEAP_BYTES] __attribute__((aligned(16)));
static HeapBlock *task_first;
static int task_ready,com_init_count,com_model=-1,security_called;
static u64 guid_counter=0x9E3779B97F4A7C15ull;
static void task_init(void) {
    if(task_ready)return;task_ready=1;task_first=(HeapBlock*)task_heap;
    task_first->size=HEAP_BYTES-sizeof(HeapBlock);task_first->next=0;
    task_first->free=1;task_first->magic=GUID_HEAP_MAGIC;
}
static u64 align16(u64 n){return (n+15u)&~15ull;}
E HRESULT CoInitializeEx(LPVOID p,DWORD flags);\nE HRESULT CoInitialize(LPVOID p) {return CoInitializeEx(p,2u);}
E HRESULT CoInitializeEx(LPVOID p,DWORD flags) {
    (void)p;int model=(flags&0x2)?1:0;
    if(!com_init_count){com_init_count=1;com_model=model;return S_OK;}
    if(model!=com_model)return RPC_E_CHANGED_MODE;
    com_init_count++;return S_FALSE;
}
E void CoUninitialize(void){if(com_init_count>0&&--com_init_count==0)com_model=-1;}
E HRESULT CoInitializeSecurity(void*a,long b,void*c,void*d,DWORD e,DWORD f,void*g,DWORD h,void*i) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;
    if(security_called)return RPC_E_TOO_LATE;
    security_called=1;return S_OK; /* no RPC security transport is active */
}
E LPVOID CoTaskMemAlloc(u64 bytes) {
    task_init();if(!bytes)bytes=1;bytes=align16(bytes);
    for(HeapBlock*b=task_first;b;b=b->next)if(b->magic==GUID_HEAP_MAGIC&&b->free&&b->size>=bytes) {
        if(b->size>=bytes+sizeof(HeapBlock)+32) {
            HeapBlock*n=(HeapBlock*)((unsigned char*)(b+1)+bytes);
            n->size=b->size-bytes-sizeof(HeapBlock);n->next=b->next;
            n->free=1;n->magic=GUID_HEAP_MAGIC;b->next=n;b->size=bytes;
        }
        b->free=0;return (void*)(b+1);
    }
    return 0;
}
E void CoTaskMemFree(LPVOID p) {
    if(!p)return;task_init();HeapBlock*b=((HeapBlock*)p)-1;
    if((unsigned char*)b<task_heap||(unsigned char*)b>=task_heap+HEAP_BYTES||b->magic!=GUID_HEAP_MAGIC||b->free)return;
    b->free=1;
    for(HeapBlock*q=task_first;q&&q->next;q=q->next)if(q->free&&q->next->free&&
       (unsigned char*)(q+1)+q->size==(unsigned char*)q->next) {
        q->size+=sizeof(HeapBlock)+q->next->size;q->next=q->next->next;
    }
}
E LPVOID CoTaskMemRealloc(LPVOID p,u64 bytes) {
    if(!p)return CoTaskMemAlloc(bytes);if(!bytes){CoTaskMemFree(p);return 0;}
    HeapBlock*b=((HeapBlock*)p)-1;
    if(b->magic!=GUID_HEAP_MAGIC||b->free)return 0;
    if(b->size>=bytes)return p;
    void*n=CoTaskMemAlloc(bytes);if(!n)return 0;
    unsigned char*d=(unsigned char*)n,*s=(unsigned char*)p;
    for(u64 i=0;i<b->size;i++)d[i]=s[i];CoTaskMemFree(p);return n;
}
static int hexval(WCHAR c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
static int guid_parse(LPCOLESTR s,GUID_LOCAL*g) {
    if(!s||!g)return 0;if(*s=='{')s++;
    DWORD d1=0;WORD d2=0,d3=0;unsigned char d4[8];
    for(int i=0;i<8;i++){int h=hexval(s[i]);if(h<0)return 0;d1=(d1<<4)|(DWORD)h;}s+=8;
    if(*s++!='-')return 0;
    for(int i=0;i<4;i++){int h=hexval(s[i]);if(h<0)return 0;d2=(WORD)((d2<<4)|h);}s+=4;
    if(*s++!='-')return 0;
    for(int i=0;i<4;i++){int h=hexval(s[i]);if(h<0)return 0;d3=(WORD)((d3<<4)|h);}s+=4;
    if(*s++!='-')return 0;
    for(int i=0;i<2;i++){int a=hexval(s[i*2]),b=hexval(s[i*2+1]);if(a<0||b<0)return 0;d4[i]=(unsigned char)((a<<4)|b);}s+=4;
    if(*s++!='-')return 0;
    for(int i=0;i<6;i++){int a=hexval(s[i*2]),b=hexval(s[i*2+1]);if(a<0||b<0)return 0;d4[i+2]=(unsigned char)((a<<4)|b);}s+=12;
    if(*s=='}')s++;if(*s)return 0;
    g->Data1=d1;g->Data2=d2;g->Data3=d3;
    for(int i=0;i<8;i++)g->Data4[i]=d4[i];return 1;
}
static WCHAR hexchar(unsigned n){return (WCHAR)(n<10?'0'+n:'A'+n-10);}
static void puthex(WCHAR*out,u64 v,int digits) {for(int i=digits-1;i>=0;i--)out[digits-1-i]=hexchar((unsigned)((v>>(i*4))&15));}
E HRESULT CLSIDFromString(LPCOLESTR s,GUID_LOCAL*out) {
    return guid_parse(s,out)?S_OK:E_INVALIDARG;
}
E HRESULT IIDFromString(LPCOLESTR s,GUID_LOCAL*out) {return CLSIDFromString(s,out);}
E int IsEqualGUID(const GUID_LOCAL*a,const GUID_LOCAL*b) {
    if(!a||!b)return 0;if(a->Data1!=b->Data1||a->Data2!=b->Data2||a->Data3!=b->Data3)return 0;
    for(int i=0;i<8;i++)if(a->Data4[i]!=b->Data4[i])return 0;return 1;
}
E int StringFromGUID2(const GUID_LOCAL*g,LPOLESTR out,int cap) {
    if(!g||!out||cap<39)return 0;
    int n=0;out[n++]='{';puthex(out+n,g->Data1,8);n+=8;out[n++]='-';
    puthex(out+n,g->Data2,4);n+=4;out[n++]='-';puthex(out+n,g->Data3,4);n+=4;out[n++]='-';
    puthex(out+n,g->Data4[0],2);puthex(out+n+2,g->Data4[1],2);n+=4;out[n++]='-';
    for(int i=2;i<8;i++){puthex(out+n,g->Data4[i],2);n+=2;}
    out[n++]='}';out[n]=0;return n+1;
}
E HRESULT CoCreateGuid(GUID_LOCAL*out) {
    if(!out)return E_INVALIDARG;
    guid_counter^=guid_counter<<13;guid_counter^=guid_counter>>7;guid_counter^=guid_counter<<17;
    out->Data1=(DWORD)guid_counter;out->Data2=(WORD)(guid_counter>>32);
    out->Data3=(WORD)((guid_counter>>48)&0x0FFF)|0x4000;
    for(int i=0;i<8;i++)out->Data4[i]=(unsigned char)(guid_counter>>(i*8));
    out->Data4[0]=(unsigned char)((out->Data4[0]&0x3F)|0x80);return S_OK;
}
E HRESULT CoCreateInstance(const void*c,LPVOID o,DWORD x,const void*i,void**p) {
    (void)c;(void)o;(void)x;(void)i;if(p)*p=0;return E_NOTIMPL;
}
E HRESULT CoGetClassObject(const void*c,DWORD x,void*s,const void*i,void**p) {
    (void)c;(void)x;(void)s;(void)i;if(p)*p=0;return E_NOTIMPL;
}
E HRESULT CoGetMalloc(DWORD context,IMalloc_LOCAL **out) {(void)context;if(out)*out=0;return E_NOTIMPL;}
E void CoFreeUnusedLibraries(void) {}
