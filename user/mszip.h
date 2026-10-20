#pragma once
/* Bounded raw-DEFLATE decoder used by CAB MSZIP folders. Types u8/u16/u32 must be defined first. */
typedef struct { const u8 *p; u32 n, pos, bits, bitbuf; } CabBits;
typedef struct { u16 count[16], symbol[288]; } CabHuff;
static u8 cab_window[32768], cab_out[32768], cab_comp[65536];
static u32 cab_wpos, cab_wcount, cab_outlen;

static int cab_bits(CabBits *b, u32 n, u32 *v) {
    while (b->bits < n) {
        if (b->pos >= b->n) return 0;
        b->bitbuf |= (u32)b->p[b->pos++] << b->bits;
        b->bits += 8;
    }
    *v = b->bitbuf & ((1u << n) - 1u);
    b->bitbuf >>= n; b->bits -= n;
    return 1;
}
static int cab_huff_build(CabHuff *h, const u8 *lengths, u32 n) {
    u16 offs[16];
    for (u32 i=0;i<16;i++) h->count[i]=0;
    for (u32 i=0;i<n;i++) {
        if (lengths[i] > 15) return 0;
        if (lengths[i]) h->count[lengths[i]]++;
    }
    int left=1;
    for (u32 len=1;len<=15;len++) {
        left=(left<<1)-(int)h->count[len];
        if (left<0) return 0;
    }
    offs[1]=0;
    for (u32 len=1;len<15;len++) offs[len+1]=(u16)(offs[len]+h->count[len]);
    for (u32 sym=0;sym<n;sym++)
        if (lengths[sym]) h->symbol[offs[lengths[sym]]++]=(u16)sym;
    return 1;
}
static int cab_huff_decode(CabBits *b, const CabHuff *h, u32 *sym) {
    u32 code=0, first=0, index=0, bit;
    for (u32 len=1;len<=15;len++) {
        if (!cab_bits(b,1,&bit)) return 0;
        code |= bit;
        u32 count=h->count[len];
        if (code >= first && code-first < count) {
            *sym=h->symbol[index+(code-first)];
            return 1;
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return 0;
}
static int cab_emit(u8 v) {
    if (cab_outlen >= sizeof cab_out) return 0;
    cab_out[cab_outlen++]=v;
    cab_window[cab_wpos]=v;
    cab_wpos=(cab_wpos+1)&32767u;
    if (cab_wcount<32768) cab_wcount++;
    return 1;
}
static int cab_inflate_codes(CabBits *b, const CabHuff *lit, const CabHuff *dist) {
    static const u16 lb[29]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const u8 le[29]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const u16 db[30]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const u8 de[30]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    for (;;) {
        u32 s;
        if (!cab_huff_decode(b,lit,&s)) return 0;
        if (s<256) { if (!cab_emit((u8)s)) return 0; continue; }
        if (s==256) return 1;
        if (s<257 || s>285) return 0;
        u32 li=s-257, extra=0, length=lb[li];
        if (le[li] && !cab_bits(b,le[li],&extra)) return 0;
        length += extra;
        if (!cab_huff_decode(b,dist,&s) || s>=30) return 0;
        extra=0;
        if (de[s] && !cab_bits(b,de[s],&extra)) return 0;
        u32 distance=db[s]+extra;
        if (!distance || distance>cab_wcount) return 0;
        while (length--) {
            u8 v=cab_window[(cab_wpos+32768-distance)&32767u];
            if (!cab_emit(v)) return 0;
        }
    }
}
static int cab_inflate_raw(const u8 *src, u32 size, u32 expected) {
    CabBits b={src,size,0,0,0};
    cab_outlen=0;
    int final=0;
    while (!final) {
        u32 v,type;
        if (!cab_bits(&b,1,&v)) return 0; final=(int)v;
        if (!cab_bits(&b,2,&type)) return 0;
        if (type==0) {
            b.bits=0; b.bitbuf=0;
            u32 len,nlen;
            if (!cab_bits(&b,16,&len) || !cab_bits(&b,16,&nlen) ||
                ((len ^ 0xFFFFu)&0xFFFFu)!=nlen) return 0;
            for (u32 i=0;i<len;i++) {
                if (!cab_bits(&b,8,&v) || !cab_emit((u8)v)) return 0;
            }
        } else if (type==1 || type==2) {
            u8 ll[288], dd[32]; CabHuff lh,dh;
            if (type==1) {
                for (u32 i=0;i<=287;i++) ll[i]=(u8)(i<=143?8:(i<=255?9:(i<=279?7:8)));
                for (u32 i=0;i<32;i++) dd[i]=5;
                if (!cab_huff_build(&lh,ll,288) || !cab_huff_build(&dh,dd,32)) return 0;
            } else {
                u32 hlit,hdist,hclen;
                if (!cab_bits(&b,5,&hlit)||!cab_bits(&b,5,&hdist)||!cab_bits(&b,4,&hclen)) return 0;
                hlit+=257; hdist+=1; hclen+=4;
                if (hlit>286 || hdist>32) return 0;
                static const u8 order[19]={16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
                u8 cl[19]; for(u32 i=0;i<19;i++) cl[i]=0;
                for(u32 i=0;i<hclen;i++){ if(!cab_bits(&b,3,&v)) return 0; cl[order[i]]=(u8)v; }
                CabHuff ch;
                if(!cab_huff_build(&ch,cl,19)) return 0;
                u8 lens[320]; u32 total=hlit+hdist, got=0;
                while(got<total) {
                    u32 s;
                    if(!cab_huff_decode(&b,&ch,&s)) return 0;
                    if(s<16) lens[got++]=(u8)s;
                    else if(s==16) {
                        u32 rep; if(!got||!cab_bits(&b,2,&rep)) return 0; rep+=3;
                        if(rep>total-got) return 0;
                        u8 prev=lens[got-1]; while(rep--) lens[got++]=prev;
                    } else if(s==17) {
                        u32 rep; if(!cab_bits(&b,3,&rep)) return 0; rep+=3;
                        if(rep>total-got) return 0; while(rep--) lens[got++]=0;
                    } else if(s==18) {
                        u32 rep; if(!cab_bits(&b,7,&rep)) return 0; rep+=11;
                        if(rep>total-got) return 0; while(rep--) lens[got++]=0;
                    } else return 0;
                }
                for(u32 i=0;i<hlit;i++) ll[i]=lens[i];
                for(u32 i=hlit;i<288;i++) ll[i]=0;
                for(u32 i=0;i<hdist;i++) dd[i]=lens[hlit+i];
                for(u32 i=hdist;i<32;i++) dd[i]=0;
                if(!cab_huff_build(&lh,ll,hlit)||!cab_huff_build(&dh,dd,hdist)) return 0;
            }
            if(!cab_inflate_codes(&b,&lh,&dh)) return 0;
        } else return 0;
        if(cab_outlen>expected) return 0;
    }
    return cab_outlen==expected;
}
