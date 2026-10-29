#pragma once
#include <stdint.h>

/* Small freestanding SHA-256 implementation for installer payload verification.
 * Input is read-only and output is the standard 32-byte digest. */
static uint32_t sent_sha_rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32u - n)); }
static void sent_sha_block(uint32_t h[8], const uint8_t b[64]) {
    static const uint32_t k[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    };
    uint32_t w[64];
    for (unsigned i=0;i<16;i++) w[i]=((uint32_t)b[i*4]<<24)|((uint32_t)b[i*4+1]<<16)|((uint32_t)b[i*4+2]<<8)|b[i*4+3];
    for (unsigned i=16;i<64;i++) {
        uint32_t x=w[i-15], y=w[i-2];
        uint32_t s0=sent_sha_rotr(x,7)^sent_sha_rotr(x,18)^(x>>3);
        uint32_t s1=sent_sha_rotr(y,17)^sent_sha_rotr(y,19)^(y>>10);
        w[i]=w[i-16]+s0+w[i-7]+s1;
    }
    uint32_t a=h[0],c=h[2],b0=h[1],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7];
    for (unsigned i=0;i<64;i++) {
        uint32_t s1=sent_sha_rotr(e,6)^sent_sha_rotr(e,11)^sent_sha_rotr(e,25);
        uint32_t ch=(e&f)^(~e&g), t1=z+s1+ch+k[i]+w[i];
        uint32_t s0=sent_sha_rotr(a,2)^sent_sha_rotr(a,13)^sent_sha_rotr(a,22);
        uint32_t maj=(a&b0)^(a&c)^(b0&c), t2=s0+maj;
        z=g; g=f; f=e; e=d+t1; d=c; c=b0; b0=a; a=t1+t2;
    }
    h[0]+=a;h[1]+=b0;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;
}
static void sentinel_sha256(const uint8_t *data, uint64_t len, uint8_t out[32]) {
    uint32_t h[8]={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    uint64_t off=0;
    while (len-off>=64) { sent_sha_block(h,data+off); off+=64; }
    uint8_t tail[128]; uint64_t rem=len-off;
    for (uint64_t i=0;i<rem;i++) tail[i]=data[off+i];
    tail[rem]=0x80;
    uint64_t total=(rem<56)?64:128;
    for (uint64_t i=rem+1;i<total;i++) tail[i]=0;
    uint64_t bits=len*8;
    for (unsigned i=0;i<8;i++) tail[total-1-i]=(uint8_t)(bits>>(i*8));
    sent_sha_block(h,tail);
    if (total==128) sent_sha_block(h,tail+64);
    for (unsigned i=0;i<8;i++) { out[i*4]=(uint8_t)(h[i]>>24);out[i*4+1]=(uint8_t)(h[i]>>16);out[i*4+2]=(uint8_t)(h[i]>>8);out[i*4+3]=(uint8_t)h[i]; }
}
