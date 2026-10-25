#pragma once
#include <stdint.h>
#include "e1000.h"

/* Small polled Ethernet/ARP/IPv4/UDP layer for the first NIC milestone.
 * No dynamic allocation or interrupt dependency. Current IPv4 config is the
 * QEMU user-network default; DHCP and TCP are intentionally separate work. */
#define NET_ETH_TYPE_IPV4 0x0800
#define NET_ETH_TYPE_ARP  0x0806
#define NET_IP_PROTO_ICMP 1
#define NET_IP_PROTO_UDP  17
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
    if(total<ihl||total>len-sizeof(NET_ETH_HDR)||ip->dst!=net_ipv4_addr) return;
    if(NetChecksum(ip,ihl)!=0) return;
    if(ip->proto==NET_IP_PROTO_UDP) NetHandleUdp(ip,(const uint8_t *)ip+ihl,total-ihl);
}
static void NetPoll(void) {
    if(!e1000_ready) return;
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
    { const uint8_t broadcast[6]={255,255,255,255,255,255}; NetSendArp(1,broadcast,net_ipv4_gateway); }
    DbgPrint("net: IPv4 10.0.2.15/24 gateway 10.0.2.2; ARP/IPv4/UDP polling initialized (DHCP/TCP pending)\n");
    return 1;
}
