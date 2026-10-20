/*
 * SentinelOS msiexec: first-stage MSI compound-file reader.
 *
 * This deliberately does not execute MSI custom actions or modify the
 * filesystem. It validates the OLE Compound File container and reports its
 * streams; interpreting MSI relational tables and installing components are
 * separate engine work.
 */
typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

enum { SYS_Exit=0, SYS_Write=1, SYS_Alloc=3, SYS_OpenFile=5, SYS_ReadFile=6, SYS_GetProcAddress=9, SYS_CloseFile=22 };
#define ARG_VA ((const char *)0x6FF00000ULL)
#define MAX_FILE (256u * 1024u * 1024u)
#define MAX_FAT 524288
#define MAX_DIR (256u * 1024u)
#define FREE_SECTOR 0xFFFFFFFFu
#define END_OF_CHAIN 0xFFFFFFFEu
#define FAT_SECTOR 0xFFFFFFFDu
#define DIFAT_SECTOR 0xFFFFFFFCu

static int msi_ok;
static u32 fat[MAX_FAT];
/* Keep the FAT-sector index off the small user-mode stack. */
static u32 fat_sids[MAX_FAT/128];
static u8 dir_buffer[MAX_DIR];
static u32 fat_count, sector_size, sector_count;
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
static void print_stream_name(const u8 *e) {
    u16 chars=rd16(e+64)/2;
    if (chars>32) chars=32;
    for (u16 i=0;i+1<chars;i++) {
        u16 ch=rd16(e+i*2);
        char b[2];
        b[0]=(ch>=32 && ch<127)?(char)ch:'?'; b[1]=0;
        out(b);
    }
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
    u32 dir_sid=rd32(file_bytes+48);
    u8 *dir=dir_buffer;
    u32 dir_bytes=read_chain(dir_sid,dir,MAX_DIR);
    if (dir_bytes<128) { out("msiexec: invalid directory stream\n"); return; }
    msi_ok=1;
    out("MSI compound database: structurally readable\n");
    out("Container version: "); out_u32(major); out("\n");
    out("Streams/storage entries:\n");
    u32 count=0;
    for (u32 off=0;off+128<=dir_bytes;off+=128) {
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
    out("Entries: "); out_u32(count); out("\n");
    out("Installer engine: not yet enabled; no files were changed.\n");
    out("Custom actions were not executed.\n");
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
     * NtAllocateVirtualMemory currently caps each request at 16 MiB.
     * Reserve the file buffer as adjacent 16 MiB chunks so the reader's
     * virtual address range remains contiguous without one oversized request.
     */
    const u32 chunk_size=16u*1024u*1024u;
    const u64 buffer_base=0x50000000ULL;
    u8 *buf=(u8 *)buffer_base;
    for (u32 off=0;off<MAX_FILE;off+=chunk_size) {
        out("msiexec: allocating buffer chunk at offset ");
        out_u32(off);
        out("\n");
        u64 p=sy3(SYS_Alloc,buffer_base+off,chunk_size,0);
        if (p!=(buffer_base+off)) {
            out("msiexec: could not allocate MSI buffer chunk at offset ");
            out_u32(off);
            out("\n");
            (void)sy3(SYS_CloseFile,h,0,0);
            show_and_exit("MSI Reader",
                "Not enough user memory to inspect this MSI. No files were changed.",1);
        }
        out("msiexec: buffer chunk ready\n");
    }
    u32 total=0;
    while(total<MAX_FILE) {
        u64 want=MAX_FILE-total;
        if (want>1024u*1024u) want=1024u*1024u;
        u64 n=sy3(SYS_ReadFile,h,(u64)(buf+total),want);
        if ((long long)n<0) break;
        if (!n) break;
        total+=(u32)n;
    }
    (void)sy3(SYS_CloseFile,h,0,0);
    file_bytes=buf; file_size=total;
    if (total==MAX_FILE) out("msiexec: note: package inspection is capped at 256 MiB\n");
    inspect_msi();
    if (msi_ok)
        show_and_exit("MSI Reader",
            "MSI container is readable. Table decoding and installation are not implemented; no files were changed.",0);
    show_and_exit("MSI Reader",
        "This file is not a supported MSI compound database. No files were changed.",1);
}
