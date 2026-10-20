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
static u32 mini_fat_count, mini_stream_size, mini_sector_count;
static u32 fat_count, sector_size, sector_count, mini_cutoff;
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
    mini_cutoff=rd32(file_bytes+56);
    if (!mini_cutoff) mini_cutoff=4096;
    u32 dir_sid=rd32(file_bytes+48);
    u8 *dir=dir_buffer;
    u32 dir_bytes=read_chain(dir_sid,dir,MAX_DIR);
    if (dir_bytes<128) { out("msiexec: invalid directory stream\n"); return; }
    if (!load_mini_stream(dir,dir_bytes)) {
        out("msiexec: mini-stream metadata is unsupported or exceeds limits\n");
        return;
    }
    msi_ok=1;
    msi_major=major;
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
    msi_entry_count=count;
    out("Entries: "); out_u32(count); out("\n");
    out("CFB mini-streams: readable\n");
    out("MSI table decoding and installation actions are still pending; no files were changed.\n");
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
    msi_ui_line(text,hwnd,11,"Installation: NOT STARTED");
    msi_ui_line(text,hwnd,12,"");
    msi_ui_line(text,hwnd,13,"This build reads the MSI container, but is not yet a");
    msi_ui_line(text,hwnd,14,"Windows Installer engine. MSI tables, authored UI,");
    msi_ui_line(text,hwnd,15,"file/component actions, rollback, and custom actions");
    msi_ui_line(text,hwnd,16,"are not implemented. Install is intentionally disabled");
    msi_ui_line(text,hwnd,17,"to avoid leaving a partial installation.");
    msi_ui_line(text,hwnd,18,"");
    msi_ui_line(text,hwnd,19,"Press Esc or click X to close.");
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
