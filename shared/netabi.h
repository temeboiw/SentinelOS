#pragma once
#include <stdint.h>
#define SENTINELOS_NET_CONFIG_VERSION 1u
#define SENTINELOS_NET_LINK_UP 0x01u
#define SENTINELOS_NET_DRIVER_READY 0x02u
#define SENTINELOS_NET_IPV4_CONFIGURED 0x04u

typedef struct {
    uint32_t Version;
    uint32_t Flags;
    uint8_t MacAddress[6];
    uint8_t Reserved[2];
    uint32_t IPv4Address; /* network byte order; zero until DHCP/static config */
    uint32_t SubnetMask;
    uint32_t Gateway;
    uint32_t DnsServer;
    uint64_t RxPackets;
    uint64_t TxPackets;
} SENTINELOS_NET_CONFIG;
