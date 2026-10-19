#pragma once
/*
 * Small polling IPv4 stack for SentinelOS. Supports Ethernet II, ARP,
 * IPv4, ICMP echo, UDP, DHCPv4 client and a bounded DNS A-record resolver.
 * No TCP, TLS, fragmentation, routing protocols, or interrupt-driven RX.
 */
#include <stdint.h>
#include "../shared/netabi.h"

#define NS_ETH_ARP 0x0806u
#define NS_ETH_IPV4 0x0800u
#define NS_IP_ICMP 1u
#define NS_IP_UDP 17u
#define NS_DHCP_CLIENT_PORT 68u
#define NS_DHCP_SERVER_PORT 67u
#define NS_DNS_PORT 53u
#define NS_FRAME_MAX 1536u
#define NS_RX_BUDGET 32u

static SENTINELOS_NET_CONFIG ns_cfg;
static uint8_t ns_frame[NS_FRAME_MAX];
static uint8_t ns_arp_mac[6];
static uint32_t ns_arp_ip;
static uint32_t ns_ip_id = 1;
static uint32_t ns_dhcp_xid = 0x534F5301u;
static uint16_t ns_dns_id = 0x534Fu, ns_dns_wait_id;
static uint32_t ns_dns_answer;
static int ns_dns_done;
static uint32_t ns_ping_target;
static int ns_ping_seen;
static int ns_dhcp_state;
static uint32_t ns_dhcp_offer_ip, ns_dhcp_server, ns_dhcp_mask, ns_dhcp_gw, ns_dhcp_dns;
static void ns_query_config(SENTINELOS_NET_CONFIG *out);

static uint16_t ns_rd16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static uint32_t ns_rd32(const uint8_t *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static void ns_wr16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
static void ns_wr32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v; }
static void ns_copy(uint8_t *d, const uint8_t *s, uint32_t n) { for(uint32_t i=0;i<n;i++) d[i]=s[i]; }
static void ns_zero(uint8_t *d, uint32_t n) { for(uint32_t i=0;i<n;i++) d[i]=0; }
static int ns_mac_equal(const uint8_t *a,const uint8_t *b) { for(int i=0;i<6;i++) if(a[i]!=b[i]) return 0; return 1; }
static uint16_t ns_checksum(const uint8_t *p, uint32_t n) {
    uint32_t sum=0;
    while(n>1){ sum += ((uint16_t)p[0]<<8)|p[1]; p+=2; n-=2; }
    if(n) sum += (uint16_t)p[0]<<8;
    while(sum>>16) sum=(sum&0xFFFFu)+(sum>>16);
    return (uint16_t)~sum;
}
static int ns_send_eth(const uint8_t dst[6], uint16_t type, const uint8_t *payload, uint16_t len) {
    uint8_t b[NS_FRAME_MAX];
    if((uint32_t)len+14>sizeof b) return -1;
    ns_copy(b,dst,6); ns_copy(b+6,ns_cfg.MacAddress,6); ns_wr16(b+12,type);
    ns_copy(b+14,payload,len);
    return net_send_frame(b,(uint16_t)(len+14));
}
static void ns_set_broadcast(uint8_t m[6]) { for(int i=0;i<6;i++)m[i]=0xFF; }
static int ns_arp_request(uint32_t target) {
    uint8_t p[28], bcast[6]; ns_zero(p,sizeof p); ns_set_broadcast(bcast);
    ns_wr16(p,1); ns_wr16(p+2,NS_ETH_IPV4); p[4]=6; p[5]=4; ns_wr16(p+6,1);
    ns_copy(p+8,ns_cfg.MacAddress,6); ns_wr32(p+14,ns_cfg.IPv4Address);
    ns_wr32(p+24,target);
    return ns_send_eth(bcast,NS_ETH_ARP,p,sizeof p);
}
static void ns_handle_arp(const uint8_t *p,uint16_t len) {
    if(len<28 || ns_rd16(p)!=1 || ns_rd16(p+2)!=NS_ETH_IPV4 || p[4]!=6 || p[5]!=4) return;
    uint16_t op=ns_rd16(p+6); uint32_t sip=ns_rd32(p+14), tip=ns_rd32(p+24);
    if(op==2 && sip==ns_arp_ip) { ns_copy(ns_arp_mac,p+8,6); return; }
    if(op==1 && tip==ns_cfg.IPv4Address && ns_cfg.IPv4Address) {
        uint8_t reply[28]; ns_zero(reply,sizeof reply);
        ns_wr16(reply,1); ns_wr16(reply+2,NS_ETH_IPV4); reply[4]=6; reply[5]=4; ns_wr16(reply+6,2);
        ns_copy(reply+8,ns_cfg.MacAddress,6); ns_wr32(reply+14,ns_cfg.IPv4Address);
        ns_copy(reply+18,p+8,6); ns_wr32(reply+24,sip);
        (void)ns_send_eth(p+8,NS_ETH_ARP,reply,sizeof reply);
    }
}
static void ns_handle_icmp(uint8_t *ip,uint16_t total) {
    if(total<28 || ip[9]!=NS_IP_ICMP) return;
    uint8_t *icmp=ip+20; uint16_t n=(uint16_t)(total-20);
    if(icmp[0]==0 && icmp[1]==0 && ns_rd32(ip+12)==ns_ping_target &&
       ns_rd16(icmp+4)==0x534F && ns_rd16(icmp+6)==1) { ns_ping_seen=1; return; }
    if(icmp[0]==8 && icmp[1]==0 && ns_checksum(icmp,n)==0) {
        uint32_t src=ns_rd32(ip+12), dst=ns_rd32(ip+16);
        if(dst!=ns_cfg.IPv4Address) return;
        icmp[0]=0; ns_wr16(icmp+2,0); ns_wr16(icmp+2,ns_checksum(icmp,n));
        ns_wr32(ip+12,ns_cfg.IPv4Address); ns_wr32(ip+16,src);
        ns_wr16(ip+10,0); ns_wr16(ip+10,ns_checksum(ip,20));
        uint8_t mac[6]; ns_copy(mac,ns_frame+6,6);
        (void)ns_send_eth(mac,NS_ETH_IPV4,ip,total);
    }
}
static void ns_handle_udp(uint8_t *ip,uint16_t total) {
    if(total<28 || ip[9]!=NS_IP_UDP) return;
    uint8_t *udp=ip+20; uint16_t ulen=ns_rd16(udp+4);
    if(ulen<8 || ulen>total-20) return;
    uint16_t src=ns_rd16(udp), dst=ns_rd16(udp+2);
    uint8_t *data=udp+8; uint16_t dlen=(uint16_t)(ulen-8);
    if(dst==NS_DHCP_CLIENT_PORT && src==NS_DHCP_SERVER_PORT &&
       dlen>=240 && ns_rd32(data+4)==ns_dhcp_xid && data[0]==2 &&
       ns_mac_equal(data+28,ns_cfg.MacAddress) &&
       data[236]==99 && data[237]==130 && data[238]==83 && data[239]==99) {
        uint32_t yi=ns_rd32(data+16), server=0, mask=0, gw=0, dns=0;
        uint8_t msg=0;
        for(uint32_t i=240;i<dlen;) {
            uint8_t tag=data[i++]; if(tag==255)break; if(tag==0)continue;
            if(i>=dlen)break; uint8_t l=data[i++]; if(i+l>dlen)break;
            if(tag==53 && l==1)msg=data[i];
            if(tag==54 && l==4)server=ns_rd32(data+i);
            if(tag==1 && l==4)mask=ns_rd32(data+i);
            if(tag==3 && l>=4)gw=ns_rd32(data+i);
            if(tag==6 && l>=4)dns=ns_rd32(data+i);
            i+=l;
        }
        if(msg==2 && ns_dhcp_state==1 && yi) {
            ns_dhcp_offer_ip=yi; ns_dhcp_server=server;
            ns_dhcp_mask=mask; ns_dhcp_gw=gw; ns_dhcp_dns=dns; ns_dhcp_state=2;
        } else if(msg==5 && ns_dhcp_state==3 && yi) {
            ns_cfg.IPv4Address=yi;
            ns_cfg.SubnetMask=mask?mask:ns_dhcp_mask;
            ns_cfg.Gateway=gw?gw:ns_dhcp_gw;
            ns_cfg.DnsServer=dns?dns:ns_dhcp_dns;
            ns_cfg.Flags|=SENTINELOS_NET_IPV4_CONFIGURED; ns_dhcp_state=4;
        }
    }
    if(dst==49152 && src==NS_DNS_PORT && dlen>=12 &&
       ns_rd16(data)==ns_dns_wait_id && ns_dns_wait_id &&
       (ns_rd16(data+2)&0x8000u) && !(ns_rd16(data+2)&0x000Fu) &&
       ns_rd16(data+6)>0) {
        uint16_t qd=ns_rd16(data+4), an=ns_rd16(data+6); uint32_t pos=12;
        for(uint16_t q=0;q<qd;q++) {
            uint32_t guard=0;
            while(pos<dlen && data[pos] && !(data[pos]&0xC0u) && guard++<255)
                pos+=(uint32_t)data[pos]+1;
            if(pos>=dlen)return;
            if((data[pos]&0xC0u)==0xC0u)pos+=2; else pos++;
            if(pos+4>dlen)return; pos+=4;
        }
        for(uint16_t a=0;a<an && pos+12<=dlen;a++) {
            if((data[pos]&0xC0u)==0xC0u)pos+=2;
            else {
                uint32_t guard=0;
                while(pos<dlen && data[pos] && !(data[pos]&0xC0u) && guard++<255)
                    pos+=(uint32_t)data[pos]+1;
                if(pos>=dlen)return; pos++;
            }
            if(pos+10>dlen)return;
            uint16_t typ=ns_rd16(data+pos), cls=ns_rd16(data+pos+2);
            uint16_t rdlen=ns_rd16(data+pos+8); pos+=10;
            if(pos+rdlen>dlen)return;
            if(typ==1 && cls==1 && rdlen==4) {
                ns_dns_answer=ns_rd32(data+pos); ns_dns_done=1; return;
            }
            pos+=rdlen;
        }
    }
}
static void ns_handle_ipv4(const uint8_t *eth,uint16_t len) {
    if(len<20) return;
    uint8_t *ip=(uint8_t *)(uintptr_t)eth;
    uint8_t ihl=(uint8_t)((ip[0]&15u)*4u); uint16_t total=ns_rd16(ip+2);
    if((ip[0]>>4)!=4 || ihl<20 || ihl>len || total<ihl || total>len ||
       ns_checksum(ip,ihl)!=0 || (ns_rd16(ip+6)&0x3FFFu)) return;
    uint32_t dst=ns_rd32(ip+16);
    if(dst!=ns_cfg.IPv4Address && dst!=0xFFFFFFFFu && !((dst&0xF0000000u)==0xE0000000u)) return;
    if(ip[9]==NS_IP_ICMP) ns_handle_icmp(ip,total);
    else if(ip[9]==NS_IP_UDP) ns_handle_udp(ip,total);
}
static void ns_handle_frame(const uint8_t *frame,uint16_t len) {
    if(len<14) return;
    uint16_t type=ns_rd16(frame+12);
    if(type==NS_ETH_ARP) ns_handle_arp(frame+14,(uint16_t)(len-14));
    else if(type==NS_ETH_IPV4) ns_handle_ipv4(frame+14,(uint16_t)(len-14));
}
static int ns_poll(void) {
    int handled=0;
    for(uint32_t i=0;i<NS_RX_BUDGET;i++) {
        int n=net_receive_frame(ns_frame,sizeof ns_frame);
        if(n<=0) break;
        ns_handle_frame(ns_frame,(uint16_t)n); handled++;
    }
    return handled;
}
static int ns_arp_resolve(uint32_t ip,uint8_t mac[6]) {
    if(!ip || !net_ready) return -1;
    if(ip==0xFFFFFFFFu){ns_set_broadcast(mac);return 0;}
    ns_arp_ip=ip; ns_zero(ns_arp_mac,6);
    if(ns_arp_request(ip)) return -1;
    for(volatile uint32_t t=0;t<2000000u;t++) {
        ns_poll();
        if(ns_arp_mac[0]|ns_arp_mac[1]|ns_arp_mac[2]|ns_arp_mac[3]|ns_arp_mac[4]|ns_arp_mac[5]) {
            ns_copy(mac,ns_arp_mac,6); return 0;
        }
    }
    return -1;
}
static int ns_send_ipv4(uint32_t dst,uint8_t proto,const uint8_t *payload,uint16_t plen) {
    if(!net_ready || !(ns_cfg.Flags&SENTINELOS_NET_IPV4_CONFIGURED) || plen>1400) return -1;
    uint8_t dstmac[6]; uint32_t next=dst;
    if(ns_cfg.SubnetMask && ((dst&ns_cfg.SubnetMask)!=(ns_cfg.IPv4Address&ns_cfg.SubnetMask)) && ns_cfg.Gateway) next=ns_cfg.Gateway;
    if(ns_arp_resolve(next,dstmac)) return -1;
    uint8_t p[1420]; ns_zero(p,20+plen); p[0]=0x45; ns_wr16(p+2,(uint16_t)(20+plen));
    ns_wr16(p+4,(uint16_t)ns_ip_id++); ns_wr16(p+6,0x4000); p[8]=64; p[9]=proto;
    ns_wr32(p+12,ns_cfg.IPv4Address); ns_wr32(p+16,dst); ns_wr16(p+10,ns_checksum(p,20));
    ns_copy(p+20,payload,plen);
    return ns_send_eth(dstmac,NS_ETH_IPV4,p,(uint16_t)(20+plen));
}
static int ns_send_udp(uint32_t dst,uint16_t sport,uint16_t dport,const uint8_t *data,uint16_t len) {
    uint8_t p[1410]; if(len>1402)return -1; ns_zero(p,8+len);
    ns_wr16(p,sport); ns_wr16(p+2,dport); ns_wr16(p+4,(uint16_t)(8+len)); ns_copy(p+8,data,len);
    return ns_send_ipv4(dst,NS_IP_UDP,p,(uint16_t)(8+len));
}
static int ns_dhcp_message(uint8_t type,uint32_t requested,uint32_t server) {
    uint8_t p[300],bcast[6]; ns_zero(p,sizeof p); p[0]=1;p[1]=1;p[2]=6;
    ns_wr32(p+4,ns_dhcp_xid); ns_wr16(p+10,0x8000); ns_copy(p+28,ns_cfg.MacAddress,6);
    p[236]=99;p[237]=130;p[238]=83;p[239]=99; uint32_t i=240;
    p[i++]=53;p[i++]=1;p[i++]=type;
    if(requested){p[i++]=50;p[i++]=4;ns_wr32(p+i,requested);i+=4;}
    if(server){p[i++]=54;p[i++]=4;ns_wr32(p+i,server);i+=4;}
    p[i++]=55;p[i++]=3;p[i++]=1;p[i++]=3;p[i++]=6;p[i++]=255;
    ns_set_broadcast(bcast);
    /* DHCP uses IP source 0.0.0.0; temporarily send the bootstrap datagram directly. */
    uint8_t ip[328]; ns_zero(ip,20+(uint32_t)i); ip[0]=0x45;ns_wr16(ip+2,(uint16_t)(20+i));
    ns_wr16(ip+4,(uint16_t)ns_ip_id++);ns_wr16(ip+6,0x4000);ip[8]=64;ip[9]=NS_IP_UDP;
    ns_wr32(ip+12,0);ns_wr32(ip+16,0xFFFFFFFFu);ns_wr16(ip+10,ns_checksum(ip,20));
    ns_wr16(ip+20,NS_DHCP_CLIENT_PORT);ns_wr16(ip+22,NS_DHCP_SERVER_PORT);
    ns_wr16(ip+24,(uint16_t)(8+i));ns_copy(ip+28,p,i);
    return ns_send_eth(bcast,NS_ETH_IPV4,ip,(uint16_t)(28+i));
}
static int ns_dhcp(void) {
    ns_query_config(0);
    if(!net_ready)return -1;
    ns_cfg.IPv4Address=ns_cfg.SubnetMask=ns_cfg.Gateway=ns_cfg.DnsServer=0;
    ns_cfg.Flags &= ~SENTINELOS_NET_IPV4_CONFIGURED;
    ns_dhcp_xid+=0x10203041u; ns_dhcp_state=1;
    if(ns_dhcp_message(1,0,0))return -1;
    for(volatile uint32_t t=0;t<3000000u && ns_dhcp_state==1;t++)ns_poll();
    if(ns_dhcp_state!=2)return -1;
    ns_dhcp_state=3;
    if(ns_dhcp_message(3,ns_dhcp_offer_ip,ns_dhcp_server))return -1;
    for(volatile uint32_t t=0;t<3000000u && ns_dhcp_state==3;t++)ns_poll();
    return ns_dhcp_state==4?0:-1;
}
static int ns_ping(uint32_t dst) {
    ns_query_config(0);
    if(!dst || !(ns_cfg.Flags&SENTINELOS_NET_IPV4_CONFIGURED))return -1;
    uint8_t p[16]; ns_zero(p,sizeof p); p[0]=8; p[1]=0;
    ns_wr16(p+4,0x534F); ns_wr16(p+6,1); ns_wr32(p+8,0x53454E54);
    ns_wr16(p+2,ns_checksum(p,sizeof p));
    ns_ping_target=dst; ns_ping_seen=0;
    if(ns_send_ipv4(dst,NS_IP_ICMP,p,sizeof p))return -1;
    for(volatile uint32_t t=0;t<2000000u && !ns_ping_seen;t++)ns_poll();
    return ns_ping_seen?0:-1;
}
static int ns_dns_name(const char *name,uint8_t *out,uint16_t cap,uint16_t *written) {
    uint16_t n=0; int label=0; uint16_t mark=0;
    for(uint16_t i=0;name[i];i++) {
        if(name[i]=='.') {
            if(!label || label>63 || n>=cap)return -1;
            out[mark]=(uint8_t)label; label=0; mark=n++;
        } else {
            if(n>=cap || label>=63)return -1;
            if(name[i]==' ' || name[i]=='/' || name[i]=='\\')return -1;
            out[n++]=(uint8_t)name[i];label++;
        }
    }
    if(!label || label>63 || n+2>cap)return -1;
    out[mark]=(uint8_t)label;out[n++]=0;*written=n;return 0;
}
static int ns_dns_query(const char *name,uint32_t *answer) {
    ns_query_config(0);
    if(!name || !answer || !ns_cfg.DnsServer ||
       !(ns_cfg.Flags&SENTINELOS_NET_IPV4_CONFIGURED))return -1;
    uint8_t p[512]; ns_zero(p,sizeof p); uint16_t qn=0;
    if(ns_dns_name(name,p+12,sizeof(p)-16,&qn))return -1;
    uint16_t id=++ns_dns_id; ns_dns_wait_id=id; ns_dns_done=0; ns_dns_answer=0;
    ns_wr16(p,id); ns_wr16(p+2,0x0100); ns_wr16(p+4,1);
    ns_wr16(p+12+qn,1); ns_wr16(p+14+qn,1);
    uint16_t plen=(uint16_t)(16+qn);
    if(ns_send_udp(ns_cfg.DnsServer,49152,NS_DNS_PORT,p,plen)) {
        ns_dns_wait_id=0; return -1;
    }
    for(volatile uint32_t t=0;t<3000000u && !ns_dns_done;t++)ns_poll();
    ns_dns_wait_id=0;
    if(!ns_dns_done)return -1;
    *answer=ns_dns_answer; return 0;
}
static void ns_query_config(SENTINELOS_NET_CONFIG *out) {
    SENTINELOS_NET_CONFIG hw;
    net_query_config(&hw);
    ns_cfg.Version=hw.Version;
    ns_cfg.Flags=(ns_cfg.Flags&SENTINELOS_NET_IPV4_CONFIGURED) |
        (hw.Flags&(SENTINELOS_NET_DRIVER_READY|SENTINELOS_NET_LINK_UP));
    ns_copy(ns_cfg.MacAddress,hw.MacAddress,6);
    ns_cfg.RxPackets=hw.RxPackets; ns_cfg.TxPackets=hw.TxPackets;
    if(out)*out=ns_cfg;
}
