#pragma once
#include <stdint.h>
#include "e1000.h"

/* Small polled Ethernet/ARP/IPv4/UDP layer for the first NIC milestone.
 * No dynamic allocation or interrupt dependency. Current IPv4 config is the
 * QEMU user-network default; DHCP and TCP are intentionally separate work. */
#define NET_ETH_TYPE_IPV4 0x0800
#define NET_ETH_TYPE_ARP  0x0806#define NET_IP_PROTO_ICMP 1
#define NET_IP_PROTO_UDP  17
#define NET_DHCP_CLIENT_PORT 68
#define NET_DHCP_SERVER_PORT 67
#define NET_DNS_PORT 53
#define NET_FRAME_MAX 1514
#define NET_UDP_SLOTS 8
#define NET_UDP_DATA_MAX 1472

typedef struct __attribute__((packed)) {
    uint8_t dst[6],src[6]; uint16_t type;
} NET_ETH_HDR;
typedef struct __attribute__((packed)) {
    uint16_t htype,ptype; uint8_t hlen,plen; uint16_t oper;
    uint8_t sha[6]; uint32_t spa; uint8_t tha[6]; uint32_t tpa;
} NET_ARP_PKT;
typedef struct __attribute__((packed)) {
    uint8_t vhl,tos; uint16_t total,id,frag; uint8_t ttl,proto; uint16_t checksum;
    uint32_t src,dst;
} NET_IP_HDR;
typedef struct __attribute__((packed)) {
    uint16_t src,dst,len,checksum;
} NET_UDP_HDR;
typedef struct {
    uint16_t port; uint16_t length; uint32_t source_ip; uint16_t source_port; uint8_t data[NET_UDP_DATA_MAX]; int ready;
} NET_UDP_SLOT;

static uint8_t net_frame[NET_FRAME_MAX];
static uint8_t net_udp_tx[NET_FRAME_MAX];
static NET_UDP_SLOT net_udp_slots[NET_UDP_SLOTS];
static uint32_t net_ipv4_addr=0x0F02000Au; /* 10.0.2.15 in little-endian memory */
static uint32_t net_ipv4_mask=0x00FFFFFFu; /* 255.255.255.0 */
static uint32_t net_ipv4_gateway=0x0202000Au; /* 10.0.2.2 */
static uint8_t net_gateway_mac[6];
static int net_gateway_mac_valid;
static uint16_t net_ip_id=1;
static int net_started;
static uint32_t net_dns_server=0x0302000Au; /* QEMU DNS fallback: 10.0.2.3 */
static uint32_t net_dhcp_xid=0x53454E54u;
static uint32_t net_dhcp_server;
static uint32_t net_dhcp_requested;
static int net_dhcp_state; /* 0=discover, 1=request, 2=bound */
static unsigned net_dhcp_poll_count;
static uint16_t net_icmp_id=0x5345, net_icmp_seq;
static uint16_t net_ping_reply_seq;
static uint32_t net_ping_reply_ip;
static int net_ping_reply_ready;

static uint16_t NetSwap16(uint16_t v) { return (uint16_t)((v<<8)|(v>>8)); }
static uint16_t NetChecksum(const void *data,unsigned len) {
    const uint8_t *p=(const uint8_t *)data; uint32_t sum=0;
    while(len>1) { sum+=((uint16_t)p[0]<<8)|p[1]; p+=2; len-=2; }
    if(len) sum+=(uint16_t)p[0]<<8;
    while(sum>>16) sum=(sum&0xFFFFu)+(sum>>16);
    return (uint16_t)~sum;
}
static void NetCopy(uint8_t *d,const uint8_t *s,unsigned n) { for(unsigned i=0;i<n;i++) d[i]=s[i]; }
static void NetZero(void *p,unsigned n) { uint8_t *b=(uint8_t *)p; for(unsigned i=0;i<n;i++) b[i]=0; }
static int NetMacEqual(const uint8_t *a,const uint8_t *b) { for(unsigned i=0;i<6;i++) if(a[i]!=b[i]) return 0; return 1; }
static void NetSendArp(uint16_t op,const uint8_t *target_mac,uint32_t target_ip) {
    NetZero(net_udp_tx,60);
    NET_ETH_HDR *e=(NET_ETH_HDR *)net_udp_tx;
    NetCopy(e->dst,target_mac,6); NetCopy(e->src,e1000_mac,6); e->type=NetSwap16(NET_ETH_TYPE_ARP);
    NET_ARP_PKT *a=(NET_ARP_PKT *)(net_udp_tx+sizeof(*e));
    a->htype=NetSwap16(1); a->ptype=NetSwap16(NET_ETH_TYPE_IPV4); a->hlen=6; a->plen=4; a->oper=NetSwap16(op);
    NetCopy(a->sha,e1000_mac,6); a->spa=net_ipv4_addr; a->tpa=target_ip;
    if(target_mac) NetCopy(a->tha,target_mac,6);
    (void)E1000SendFrame(net_udp_tx,60);
}
static void NetHandleArp(const uint8_t *frame,unsigned len) {
    if(len<sizeof(NET_ETH_HDR)+sizeof(NET_ARP_PKT)) return;
    const NET_ARP_PKT *a=(const NET_ARP_PKT *)(frame+sizeof(NET_ETH_HDR));
    if(NetSwap16(a->htype)!=1||NetSwap16(a->ptype)!=NET_ETH_TYPE_IPV4||a->hlen!=6||a->plen!=4) return;
    if(a->spa==net_ipv4_gateway) { NetCopy(net_gateway_mac,a->sha,6); net_gateway_mac_valid=1; }
    if(NetSwap16(a->oper)==1&&a->tpa==net_ipv4_addr) NetSendArp(2,a->sha,a->spa);
}

typedef struct __attribute__((packed)) {
    uint8_t op, htype, hlen, hops;
    uint32_t xid;
    uint16_t secs, flags;
    uint32_t ciaddr, yiaddr, siaddr, giaddr;
    uint8_t chaddr[16], sname[64], file[128];
    uint32_t cookie;
    uint8_t options[312];
} NET_DHCP_PKT;

typedef struct __attribute__((packed)) {
    uint8_t type, code; uint16_t checksum; uint16_t id, seq;
} NET_ICMP_HDR;

static void NetPut32BE(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}
static uint32_t NetGet32BE(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static void NetSendDhcp(uint8_t message_type) {
    uint8_t frame[600]; NetZero(frame,sizeof(frame));
    NET_ETH_HDR *e=(NET_ETH_HDR *)frame;
    for(unsigned i=0;i<6;i++) e->dst[i]=0xFF;
    NetCopy(e->src,e1000_mac,6); e->type=NetSwap16(NET_ETH_TYPE_IPV4);
    NET_IP_HDR *ip=(NET_IP_HDR *)(frame+sizeof(*e));
    NET_UDP_HDR *udp=(NET_UDP_HDR *)((uint8_t *)ip+sizeof(*ip));
    NET_DHCP_PKT *d=(NET_DHCP_PKT *)((uint8_t *)udp+sizeof(*udp));
    d->op=1; d->htype=1; d->hlen=6; d->xid=NetSwap16((uint16_t)(net_dhcp_xid>>16));
    d->xid=__builtin_bswap32(net_dhcp_xid); d->flags=NetSwap16(0x8000);
    NetCopy(d->chaddr,e1000_mac,6); d->cookie=0x63538263u;
    unsigned o=0;
    d->options[o++]=53; d->options[o++]=1; d->options[o++]=message_type;
    if(message_type==3) {
        d->options[o++]=50; d->options[o++]=4; NetPut32BE(&d->options[o],__builtin_bswap32(net_dhcp_requested)); o+=4;
        if(net_dhcp_server) { d->options[o++]=54; d->options[o++]=4; NetPut32BE(&d->options[o],__builtin_bswap32(net_dhcp_server)); o+=4; }
    }
    d->options[o++]=55; d->options[o++]=4; d->options[o++]=1; d->options[o++]=3; d->options[o++]=6; d->options[o++]=51;
    d->options[o++]=255;
    unsigned dhcp_len=(unsigned)__builtin_offsetof(NET_DHCP_PKT,options)+o;
    unsigned udp_len=sizeof(*udp)+dhcp_len, ip_len=sizeof(*ip)+udp_len;
    udp->src=NetSwap16(NET_DHCP_CLIENT_PORT); udp->dst=NetSwap16(NET_DHCP_SERVER_PORT); udp->len=NetSwap16((uint16_t)udp_len);
    ip->vhl=0x45; ip->total=NetSwap16((uint16_t)ip_len); ip->id=NetSwap16(net_ip_id++);
    ip->frag=NetSwap16(0x4000); ip->ttl=64; ip->proto=NET_IP_PROTO_UDP;
    ip->src=0; ip->dst=0xFFFFFFFFu; ip->checksum=NetSwap16(NetChecksum(ip,sizeof(*ip)));
    unsigned frame_len=sizeof(*e)+ip_len; if(frame_len<60)frame_len=60;
    (void)E1000SendFrame(frame,frame_len);
}
static void NetDhcpStart(void) {
    net_dhcp_state=0; net_dhcp_poll_count=0; net_dhcp_server=0; net_dhcp_requested=0;
    NetSendDhcp(1);
}
static void NetHandleDhcp(const NET_IP_HDR *ip,const uint8_t *payload,unsigned len) {
    if(len<sizeof(NET_UDP_HDR)+__builtin_offsetof(NET_DHCP_PKT,options))return;
    const NET_UDP_HDR *u=(const NET_UDP_HDR *)payload;
    if(NetSwap16(u->dst)!=NET_DHCP_CLIENT_PORT)return;
    const NET_DHCP_PKT *d=(const NET_DHCP_PKT *)(payload+sizeof(*u));
    if(d->op!=2||d->xid!=__builtin_bswap32(net_dhcp_xid)||d->cookie!=0x63538263u)return;
    unsigned off=(unsigned)__builtin_offsetof(NET_DHCP_PKT,options), end=len-sizeof(*u), msg=0;
    uint32_t mask=0,router=0,dns=0,server=ip->src;
    while(off<end) {
        uint8_t code=((const uint8_t *)d)[off++];
        if(code==255)break;if(code==0)continue;if(off>=end)break;
        uint8_t n=((const uint8_t *)d)[off++];if(off+n>end)break;
        const uint8_t *v=((const uint8_t *)d)+off;
        if(code==53&&n>=1)msg=v[0];
        else if(code==1&&n==4)mask=NetGet32BE(v);
        else if(code==3&&n>=4)router=NetGet32BE(v);
        else if(code==6&&n>=4)dns=NetGet32BE(v);
        else if(code==54&&n==4)server=NetGet32BE(v);
        off+=n;
    }
    if(msg==2&&net_dhcp_state==0&&d->yiaddr) {
        net_dhcp_requested=d->yiaddr; net_dhcp_server=__builtin_bswap32(server);
        net_dhcp_state=1; NetSendDhcp(3);
    } else if(msg==5&&net_dhcp_state==1&&d->yiaddr) {
        net_ipv4_addr=d->yiaddr;
        if(mask)net_ipv4_mask=__builtin_bswap32(mask);
        if(router)net_ipv4_gateway=__builtin_bswap32(router);
        if(dns)net_dns_server=__builtin_bswap32(dns);
        net_dhcp_state=2; net_gateway_mac_valid=0;
        { const uint8_t b[6]={255,255,255,255,255,255}; NetSendArp(1,b,net_ipv4_gateway); }
        DbgPrint("net: DHCP bound IPv4 %u.%u.%u.%u\n",
            (unsigned)(net_ipv4_addr&255),(unsigned)((net_ipv4_addr>>8)&255),
            (unsigned)((net_ipv4_addr>>16)&255),(unsigned)(net_ipv4_addr>>24));
    }
}

static void NetHandleUdp(const NET_IP_HDR *ip,const uint8_t *payload,unsigned len) {
    if(len<sizeof(NET_UDP_HDR)) return;
    const NET_UDP_HDR *u=(const NET_UDP_HDR *)payload;
    unsigned udp_len=NetSwap16(u->len);
    if(udp_len<sizeof(*u)||udp_len>len) return;
    uint16_t dport=NetSwap16(u->dst);
    for(unsigned i=0;i<NET_UDP_SLOTS;i++) if(net_udp_slots[i].port==dport) {
        unsigned n=udp_len-sizeof(*u); if(n>NET_UDP_DATA_MAX)n=NET_UDP_DATA_MAX;
        NetCopy(net_udp_slots[i].data,payload+sizeof(*u),n);
        net_udp_slots[i].length=(uint16_t)n; net_udp_slots[i].source_ip=ip->src;
        net_udp_slots[i].source_port=NetSwap16(u->src); net_udp_slots[i].ready=1;
        return;
    }
}
static void NetHandleIpv4(const uint8_t *frame,unsigned len) {
    if(len<sizeof(NET_ETH_HDR)+sizeof(NET_IP_HDR)) return;
    const NET_IP_HDR *ip=(const NET_IP_HDR *)(frame+sizeof(NET_ETH_HDR));
    if((ip->vhl>>4)!=4) return;
    unsigned ihl=(ip->vhl&15u)*4u;
    if(ihl<sizeof(NET_IP_HDR)||len<sizeof(NET_ETH_HDR)+ihl) return;
    unsigned total=NetSwap16(ip->total);
    if(total<ihl||total>len-sizeof(NET_ETH_HDR)) return;
    if(NetChecksum(ip,ihl)!=0) return;
    if(ip->proto==NET_IP_PROTO_UDP) {
        const uint8_t *payload=(const uint8_t *)ip+ihl;
        if(total-ihl>=sizeof(NET_UDP_HDR) && NetSwap16(((const NET_UDP_HDR *)payload)->dst)==NET_DHCP_CLIENT_PORT) {
            NetHandleDhcp(ip,payload,total-ihl);
            return;
        }
        if(ip->dst!=net_ipv4_addr && ip->dst!=0xFFFFFFFFu) return;
        if(ip->dst==net_ipv4_addr) NetHandleUdp(ip,payload,total-ihl);
    } else if(ip->proto==NET_IP_PROTO_ICMP && ip->dst==net_ipv4_addr && total-ihl>=sizeof(NET_ICMP_HDR)) {
        const NET_ICMP_HDR *ic=(const NET_ICMP_HDR *)((const uint8_t *)ip+ihl);
        if(ic->type==0 && ic->code==0 && NetSwap16(ic->id)==net_icmp_id) {
            net_ping_reply_seq=NetSwap16(ic->seq); net_ping_reply_ip=ip->src; net_ping_reply_ready=1;
        }
    }
}
static void NetPoll(void) {
    if(!e1000_ready) return;
    if(net_dhcp_state!=2 && (++net_dhcp_poll_count%20000u)==0) NetSendDhcp(net_dhcp_state==0?1:3);
    for(unsigned i=0;i<32;i++) {
        unsigned n=E1000ReceiveFrame(net_frame,sizeof(net_frame));
        if(!n) break;
        if(n<sizeof(NET_ETH_HDR)) continue;
        const NET_ETH_HDR *e=(const NET_ETH_HDR *)net_frame;
        uint16_t type=NetSwap16(e->type);
        if(type==NET_ETH_TYPE_ARP) NetHandleArp(net_frame,n);
        else if(type==NET_ETH_TYPE_IPV4) NetHandleIpv4(net_frame,n);
    }
}
static int NetUdpBind(uint16_t port) {
    if(!port) return -1;
    for(unsigned i=0;i<NET_UDP_SLOTS;i++) if(net_udp_slots[i].port==port) return (int)i;
    for(unsigned i=0;i<NET_UDP_SLOTS;i++) if(!net_udp_slots[i].port) {
        net_udp_slots[i].port=port; net_udp_slots[i].ready=0; return (int)i;
    }
    return -1;
}
static int NetUdpReceiveFrom(uint16_t port,void *out,unsigned cap,uint32_t *source_ip,uint16_t *source_port) {
    int slot=-1;
    for(unsigned i=0;i<NET_UDP_SLOTS;i++) if(net_udp_slots[i].port==port) {slot=(int)i;break;}
    if(slot<0||!out||!net_udp_slots[slot].ready) return 0;
    unsigned n=net_udp_slots[slot].length; if(n>cap)n=cap;
    NetCopy((uint8_t *)out,net_udp_slots[slot].data,n);
    if(source_ip)*source_ip=net_udp_slots[slot].source_ip;
    if(source_port)*source_port=net_udp_slots[slot].source_port;
    net_udp_slots[slot].ready=0; return (int)n;
}
static int NetUdpReceive(uint16_t port,void *out,unsigned cap) {
    return NetUdpReceiveFrom(port,out,cap,0,0);
}
static int NetUdpSend(uint32_t dst_ip,uint16_t src_port,uint16_t dst_port,const void *data,unsigned len) {
    if(!e1000_ready||!src_port||!dst_port||(!data&&len)||len>NET_UDP_DATA_MAX) return -1;
    uint8_t dst_mac[6]; const uint8_t *mac=0;
    /* This first routing milestone uses the QEMU user-network gateway as
       the Ethernet next hop for external peers and QEMU's DNS alias too. */
    if(net_gateway_mac_valid) { NetCopy(dst_mac,net_gateway_mac,6); mac=dst_mac; }
    if(!mac) { uint8_t broadcast[6]={255,255,255,255,255,255}; NetSendArp(1,broadcast,net_ipv4_gateway); return -3; }
    unsigned iplen=sizeof(NET_IP_HDR)+sizeof(NET_UDP_HDR)+len;
    unsigned framelen=sizeof(NET_ETH_HDR)+iplen;
    if(framelen<60) framelen=60;
    NetZero(net_udp_tx,framelen);
    NET_ETH_HDR *e=(NET_ETH_HDR *)net_udp_tx;
    NetCopy(e->dst,mac,6); NetCopy(e->src,e1000_mac,6); e->type=NetSwap16(NET_ETH_TYPE_IPV4);
    NET_IP_HDR *ip=(NET_IP_HDR *)(net_udp_tx+sizeof(*e));
    ip->vhl=0x45; ip->total=NetSwap16((uint16_t)iplen); ip->id=NetSwap16(net_ip_id++);
    ip->frag=NetSwap16(0x4000); ip->ttl=64; ip->proto=NET_IP_PROTO_UDP; ip->src=net_ipv4_addr; ip->dst=dst_ip;
    ip->checksum=NetSwap16(NetChecksum(ip,sizeof(*ip)));
    NET_UDP_HDR *u=(NET_UDP_HDR *)((uint8_t *)ip+sizeof(*ip));
    u->src=NetSwap16(src_port); u->dst=NetSwap16(dst_port);
    u->len=NetSwap16((uint16_t)(sizeof(*u)+len)); u->checksum=0;
    if(len) NetCopy((uint8_t *)u+sizeof(*u),(const uint8_t *)data,len);
    return E1000SendFrame(net_udp_tx,framelen)?(int)len:-4;
}
static int NetInit(void) {
    if(!e1000_ready) return 0;
    NetZero(net_udp_slots,sizeof(net_udp_slots));
    net_gateway_mac_valid=0; net_started=1;
    NetDhcpStart();
    DbgPrint("net: Ethernet, ARP, IPv4, ICMP, UDP initialized; DHCP discovery started\n");
    return 1;
}

/* Returns 1 when an ICMP echo reply arrives, 0 on timeout, -1 on send failure. */
static int NetPing(uint32_t dst_ip, uint32_t *reply_ip, uint32_t *polls) {
    if(!e1000_ready||!dst_ip)return -1;
    uint8_t dst_mac[6];
    if(!net_gateway_mac_valid) {
        const uint8_t b[6]={255,255,255,255,255,255};
        NetSendArp(1,b,net_ipv4_gateway);
        for(unsigned wait=0;wait<100000;wait++) {
            NetPoll();
            if(net_gateway_mac_valid)break;
            __asm__ volatile("pause");
        }
    }
    if(!net_gateway_mac_valid)return -1;
    NetCopy(dst_mac,net_gateway_mac,6);
    uint8_t frame[60]; NetZero(frame,sizeof(frame));
    NET_ETH_HDR *e=(NET_ETH_HDR *)frame; NetCopy(e->dst,dst_mac,6); NetCopy(e->src,e1000_mac,6); e->type=NetSwap16(NET_ETH_TYPE_IPV4);
    NET_IP_HDR *ip=(NET_IP_HDR *)(frame+sizeof(*e)); NET_ICMP_HDR *ic=(NET_ICMP_HDR *)((uint8_t *)ip+sizeof(*ip));
    ic->type=8; ic->id=NetSwap16(net_icmp_id); ic->seq=NetSwap16(++net_icmp_seq); ic->checksum=NetSwap16(NetChecksum(ic,sizeof(*ic)));
    ip->vhl=0x45; ip->total=NetSwap16((uint16_t)(sizeof(*ip)+sizeof(*ic))); ip->id=NetSwap16(net_ip_id++); ip->frag=NetSwap16(0x4000);
    ip->ttl=64; ip->proto=NET_IP_PROTO_ICMP; ip->src=net_ipv4_addr; ip->dst=dst_ip; ip->checksum=NetSwap16(NetChecksum(ip,sizeof(*ip)));
    net_ping_reply_ready=0;
    if(!E1000SendFrame(frame,sizeof(frame)))return -1;
    uint32_t n;
    for(n=0;n<500000;n++) {
        NetPoll();
        if(net_ping_reply_ready&&net_ping_reply_seq==net_icmp_seq) {
            if(reply_ip)*reply_ip=net_ping_reply_ip;if(polls)*polls=n+1;return 1;
        }
        __asm__ volatile("pause");
    }
    if(polls)*polls=n;
    return 0;
}

