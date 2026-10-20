/*
 * SentinelOS TrueType outline reader.
 * Supports sfnt TrueType fonts, cmap format 4, horizontal metrics, loca/glyf,
 * simple glyph contours, quadratic curves and 2x2 coverage rasterization.
 * Composite glyphs and CFF outlines deliberately fall back to the system glyph.
 */
#pragma once

typedef struct {
    const unsigned char *data;
    unsigned int size, cmap, glyf, loca, hmtx;
    unsigned int num_glyphs, num_hmetrics, units_per_em;
    int loca_long;
    int valid;
} U32_TTF;

static unsigned short u32_ttf_u16(const unsigned char *p) {
    return (unsigned short)(((unsigned int)p[0] << 8) | p[1]);
}
static short u32_ttf_s16(const unsigned char *p) {
    return (short)u32_ttf_u16(p);
}
static unsigned int u32_ttf_u32(const unsigned char *p) {
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8) | (unsigned int)p[3];
}
static unsigned int u32_ttf_table(const unsigned char *d, unsigned int n,
                                  const char tag[4]) {
    if (!d || n < 12) return 0;
    unsigned int count = u32_ttf_u16(d + 4);
    if (count > 256 || 12u + count * 16u > n) return 0;
    for (unsigned int i=0;i<count;i++) {
        const unsigned char *e=d+12+i*16;
        if (e[0]==(unsigned char)tag[0] && e[1]==(unsigned char)tag[1] &&
            e[2]==(unsigned char)tag[2] && e[3]==(unsigned char)tag[3]) {
            unsigned int off=u32_ttf_u32(e+8), len=u32_ttf_u32(e+12);
            return off<=n && len<=n-off ? off : 0;
        }
    }
    return 0;
}
static int u32_ttf_init(U32_TTF *f, const unsigned char *d, unsigned int n) {
    if (!f || !d || n < 12 ||
        (u32_ttf_u32(d)!=0x00010000u && u32_ttf_u32(d)!=0x74727565u))
        return 0;
    unsigned int head=u32_ttf_table(d,n,"head");
    unsigned int maxp=u32_ttf_table(d,n,"maxp");
    unsigned int hhea=u32_ttf_table(d,n,"hhea");
    unsigned int cmap=u32_ttf_table(d,n,"cmap");
    unsigned int glyf=u32_ttf_table(d,n,"glyf");
    unsigned int loca=u32_ttf_table(d,n,"loca");
    unsigned int hmtx=u32_ttf_table(d,n,"hmtx");
    if (!head || !maxp || !hhea || !cmap || !glyf || !loca || !hmtx ||
        head+54>n || maxp+6>n || hhea+36>n || cmap+4>n)
        return 0;
    f->data=d; f->size=n; f->glyf=glyf; f->loca=loca; f->hmtx=hmtx;
    f->units_per_em=u32_ttf_u16(d+head+18);
    f->loca_long=u32_ttf_s16(d+head+50)!=0;
    f->num_glyphs=u32_ttf_u16(d+maxp+4);
    f->num_hmetrics=u32_ttf_u16(d+hhea+34);
    f->cmap=0; f->valid=0;
    if (!f->units_per_em || !f->num_glyphs || !f->num_hmetrics ||
        f->num_hmetrics>f->num_glyphs) return 0;
    unsigned int nt=u32_ttf_u16(d+cmap+2);
    if (nt>64 || cmap+4+nt*8>n) return 0;
    for (unsigned int i=0;i<nt;i++) {
        const unsigned char *e=d+cmap+4+i*8;
        unsigned int sub=cmap+u32_ttf_u32(e+4);
        if (sub+2>n || u32_ttf_u16(d+sub)!=4) continue;
        unsigned short platform=u32_ttf_u16(e), enc=u32_ttf_u16(e+2);
        if (platform==3 && (enc==1 || enc==10)) { f->cmap=sub; break; }
        if (!f->cmap && platform==0) f->cmap=sub;
    }
    if (!f->cmap) return 0;
    f->valid=1;
    return 1;
}
static unsigned int u32_ttf_glyph(U32_TTF *f, unsigned int ch) {
    if (!f || !f->valid || ch>0xFFFF) return 0;
    const unsigned char *d=f->data, *c=d+f->cmap;
    unsigned int n=u32_ttf_u16(c+6);
    if (n<16 || f->cmap+n>f->size) return 0;
    unsigned int seg=u32_ttf_u16(c+6)/2;
    const unsigned char *end=c+14, *start=end+2*seg+2;
    const unsigned char *delta=start+2*seg, *range=delta+2*seg;
    if (range+2*seg>c+n) return 0;
    for (unsigned int i=0;i<seg;i++) {
        unsigned int e=u32_ttf_u16(end+i*2);
        unsigned int s=u32_ttf_u16(start+i*2);
        if (ch<s || ch>e) continue;
        int delta_v=u32_ttf_s16(delta+i*2);
        unsigned int ro=u32_ttf_u16(range+i*2);
        if (!ro) return (ch+delta_v)&0xFFFFu;
        const unsigned char *rp=range+i*2+ro+(ch-s)*2;
        if (rp+2>d+f->size) return 0;
        unsigned int g=u32_ttf_u16(rp);
        return g ? (g+delta_v)&0xFFFFu : 0;
    }
    return 0;
}
static unsigned int u32_ttf_advance(U32_TTF *f, unsigned int g) {
    if (!f || !f->valid || g>=f->num_glyphs) return 0;
    unsigned int idx=g<f->num_hmetrics?g:f->num_hmetrics-1;
    unsigned int p=f->hmtx+idx*4;
    if (p+2>f->size) return 0;
    return u32_ttf_u16(f->data+p);
}

#define U32_TTF_POINTS 768
#define U32_TTF_EDGES  4096
typedef struct { int x,y,on; } U32_TTF_POINT;
typedef struct { int x1,y1,x2,y2; } U32_TTF_EDGE;
static U32_TTF_POINT u32_ttf_points[U32_TTF_POINTS];
static U32_TTF_EDGE u32_ttf_edges[U32_TTF_EDGES];
static unsigned short u32_ttf_ends[128];

static void u32_ttf_edge(int x1,int y1,int x2,int y2,unsigned int *n) {
    if (*n>=U32_TTF_EDGES) return;
    u32_ttf_edges[*n].x1=x1; u32_ttf_edges[*n].y1=y1;
    u32_ttf_edges[*n].x2=x2; u32_ttf_edges[*n].y2=y2; (*n)++;
}
static void u32_ttf_quad(int x0,int y0,int cx,int cy,int x1,int y1,
                         unsigned int *ne) {
    int px=x0,py=y0;
    for(int s=1;s<=6;s++) {
        int t=s, u=6-s;
        int x=(u*u*x0+2*u*t*cx+t*t*x1)/36;
        int y=(u*u*y0+2*u*t*cy+t*t*y1)/36;
        u32_ttf_edge(px,py,x,y,ne); px=x; py=y;
    }
}
static int u32_ttf_inside(int x,int y,unsigned int ne) {
    int inside=0;
    for(unsigned int i=0;i<ne;i++) {
        U32_TTF_EDGE *e=&u32_ttf_edges[i];
        if ((e->y1>y)!=(e->y2>y)) {
            long long lhs=(long long)(x-e->x1)*(e->y2-e->y1);
            long long rhs=(long long)(e->x2-e->x1)*(y-e->y1);
            if ((e->y2>e->y1 && lhs<rhs) || (e->y2<e->y1 && lhs>rhs))
                inside=!inside;
        }
    }
    return inside;
}
static void u32_ttf_blend(int x,int y,unsigned int col,unsigned int a) {
    if (!g_front || x<0 || y<0 || (unsigned int)x>=g_fw || (unsigned int)y>=g_fh || !a) return;
    unsigned int *p=&g_front[(unsigned int)y*g_pitch+(unsigned int)x];
    unsigned int old=*p;
    unsigned int r=(((col>>16)&255)*a+((old>>16)&255)*(255-a))/255;
    unsigned int g=(((col>>8)&255)*a+((old>>8)&255)*(255-a))/255;
    unsigned int b=((col&255)*a+(old&255)*(255-a))/255;
    *p=0xFF000000u|(r<<16)|(g<<8)|b;
}
static int u32_ttf_draw(U32_TTF *f,int penx,int baseline,unsigned int ch,
                        unsigned int px,unsigned int col) {
    if (!f || !f->valid || !px) return -1;
    unsigned int glyph=u32_ttf_glyph(f,ch);
    unsigned int adv=u32_ttf_advance(f,glyph);
    unsigned int metric=f->hmtx+(glyph<f->num_hmetrics?glyph:f->num_hmetrics-1)*4;
    if (metric+4>f->size) return -1;
    int lsb=u32_ttf_s16(f->data+metric+2);
    unsigned int lo=f->loca+(f->loca_long?glyph*4:glyph*2);
    unsigned int hi=lo+(f->loca_long?4:2);
    if (hi+ (f->loca_long?4:2)>f->size) return -1;
    unsigned int a=f->loca_long?u32_ttf_u32(f->data+lo):u32_ttf_u16(f->data+lo)*2u;
    unsigned int b=f->loca_long?u32_ttf_u32(f->data+hi):u32_ttf_u16(f->data+hi)*2u;
    if (a>b || f->glyf+a>f->size || b>f->size-f->glyf) return -1;
    int advance=(int)((adv*px+f->units_per_em/2)/f->units_per_em);
    if (a==b) return advance;
    const unsigned char *g=f->data+f->glyf+a;
    if (f->glyf+a+10>f->size) return -1;
    int contours=u32_ttf_s16(g);
    if (contours<0 || contours>128) return -1;
    if (!contours) return advance;
    int xmin=u32_ttf_s16(g+2), ymin=u32_ttf_s16(g+4);
    int xmax=u32_ttf_s16(g+6), ymax=u32_ttf_s16(g+8);
    const unsigned char *p=g+10;
    if (p+contours*2+2>f->data+f->size) return -1;
    for(int i=0;i<contours;i++) u32_ttf_ends[i]=u32_ttf_u16(p+i*2);
    unsigned int np=(unsigned int)u32_ttf_ends[contours-1]+1;
    p+=contours*2;
    unsigned int ins=u32_ttf_u16(p); p+=2;
    if (np>U32_TTF_POINTS || p+ins>f->data+f->size) return -1;
    p+=ins;
    unsigned int got=0;
    while(got<np && p<f->data+f->size) {
        unsigned char flag=*p++;
        unsigned int rep=1;
        if(flag&8) { if(p>=f->data+f->size)return -1; rep+=(unsigned int)*p++; }
        while(rep-- && got<np) u32_ttf_points[got++].on=(flag&1)!=0;
    }
    int v=0;
    for(unsigned int i=0;i<np;i++) {
        unsigned char flag=u32_ttf_points[i].on?0:0;
        (void)flag;
        /* Flags were decoded into on; the packed flags are reread below. */
    }
    /* Rewind to flags and decode coordinates with the same repeat rules. */
    const unsigned char *fp=g+10+contours*2+2+ins;
    unsigned char flags[U32_TTF_POINTS];
    got=0;
    while(got<np && fp<f->data+f->size) {
        unsigned char flag=*fp++; unsigned int rep=1;
        if(flag&8) { if(fp>=f->data+f->size)return -1; rep+=(unsigned int)*fp++; }
        while(rep-- && got<np) flags[got++]=flag;
    }
    int coord=0;
    for(unsigned int i=0;i<np;i++) {
        unsigned char fl=flags[i]; int dlt=0;
        if(fl&2) { if(p>=f->data+f->size)return -1; dlt=*p++; if(!(fl&16))dlt=-dlt; }
        else if(!(fl&16)) { if(p+2>f->data+f->size)return -1; dlt=u32_ttf_s16(p);p+=2; }
        coord+=dlt; u32_ttf_points[i].x=coord;
    }
    coord=0;
    for(unsigned int i=0;i<np;i++) {
        unsigned char fl=flags[i]; int dlt=0;
        if(fl&4) { if(p>=f->data+f->size)return -1; dlt=*p++; if(!(fl&32))dlt=-dlt; }
        else if(!(fl&32)) { if(p+2>f->data+f->size)return -1; dlt=u32_ttf_s16(p);p+=2; }
        coord+=dlt; u32_ttf_points[i].y=coord;
    }
    unsigned int ne=0, first=0;
    for(int c=0;c<contours;c++) {
        unsigned int last=u32_ttf_ends[c], count=last-first+1;
        if(!count) { first=last+1; continue; }
        U32_TTF_POINT p0=u32_ttf_points[first], plast=u32_ttf_points[last];
        int sx,sy,idx;
        if(p0.on) { sx=p0.x; sy=p0.y; idx=first+1; }
        else if(plast.on) { sx=plast.x; sy=plast.y; idx=first; }
        else { sx=(p0.x+plast.x)/2; sy=(p0.y+plast.y)/2; idx=first; }
        int cx=sx,cy=sy;
        for(unsigned int k=0;k<count;k++) {
            unsigned int at=first+(idx-first+k)%count;
            U32_TTF_POINT q=u32_ttf_points[at];
            if(q.on) { u32_ttf_edge(cx,cy,q.x,q.y,&ne); cx=q.x;cy=q.y; }
            else {
                unsigned int nx=first+(at-first+1)%count;
                U32_TTF_POINT next=u32_ttf_points[nx];
                int ex=next.on?next.x:(q.x+next.x)/2;
                int ey=next.on?next.y:(q.y+next.y)/2;
                u32_ttf_quad(cx,cy,q.x,q.y,ex,ey,&ne);
                cx=ex;cy=ey;
                if(next.on) k++;
            }
        }
        u32_ttf_edge(cx,cy,sx,sy,&ne);
        first=last+1;
    }
    if (!ne) return advance;
    int scale_num=(int)px, units=(int)f->units_per_em;
    int x0=penx+(xmin*scale_num)/units-1;
    int x1=penx+(xmax*scale_num)/units+1;
    int y0=baseline-(ymax*scale_num)/units-1;
    int y1=baseline-(ymin*scale_num)/units+1;
    if(x1-x0>px*4+12 || y1-y0>px*4+12) return advance;
    for(int yy=y0;yy<y1;yy++) for(int xx=x0;xx<x1;xx++) {
        unsigned int hits=0;
        for(int sy=0;sy<2;sy++) for(int sx=0;sx<2;sx++) {
            int fx=(int)(((long long)(xx-penx)*units*64/scale_num)+(sx?48:16));
            int fy=(int)(((long long)(baseline-yy)*units*64/scale_num)-(sy?16:48));
            if(u32_ttf_inside(fx,fy,ne)) hits++;
        }
        if(hits) u32_ttf_blend(xx,yy,col,hits*255/4);
    }
    (void)lsb;
    return advance;
}

static unsigned char u32_ttf_data[768u*1024u];
static U32_TTF u32_ttf_font;
static int u32_ttf_attempted;
static void u32_ttf_load(void) {
    if(u32_ttf_attempted) return;
    u32_ttf_attempted=1;
    static const char path[]="Lato-Regular.ttf";
    unsigned long long h=sy3(5,(unsigned long long)path,0,0);
    if(h<0x100 || h==(unsigned long long)-1) return;
    unsigned long long n=sy3(6,h,(unsigned long long)u32_ttf_data,
                             sizeof(u32_ttf_data));
    if(n>0 && n<=sizeof(u32_ttf_data))
        (void)u32_ttf_init(&u32_ttf_font,u32_ttf_data,(unsigned int)n);
}
