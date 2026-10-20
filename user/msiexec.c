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

enum { SYS_Exit=0, SYS_Write=1, SYS_Alloc=3, SYS_OpenFile=5, SYS_ReadFile=6, SYS_GetProcAddress=9, SYS_CloseFile=22 };
#define ARG_VA ((const char *)0x6FF00000ULL)
#define MAX_BUFFER 0x70000000u /* 1.75 GiB from 0x50000000 to 0xC0000000 */
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
static u32 fat[MAX_FAT];
/* Keep the FAT-sector index off the small user-mode stack. */
static u32 fat_sids[MAX_FAT/128];
static u8 dir_buffer[MAX_DIR];
/* CFB mini-stream storage is separate from the main FAT stream space. */
static u32 mini_fat[MAX_MINIFAT];
static u8 mini_stream[MAX_MINI_STREAM];
static u8 table_buffer[MAX_TABLE_STREAM];
static u8 string_data[MAX_TABLE_STREAM];
static u32 string_offset[MSI_MAX_STRINGS], string_length[MSI_MAX_STRINGS];
static u32 string_count, string_data_size;
static u32 mini_fat_count, mini_stream_size, mini_sector_count;
static u32 fat_count, sector_size, sector_count, mini_cutoff, directory_bytes;
static u32 msi_table_count, msi_column_count, msi_codepage;
static int msi_long_refs, msi_db_ok;
typedef struct { char name[64]; u32 rows, columns, row_size, stream_size; } MsiTable;
typedef struct { char table[64], name[64]; u16 type, number; u8 width; } MsiColumn;
static MsiTable msi_tables[MSI_MAX_TABLES];
static MsiColumn msi_columns[MSI_MAX_COLUMNS];
static const u8 *file_bytes;
static u32 file_size;

static u64 sy3(u64 n, u64 a, u64 b, u64 c) {
    u64 r;
    __asm__ volatile(
        "mov %[n], %%rax\nmov %[a], %%rdi\nmov %[b], %%rsi\nmov %[c], %%rdx\nint $0x80\nmov %%rax, %[r]"
        : [r]"=r"(r) : [n]"r"(n),[a]"r"(a),[b]"r"(b),[c]"r"(c)
        : "rax","rdi","rsi","rdx","memory","cc");
    return r;
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

static int msi_name_value(u16 c) {
    if (c>='0' && c<='9') return c-'0';
    if (c>='A' && c<='Z') return c-'A'+10;
    if (c>='a' && c<='z') return c-'a'+36;
    if (c=='.') return 62;
    if (c=='_') return 63;
    return -1;
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
    const u8 *entry=find_stream_entry(table,1);
    if (!entry || read_entry_stream(entry,table_buffer,MAX_TABLE_STREAM)!=t->stream_size)
        return 0;
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
    } else {
        out("MSI catalog decoding failed or exceeded the current bounded buffers.\n");
    }
    out("File extraction, install sequencing, and writable storage are still pending; no files were changed.\n");
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
    msi_ui_line(text,hwnd,12,"Installation: NOT STARTED");
    msi_ui_line(text,hwnd,13,"This build reads the container and decodes the table catalog.");
    msi_ui_line(text,hwnd,14,"File extraction, component state, install sequences,");
    msi_ui_line(text,hwnd,15,"rollback, and safe writable storage are not yet implemented.");
    msi_ui_line(text,hwnd,16,"No files have been changed.");
    msi_ui_line(text,hwnd,17,"");
    msi_ui_line(text,hwnd,18,"Press Esc or click X to close.");
    (void)invalidate(hwnd,0,1);
    MSI_MSG msg;
    for (;;) {
        if (!getmsg(&msg,0,0,0)) continue;
        if (msg.message==0x0100u && msg.wParam==27u) {
            (void)sy3(SYS_Exit,0,0,0);
            for (;;) __asm__ volatile("pause");
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
    u64 h=sy3(SYS_OpenFile,(u64)name,0,0);
    if ((long long)h<0) {
        out("msiexec: cannot open package in the current directory\n");
        for (;;) __asm__ volatile("pause");
    }
    /*
     * Grow the contiguous user buffer in 32 MiB chunks and read in 8 MiB
     * requests. Larger reads reduce syscall overhead for large MSI payloads.
     * Growth is bounded by the available user virtual-address window.
     */
    const u32 chunk_size=32u*1024u*1024u;
    const u32 read_size=8u*1024u*1024u;
    const u64 buffer_base=0x50000000ULL;
    u8 *buf=(u8 *)buffer_base;
    u32 capacity=0, total=0;
    int read_error=0;
    for (;;) {
        if (total==capacity) {
            if (capacity>=MAX_BUFFER) {
                out("msiexec: package exceeds the available contiguous user buffer\n");
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
