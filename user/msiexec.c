/*
 * SentinelOS MSI engine foundation.
 *
 * Reads OLE Compound Files including both regular FAT streams and CFB mini
 * streams. This is groundwork for MSI relational-table decoding. It does not
 * claim to install packages until the table/action and writable-filesystem
 * layers are implemented.
 */
typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

enum { SYS_Exit=0, SYS_Write=1, SYS_Alloc=3, SYS_OpenFile=5, SYS_ReadFile=6, SYS_GetProcAddress=9, SYS_CloseFile=22, SYS_WriteFileHandle=23, SYS_SeekFile=24, SYS_CreateDirectory=25 };
#define ARG_VA ((const char *)0x6FF00000ULL)
#define MAX_BUFFER 0xFFFFFFFFu /* grow until allocation fails; parser offsets are currently 32-bit */
#define MAX_FAT 524288
#define MAX_DIR (256u * 1024u)
#define MAX_MINIFAT 262144u
#define MAX_MINI_STREAM (8u * 1024u * 1024u)
#define MAX_TABLE_STREAM (8u * 1024u * 1024u)
#define MSI_MAX_STRINGS 65536u
#define MSI_MAX_TABLES 512u
#define MSI_MAX_COLUMNS 4096u
#define MINI_SECTOR_SIZE 64u
#define FREE_SECTOR 0xFFFFFFFFu
#define END_OF_CHAIN 0xFFFFFFFEu
#define FAT_SECTOR 0xFFFFFFFDu
#define DIFAT_SECTOR 0xFFFFFFFCu

static int msi_ok;
static u32 msi_entry_count, msi_major;
/* Large workspaces are allocated at runtime so the PE image stays small
 * enough not to overlap user32.dll's fixed image base. */
static u32 *fat;
/* Keep the FAT-sector index off the small user-mode stack. */
static u32 *fat_sids;
static u32 *cab_sector_ids;
static u8 *dir_buffer;
/* CFB mini-stream storage is separate from the main FAT stream space. */
static u32 *mini_fat;
static u8 *mini_stream;
static u8 *table_buffer;
static u8 *string_data;
static u32 string_offset[MSI_MAX_STRINGS], string_length[MSI_MAX_STRINGS];
static u32 string_count, string_data_size;
static u32 mini_fat_count, mini_stream_size, mini_sector_count;
static u32 fat_count, sector_size, sector_count, mini_cutoff, directory_bytes;
static u32 msi_table_count, msi_column_count, msi_codepage;
static int msi_long_refs, msi_db_ok;
static char msi_cached_table[64];
static u32 msi_cached_table_size;
typedef struct { char name[64]; u32 rows, columns, row_size, stream_size; } MsiTable;
typedef struct { char table[64], name[64]; u16 type, number; u8 width; } MsiColumn;
static MsiTable msi_tables[MSI_MAX_TABLES];
static MsiColumn msi_columns[MSI_MAX_COLUMNS];
static const u8 *file_bytes;
static u32 file_size;
typedef struct { char path[512]; u8 is_dir, temporary; } MsiChange;
#define MSI_MAX_CHANGES 4096u
static MsiChange msi_changes[MSI_MAX_CHANGES];
static u32 msi_change_count;
static int msi_tx_active;

static u64 sy3(u64 n, u64 a, u64 b, u64 c) {
    u64 r;
    __asm__ volatile(
        "mov %[n], %%rax\nmov %[a], %%rdi\nmov %[b], %%rsi\nmov %[c], %%rdx\nint $0x80\nmov %%rax, %[r]"
        : [r]"=r"(r) : [n]"r"(n),[a]"r"(a),[b]"r"(b),[c]"r"(c)
        : "rax","rdi","rsi","rdx","memory","cc");
    return r;
}

/* NtAllocateVirtualMemory currently caps each request at 16 MiB. Keep every
 * request below that limit and outside the fixed DLL/image range. */
static int init_msi_workspace(void) {
    u64 va=0x43000000ULL, p, n;
#define MSI_WS(ptr, type, bytes) do { \
        n=((u64)(bytes)+0xFFFULL)&~0xFFFULL; \
        p=sy3(SYS_Alloc,va,n,0); \
        if (p!=va) return 0; \
        (ptr)=(type *)p; va+=n; \
    } while (0)
    MSI_WS(fat,u32,MAX_FAT*sizeof(u32));
    MSI_WS(fat_sids,u32,(MAX_FAT/128)*sizeof(u32));
    MSI_WS(cab_sector_ids,u32,MAX_FAT*sizeof(u32));
    MSI_WS(dir_buffer,u8,MAX_DIR);
    MSI_WS(mini_fat,u32,MAX_MINIFAT*sizeof(u32));
    MSI_WS(mini_stream,u8,MAX_MINI_STREAM);
    MSI_WS(table_buffer,u8,MAX_TABLE_STREAM);
    MSI_WS(string_data,u8,MAX_TABLE_STREAM);
#undef MSI_WS
    return 1;
}
static u64 msi_path_type(const char *path) {
    return sy3(28,(u64)path,0,0);
}
static int msi_record_change(const char *path, int is_dir, int temporary) {
    if (!msi_tx_active || !path || !path[0] || msi_change_count>=MSI_MAX_CHANGES) return 0;
    u32 n=0; while(path[n] && n+1<sizeof msi_changes[0].path) n++;
    if(path[n]) return 0;
    MsiChange *c=&msi_changes[msi_change_count++];
    for(u32 i=0;i<=n;i++) c->path[i]=path[i];
    c->is_dir=(u8)(is_dir!=0); c->temporary=(u8)(temporary!=0);
    return 1;
}
static void msi_rollback(void) {
    for(u32 i=msi_change_count;i>0;i--) {
        MsiChange *c=&msi_changes[i-1];
        if(!c->path[0]) continue;
        if(c->is_dir) (void)sy3(27,(u64)c->path,0,0);
        else (void)sy3(26,(u64)c->path,0,0);
    }
    msi_change_count=0; msi_tx_active=0;
}
static int msi_commit_cleanup(void) {
    int ok=1;
    for(u32 i=msi_change_count;i>0;i--) {
        MsiChange *c=&msi_changes[i-1];
        if(!c->path[0] || !c->temporary) continue;
        u64 r=sy3(c->is_dir?27:26,(u64)c->path,0,0);
        if(!r) ok=0;
        else c->path[0]=0;
    }
    u32 w=0;
    for(u32 i=0;i<msi_change_count;i++) if(msi_changes[i].path[0]) {
        if(w!=i) msi_changes[w]=msi_changes[i];
        w++;
    }
    msi_change_count=w;
    if(!ok) return 0;
    msi_change_count=0; msi_tx_active=0;
    return 1;
}
static int msi_make_path(char *out_path, u32 cap, const char *dir, const char *leaf) {
    if(!out_path || !cap || !dir || !leaf || !leaf[0]) return 0;
    u32 n=0;
    while(dir[n] && n+1<cap){out_path[n]=dir[n];n++;}
    if(dir[n]) return 0;
    if(n && out_path[n-1]!='\\' && out_path[n-1]!='/') {
        if(n+1>=cap) return 0;
        out_path[n++]='\\';
    }
    for(u32 i=0;leaf[i];i++) {
        if(n+1>=cap) return 0;
        out_path[n++]=leaf[i];
    }
    out_path[n]=0;
    return 1;
}
static void out(const char *s) {
    u64 n=0; while(s[n]) n++;
    (void)sy3(SYS_Write,1,(u64)s,n);
}
typedef int (*MessageBoxA_t)(void *, const char *, const char *, u32);
static void show_and_exit(const char *title, const char *message, u64 code) {
    out(message);
    out("\n");
    MessageBoxA_t box=(MessageBoxA_t)sy3(SYS_GetProcAddress,
        (u64)"user32.dll",(u64)"MessageBoxA",0);
    if (box) (void)box(0,message,title,0);
    (void)sy3(SYS_Exit,code,0,0);
    for (;;) __asm__ volatile("pause");
}
static void out_u32(u32 v) {
    char b[12]; int n=0;
    if (!v) { out("0"); return; }
    while(v && n<11) { b[n++]=(char)('0'+v%10); v/=10; }
    while(n) { char c[2]={b[--n],0}; out(c); }
}
static u16 rd16(const u8 *p) { return (u16)(p[0] | ((u16)p[1]<<8)); }
static u32 rd32(const u8 *p) { return (u32)p[0] | ((u32)p[1]<<8) | ((u32)p[2]<<16) | ((u32)p[3]<<24); }
static u64 rd64(const u8 *p) { return (u64)rd32(p) | ((u64)rd32(p+4)<<32); }
static const u8 *sector(u32 sid) {
    u64 off=((u64)sid+1)*sector_size;
    if (sid>=sector_count || off+sector_size>file_size) return 0;
    return file_bytes+off;
}
static int load_fat(void) {
    const u8 *h=file_bytes;
    u32 num_fat=rd32(h+44), difat_sid=rd32(h+68), num_difat=rd32(h+72);
    if (!num_fat || num_fat > MAX_FAT/128 ||
        num_fat > MAX_FAT/(sector_size/4)) return 0;
    u32 nfat=0;
    for (u32 i=0;i<109 && nfat<num_fat;i++) {
        u32 sid=rd32(h+76+i*4);
        if (sid!=FREE_SECTOR) fat_sids[nfat++]=sid;
    }
    for (u32 d=0; d<num_difat && nfat<num_fat; d++) {
        const u8 *p=sector(difat_sid);
        if (!p) return 0;
        u32 entries=sector_size/4-1;
        for (u32 j=0;j<entries && nfat<num_fat;j++) {
            u32 sid=rd32(p+j*4);
            if (sid!=FREE_SECTOR) fat_sids[nfat++]=sid;
        }
        difat_sid=rd32(p+sector_size-4);
        if (difat_sid==END_OF_CHAIN) break;
    }
    if (nfat<num_fat || (u64)num_fat*(sector_size/4)>MAX_FAT) return 0;
    fat_count=0;
    for (u32 i=0;i<nfat;i++) {
        const u8 *p=sector(fat_sids[i]);
        if (!p) return 0;
        for (u32 j=0;j<sector_size/4;j++) {
            if (fat_count>=MAX_FAT) return 0;
            fat[fat_count++]=rd32(p+j*4);
        }
    }
    return 1;
}
static u32 read_chain(u32 first, u8 *dst, u32 cap) {
    u32 sid=first, done=0, guard=0;
    while (sid!=END_OF_CHAIN && sid!=FREE_SECTOR && sid<fat_count &&
           done<cap && guard++<sector_count) {
        const u8 *p=sector(sid);
        if (!p) break;
        u32 n=sector_size;
        if (n>cap-done) n=cap-done;
        for (u32 i=0;i<n;i++) dst[done+i]=p[i];
        done+=n;
        u32 next=fat[sid];
        if (next==sid || next==FAT_SECTOR || next==DIFAT_SECTOR) break;
        sid=next;
    }
    return done;
}
static int load_mini_stream(const u8 *dir, u32 dir_bytes) {
    const u8 *root=0;
    for (u32 off=0; off+128<=dir_bytes; off+=128) {
        const u8 *e=dir+off;
        if (e[66]==5) { root=e; break; }
    }
    if (!root) return 0;

    u64 root_size=rd64(root+120);
    if (root_size>MAX_MINI_STREAM) return 0;
    mini_stream_size=(u32)root_size;
    mini_sector_count=(mini_stream_size+MINI_SECTOR_SIZE-1)/MINI_SECTOR_SIZE;
    if (mini_stream_size) {
        u32 got=read_chain(rd32(root+116),mini_stream,mini_stream_size);
        if (got<mini_stream_size) return 0;
    }

    u32 first=rd32(file_bytes+60);
    u32 sectors=rd32(file_bytes+64);
    if (!sectors) { mini_fat_count=0; return 1; }
    if (sectors>MAX_MINIFAT/(sector_size/4)) return 0;
    u32 bytes_cap=sectors*sector_size;
    u32 got=read_chain(first,(u8 *)mini_fat,bytes_cap);
    mini_fat_count=got/4;
    return mini_fat_count>0;
}

static u32 read_mini_chain(u32 first, u8 *dst, u32 size) {
    u32 sid=first, done=0, guard=0;
    while (sid!=END_OF_CHAIN && sid!=FREE_SECTOR &&
           sid<mini_fat_count && done<size && guard++<=mini_fat_count) {
        u64 off=(u64)sid*MINI_SECTOR_SIZE;
        if (off>=mini_stream_size) break;
        u32 n=MINI_SECTOR_SIZE;
        if (n>size-done) n=size-done;
        if (n>mini_stream_size-(u32)off) n=mini_stream_size-(u32)off;
        for (u32 i=0;i<n;i++) dst[done+i]=mini_stream[(u32)off+i];
        done+=n;
        u32 next=rd32((const u8 *)mini_fat+sid*4);
        if (next==sid || next==FAT_SECTOR || next==DIFAT_SECTOR) break;
        sid=next;
    }
    return done;
}

static u32 read_entry_stream(const u8 *e, u8 *dst, u32 cap) {
    u64 raw_size=rd64(e+120);
    if (raw_size>cap || raw_size>0xFFFFFFFFu) return 0;
    u32 size=(u32)raw_size;
    if (!size) return 0;
    if (size<mini_cutoff) {
        if (!mini_fat_count || !mini_stream_size) return 0;
        return read_mini_chain(rd32(e+116),dst,size);
    }
    return read_chain(rd32(e+116),dst,size);
}

static char msi_name_char(u32 v) {
    static const char alphabet[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz._";
    return alphabet[v&63u];
}
static u32 decode_stream_name(const u8 *e, char *dst, u32 cap, int *is_table) {
    u16 chars=rd16(e+64)/2;
    if (!chars || chars>32 || cap<2) return 0;
    u32 outn=0, i=0;
    if (rd16(e)==0x4840) { if (is_table) *is_table=1; i=1; }
    else if (is_table) *is_table=0;
    for (; i+1<chars && outn+2<cap; i++) {
        u16 ch=rd16(e+i*2);
        if (ch>=0x3800 && ch<0x4800) {
            u32 v=ch-0x3800;
            dst[outn++]=msi_name_char(v&63u);
            dst[outn++]=msi_name_char((v>>6)&63u);
        } else if (ch>=0x4800 && ch<0x4840) {
            dst[outn++]=msi_name_char(ch-0x4800);
        } else {
            dst[outn++]=(ch>=32 && ch<127)?(char)ch:'?';
        }
    }
    dst[outn]=0;
    return outn;
}
static void print_stream_name(const u8 *e) {
    char name[96];
    (void)decode_stream_name(e,name,sizeof(name),0);
    out(name);
}
static int same_name(const char *a, const char *b) {
    u32 i=0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && a[i]==b[i]) i++;
    return a[i]==0 && b[i]==0;
}
static const u8 *find_stream_entry(const char *name, int table_stream) {
    for (u32 off=0; off+128<=directory_bytes; off+=128) {
        const u8 *e=dir_buffer+off;
        if (e[66]!=2) continue;
        char decoded[96]; int is_table=0;
        (void)decode_stream_name(e,decoded,sizeof(decoded),&is_table);
        if (is_table==table_stream && same_name(decoded,name)) return e;
    }
    return 0;
}
static u32 read_named_stream(const char *name, int table_stream, u8 *dst, u32 cap) {
    const u8 *e=find_stream_entry(name,table_stream);
    if (!e) return 0;
    return read_entry_stream(e,dst,cap);
}
static u32 msi_string_width(void) { return msi_long_refs?3u:2u; }
static u32 read_string_ref(const u8 *p) {
    return msi_long_refs ? ((u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)) : rd16(p);
}
static int msi_string(u32 id, char *dst, u32 cap) {
    if (!dst || !cap) return 0;
    dst[0]=0;
    if (!id) return 1;
    if (id>string_count) return 0;
    u32 off=string_offset[id-1], len=string_length[id-1];
    if (off>string_data_size || len>string_data_size-off) return 0;
    u32 n=len;
    if (n>=cap) n=cap-1;
    for (u32 i=0;i<n;i++) {
        u8 c=string_data[off+i];
        dst[i]=(c>=32 && c<127)?(char)c:'?';
    }
    dst[n]=0;
    return 1;
}
static int parse_msi_string_pool(void) {
    u32 pool_size=read_named_stream("_StringPool",1,table_buffer,MAX_TABLE_STREAM);
    if (pool_size<4) return 0;
    string_data_size=read_named_stream("_StringData",1,string_data,MAX_TABLE_STREAM);
    if (!string_data_size) return 0;
    msi_codepage=rd16(table_buffer);
    msi_long_refs=(rd16(table_buffer+2)&0x8000u)!=0;
    string_count=0;
    u32 pos=4, data_pos=0;
    while (pos+4<=pool_size && string_count<MSI_MAX_STRINGS) {
        u32 len=rd16(table_buffer+pos);
        u32 refs=rd16(table_buffer+pos+2);
        pos+=4;
        if (!len && refs) {
            if (pos+4>pool_size) return 0;
            len=rd32(table_buffer+pos);
            pos+=4;
        }
        if (len>string_data_size-data_pos) return 0;
        string_offset[string_count]=data_pos;
        string_length[string_count]=len;
        string_count++;
        data_pos+=len;
    }
    return pos==pool_size || pool_size-pos<4;
}
static u8 msi_column_width(u16 type) {
    if ((type&0x0800u) && !(type&0x0400u)) return 2; /* binary/object cell */
    if (type&0x0800u) return (u8)msi_string_width();
    return ((type&0xFFu)==4u)?4u:2u;
}
static MsiTable *msi_find_table(const char *name) {
    for (u32 i=0;i<msi_table_count;i++)
        if (same_name(msi_tables[i].name,name)) return &msi_tables[i];
    return 0;
}
static MsiColumn *msi_find_column(const char *table, u32 number) {
    for (u32 i=0;i<msi_column_count;i++)
        if (same_name(msi_columns[i].table,table) && msi_columns[i].number==number)
            return &msi_columns[i];
    return 0;
}
/* Decode one cell from MSI's column-major table stream. */
static int msi_table_cell(const char *table, u32 row, u32 column,
                          char *text, u32 text_cap, long long *integer,
                          int *is_text) {
    MsiTable *t=msi_find_table(table);
    if (!t || row>=t->rows || column>=t->columns || !t->stream_size ||
        t->stream_size>MAX_TABLE_STREAM) return 0;
    if (!same_name(msi_cached_table,table) || msi_cached_table_size!=t->stream_size) {
        const u8 *entry=find_stream_entry(table,1);
        if (!entry || read_entry_stream(entry,table_buffer,MAX_TABLE_STREAM)!=t->stream_size)
            return 0;
        u32 i=0;
        while (table[i] && i+1<sizeof(msi_cached_table)) {
            msi_cached_table[i]=table[i]; i++;
        }
        msi_cached_table[i]=0;
        msi_cached_table_size=t->stream_size;
    }
    MsiColumn *target=msi_find_column(table,column+1);
    if (!target) return 0;
    u32 width=target->width, base=0;
    for (u32 c=1;c<=t->columns;c++) {
        MsiColumn *col=msi_find_column(table,c);
        if (!col) return 0;
        if (c<column+1) base+=(u32)col->width*t->rows;
    }
    u64 pos=(u64)base+(u64)row*width;
    if (pos+width>t->stream_size) return 0;
    const u8 *p=table_buffer+(u32)pos;
    if ((target->type&0x0800u) && (target->type&0x0400u)) {
        u32 id=read_string_ref(p);
        if (!msi_string(id,text,text_cap)) return 0;
        if (is_text) *is_text=1;
        if (integer) *integer=0;
        return 1;
    }
    if (text && text_cap) text[0]=0;
    if (is_text) *is_text=0;
    if (width==4) {
        u32 v=rd32(p);
        if (integer) *integer=v ? (long long)((long long)v-0x80000000LL) : 0;
    } else {
        u16 v=rd16(p);
        if (integer) *integer=v ? (long long)((long long)v-0x8000LL) : 0;
    }
    return 1;
}
static u32 cfb_stream_sector_list(const u8 *entry, u32 *ids, u32 cap) {
    u32 sid=rd32(entry+116), count=0;
    while (sid!=END_OF_CHAIN && sid!=FREE_SECTOR && sid<fat_count &&
           count<cap && count<sector_count) {
        ids[count++]=sid;
        u32 next=fat[sid];
        if (next==sid || next==FAT_SECTOR || next==DIFAT_SECTOR) return 0;
        sid=next;
    }
    return sid==END_OF_CHAIN ? count : 0;
}
static int cfb_stream_read_list(const u32 *ids, u32 count, u64 stream_size,
                                u64 offset, u8 *dst, u32 len) {
    if (offset>stream_size || len>stream_size-offset) return 0;
    u32 done=0;
    while (done<len) {
        u64 pos=offset+done;
        u64 idx=pos/sector_size;
        u32 in=(u32)(pos%sector_size);
        if (idx>=count) return 0;
        const u8 *p=sector(ids[idx]);
        if (!p) return 0;
        u32 n=sector_size-in;
        if (n>len-done) n=len-done;
        for (u32 i=0;i<n;i++) dst[done+i]=p[in+i];
        done+=n;
    }
    return 1;
}
typedef struct {
    char name[240];
    u32 size, offset;
    u16 folder;
} CabFile;
typedef struct { char name[240]; u32 size; } MsiStageFile;
#define CAB_MAX_FILES 4096u
static CabFile cab_files[CAB_MAX_FILES];
static u32 cab_file_count;
static MsiStageFile msi_stage_files[CAB_MAX_FILES];
static u32 msi_stage_file_count;
static const char cab_stage_path[]="C:\\MSIStage";

/* Bounded raw-DEFLATE decoder for CAB MSZIP blocks. The 32-KiB history
 * persists across CFDATA blocks in one folder, as required by MSZIP. */
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
static u32 cab_checksum(const u8 *p, u32 n, u16 unpacked) {
    u32 sum=(n&0xFFFFu)|((u32)unpacked<<16), i=0;
    while(i+4<=n){sum^=rd32(p+i);i+=4;}
    if(i<n){u32 tail=0,shift=0;while(i<n){tail|=(u32)p[i++]<<shift;shift+=8;}sum^=tail;}
    return sum;
}
typedef int (*CabBlockSink)(u32 folder, u32 offset, const u8 *data, u32 len, void *context);
static int cab_stage_block(u32 folder, u32 offset, const u8 *data, u32 len, void *context) {
    (void)context;
    u64 block_end=(u64)offset+len;
    for (u32 i=0;i<cab_file_count;i++) {
        CabFile *f=&cab_files[i];
        if (f->folder!=folder || !f->size) continue;
        u64 file_start=f->offset, file_end=file_start+f->size;
        u64 lo=file_start>offset?file_start:offset;
        u64 hi=file_end<block_end?file_end:block_end;
        if (lo>=hi) continue;
        char path[256]; u32 p=0;
        for (u32 j=0;cab_stage_path[j] && p+1<sizeof path;j++) path[p++]=cab_stage_path[j];
        if (p+1<sizeof path) path[p++]='\\';
        for (u32 j=0;f->name[j] && p+1<sizeof path;j++) path[p++]=f->name[j];
        if (f->name[0]==0 || p+1>=sizeof path) return 0;
        path[p]=0;
        long long h=(long long)sy3(SYS_OpenFile,(u64)path,0x40000000u,3);
        if (h<0) return 0;
        long long seek=(long long)sy3(SYS_SeekFile,(u64)h,lo-file_start,0);
        if (seek<0) { (void)sy3(SYS_CloseFile,(u64)h,0,0); return 0; }
        u64 wrote=sy3(SYS_WriteFileHandle,(u64)h,(u64)(data+(u32)(lo-offset)),hi-lo);
        (void)sy3(SYS_CloseFile,(u64)h,0,0);
        if ((long long)wrote<0 || wrote!=hi-lo) return 0;
    }
    return 1;
}

static int cab_read_folder_payload(u32 folder_start, u16 blocks, u16 compression,
                                   u8 reserve_data, u32 sectors, u64 cab_size,
                                   u32 folder_index, CabBlockSink sink, void *context,
                                   u32 *total_out) {
    u64 pos=folder_start; *total_out=0;
    cab_wpos=cab_wcount=0;
    u32 method=compression&0x000Fu;
    if(method>1) return 0; /* none and MSZIP only */
    for(u32 i=0;i<blocks;i++) {
        u8 hdr[8];
        if (pos>cab_size || 8u+reserve_data>cab_size-pos ||
            !cfb_stream_read_list(cab_sector_ids,sectors,cab_size,pos,hdr,8)) return 0;
        u32 checksum=rd32(hdr); u16 cbdata=rd16(hdr+4), cbuncomp=rd16(hdr+6);
        pos+=8u+reserve_data;
        if(!cbuncomp || cbuncomp>32768 || cbdata>sizeof cab_comp ||
           pos>cab_size || cbdata>cab_size-pos) return 0;
        if(!cfb_stream_read_list(cab_sector_ids,sectors,cab_size,pos,cab_comp,cbdata)) return 0;
        if(checksum && cab_checksum(cab_comp,cbdata,cbuncomp)!=checksum) return 0;
        if(method==0) {
            if(cbdata!=cbuncomp) return 0;
            cab_outlen=0;
            for(u32 j=0;j<cbdata;j++) if(!cab_emit(cab_comp[j])) return 0;
        } else {
            if(cbdata<2 || cab_comp[0]!='C' || cab_comp[1]!='K') return 0;
            if(!cab_inflate_raw(cab_comp+2,cbdata-2,cbuncomp)) return 0;
        }
        if (sink && !sink(folder_index,*total_out,cab_out,cab_outlen,context)) return 0;
        if (*total_out > 0xFFFFFFFFu - cab_outlen) return 0;
        *total_out += cab_outlen;
        pos += cbdata;
    }
    return 1;
}

static int cab_skip_string(u32 sectors, u64 stream_size, u64 cab_size, u64 *pos) {
    for (u32 i=0;i<4096;i++) {
        u8 ch;
        if (*pos>=cab_size || !cfb_stream_read_list(cab_sector_ids,sectors,stream_size,*pos,&ch,1)) return 0;
        (*pos)++;
        if (!ch) return 1;
    }
    return 0;
}
static int msi_list_embedded_cabinet(const char *stream_name, int extract) {
    const u8 *entry=find_stream_entry(stream_name,0);
    if (!entry) {
        out("  Embedded cabinet stream missing: "); out(stream_name); out("\n");
        return 0;
    }
    u64 stream_size=rd64(entry+120);
    if (stream_size<36 || stream_size>file_size) return 0;
    u32 sectors=cfb_stream_sector_list(entry,cab_sector_ids,MAX_FAT);
    if (!sectors) return 0;
    u8 h[40];
    if (!cfb_stream_read_list(cab_sector_ids,sectors,stream_size,0,h,36) ||
        h[0]!='M' || h[1]!='S' || h[2]!='C' || h[3]!='F') {
        out("  Invalid embedded cabinet: "); out(stream_name); out("\n");
        return 0;
    }
    u32 cab_size=rd32(h+8), files_off=rd32(h+16);
    u16 folder_count=rd16(h+26), files=rd16(h+28), flags=rd16(h+30);
    if (cab_size>stream_size || cab_size<36 || files_off>=cab_size ||
        !folder_count || folder_count>128) return 0;

    u8 reserve_header=0, reserve_folder=0, reserve_data=0;
    u64 pos=36;
    if (flags&0x0004) {
        u8 r[4];
        if (!cfb_stream_read_list(cab_sector_ids,sectors,stream_size,pos,r,4)) return 0;
        reserve_header=r[0]; reserve_folder=r[1]; reserve_data=r[2];
        pos+=4u+reserve_header;
    }
    if (flags&0x0001) {
        if (!cab_skip_string(sectors,stream_size,cab_size,&pos) ||
            !cab_skip_string(sectors,stream_size,cab_size,&pos)) return 0;
    }
    if (flags&0x0002) {
        if (!cab_skip_string(sectors,stream_size,cab_size,&pos) ||
            !cab_skip_string(sectors,stream_size,cab_size,&pos)) return 0;
    }
    typedef struct { u32 data_off; u16 blocks, compression; } CabFolder;
    static CabFolder folders[128];
    if (pos+(u64)folder_count*(8u+reserve_folder)>cab_size ||
        pos+(u64)folder_count*(8u+reserve_folder)>files_off) return 0;
    for (u32 i=0;i<folder_count;i++) {
        u8 rec[8];
        if (!cfb_stream_read_list(cab_sector_ids,sectors,stream_size,pos,rec,8)) return 0;
        folders[i].data_off=rd32(rec);
        folders[i].blocks=rd16(rec+4);
        folders[i].compression=rd16(rec+6);
        if (folders[i].data_off>=cab_size ||
            folders[i].data_off+(u64)folders[i].blocks*(8u+reserve_data)>cab_size) return 0;
        pos+=8u+reserve_folder;
    }

    if (files>CAB_MAX_FILES) return 0;
    cab_file_count=0;
    out("Embedded cabinet "); out(stream_name); out(": ");
    out_u32(files); out(" files, "); out_u32(folder_count); out(" folders\n");
    pos=files_off;
    for (u32 i=0;i<files;i++) {
        u8 rec[16];
        if (pos+16>cab_size ||
            !cfb_stream_read_list(cab_sector_ids,sectors,stream_size,pos,rec,16)) return 0;
        u32 file_len=rd32(rec);
        u16 folder_id=rd16(rec+8);
        pos+=16;
        char name[260]; u32 n=0; int ended=0;
        while (pos<cab_size && n+1<sizeof(name)) {
            u8 ch;
            if (!cfb_stream_read_list(cab_sector_ids,sectors,stream_size,pos,&ch,1)) return 0;
            pos++;
            if (!ch) { ended=1; break; }
            if (extract && (ch<32 || ch>=127)) return 0;
            name[n++]=(ch>=32 && ch<127)?(char)ch:'?';
        }
        name[n]=0;
        if (!ended || n>=sizeof cab_files[0].name ||
            folder_id>=folder_count || !name[0] ||
            (name[0]=='.' && (!name[1] || (name[1]=='.' && !name[2])))) return 0;
        if (extract) {
            for (u32 j=0;j<n;j++)
                if (name[j]=='/' || name[j]=='\\' || name[j]==':' ||
                    (u8)name[j]<32 || (u8)name[j]>=127) return 0;
            for (u32 j=0;j<cab_file_count;j++) if (same_name(cab_files[j].name,name)) return 0;
        }
        CabFile *cf=&cab_files[cab_file_count++];
        cf->size=file_len; cf->offset=rd32(rec+4); cf->folder=folder_id;
        for (u32 j=0;j<=n;j++) cf->name[j]=name[j];
        if (i<16) {
            out("    "); out(name); out(" ("); out_u32(file_len); out(" bytes)\n");
        }
    }
    if (files>16) { out("    ... "); out_u32(files-16); out(" additional files\n"); }

    /* First pass validates every folder and every file range without writes. */
    static u32 folder_totals[128];
    for (u32 i=0;i<folder_count;i++) {
        u32 total=0;
        if (!cab_read_folder_payload(folders[i].data_off,folders[i].blocks,
                                     folders[i].compression,reserve_data,
                                     sectors,cab_size,i,0,0,&total)) {
            out("  CAB folder "); out_u32(i); out(": unsupported or corrupt compression/data\n");
            return 0;
        }
        folder_totals[i]=total;
        out("  CAB folder "); out_u32(i); out(": decompressed ");
        out_u32(total); out(" bytes (validated)\n");
    }
    for (u32 i=0;i<cab_file_count;i++) {
        CabFile *cf=&cab_files[i];
        if ((u64)cf->offset+cf->size>folder_totals[cf->folder]) return 0;
        for (u32 j=0;j<i;j++) if (cab_files[j].folder==cf->folder &&
            cf->size && cab_files[j].size &&
            (u64)cf->offset < (u64)cab_files[j].offset+cab_files[j].size &&
            (u64)cab_files[j].offset < (u64)cf->offset+cf->size) return 0;
    }
    if (extract) {
        /* Pre-create all destinations before streaming blocks. */
        for (u32 i=0;i<cab_file_count;i++) {
            char path[256]; u32 p=0;
            for (u32 j=0;cab_stage_path[j] && p+1<sizeof path;j++) path[p++]=cab_stage_path[j];
            if (p+1<sizeof path) path[p++]='\\';
            for (u32 j=0;cab_files[i].name[j] && p+1<sizeof path;j++) path[p++]=cab_files[i].name[j];
            if (p+1>=sizeof path) return 0;
            path[p]=0;
            long long fh=(long long)sy3(SYS_OpenFile,(u64)path,0x40000000u,1);
            if (fh<0) return 0;
            (void)sy3(SYS_CloseFile,(u64)fh,0,0);
            if (!msi_record_change(path,0,1)) {
                (void)sy3(26,(u64)path,0,0);
                return 0;
            }
        }
        for (u32 i=0;i<folder_count;i++) {
            u32 total=0;
            if (!cab_read_folder_payload(folders[i].data_off,folders[i].blocks,
                                         folders[i].compression,reserve_data,
                                         sectors,cab_size,i,cab_stage_block,0,&total) ||
                total!=folder_totals[i]) return 0;
        }
        out("  CAB payloads extracted to C:\\MSIStage\\ (staging only)\n");
    }
    return 1;
}
static void print_msi_cabinets(void) {
    MsiTable *media=msi_find_table("Media");
    if (!media) { out("Media table: absent\n"); return; }
    for (u32 r=0;r<media->rows;r++) {
        char cabinet[256]; long long v=0; int is_text=0;
        if (!msi_table_cell("Media",r,3,cabinet,sizeof(cabinet),&v,&is_text) || !is_text)
            continue;
        if (cabinet[0]=='#' && cabinet[1])
            (void)msi_list_embedded_cabinet(cabinet+1,0);
        else if (cabinet[0])
            { out("External cabinet required: "); out(cabinet); out("\n"); }
    }
}
static int msi_extract_embedded_cabinets(void) {
    MsiTable *media=msi_find_table("Media");
    if (!msi_tx_active || !media) {
        out("Install blocked: no active transaction or Media table.\n"); return 0;
    }
    int found=0;
    for (u32 r=0;r<media->rows;r++) {
        char cabinet[256]; long long v=0; int is_text=0;
        if (!msi_table_cell("Media",r,3,cabinet,sizeof(cabinet),&v,&is_text) ||
            !is_text || !cabinet[0]) continue;
        if (cabinet[0]=='#' && cabinet[1]) found=1;
        else {
            out("Install blocked: external cabinet is unsupported: "); out(cabinet); out("\n");
            return 0;
        }
    }
    if (!found || msi_path_type(cab_stage_path)!=0 ||
        !sy3(SYS_CreateDirectory,(u64)cab_stage_path,0,0)) {
        out("Install blocked: staging directory is unavailable or already exists.\n");
        return 0;
    }
    if (!msi_record_change(cab_stage_path,1,1)) {
        (void)sy3(SYS_RemoveDirectory,(u64)cab_stage_path,0,0);
        return 0;
    }
    msi_stage_file_count=0;
    for (u32 r=0;r<media->rows;r++) {
        char cabinet[256]; long long v=0; int is_text=0;
        if (!msi_table_cell("Media",r,3,cabinet,sizeof(cabinet),&v,&is_text) ||
            !is_text || !cabinet[0]) continue;
        if (!msi_list_embedded_cabinet(cabinet+1,1)) return 0;
        if (cab_file_count>CAB_MAX_FILES-msi_stage_file_count) return 0;
        for (u32 i=0;i<cab_file_count;i++) {
            for (u32 j=0;j<msi_stage_file_count;j++)
                if (same_name(msi_stage_files[j].name,cab_files[i].name)) return 0;
            MsiStageFile *sf=&msi_stage_files[msi_stage_file_count++];
            sf->size=cab_files[i].size;
            for (u32 j=0;j<sizeof sf->name;j++) {
                sf->name[j]=cab_files[i].name[j];
                if (!sf->name[j]) break;
            }
        }
    }
    return msi_stage_file_count!=0;
}
static void print_msi_product_summary(void) {
    MsiTable *p=msi_find_table("Property");
    if (p) {
        for (u32 r=0;r<p->rows;r++) {
            char key[128], value[256]; long long n=0; int is_text=0;
            if (!msi_table_cell("Property",r,0,key,sizeof(key),&n,&is_text) || !is_text)
                continue;
            if (!msi_table_cell("Property",r,1,value,sizeof(value),&n,&is_text) || !is_text)
                continue;
            if (same_name(key,"ProductName") || same_name(key,"ProductVersion") ||
                same_name(key,"Manufacturer") || same_name(key,"ProductCode")) {
                out("  "); out(key); out(": "); out(value); out("\n");
            }
        }
    }
    MsiTable *files=msi_find_table("File");
    MsiTable *media=msi_find_table("Media");
    MsiTable *dirs=msi_find_table("Directory");
    if (files) { out("File records: "); out_u32(files->rows); out("\n"); }
    if (media) { out("Media records: "); out_u32(media->rows); out("\n"); }
    if (dirs) { out("Directory records: "); out_u32(dirs->rows); out("\n"); }
}
typedef struct {
    char action[96];
    char condition[128];
    long long sequence;
} MsiSequenceRow;

static void inspect_execute_sequence(void) {
    MsiTable *t = msi_find_table("InstallExecuteSequence");
    if (!t) {
        out("InstallExecuteSequence: absent\n");
        return;
    }
    out("InstallExecuteSequence: ");
    out_u32(t->rows);
    out(" rows (inspection only; actions are not executed)\n");

    /* Bounded, sorted preview. Sequence rows are database data and must not
       be treated as executable instructions until condition/action support
       and the transaction engine exist. */
    static MsiSequenceRow rows[128];
    u32 count = t->rows < 128 ? t->rows : 128;
    for (u32 i = 0; i < count; ++i) {
        long long seq = 0;
        int action_text = 0, condition_text = 0;
        rows[i].action[0] = rows[i].condition[0] = 0;
        rows[i].sequence = 0;
        if (!msi_table_cell("InstallExecuteSequence", i, 0,
                            rows[i].action, sizeof rows[i].action,
                            0, &action_text) || !action_text ||
            !msi_table_cell("InstallExecuteSequence", i, 1,
                            rows[i].condition, sizeof rows[i].condition,
                            0, &condition_text) ||
            !msi_table_cell("InstallExecuteSequence", i, 2,
                            0, 0, &seq, 0)) {
            rows[i].action[0] = 0;
            continue;
        }
        if (!condition_text) rows[i].condition[0] = 0;
        rows[i].sequence = seq;
    }
    /* Insertion sort is adequate for this small, fixed preview. */
    for (u32 i = 1; i < count; ++i) {
        MsiSequenceRow v = rows[i];
        u32 j = i;
        while (j && rows[j-1].sequence > v.sequence) {
            rows[j] = rows[j-1];
            --j;
        }
        rows[j] = v;
    }
    for (u32 i = 0; i < count; ++i) {
        if (!rows[i].action[0]) continue;
        out("  [");
        out_u32((u32)rows[i].sequence);
        out("] ");
        out(rows[i].action);
        if (rows[i].condition[0]) {
            out(" if ");
            out(rows[i].condition);
        }
        out("\n");
    }
    if (t->rows > count) {
        out("  ... ");
        out_u32(t->rows - count);
        out(" additional sequence rows not shown\n");
    }
}

static int parse_msi_catalog(void) {
    if (!parse_msi_string_pool()) return 0;
    u32 tables_size=read_named_stream("_Tables",1,table_buffer,MAX_TABLE_STREAM);
    u32 sw=msi_string_width();
    if (!tables_size || tables_size%sw) return 0;
    msi_table_count=0;
    for (u32 pos=0; pos<tables_size && msi_table_count<MSI_MAX_TABLES; pos+=sw) {
        u32 id=read_string_ref(table_buffer+pos);
        MsiTable *t=&msi_tables[msi_table_count];
        if (!msi_string(id,t->name,sizeof(t->name)) || !t->name[0]) return 0;
        t->rows=t->columns=t->row_size=t->stream_size=0;
        msi_table_count++;
    }
    u32 cols_size=read_named_stream("_Columns",1,table_buffer,MAX_TABLE_STREAM);
    u32 col_row_size=2u*sw+4u;
    if (!cols_size || cols_size%col_row_size) return 0;
    u32 rows=cols_size/col_row_size;
    if (rows>MSI_MAX_COLUMNS) return 0;
    msi_column_count=rows;
    const u8 *table_col=table_buffer;
    const u8 *number_col=table_col+rows*sw;
    const u8 *name_col=number_col+rows*2;
    const u8 *type_col=name_col+rows*sw;
    for (u32 i=0;i<rows;i++) {
        MsiColumn *c=&msi_columns[i];
        if (!msi_string(read_string_ref(table_col+i*sw),c->table,sizeof(c->table)) ||
            !msi_string(read_string_ref(name_col+i*sw),c->name,sizeof(c->name))) return 0;
        c->number=(u16)(rd16(number_col+i*2)^0x8000u);
        c->type=(u16)(rd16(type_col+i*2)^0x8000u);
        c->width=msi_column_width(c->type);
        if (!c->table[0] || !c->name[0] || !c->width) return 0;
    }
    for (u32 i=0;i<msi_table_count;i++) {
        MsiTable *t=&msi_tables[i];
        for (u32 j=0;j<msi_column_count;j++) if (same_name(msi_columns[j].table,t->name)) {
            t->columns++;
            t->row_size+=msi_columns[j].width;
        }
        const u8 *e=find_stream_entry(t->name,1);
        if (e) {
            u64 sz=rd64(e+120);
            if (sz>0xFFFFFFFFu) return 0;
            t->stream_size=(u32)sz;
            if (t->row_size && sz%t->row_size) return 0;
            t->rows=t->row_size?(u32)(sz/t->row_size):0;
        }
    }
    msi_db_ok=1;
    return 1;
}
#include "msi_install.h"

static void inspect_msi(void) {
    static const u8 signature[8]={0xD0,0xCF,0x11,0xE0,0xA1,0xB1,0x1A,0xE1};
    if (file_size<512) { out("msiexec: file is too small for an MSI compound database\n"); return; }
    for (int i=0;i<8;i++) if (file_bytes[i]!=signature[i]) {
        out("msiexec: invalid MSI/OLE compound-file signature\n"); return;
    }
    u16 major=rd16(file_bytes+26), shift=rd16(file_bytes+30);
    if ((major!=3 && major!=4) ||
        (major==3 && shift!=9) || (major==4 && shift!=12)) {
        out("msiexec: unsupported compound-file version\n"); return;
    }
    sector_size=1u<<shift;
    sector_count=(file_size-1)/sector_size;
    if (!sector_size || sector_count==0 || !load_fat()) {
        out("msiexec: malformed or unsupported FAT/DIFAT layout\n"); return;
    }
    mini_cutoff=rd32(file_bytes+56);
    if (!mini_cutoff) mini_cutoff=4096;
    u32 dir_sid=rd32(file_bytes+48);
    u8 *dir=dir_buffer;
    directory_bytes=read_chain(dir_sid,dir,MAX_DIR);
    if (directory_bytes<128) { out("msiexec: invalid directory stream\n"); return; }
    if (!load_mini_stream(dir,directory_bytes)) {
        out("msiexec: mini-stream metadata is unsupported or exceeds limits\n");
        return;
    }
    msi_ok=1;
    msi_major=major;
    out("MSI compound database: structurally readable\n");
    out("Container version: "); out_u32(major); out("\n");
    out("Streams/storage entries:\n");
    u32 count=0;
    for (u32 off=0;off+128<=directory_bytes;off+=128) {
        const u8 *e=dir+off;
        u8 type=e[66];
        if (type!=2 && type!=5) continue;
        out("  ");
        print_stream_name(e);
        if (type==2) {
            out("  ("); out_u32((u32)rd64(e+120)); out(" bytes)");
        } else out("  (storage)");
        out("\n");
        count++;
    }
    msi_entry_count=count;
    out("Entries: "); out_u32(count); out("\n");
    out("CFB mini-streams: readable\n");
    if (parse_msi_catalog()) {
        out("MSI database catalog: decoded\n");
        out("Code page: "); out_u32(msi_codepage); out("\n");
        out("String references: "); out_u32(string_count); out("\n");
        out("Tables: "); out_u32(msi_table_count); out("\n");
        out("Columns: "); out_u32(msi_column_count); out("\n");
        for (u32 i=0;i<msi_table_count;i++) {
            out("  "); out(msi_tables[i].name); out(": ");
            out_u32(msi_tables[i].rows); out(" rows, ");
            out_u32(msi_tables[i].columns); out(" columns\n");
        }
        out("Package metadata:\n");
        print_msi_product_summary();
        print_msi_cabinets();
        inspect_execute_sequence();
    } else {
        out("MSI catalog decoding failed or exceeded the current bounded buffers.\n");
    }
    out("Inspection mode: this pass changed no files.\n");
    out("CAB: bounded uncompressed/MSZIP decoding; install mode can stage supported embedded cabinets.\n");
    out("Storage: guarded ext4 create/growth/delete on a limited non-journaled feature subset.\n");
    out("MSI: limited file-install sequence, feature levels, and best-effort runtime rollback.\n");
    out("Custom actions: SetProperty, SetDirectory, and Error only; native DLL/EXE/script actions are rejected.\n");
    out("This implementation has not been built or tested; power-loss rollback is unavailable.\n");
    out("Custom actions were not executed.\n");
}
typedef struct {
    u32 hwnd, message;
    u64 wParam;
    long long lParam;
    u32 time;
    int pt_x, pt_y;
} MSI_MSG;
typedef void *(*MSI_CreateWindowExA)(u32, const char *, const char *, u32,
    int, int, int, int, void *, void *, void *, void *);
typedef int (*MSI_ShowWindow)(void *, int);
typedef int (*MSI_TextOutA)(void *, int, int, const char *, int);
typedef int (*MSI_InvalidateRect)(void *, void *, int);
typedef int (*MSI_GetMessageA)(MSI_MSG *, void *, u32, u32);

static u32 msi_strlen(const char *s) {
    u32 n=0;
    while (s && s[n]) n++;
    return n;
}
static void msi_ui_line(MSI_TextOutA text, void *hwnd, int row, const char *s) {
    if (text && s) (void)text(hwnd, 0, row, s, (int)msi_strlen(s));
}
static void show_installer_window(const char *name) {
    MSI_CreateWindowExA create=(MSI_CreateWindowExA)sy3(SYS_GetProcAddress,
        (u64)"user32.dll",(u64)"CreateWindowExA",0);
    MSI_ShowWindow show=(MSI_ShowWindow)sy3(SYS_GetProcAddress,
        (u64)"user32.dll",(u64)"ShowWindow",0);
    MSI_TextOutA text=(MSI_TextOutA)sy3(SYS_GetProcAddress,
        (u64)"user32.dll",(u64)"TextOutA",0);
    MSI_InvalidateRect invalidate=(MSI_InvalidateRect)sy3(SYS_GetProcAddress,
        (u64)"user32.dll",(u64)"InvalidateRect",0);
    MSI_GetMessageA getmsg=(MSI_GetMessageA)sy3(SYS_GetProcAddress,
        (u64)"user32.dll",(u64)"GetMessageA",0);
    if (!create || !show || !text || !invalidate || !getmsg) {
        show_and_exit("MSI Reader",
            "The installer window APIs are unavailable. No files were changed.",1);
        return;
    }
    void *hwnd=create(0,"STATIC","SentinelOS MSI Installer",0x10CF0000u,
        120,70,720,440,0,0,0,0);
    if (!hwnd) {
        show_and_exit("MSI Reader","Could not create installer window.",1);
        return;
    }
    (void)show(hwnd,5);
    msi_ui_line(text,hwnd,0,"SentinelOS Installer");
    msi_ui_line(text,hwnd,1,"Windows Installer package recognized");
    msi_ui_line(text,hwnd,2,"");
    msi_ui_line(text,hwnd,3,"Package:");
    msi_ui_line(text,hwnd,4,name);
    msi_ui_line(text,hwnd,5,"");
    msi_ui_line(text,hwnd,6,"Container validation: PASSED");
    msi_ui_line(text,hwnd,7,"Compound-file version:");
    msi_ui_line(text,hwnd,8,msi_major==4?"4":"3");
    msi_ui_line(text,hwnd,9,"Directory entries:");
    char entries[16];
    u32 ev=msi_entry_count, en=0;
    if (!ev) entries[en++]=48;
    else {
        char rev[16]; u32 rn=0;
        while (ev && rn<15) { rev[rn++]=(char)(48+ev%10); ev/=10; }
        while (rn) entries[en++]=rev[--rn];
    }
    entries[en]=0;
    msi_ui_line(text,hwnd,10,entries);
    msi_ui_line(text,hwnd,11,msi_db_ok?"MSI database catalog: DECODED":"MSI database catalog: UNSUPPORTED");
    msi_ui_line(text,hwnd,12,"Installation: READY FOR PREFLIGHT");
    msi_ui_line(text,hwnd,13,"Press Enter to install the supported MSI subset.");
    msi_ui_line(text,hwnd,14,"Unsupported actions and conditions are rejected.");
    msi_ui_line(text,hwnd,15,"Runtime failures trigger best-effort file rollback.");
    msi_ui_line(text,hwnd,16,"Native Windows DLL custom actions are never executed.");
    msi_ui_line(text,hwnd,17,"");
    msi_ui_line(text,hwnd,18,"Press Enter to install, Esc or X to close.");
    (void)invalidate(hwnd,0,1);
    MSI_MSG msg; int started=0;
    for (;;) {
        if (!getmsg(&msg,0,0,0)) continue;
        if (msg.message==0x0100u && msg.wParam==27u) {
            (void)sy3(SYS_Exit,0,0,0);
            for (;;) __asm__ volatile("pause");
        }
        if (msg.message==0x0100u && msg.wParam==13u && !started) {
            started=1;
            msi_ui_line(text,hwnd,12,"Installation: RUNNING PREFLIGHT");
            (void)invalidate(hwnd,0,1);
            if (msi_install_package()) {
                msi_ui_line(text,hwnd,12,"Installation: COMPLETED");
                msi_ui_line(text,hwnd,13,"Supported file payloads were installed.");
                msi_ui_line(text,hwnd,14,"Registration and unsupported actions were skipped.");
                msi_ui_line(text,hwnd,15,"");
            } else {
                msi_ui_line(text,hwnd,12,"Installation: FAILED (rollback attempted)");
                msi_ui_line(text,hwnd,13,"Check the serial log for the reason.");
                msi_ui_line(text,hwnd,14,"Power-loss rollback is not available.");
            }
            (void)invalidate(hwnd,0,1);
        }
    }
}

void mainCRTStartup(void) {
    const char *name=ARG_VA;
    if (!name[0]) {
        out("Usage: double-click an .msi in File Explorer.\n");
        for (;;) __asm__ volatile("pause");
    }
    out("msiexec: inspecting "); out(name); out("\n");
    if (!init_msi_workspace()) {
        show_and_exit("MSI Reader",
            "Not enough memory for MSI parser workspaces. No files were changed.",1);
    }
    u64 h=sy3(SYS_OpenFile,(u64)name,0,0);
    if ((long long)h<0) {
        out("msiexec: cannot open package in the current directory\n");
        for (;;) __asm__ volatile("pause");
    }
    /*
     * Grow the total MSI buffer as needed. 16 MiB is only the maximum size
     * of each individual kernel allocation, not a limit on the whole package.
     * Keep allocating adjacent chunks until EOF or the allocator runs out of
     * virtual/physical memory. File-read syscalls are capped at 1 MiB each.
     * The parser currently uses 32-bit file offsets.
     */
    const u32 chunk_size=16u*1024u*1024u;
    const u32 read_size=1024u*1024u;
    const u64 buffer_base=0x50000000ULL;
    u8 *buf=(u8 *)buffer_base;
    u32 capacity=0, total=0;
    int read_error=0;
    for (;;) {
        if (total==capacity) {
            if (capacity>=MAX_BUFFER) {
                out("msiexec: MSI exceeds the 32-bit parser size limit\n");
                read_error=1;
                break;
            }
            u32 grow=chunk_size;
            if (grow>MAX_BUFFER-capacity) grow=MAX_BUFFER-capacity;
            u64 p=sy3(SYS_Alloc,buffer_base+capacity,grow,0);
            if (p!=(buffer_base+capacity)) {
                out("msiexec: cannot grow MSI buffer; package size or memory limit reached\n");
                read_error=1;
                break;
            }
            capacity+=grow;
        }
        u64 want=capacity-total;
        if (want>read_size) want=read_size;
        u64 n=sy3(SYS_ReadFile,h,(u64)(buf+total),want);
        if ((long long)n<0) { read_error=1; break; }
        if (!n) break;
        total+=(u32)n;
    }
    (void)sy3(SYS_CloseFile,h,0,0);
    if (read_error) {
        show_and_exit("MSI Reader",
            "Could not read the complete MSI within available memory. No files were changed.",1);
    }
    file_bytes=buf; file_size=total;
    inspect_msi();
    if (msi_ok) {
        show_installer_window(name);
        (void)sy3(SYS_Exit,0,0,0);
        for (;;) __asm__ volatile("pause");
    }
    show_and_exit("MSI Reader",
        "This file is not a supported MSI compound database. No files were changed.",1);
}
