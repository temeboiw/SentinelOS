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

#define SYS_NtSocketCreate 44
#define SYS_NtSocketClose 45
#define SYS_NtSocketBind 46
#define SYS_NtSocketSendTo 47
#define SYS_NtSocketRecvFrom 48
static unsigned long long ws2_syscall6(unsigned long long n,unsigned long long a,unsigned long long b,unsigned long long c,unsigned long long d,unsigned long long e,unsigned long long f) {
    unsigned long long r;
    __asm__ volatile(
        "mov %[n], %%rax\nmov %[a], %%rdi\nmov %[b], %%rsi\nmov %[c], %%rdx\nmov %[d], %%rcx\nmov %[e], %%r8\nmov %[f], %%r9\nint $0x80\nmov %%rax, %[r]"
        : [r]"=r"(r)
        : [n]"r"(n),[a]"r"(a),[b]"r"(b),[c]"r"(c),[d]"r"(d),[e]"r"(e),[f]"r"(f)
        : "rax","rdi","rsi","rdx","rcx","r8","r9","memory","cc");
    return r;
}
static unsigned long long ws2_syscall3(unsigned long long n,unsigned long long a,unsigned long long b,unsigned long long c) {
    return ws2_syscall6(n,a,b,c,0,0,0);
}
typedef struct { unsigned long long s; SOCKADDR_IN peer; int connected; } WS2_PEER;
static WS2_PEER ws2_peers[32];
static int ws2_peer_slot(SOCKET s,int create) {
    for(int i=0;i<32;i++) if(ws2_peers[i].s==s && ws2_peers[i].s) return i;
    if(create) for(int i=0;i<32;i++) if(!ws2_peers[i].s) { ws2_peers[i].s=s; return i; }
    return -1;
}

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

/* Legacy Winsock database calls have explicit failure semantics until the
 * SentinelOS DNS/protocol/service database is implemented. */
typedef struct { char *h_name; char **h_aliases; short h_addrtype; short h_length; char **h_addr_list; } WS2_HOSTENT;
typedef struct { char *s_name; char **s_aliases; short s_port; char *s_proto; } WS2_SERVENT;
typedef struct { char *p_name; char **p_aliases; short p_proto; } WS2_PROTOENT;
__attribute__((dllexport)) WS2_HOSTENT *gethostbyaddr(const char *addr,int len,int type) {
    (void)addr;(void)len;(void)type;ws2_last_error=WSAHOST_NOT_FOUND;return 0;
}
__attribute__((dllexport)) WS2_HOSTENT *gethostbyname(const char *name) {
    (void)name;ws2_last_error=WSAHOST_NOT_FOUND;return 0;
}
__attribute__((dllexport)) WS2_PROTOENT *getprotobyname(const char *name) {
    (void)name;ws2_last_error=WSAEOPNOTSUPP;return 0;
}
__attribute__((dllexport)) WS2_PROTOENT *getprotobynumber(int number) {
    (void)number;ws2_last_error=WSAEOPNOTSUPP;return 0;
}
__attribute__((dllexport)) WS2_SERVENT *getservbyname(const char *name,const char *proto) {
    (void)name;(void)proto;ws2_last_error=WSAEOPNOTSUPP;return 0;
}
__attribute__((dllexport)) WS2_SERVENT *getservbyport(int port,const char *proto) {
    (void)port;(void)proto;ws2_last_error=WSAEOPNOTSUPP;return 0;
}
__attribute__((dllexport)) int __WSAFDIsSet(SOCKET s, void *set) {
    if(!set)return 0;
    struct ws2_fd_set_min { unsigned int count; SOCKET sockets[64]; };
    struct ws2_fd_set_min *fds=(struct ws2_fd_set_min *)set;
    unsigned int count=fds->count>64?64:fds->count;
    for(unsigned int i=0;i<count;i++)if(fds->sockets[i]==s)return 1;
    return 0;
}

__attribute__((dllexport)) int WSAStartup(WORD version, WSADATA *data) {
    unsigned int requested=version;
    if (!data) return WSAEFAULT;
    if ((requested & 255u)<1 || (requested & 255u)>2 || (requested>>8)>2)
        return 10092; /* WSAVERNOTSUPPORTED */
    data->wVersion=(WORD)requested;
    data->wHighVersion=0x0202;
    ws2_copy(data->szDescription,"SentinelOS Winsock 2.2",sizeof(data->szDescription));
    ws2_copy(data->szSystemStatus,"SentinelOS Winsock: IPv4 UDP available; TCP pending",sizeof(data->szSystemStatus));
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
    return (oct[0])|(oct[1]<<8)|(oct[2]<<16)|(oct[3]<<24);
}
__attribute__((dllexport)) const char *inet_ntoa(IN_ADDR addr) {
    char *out=ws2_ntoa_buf[(ws2_ntoa_slot++)&3u];
    u32 a=addr.s_addr;
    unsigned int oct[4]={a&255u,(a>>8)&255u,(a>>16)&255u,(a>>24)&255u};
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
        u8 *p=(u8*)dst; p[0]=(u8)value;p[1]=(u8)(value>>8);p[2]=(u8)(value>>16);p[3]=(u8)(value>>24);
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
    if(len<9){ws2_last_error=WSAEFAULT;return SOCKET_ERROR;}
    ws2_copy(name,"sentinel", (unsigned int)len);ws2_last_error=0;return 0;
}
__attribute__((dllexport)) SOCKET socket(int af,int type,int protocol);
__attribute__((dllexport)) int closesocket(SOCKET s);
__attribute__((dllexport)) int sendto(SOCKET s,const char *b,int n,int f,const SOCKADDR *a,int al);
__attribute__((dllexport)) int recvfrom(SOCKET s,char *b,int n,int f,SOCKADDR *a,int *al);
typedef struct WS2_ADDRINFO {
    int ai_flags,ai_family,ai_socktype,ai_protocol;
    unsigned long ai_addrlen;
    char *ai_canonname;
    SOCKADDR *ai_addr;
    struct WS2_ADDRINFO *ai_next;
} WS2_ADDRINFO;
typedef struct { SOCKADDR_IN addr; } WS2_ADDR_NODE;
static WS2_ADDRINFO ws2_addrinfo_nodes[8];
static WS2_ADDR_NODE ws2_addr_nodes[8];
static char ws2_canon_nodes[8][256];
static unsigned ws2_addrinfo_used;
static int ws2_parse_service(const char *s,unsigned short *port) {
    unsigned v=0;if(!s||!*s){*port=0;return 1;}
    for(;*s;s++){if(*s<'0'||*s>'9')return 0;v=v*10u+(unsigned)(*s-'0');if(v>65535)return 0;}
    *port=(unsigned short)v;return 1;
}
__attribute__((dllexport)) int getaddrinfo(const char *node,const char *service,const void *hints,void **result) {
    if(result)*result=0;
    if(!ws2_started){ws2_last_error=WSANOTINITIALISED;return WSANOTINITIALISED;}
    if(!result||!node){ws2_last_error=WSAEFAULT;return WSAEINVAL;}
    unsigned short port;if(!ws2_parse_service(service,&port)){ws2_last_error=WSAEINVAL;return WSAEINVAL;}
    const unsigned char *hint=(const unsigned char *)hints;
    int family=hint?*(const int *)(hint+4):AF_INET;
    int stype=hint?*(const int *)(hint+8):0;
    int proto=hint?*(const int *)(hint+12):0;
    if(family!=AF_UNSPEC&&family!=AF_INET){ws2_last_error=WSAEAFNOSUPPORT;return WSAEAFNOSUPPORT;}
    unsigned int ip=inet_addr(node);
    if(ip==0xFFFFFFFFu && !ws2_eq(node,"255.255.255.255")) {
        /* QEMU user networking exposes its DNS proxy at 10.0.2.3:53. */
        unsigned char query[512];unsigned qn=12;static unsigned short txid=0x4A31;
        unsigned short id=++txid;
        query[0]=(unsigned char)(id>>8);query[1]=(unsigned char)id;
        query[2]=1;query[3]=0;query[4]=0;query[5]=1;
        query[6]=query[7]=query[8]=query[9]=query[10]=query[11]=0;
        const char *p=node;
        while(*p) {
            const char *dot=p;unsigned lab=0;
            while(*dot&&*dot!='.') {dot++;lab++;}
            if(!lab||lab>63||qn+lab+1+5>sizeof(query)){ws2_last_error=WSAHOST_NOT_FOUND;return WSAHOST_NOT_FOUND;}
            query[qn++]=(unsigned char)lab;
            while(p<dot)query[qn++]=(unsigned char)*p++;
            if(*p=='.')p++;
            if(!*p&&p[-1]=='.')break;
        }
        query[qn++]=0;query[qn++]=0;query[qn++]=1;query[qn++]=0;query[qn++]=1;
        SOCKET ds=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if(ds==INVALID_SOCKET){ws2_last_error=WSAHOST_NOT_FOUND;return WSAHOST_NOT_FOUND;}
        SOCKADDR_IN dns;dns.sin_family=AF_INET;dns.sin_port=htons(53);dns.sin_addr=0x0302000Au;
        for(unsigned z=0;z<8;z++)dns.sin_zero[z]=0;
        int sent=sendto(ds,(const char *)query,(int)qn,0,(const SOCKADDR *)&dns,sizeof(dns));
        if(sent<0){closesocket(ds);ws2_last_error=WSAHOST_NOT_FOUND;return WSAHOST_NOT_FOUND;}
        unsigned char reply[1024];SOCKADDR_IN from;int fromlen=sizeof(from);int rn=-1;
        for(unsigned tries=0;tries<250000;tries++) {
            fromlen=sizeof(from);
            rn=recvfrom(ds,(char *)reply,sizeof(reply),0,(SOCKADDR *)&from,&fromlen);
            if(rn>=0)break;
        }
        closesocket(ds);
        if(rn<12||reply[0]!=(unsigned char)(id>>8)||reply[1]!=(unsigned char)id||
           !(reply[2]&0x80)||(reply[3]&0x0F)||!((reply[6]<<8)|reply[7])) {
            ws2_last_error=WSAHOST_NOT_FOUND;return WSAHOST_NOT_FOUND;
        }
        unsigned answers=((unsigned)reply[6]<<8)|reply[7];unsigned off=12;
        /* Skip the encoded question name; compression pointers are accepted. */
        while(off<(unsigned)rn&&reply[off]) {
            if((reply[off]&0xC0)==0xC0){off+=2;break;}
            off+=(unsigned)reply[off]+1;
        }
        if(off<(unsigned)rn&&reply[off]==0)off++;
        if(off+4>(unsigned)rn){ws2_last_error=WSAHOST_NOT_FOUND;return WSAHOST_NOT_FOUND;}
        off+=4;ip=0;int found=0;
        for(unsigned ai=0;ai<answers&&off+12<=(unsigned)rn;ai++) {
            if((reply[off]&0xC0)==0xC0)off+=2;
            else {while(off<(unsigned)rn&&reply[off]){unsigned l=reply[off];if((l&0xC0)==0xC0){off+=2;break;}off+=l+1;}if(off<(unsigned)rn&&reply[off]==0)off++;}
            if(off+10>(unsigned)rn)break;
            unsigned typ=((unsigned)reply[off]<<8)|reply[off+1];
            unsigned cls=((unsigned)reply[off+2]<<8)|reply[off+3];
            unsigned rdlen=((unsigned)reply[off+8]<<8)|reply[off+9];off+=10;
            if(off+rdlen>(unsigned)rn)break;
            if(typ==1&&cls==1&&rdlen==4){ip=(unsigned)reply[off]|((unsigned)reply[off+1]<<8)|((unsigned)reply[off+2]<<16)|((unsigned)reply[off+3]<<24);found=1;break;}
            off+=rdlen;
        }
        if(!found){ws2_last_error=WSAHOST_NOT_FOUND;return WSAHOST_NOT_FOUND;}
    }
    unsigned slot=ws2_addrinfo_used++%8u;
    WS2_ADDRINFO *ai=&ws2_addrinfo_nodes[slot];WS2_ADDR_NODE *an=&ws2_addr_nodes[slot];
    an->addr.sin_family=AF_INET;an->addr.sin_port=htons(port);an->addr.sin_addr=ip;
    for(unsigned i=0;i<8;i++)an->addr.sin_zero[i]=0;
    ai->ai_flags=hint?*(const int *)hint:0;ai->ai_family=AF_INET;ai->ai_socktype=stype;ai->ai_protocol=proto;
    ai->ai_addrlen=sizeof(SOCKADDR_IN);ai->ai_addr=(SOCKADDR *)&an->addr;ai->ai_next=0;
    ai->ai_canonname=0;
    if(ai->ai_flags&2){unsigned i=0;for(;node[i]&&i<255;i++)ws2_canon_nodes[slot][i]=node[i];ws2_canon_nodes[slot][i]=0;ai->ai_canonname=ws2_canon_nodes[slot];}
    *result=ai;ws2_last_error=0;return 0;
}
__attribute__((dllexport)) void freeaddrinfo(void *result) { (void)result; }
__attribute__((dllexport)) int getnameinfo(const SOCKADDR *sa,unsigned int salen,char *host,unsigned int hostlen,char *serv,unsigned int servlen,int flags) {
    (void)flags;
    if(!ws2_started){ws2_last_error=WSANOTINITIALISED;return WSANOTINITIALISED;}
    if(!sa||salen<sizeof(SOCKADDR_IN)){ws2_last_error=WSAEFAULT;return WSAEINVAL;}
    const SOCKADDR_IN *in=(const SOCKADDR_IN *)sa;
    if(in->sin_family!=AF_INET){ws2_last_error=WSAEAFNOSUPPORT;return WSAEAFNOSUPPORT;}
    if(host&&hostlen){char tmp[16];if(!inet_ntop(AF_INET,&in->sin_addr,tmp,sizeof(tmp))){ws2_last_error=WSAEFAULT;return WSAEINVAL;}
        unsigned i=0;while(tmp[i]&&i+1<hostlen){host[i]=tmp[i];i++;}if(tmp[i]){ws2_last_error=WSAEFAULT;return WSAEINVAL;}host[i]=0;}
    if(serv&&servlen){unsigned p=ntohs(in->sin_port);char tmp[6];unsigned i=0;if(!p)tmp[i++]='0';else{char rev[6];unsigned j=0;while(p){rev[j++]=(char)('0'+p%10);p/=10;}while(j)tmp[i++]=rev[--j];}tmp[i]=0;
        unsigned j=0;while(tmp[j]&&j+1<servlen){serv[j]=tmp[j];j++;}if(tmp[j]){ws2_last_error=WSAEFAULT;return WSAEINVAL;}serv[j]=0;}
    ws2_last_error=0;return 0;
}

/*
 * UDP sockets use the kernel's per-process socket table and Ethernet/IPv4/UDP
 * stack. TCP remains unsupported until the kernel TCP state machine exists.
 */
static SOCKET ws2_no_socket(void) { ws2_last_error=ws2_started?WSAEOPNOTSUPP:WSANOTINITIALISED; return INVALID_SOCKET; }
static int ws2_no_io(void) { ws2_last_error=ws2_started?WSAEOPNOTSUPP:WSANOTINITIALISED; return SOCKET_ERROR; }
__attribute__((dllexport)) SOCKET socket(int af,int type,int protocol) {
    if(!ws2_started){ws2_last_error=WSANOTINITIALISED;return INVALID_SOCKET;}
    if(af!=AF_INET||(type!=SOCK_DGRAM&&type!=SOCK_STREAM)||(protocol&&protocol!=(type==SOCK_DGRAM?IPPROTO_UDP:IPPROTO_TCP))){ws2_last_error=WSAEAFNOSUPPORT;return INVALID_SOCKET;}
    if(type==SOCK_STREAM){ws2_last_error=WSAEOPNOTSUPP;return INVALID_SOCKET;}
    SOCKET s=(SOCKET)ws2_syscall3(SYS_NtSocketCreate,af,type,protocol);
    if(s==INVALID_SOCKET){ws2_last_error=WSAENOBUFS;return INVALID_SOCKET;}
    (void)ws2_peer_slot(s,1);ws2_last_error=0;return s;
}
__attribute__((dllexport)) int closesocket(SOCKET s) {
    if(ws2_syscall3(SYS_NtSocketClose,s,0,0)!=(unsigned long long)0){ws2_last_error=WSAENOTSOCK;return SOCKET_ERROR;}
    int p=ws2_peer_slot(s,0);if(p>=0){ws2_peers[p].s=0;ws2_peers[p].connected=0;}
    ws2_last_error=0;return 0;
}
__attribute__((dllexport)) int connect(SOCKET s,const SOCKADDR *a,int n) {
    if(!ws2_started){ws2_last_error=WSANOTINITIALISED;return SOCKET_ERROR;}
    if(!a||n<(int)sizeof(SOCKADDR_IN)||a->sa_family!=AF_INET){ws2_last_error=WSAEFAULT;return SOCKET_ERROR;}
    int p=ws2_peer_slot(s,0);if(p<0){ws2_last_error=WSAENOTSOCK;return SOCKET_ERROR;}
    ws2_peers[p].peer=*(const SOCKADDR_IN *)a;ws2_peers[p].connected=1;ws2_last_error=0;return 0;
}
__attribute__((dllexport)) int bind(SOCKET s,const SOCKADDR *a,int n) {
    if(!ws2_started){ws2_last_error=WSANOTINITIALISED;return SOCKET_ERROR;}
    if(!a||n<(int)sizeof(SOCKADDR_IN)||a->sa_family!=AF_INET){ws2_last_error=WSAEFAULT;return SOCKET_ERROR;}
    int r=(int)ws2_syscall6(SYS_NtSocketBind,s,(unsigned long long)a,(unsigned long long)n,0,0,0);
    if(r<0){ws2_last_error=WSAEADDRINUSE;return SOCKET_ERROR;}ws2_last_error=0;return 0;
}
__attribute__((dllexport)) int listen(SOCKET s,int backlog) { (void)s;(void)backlog;return ws2_no_io(); }
__attribute__((dllexport)) SOCKET accept(SOCKET s,SOCKADDR *a,int *n) { (void)s;(void)a;(void)n;return ws2_no_socket(); }
__attribute__((dllexport)) int sendto(SOCKET s,const char *b,int n,int f,const SOCKADDR *a,int al);
__attribute__((dllexport)) int send(SOCKET s,const char *b,int n,int f) {
    (void)f;int p=ws2_peer_slot(s,0);if(p<0||!ws2_peers[p].connected){ws2_last_error=WSAEDESTADDRREQ;return SOCKET_ERROR;}
    return sendto(s,b,n,0,(const SOCKADDR *)&ws2_peers[p].peer,sizeof(SOCKADDR_IN));
}
__attribute__((dllexport)) int recvfrom(SOCKET s,char *b,int n,int f,SOCKADDR *a,int *al);
__attribute__((dllexport)) int recv(SOCKET s,char *b,int n,int f) { (void)f;return recvfrom(s,b,n,0,0,0); }
__attribute__((dllexport)) int sendto(SOCKET s,const char *b,int n,int f,const SOCKADDR *a,int al) {
    (void)f;if(!ws2_started){ws2_last_error=WSANOTINITIALISED;return SOCKET_ERROR;}
    if(n<0||(!b&&n)||!a||al<(int)sizeof(SOCKADDR_IN)||a->sa_family!=AF_INET){ws2_last_error=WSAEFAULT;return SOCKET_ERROR;}
    int r=(int)ws2_syscall6(SYS_NtSocketSendTo,s,(unsigned long long)b,(unsigned long long)n,0,(unsigned long long)a,(unsigned long long)al);
    if(r<0){ws2_last_error=(r==-3)?WSAEWOULDBLOCK:WSAENETUNREACH;return SOCKET_ERROR;}
    ws2_last_error=0;return r;
}
__attribute__((dllexport)) int recvfrom(SOCKET s,char *b,int n,int f,SOCKADDR *a,int *al) {
    (void)f;if(!ws2_started){ws2_last_error=WSANOTINITIALISED;return SOCKET_ERROR;}
    if(n<0||(!b&&n)){ws2_last_error=WSAEFAULT;return SOCKET_ERROR;}
    int r=(int)ws2_syscall6(SYS_NtSocketRecvFrom,s,(unsigned long long)b,(unsigned long long)n,0,(unsigned long long)a,(unsigned long long)al);
    if(r<0){ws2_last_error=WSAENOTSOCK;return SOCKET_ERROR;}
    if(r==0){ws2_last_error=WSAEWOULDBLOCK;return SOCKET_ERROR;}
    ws2_last_error=0;return r;
}
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

typedef struct { unsigned long len; char *buf; } WS2_BUF;
typedef struct WS2_ADDRINFOW {
 int ai_flags,ai_family,ai_socktype,ai_protocol;
 unsigned long ai_addrlen;
 unsigned short *ai_canonname;
 SOCKADDR *ai_addr;
 struct WS2_ADDRINFOW *ai_next;
} WS2_ADDRINFOW;
static WS2_ADDRINFOW ws2_wide_info[8];
static unsigned short ws2_wide_canon[8][256];
static void ws2_wide_to_ascii(char *out,unsigned cap,const unsigned short *in) {
 unsigned i=0;if(!cap)return;if(!in){out[0]=0;return;}
 while(in[i]&&i+1<cap){out[i]=(in[i]<128)?(char)in[i]:'?';i++;}out[i]=0;
}
static void ws2_ascii_to_wide(unsigned short *out,unsigned cap,const char *in) {
 unsigned i=0;if(!cap)return;if(!in){out[0]=0;return;}
 while(in[i]&&i+1<cap){out[i]=(unsigned char)in[i];i++;}out[i]=0;
}
__attribute__((dllexport)) SOCKET WSASocketW(int af,int type,int protocol,void *info,unsigned int group,DWORD flags) {
 (void)info;(void)group;(void)flags;return socket(af,type,protocol);
}
__attribute__((dllexport)) int WSARecvFrom(SOCKET s,WS2_BUF *buffers,DWORD count,DWORD *received,DWORD *flags,SOCKADDR *from,int *fromlen,void *overlapped,void *completion) {
 if(received)*received=0;if(!buffers||!count||overlapped||completion){ws2_last_error=WSAEOPNOTSUPP;return SOCKET_ERROR;}
 int r=recvfrom(s,buffers[0].buf,(int)buffers[0].len,flags?*flags:0,from,fromlen);
 if(r==SOCKET_ERROR)return r;if(received)*received=(DWORD)r;if(flags)*flags=0;return 0;
}
__attribute__((dllexport)) int WSASendTo(SOCKET s,WS2_BUF *buffers,DWORD count,DWORD *sent,DWORD flags,const SOCKADDR *to,int tolen,void *overlapped,void *completion) {
 if(sent)*sent=0;if(!buffers||!count||overlapped||completion){ws2_last_error=WSAEOPNOTSUPP;return SOCKET_ERROR;}
 int r=sendto(s,buffers[0].buf,(int)buffers[0].len,(int)flags,to,tolen);
 if(r==SOCKET_ERROR)return r;if(sent)*sent=(DWORD)r;return 0;
}
__attribute__((dllexport)) int GetAddrInfoW(const unsigned short *node,const unsigned short *service,const void *hints,WS2_ADDRINFOW **result) {
 if(result)*result=0;if(!node||!result){ws2_last_error=WSAEFAULT;return WSAEINVAL;}
 char n[256],s[32];ws2_wide_to_ascii(n,sizeof(n),node);ws2_wide_to_ascii(s,sizeof(s),service);
 WS2_ADDRINFO *a=0;int r=getaddrinfo(n,service?s:0,hints,(void **)&a);if(r)return r;
 unsigned slot=0;while(a&&slot<8){WS2_ADDRINFOW *d=&ws2_wide_info[slot];d->ai_flags=a->ai_flags;d->ai_family=a->ai_family;d->ai_socktype=a->ai_socktype;d->ai_protocol=a->ai_protocol;d->ai_addrlen=a->ai_addrlen;d->ai_addr=a->ai_addr;d->ai_canonname=ws2_wide_canon[slot];ws2_ascii_to_wide(ws2_wide_canon[slot],256,a->ai_canonname);d->ai_next=0;if(slot)(ws2_wide_info[slot-1]).ai_next=d;else *result=d;a=a->ai_next;slot++;}
 ws2_last_error=0;return 0;
}
__attribute__((dllexport)) void FreeAddrInfoW(WS2_ADDRINFOW *result) {(void)result;}
__attribute__((dllexport)) int GetNameInfoW(const SOCKADDR *sa,unsigned int salen,unsigned short *host,unsigned int hostlen,unsigned short *serv,unsigned int servlen,int flags) {
 char h[256],s[32];int r=getnameinfo(sa,salen,h,sizeof(h),s,sizeof(s),flags);if(r)return r;
 if(host)ws2_ascii_to_wide(host,hostlen,h);if(serv)ws2_ascii_to_wide(serv,servlen,s);return 0;
}
__attribute__((dllexport)) int WSAAddressToStringW(SOCKADDR *address,DWORD length,void *protocol,unsigned short *text,DWORD *textlen) {
 (void)protocol;if(!address||!textlen||length<sizeof(SOCKADDR_IN)||address->sa_family!=AF_INET){ws2_last_error=WSAEFAULT;return SOCKET_ERROR;}
 const unsigned char *p=(const unsigned char *)&((SOCKADDR_IN *)address)->sin_addr;
 char tmp[32];unsigned ip=(unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);
 unsigned char *oct=(unsigned char *)&ip;unsigned pos=0;
 for(unsigned i=0;i<4;i++){unsigned v=oct[i];char rev[4];unsigned n=0;do{rev[n++]=(char)('0'+v%10);v/=10;}while(v&&n<4);while(n)tmp[pos++]=rev[--n];if(i<3)tmp[pos++]='.';}tmp[pos]=0;
 unsigned need=pos+1;if(!text||*textlen<need){*textlen=need;ws2_last_error=WSAEFAULT;return SOCKET_ERROR;}ws2_ascii_to_wide(text,*textlen,tmp);*textlen=need;ws2_last_error=0;return 0;
}
__attribute__((dllexport)) int WSAStringToAddressW(unsigned short *text,int family,void *protocol,SOCKADDR *address,int *length) {
 (void)protocol;if(!text||!address||!length||*length<(int)sizeof(SOCKADDR_IN)||family!=AF_INET){ws2_last_error=WSAEAFNOSUPPORT;return SOCKET_ERROR;}
 char tmp[64];ws2_wide_to_ascii(tmp,sizeof(tmp),text);unsigned ip=inet_addr(tmp);if(ip==0xffffffffu&&!ws2_eq(tmp,"255.255.255.255")){ws2_last_error=WSAEINVAL;return SOCKET_ERROR;}
 SOCKADDR_IN *a=(SOCKADDR_IN *)address;a->sin_family=AF_INET;a->sin_addr=ip;ws2_last_error=0;return 0;
}
__attribute__((dllexport)) BOOL WSASetEvent(HANDLE event) {(void)event;ws2_last_error=WSAENETDOWN;return 0;}
__attribute__((dllexport)) BOOL WSAResetEvent(HANDLE event) {(void)event;ws2_last_error=WSAENETDOWN;return 0;}

__attribute__((dllexport)) int WSAGetLastErrorCompat2(void) { return ws2_last_error; }
