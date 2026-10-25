/*
 * SentinelOS Winsock 2 compatibility runtime.
 *
 * This file deliberately distinguishes implemented Winsock utility semantics
 * from the absent kernel packet/socket backend. It never reports a remote
 * socket as successfully created or connected when packets cannot be sent.
 */
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned int DWORD;
typedef unsigned long long ULONG_PTR;
typedef unsigned long long SOCKET;
typedef int BOOL;
typedef unsigned int UINT;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef long long i64;
typedef void *HANDLE;

#define INVALID_SOCKET ((SOCKET)~(SOCKET)0)
#define SOCKET_ERROR (-1)
#define AF_UNSPEC 0
#define AF_INET 2
#define AF_INET6 23
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define WSAEINTR 10004
#define WSAEBADF 10009
#define WSAEACCES 10013
#define WSAEFAULT 10014
#define WSAEINVAL 10022
#define WSAEMFILE 10024
#define WSAEWOULDBLOCK 10035
#define WSAEINPROGRESS 10036
#define WSAEALREADY 10037
#define WSAENOTSOCK 10038
#define WSAEDESTADDRREQ 10039
#define WSAEMSGSIZE 10040
#define WSAEPROTOTYPE 10041
#define WSAENOPROTOOPT 10042
#define WSAEPROTONOSUPPORT 10043
#define WSAESOCKTNOSUPPORT 10044
#define WSAEOPNOTSUPP 10045
#define WSAEPFNOSUPPORT 10046
#define WSAEAFNOSUPPORT 10047
#define WSAEADDRINUSE 10048
#define WSAEADDRNOTAVAIL 10049
#define WSAENETDOWN 10050
#define WSAENETUNREACH 10051
#define WSAENETRESET 10052
#define WSAECONNABORTED 10053
#define WSAECONNRESET 10054
#define WSAENOBUFS 10055
#define WSAEISCONN 10056
#define WSAENOTCONN 10057
#define WSAESHUTDOWN 10058
#define WSAETIMEDOUT 10060
#define WSAECONNREFUSED 10061
#define WSAEHOSTUNREACH 10065
#define WSANOTINITIALISED 10093
#define WSAHOST_NOT_FOUND 11001
#define WSATRY_AGAIN 11002
#define WSANO_RECOVERY 11003
#define WSANO_DATA 11004

typedef struct {
    WORD wVersion;
    WORD wHighVersion;
    char szDescription[257];
    char szSystemStatus[129];
    WORD iMaxSockets;
    WORD iMaxUdpDg;
    char *lpVendorInfo;
} WSADATA;

typedef struct { short sin_family; u16 sin_port; u32 sin_addr; char sin_zero[8]; } SOCKADDR_IN;
typedef struct { short sa_family; char sa_data[14]; } SOCKADDR;
typedef struct { u32 s_addr; } IN_ADDR;
typedef struct { u8 s6_addr[16]; } IN6_ADDR;

static int ws2_started;
static unsigned int ws2_start_refs;
static int ws2_last_error;
static char ws2_ntoa_buf[4][16];
static unsigned int ws2_ntoa_slot;

static void ws2_copy(char *d, const char *s, unsigned int n) {
    unsigned int i=0;
    if (!d || !n) return;
    if (s) { while (i+1<n && s[i]) { d[i]=s[i]; i++; } }
    d[i]=0;
    while (++i<n) d[i]=0;
}
static int ws2_eq(const char *a,const char *b) {
    if (!a || !b) return 0;
    while (*a && *a==*b) { a++; b++; }
    return *a==*b;
}
static int ws2_digit(char c) { return c>='0' && c<='9'; }
static int ws2_hex(char c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}

__attribute__((dllexport)) int WSAStartup(WORD version, WSADATA *data) {
    unsigned int requested=version;
    if (!data) return WSAEFAULT;
    if ((requested & 255u)<1 || (requested & 255u)>2 || (requested>>8)>2)
        return 10092; /* WSAVERNOTSUPPORTED */
    data->wVersion=0x0202;
    data->wHighVersion=0x0202;
    ws2_copy(data->szDescription,"SentinelOS Winsock 2.2",sizeof(data->szDescription));
    ws2_copy(data->szSystemStatus,"Winsock utilities available; kernel network backend unavailable",sizeof(data->szSystemStatus));
    data->iMaxSockets=0;
    data->iMaxUdpDg=0;
    data->lpVendorInfo=0;
    ws2_started=1;
    ws2_start_refs++;
    ws2_last_error=0;
    return 0;
}
__attribute__((dllexport)) int WSACleanup(void) {
    if (!ws2_started || !ws2_start_refs) { ws2_last_error=WSANOTINITIALISED; return SOCKET_ERROR; }
    if (--ws2_start_refs==0) ws2_started=0;
    ws2_last_error=0;
    return 0;
}
__attribute__((dllexport)) int WSAGetLastError(void) { return ws2_last_error; }
__attribute__((dllexport)) void WSASetLastError(int error) { ws2_last_error=error; }
__attribute__((dllexport)) int WSAIsBlocking(void) { return 0; }
__attribute__((dllexport)) int WSAGetLastErrorCompat(void) { return ws2_last_error; }

__attribute__((dllexport)) u16 htons(u16 x) { return (u16)((x<<8)|(x>>8)); }
__attribute__((dllexport)) u16 ntohs(u16 x) { return htons(x); }
__attribute__((dllexport)) u32 htonl(u32 x) { return ((x&0x000000ffu)<<24)|((x&0x0000ff00u)<<8)|((x&0x00ff0000u)>>8)|((x&0xff000000u)>>24); }
__attribute__((dllexport)) u32 ntohl(u32 x) { return htonl(x); }

__attribute__((dllexport)) u32 inet_addr(const char *s) {
    u32 oct[4]={0,0,0,0}; unsigned int part=0,digits=0;
    if(!s || !*s) return 0xffffffffu;
    while(*s) {
        if(ws2_digit(*s)) {
            oct[part]=oct[part]*10u+(unsigned int)(*s-'0');
            if(oct[part]>255u) return 0xffffffffu;
            digits++;
        } else if(*s=='.' && part<3 && digits) { part++; digits=0; }
        else return 0xffffffffu;
        s++;
    }
    if(part!=3 || !digits) return 0xffffffffu;
    return (oct[0]<<24)|(oct[1]<<16)|(oct[2]<<8)|oct[3];
}
__attribute__((dllexport)) const char *inet_ntoa(IN_ADDR addr) {
    char *out=ws2_ntoa_buf[(ws2_ntoa_slot++)&3u];
    u32 a=addr.s_addr;
    unsigned int oct[4]={(a>>24)&255u,(a>>16)&255u,(a>>8)&255u,a&255u};
    unsigned int p=0;
    for(unsigned int i=0;i<4;i++) {
        unsigned int v=oct[i],div=100,started=0;
        while(div) { unsigned int d=v/div; if(d||started||div==1){out[p++]=(char)('0'+d);started=1;} v%=div;div/=10; }
        if(i!=3)out[p++]='.';
    }
    out[p]=0; return out;
}
__attribute__((dllexport)) int inet_pton(int family, const char *src, void *dst) {
    if(!src || !dst) { ws2_last_error=WSAEFAULT; return -1; }
    if(family==AF_INET) {
        u32 value=inet_addr(src);
        if(value==0xffffffffu && !ws2_eq(src,"255.255.255.255")) return 0;
        u8 *p=(u8*)dst; p[0]=(u8)(value>>24);p[1]=(u8)(value>>16);p[2]=(u8)(value>>8);p[3]=(u8)value;
        return 1;
    }
    if(family!=AF_INET6) { ws2_last_error=WSAEAFNOSUPPORT; return -1; }
    /* Strict IPv6 parser including one optional :: compression. */
    u16 words[8]={0}; unsigned int n=0,compress=8;
    const char *p=src;
    if(*p==':' && p[1]==':'){compress=0;p+=2;}
    while(*p) {
        if(n>=8) return 0;
        unsigned int v=0,d=0;
        while(ws2_hex(*p)>=0) { if(d==4)return 0; v=(v<<4)|(unsigned int)ws2_hex(*p++);d++; }
        if(!d)return 0;
        words[n++]=(u16)v;
        if(!*p)break;
        if(*p!=':')return 0;
        p++;
        if(*p==':') { if(compress!=8)return 0; compress=n;p++;if(!*p)break; }
    }
    if(compress==8 && n!=8)return 0;
    if(compress!=8) {
        if(n>=8)return 0;
        unsigned int zeros=8-n;
        for(unsigned int i=n;i>compress;i--)words[i+zeros-1]=words[i-1];
        for(unsigned int i=compress;i<compress+zeros;i++)words[i]=0;
    }
    u8 *o=(u8*)dst; for(unsigned int i=0;i<8;i++){o[i*2]=(u8)(words[i]>>8);o[i*2+1]=(u8)words[i];}
    return 1;
}
__attribute__((dllexport)) const char *inet_ntop(int family,const void *src,char *dst,unsigned long long size) {
    if(!src||!dst){ws2_last_error=WSAEFAULT;return 0;}
    if(family==AF_INET) {
        const u8 *p=(const u8*)src; IN_ADDR a; a.s_addr=((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];
        const char *s=inet_ntoa(a); unsigned int n=0;while(s[n])n++;
        if(size<=n){ws2_last_error=WSAEFAULT;return 0;} ws2_copy(dst,s,(unsigned int)size);return dst;
    }
    if(family==AF_INET6) {
        const u8 *p=(const u8*)src; unsigned int used=0;
        for(unsigned int i=0;i<8;i++) {
            u16 v=(u16)(((u16)p[i*2]<<8)|p[i*2+1]);
            char tmp[5]; unsigned int n=0;
            do { unsigned int d=v&15u;tmp[n++]=(char)(d<10?'0'+d:'a'+d-10);v>>=4; } while(v);
            if(used+1+n>=size){ws2_last_error=WSAEFAULT;return 0;}
            if(i)dst[used++]=':';
            while(n)dst[used++]=tmp[--n];
        }
        dst[used]=0;return dst;
    }
    ws2_last_error=WSAEAFNOSUPPORT;return 0;
}
__attribute__((dllexport)) int gethostname(char *name,int len) {
    if(!name||len<=0){ws2_last_error=WSAEFAULT;return SOCKET_ERROR;}
    if(len<10){ws2_last_error=WSAEFAULT;return SOCKET_ERROR;}
    ws2_copy(name,"sentinel", (unsigned int)len);ws2_last_error=0;return 0;
}
__attribute__((dllexport)) int getaddrinfo(const char *node,const char *service,const void *hints,void **result) {
    (void)node;(void)service;(void)hints;
    if(result)*result=0;
    ws2_last_error=WSANOTINITIALISED;
    return WSANOTINITIALISED;
}
__attribute__((dllexport)) void freeaddrinfo(void *result) { (void)result; }
__attribute__((dllexport)) int getnameinfo(const SOCKADDR *sa,unsigned int salen,char *host,unsigned int hostlen,char *serv,unsigned int servlen,int flags) {
    (void)sa;(void)salen;(void)host;(void)hostlen;(void)serv;(void)servlen;(void)flags;
    ws2_last_error=WSANOTINITIALISED;return WSANOTINITIALISED;
}

/*
 * Socket operations fail explicitly until the kernel exposes packet I/O and
 * per-process socket handles. These are not fake successful sockets.
 */
static SOCKET ws2_no_socket(void) { ws2_last_error=ws2_started?WSAENETDOWN:WSANOTINITIALISED; return INVALID_SOCKET; }
static int ws2_no_io(void) { ws2_last_error=ws2_started?WSAENETDOWN:WSANOTINITIALISED; return SOCKET_ERROR; }
__attribute__((dllexport)) SOCKET socket(int af,int type,int protocol) { (void)af;(void)type;(void)protocol;return ws2_no_socket(); }
__attribute__((dllexport)) int closesocket(SOCKET s) { (void)s;return ws2_no_io(); }
__attribute__((dllexport)) int connect(SOCKET s,const SOCKADDR *a,int n) { (void)s;(void)a;(void)n;return ws2_no_io(); }
__attribute__((dllexport)) int bind(SOCKET s,const SOCKADDR *a,int n) { (void)s;(void)a;(void)n;return ws2_no_io(); }
__attribute__((dllexport)) int listen(SOCKET s,int backlog) { (void)s;(void)backlog;return ws2_no_io(); }
__attribute__((dllexport)) SOCKET accept(SOCKET s,SOCKADDR *a,int *n) { (void)s;(void)a;(void)n;return ws2_no_socket(); }
__attribute__((dllexport)) int send(SOCKET s,const char *b,int n,int f) { (void)s;(void)b;(void)n;(void)f;return ws2_no_io(); }
__attribute__((dllexport)) int recv(SOCKET s,char *b,int n,int f) { (void)s;(void)b;(void)n;(void)f;return ws2_no_io(); }
__attribute__((dllexport)) int sendto(SOCKET s,const char *b,int n,int f,const SOCKADDR *a,int al) { (void)s;(void)b;(void)n;(void)f;(void)a;(void)al;return ws2_no_io(); }
__attribute__((dllexport)) int recvfrom(SOCKET s,char *b,int n,int f,SOCKADDR *a,int *al) { (void)s;(void)b;(void)n;(void)f;(void)a;(void)al;return ws2_no_io(); }
__attribute__((dllexport)) int shutdown(SOCKET s,int how) { (void)s;(void)how;return ws2_no_io(); }
__attribute__((dllexport)) int select(int nfds,void *r,void *w,void *e,const void *t) { (void)nfds;(void)r;(void)w;(void)e;(void)t;return ws2_no_io(); }
__attribute__((dllexport)) int ioctlsocket(SOCKET s,long cmd,unsigned long *arg) { (void)s;(void)cmd;(void)arg;return ws2_no_io(); }
__attribute__((dllexport)) int setsockopt(SOCKET s,int level,int opt,const char *v,int n) { (void)s;(void)level;(void)opt;(void)v;(void)n;return ws2_no_io(); }
__attribute__((dllexport)) int getsockopt(SOCKET s,int level,int opt,char *v,int *n) { (void)s;(void)level;(void)opt;(void)v;(void)n;return ws2_no_io(); }
__attribute__((dllexport)) int getsockname(SOCKET s,SOCKADDR *a,int *n) { (void)s;(void)a;(void)n;return ws2_no_io(); }
__attribute__((dllexport)) int getpeername(SOCKET s,SOCKADDR *a,int *n) { (void)s;(void)a;(void)n;return ws2_no_io(); }
__attribute__((dllexport)) int WSAIoctl(SOCKET s,DWORD c,void *in,DWORD il,void *out,DWORD ol,DWORD *ret,void *ov,void *cb) { (void)s;(void)c;(void)in;(void)il;(void)out;(void)ol;if(ret)*ret=0;(void)ov;(void)cb;return ws2_no_io(); }
__attribute__((dllexport)) int WSAEventSelect(SOCKET s,HANDLE e,long events) { (void)s;(void)e;(void)events;return ws2_no_io(); }
__attribute__((dllexport)) HANDLE WSACreateEvent(void) { ws2_last_error=WSAENETDOWN;return (HANDLE)0; }
__attribute__((dllexport)) BOOL WSACloseEvent(HANDLE e) { (void)e;ws2_last_error=WSAENETDOWN;return 0; }
__attribute__((dllexport)) int WSAWaitForMultipleEvents(DWORD n,const HANDLE *e,BOOL all,DWORD ms,BOOL alert) { (void)n;(void)e;(void)all;(void)ms;(void)alert;ws2_last_error=WSAENETDOWN;return -1; }
__attribute__((dllexport)) int WSAEnumNetworkEvents(SOCKET s,HANDLE e,void *events) { (void)s;(void)e;(void)events;return ws2_no_io(); }
__attribute__((dllexport)) int WSAAsyncSelect(SOCKET s,void *w,unsigned int msg,long events) { (void)s;(void)w;(void)msg;(void)events;return ws2_no_io(); }
__attribute__((dllexport)) int WSAGetLastErrorCompat2(void) { return ws2_last_error; }
