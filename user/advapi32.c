typedef unsigned char BYTE;
typedef unsigned short WCHAR;
typedef unsigned int DWORD;
typedef unsigned int UINT;
typedef unsigned long ULONG;
typedef int BOOL;
typedef long LONG;
typedef void *HANDLE;
typedef void *HKEY;
typedef const char *LPCSTR;
typedef char *LPSTR;
typedef const WCHAR *LPCWSTR;
typedef WCHAR *LPWSTR;
typedef unsigned long long ULONG_PTR;
typedef unsigned long long u64;
typedef struct { DWORD dwLowDateTime; DWORD dwHighDateTime; } FILETIME;
typedef void *PVOID;
typedef FILETIME *PFILETIME;

#define ERROR_FILE_NOT_FOUND 2
#define ERROR_INVALID_HANDLE 6
#define ERROR_INSUFFICIENT_BUFFER 122
#define ERROR_NO_TOKEN 1008
#define ERROR_CALL_NOT_IMPLEMENTED 120
#define ERROR_NOT_SUPPORTED 50
#define ERROR_SUCCESS 0
#define ERROR_ACCESS_DENIED 5
#define ERROR_NO_MORE_ITEMS 259
#define ERROR_MORE_DATA 234
#define ERROR_BADKEY 1000
#define ERROR_INVALID_PARAMETER 87
#define ERROR_ALREADY_EXISTS 183
#define ERROR_CANTOPEN 1105
#define ERROR_CANTREAD 1106
#define ERROR_CANTWRITE 1107

/* Registry value types */
#define REG_NONE 0
#define REG_SZ 1
#define REG_EXPAND_SZ 2
#define REG_BINARY 3
#define REG_DWORD 4
#define REG_DWORD_LITTLE_ENDIAN 4
#define REG_DWORD_BIG_ENDIAN 5
#define REG_LINK 6
#define REG_MULTI_SZ 7
#define REG_QWORD 11

/* Predefined keys (sentinel HKEY values; mapped internally to table slots) */
#define HKEY_CLASSES_ROOT       ((HKEY)(unsigned long long)0x80000000ULL)
#define HKEY_CURRENT_USER       ((HKEY)(unsigned long long)0x80000001ULL)
#define HKEY_LOCAL_MACHINE      ((HKEY)(unsigned long long)0x80000002ULL)
#define HKEY_USERS              ((HKEY)(unsigned long long)0x80000003ULL)
#define HKEY_CURRENT_CONFIG     ((HKEY)(unsigned long long)0x80000005ULL)

/* Key access masks */
#define KEY_QUERY_VALUE         0x0001
#define KEY_SET_VALUE           0x0002
#define KEY_CREATE_SUB_KEY      0x0004
#define KEY_ENUMERATE_SUB_KEYS  0x0008
#define KEY_NOTIFY              0x0010
#define KEY_READ                0x00020019u
#define KEY_WRITE               0x00020006u
#define KEY_ALL_ACCESS          0x000f003fu
#define KEY_WOW64_64KEY         0x0100

/* RegGetValue flags */
#define RRF_RT_REG_SZ           0x00000001u
#define RRF_RT_REG_DWORD        0x00000002u
#define RRF_RT_REG_QWORD        0x00000040u
#define RRF_RT_ANY              0x0000FFFFu
#define RRF_ZEROONFAILURE       0x00000001u

/* Create options / dispositions */
#define REG_OPTION_NON_VOLATILE 0
#define REG_OPTION_VOLATILE     1
#define REG_OPENED_EXISTING_KEY 1
#define REG_CREATED_NEW_KEY     2

/* Hive file: precreated at build time (build.ps1) because NTFS cannot grow
   files and there is no runtime file-create syscall. We overwrite within the
   preallocated size on every mutation, so changes survive reboot. */
#define REG_HIVE_PATH "System32/config/registry.hive"
#define REG_HIVE_MAGIC "SENHIVE1"
#define REG_HIVE_VERSION 1

#define REG_MAX_KEYS 1024
#define REG_MAX_VALUES 4096
#define REG_NAME_MAX 128     /* WCHARs */
#define REG_HEAP_SIZE (4u * 1024u * 1024u)  /* value data pool */

typedef struct {
    int used;
    int parent;          /* index into reg_keys, -1 = root */
    WCHAR name[REG_NAME_MAX];
    int value_count;
    DWORD access_mask;   /* granted access */
    BYTE sd[256];        /* per-key security descriptor */
} ADVAPI_KEY;

typedef struct {
    int used;
    int owner;           /* index into reg_keys */
    DWORD type;
    DWORD data_size;
    DWORD data_offset;   /* offset into reg_heap, 0xFFFFFFFF if none */
    WCHAR name[REG_NAME_MAX];
} ADVAPI_VALUE;

static ADVAPI_KEY reg_keys[REG_MAX_KEYS];
static ADVAPI_VALUE reg_values[REG_MAX_VALUES];
static BYTE reg_heap[REG_HEAP_SIZE];
static DWORD reg_heap_used;

/* root sentinel -> table index */
static int g_root_idx[5];

/* ---------- low level helpers ---------- */

static u64 reg_sy3(u64 n, u64 a, u64 b, u64 c) {
    u64 ret;
    __asm__ volatile("mov %1, %%rax\n mov %2, %%rdi\n mov %3, %%rsi\n mov %4, %%rdx\n int $0x80\n mov %%rax, %0"
                     : "=r"(ret)
                     : "r"(n), "r"(a), "r"(b), "r"(c)
                     : "rax", "rdi", "rsi", "rdx", "memory");
    return ret;
}

static void *reg_memset(void *s, int c, u64 n) {
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}
static void *reg_memcpy(void *d, const void *s, u64 n) {
    unsigned char *pd = (unsigned char *)d;
    const unsigned char *ps = (const unsigned char *)s;
    while (n--) *pd++ = *ps++;
    return d;
}
static int reg_memcmp(const void *a, const void *b, u64 n) {
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    while (n--) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        pa++; pb++;
    }
    return 0;
}
static int reg_strlen(const char *s) {
    int n = 0;
    while (s && *s) { n++; s++; }
    return n;
}
static int reg_wcslen(const WCHAR *s) {
    int n = 0;
    while (s && *s) { n++; s++; }
    return n;
}
static int reg_wcscmp(const WCHAR *a, const WCHAR *b) {
    if (!a || !b) return a == b ? 0 : 1;
    while (*a && *a == *b) { a++; b++; }
    return (int)*a - (int)*b;
}
static int reg_wcscmpi(const WCHAR *a, const WCHAR *b) {
    if (!a || !b) return a == b ? 0 : 1;
    while (*a && *b) {
        WCHAR ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += (WCHAR)0x20;
        if (cb >= 'A' && cb <= 'Z') cb += (WCHAR)0x20;
        if (ca != cb) return (int)ca - (int)cb;
        a++; b++;
    }
    WCHAR ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca += (WCHAR)0x20;
    if (cb >= 'A' && cb <= 'Z') cb += (WCHAR)0x20;
    return (int)ca - (int)cb;
}
static void reg_wcscpy(WCHAR *d, const WCHAR *s) {
    while (*s) { *d++ = *s++; }
    *d = 0;
}

/* ANSI <-> WCHAR conversion (static buffers; regedit is single-threaded) */
static WCHAR reg_wbuf[2048];
static char reg_abuf[4096];
static int reg_mbstowcs(WCHAR *out, int outsz, const char *in) {
    int i = 0;
    if (!out || !in || outsz <= 0) return 0;
    while (in[i] && i + 1 < outsz) { out[i] = (WCHAR)(unsigned char)in[i]; i++; }
    out[i] = 0;
    return i;
}
static int reg_wcstombs(char *out, int outsz, const WCHAR *in) {
    int i = 0;
    if (!out || !in || outsz <= 0) return 0;
    while (in[i] && i + 1 < outsz) { out[i] = (char)(in[i] & 0xff); i++; }
    out[i] = 0;
    return i;
}

/* ---------- file I/O (raw syscalls, no kernel32 needed) ---------- */

static int reg_file_open(void) {
    /* SYS_OpenFile(5, name, 0, 0): opens existing file read/write. Path is narrow char. */
    return (int)reg_sy3(5, (u64)REG_HIVE_PATH, 0, 0);
}
static int reg_file_read(int h, void *buf, unsigned len) {
    return (int)reg_sy3(6, (u64)h, (u64)buf, (u64)len);
}
static int reg_file_write(int h, const void *buf, unsigned len) {
    /* SYS_NtWriteOutputFile(52): overwrites within existing file size at h->pos */
    return (int)reg_sy3(52, (u64)h, (u64)buf, (u64)len);
}
static int reg_file_seek(int h, long long off, int method) {
    /* SYS_NtSetFilePosition(26): method 0 = FILE_BEGIN */
    return (int)reg_sy3(26, (u64)h, (u64)(unsigned long long)off, (u64)method);
}
static int reg_file_size(int h) {
    return (int)reg_sy3(25, (u64)h, 0, 0);
}
static int reg_file_close(int h) {
    return (int)reg_sy3(22, (u64)h, 0, 0);
}

/* ---------- heap allocator for value data ---------- */

static DWORD reg_heap_alloc(DWORD n) {
    if (!n) return 0xFFFFFFFF;
    if (reg_heap_used + n > REG_HEAP_SIZE) return 0xFFFFFFFF;
    DWORD off = reg_heap_used;
    reg_heap_used += n;
    return off;
}

/* ---------- handle / access helpers ---------- */

static int reg_key_index(ADVAPI_KEY *k) {
    return (int)(k - reg_keys);
}

static ADVAPI_KEY *reg_key_from_handle(HKEY h) {
    unsigned long long v = (unsigned long long)h;
    if (h == HKEY_CLASSES_ROOT)       return g_root_idx[0] >= 0 ? &reg_keys[g_root_idx[0]] : 0;
    if (h == HKEY_CURRENT_USER)       return g_root_idx[1] >= 0 ? &reg_keys[g_root_idx[1]] : 0;
    if (h == HKEY_LOCAL_MACHINE)      return g_root_idx[2] >= 0 ? &reg_keys[g_root_idx[2]] : 0;
    if (h == HKEY_USERS)              return g_root_idx[3] >= 0 ? &reg_keys[g_root_idx[3]] : 0;
    if (h == HKEY_CURRENT_CONFIG)     return g_root_idx[4] >= 0 ? &reg_keys[g_root_idx[4]] : 0;
    int idx = (int)v - 1;
    if (idx < 0 || idx >= REG_MAX_KEYS) return 0;
    return reg_keys[idx].used ? &reg_keys[idx] : 0;
}

static int reg_find_child(ADVAPI_KEY *parent, const WCHAR *name) {
    int pidx = reg_key_index(parent);
    for (int i = 0; i < REG_MAX_KEYS; i++) {
        if (reg_keys[i].used && reg_keys[i].parent == pidx &&
            reg_wcscmpi(reg_keys[i].name, name) == 0)
            return i;
    }
    return -1;
}
static int reg_find_value(int owner, const WCHAR *name) {
    for (int i = 0; i < REG_MAX_VALUES; i++) {
        if (reg_values[i].used && reg_values[i].owner == owner &&
            reg_wcscmp(reg_values[i].name, name) == 0)
            return i;
    }
    return -1;
}

/* ---------- hive persistence ---------- */

static void reg_establish_roots(void) {
    for (int i = 0; i < 5; i++) g_root_idx[i] = -1;
    for (int i = 0; i < REG_MAX_KEYS; i++) {
        if (!reg_keys[i].used || reg_keys[i].parent != -1) continue;
        if (reg_wcscmp(reg_keys[i].name, L"Classes") == 0)       g_root_idx[0] = i;
        else if (reg_wcscmp(reg_keys[i].name, L"CurrentUser") == 0) g_root_idx[1] = i;
        else if (reg_wcscmp(reg_keys[i].name, L"Machine") == 0)    g_root_idx[2] = i;
        else if (reg_wcscmp(reg_keys[i].name, L"Users") == 0)      g_root_idx[3] = i;
        else if (reg_wcscmp(reg_keys[i].name, L"CurrentConfig") == 0) g_root_idx[4] = i;
    }
}

static int reg_hive_save(void) {
    int h = reg_file_open();
    if (h < 0) return -1;
    DWORD g_cksum = 0;
    int nkeys = 0, nvalues = 0;
    for (int i = 0; i < REG_MAX_KEYS; i++) if (reg_keys[i].used) nkeys++;
    for (int i = 0; i < REG_MAX_VALUES; i++) if (reg_values[i].used) nvalues++;

    BYTE hdr[28];
    reg_memcpy(hdr, REG_HIVE_MAGIC, 8);
    hdr[8] = 1 & 0xff; hdr[9] = 0; hdr[10] = 0; hdr[11] = 0;            /* version */
    hdr[12] = (BYTE)nkeys; hdr[13] = (BYTE)(nkeys >> 8); hdr[14] = (BYTE)(nkeys >> 16); hdr[15] = (BYTE)(nkeys >> 24);
    hdr[16] = (BYTE)nvalues; hdr[17] = (BYTE)(nvalues >> 8); hdr[18] = (BYTE)(nvalues >> 16); hdr[19] = (BYTE)(nvalues >> 24);
    hdr[20] = (BYTE)reg_heap_used; hdr[21] = (BYTE)(reg_heap_used >> 8); hdr[22] = (BYTE)(reg_heap_used >> 16); hdr[23] = (BYTE)(reg_heap_used >> 24);
    hdr[24] = 0; hdr[25] = 0; hdr[26] = 0; hdr[27] = 0;                 /* checksum placeholder */
    for (int i = 0; i < 24; i++) g_cksum += hdr[i];
    reg_file_write(h, hdr, 28);

    for (int i = 0; i < REG_MAX_KEYS; i++) {
        if (!reg_keys[i].used) continue;
        ADVAPI_KEY *k = &reg_keys[i];
        BYTE rec[16];
        unsigned p = (unsigned)(k->parent == -1 ? 0xFFFFFFFFu : (unsigned)k->parent);
        rec[0] = (BYTE)p; rec[1] = (BYTE)(p >> 8); rec[2] = (BYTE)(p >> 16); rec[3] = (BYTE)(p >> 24);
        DWORD nlen = (DWORD)reg_wcslen(k->name);
        rec[4] = (BYTE)nlen; rec[5] = (BYTE)(nlen >> 8); rec[6] = (BYTE)(nlen >> 16); rec[7] = (BYTE)(nlen >> 24);
        rec[8] = (BYTE)k->access_mask; rec[9] = (BYTE)(k->access_mask >> 8);
        rec[10] = (BYTE)(k->access_mask >> 16); rec[11] = (BYTE)(k->access_mask >> 24);
        rec[12] = (BYTE)k->value_count; rec[13] = (BYTE)(k->value_count >> 8);
        rec[14] = (BYTE)(k->value_count >> 16); rec[15] = (BYTE)(k->value_count >> 24);
        for (int j = 0; j < 16; j++) g_cksum += rec[j];
        reg_file_write(h, rec, 16);
        for (DWORD j = 0; j < nlen; j++) {
            g_cksum += (BYTE)k->name[j]; g_cksum += (BYTE)(k->name[j] >> 8);
        }
        reg_file_write(h, k->name, nlen * 2);
    }
    for (int i = 0; i < REG_MAX_VALUES; i++) {
        if (!reg_values[i].used) continue;
        ADVAPI_VALUE *v = &reg_values[i];
        BYTE rec[20];
        rec[0] = (BYTE)v->owner; rec[1] = (BYTE)(v->owner >> 8); rec[2] = (BYTE)(v->owner >> 16); rec[3] = (BYTE)(v->owner >> 24);
        rec[4] = (BYTE)v->type; rec[5] = (BYTE)(v->type >> 8); rec[6] = (BYTE)(v->type >> 16); rec[7] = (BYTE)(v->type >> 24);
        rec[8] = (BYTE)v->data_size; rec[9] = (BYTE)(v->data_size >> 8); rec[10] = (BYTE)(v->data_size >> 16); rec[11] = (BYTE)(v->data_size >> 24);
        rec[12] = (BYTE)v->data_offset; rec[13] = (BYTE)(v->data_offset >> 8); rec[14] = (BYTE)(v->data_offset >> 16); rec[15] = (BYTE)(v->data_offset >> 24);
        DWORD nlen = (DWORD)reg_wcslen(v->name);
        rec[16] = (BYTE)nlen; rec[17] = (BYTE)(nlen >> 8); rec[18] = (BYTE)(nlen >> 16); rec[19] = (BYTE)(nlen >> 24);
        for (int j = 0; j < 20; j++) g_cksum += rec[j];
        reg_file_write(h, rec, 20);
        for (DWORD j = 0; j < nlen; j++) {
            g_cksum += (BYTE)v->name[j]; g_cksum += (BYTE)(v->name[j] >> 8);
        }
        reg_file_write(h, v->name, nlen * 2);
        if (v->data_size && v->data_offset != 0xFFFFFFFF) {
            for (DWORD j = 0; j < v->data_size; j++) g_cksum += reg_heap[v->data_offset + j];
            reg_file_write(h, reg_heap + v->data_offset, v->data_size);
        }
    }
    reg_file_seek(h, 24, 0);
    reg_file_write(h, &g_cksum, 4);
    reg_file_close(h);
    return 0;
}

static int reg_hive_load(void) {
    int h = reg_file_open();
    if (h < 0) return -1;
    int sz = reg_file_size(h);
    if (sz < 28) { reg_file_close(h); return -1; }
    BYTE hdr[28];
    if (reg_file_read(h, hdr, 28) != 28) { reg_file_close(h); return -1; }
    if (reg_memcmp(hdr, REG_HIVE_MAGIC, 8) != 0) { reg_file_close(h); return -1; }
    if (hdr[8] != REG_HIVE_VERSION) { reg_file_close(h); return -1; }
    DWORD nkeys = (DWORD)hdr[12] | ((DWORD)hdr[13] << 8) | ((DWORD)hdr[14] << 16) | ((DWORD)hdr[15] << 24);
    DWORD nvalues = (DWORD)hdr[16] | ((DWORD)hdr[17] << 8) | ((DWORD)hdr[18] << 16) | ((DWORD)hdr[19] << 24);
    DWORD heap_used = (DWORD)hdr[20] | ((DWORD)hdr[21] << 8) | ((DWORD)hdr[22] << 16) | ((DWORD)hdr[23] << 24);
    DWORD stored_ck = (DWORD)hdr[24] | ((DWORD)hdr[25] << 8) | ((DWORD)hdr[26] << 16) | ((DWORD)hdr[27] << 24);
    if (nkeys > REG_MAX_KEYS || nvalues > REG_MAX_VALUES || heap_used > REG_HEAP_SIZE) {
        reg_file_close(h); return -1;
    }
    DWORD g_cksum = 0;
    for (int i = 0; i < 24; i++) g_cksum += hdr[i];

    reg_memset(reg_keys, 0, sizeof(reg_keys));
    reg_memset(reg_values, 0, sizeof(reg_values));
    reg_heap_used = 0;

    for (DWORD i = 0; i < nkeys; i++) {
        BYTE rec[16];
        if (reg_file_read(h, rec, 16) != 16) { reg_file_close(h); return -1; }
        int parent = (int)((DWORD)rec[0] | ((DWORD)rec[1] << 8) | ((DWORD)rec[2] << 16) | ((DWORD)rec[3] << 24));
        if (parent == 0xFFFFFFFF) parent = -1;
        DWORD nlen = (DWORD)rec[4] | ((DWORD)rec[5] << 8) | ((DWORD)rec[6] << 16) | ((DWORD)rec[7] << 24);
        DWORD access = (DWORD)rec[8] | ((DWORD)rec[9] << 8) | ((DWORD)rec[10] << 16) | ((DWORD)rec[11] << 24);
        DWORD vcount = (DWORD)rec[12] | ((DWORD)rec[13] << 8) | ((DWORD)rec[14] << 16) | ((DWORD)rec[15] << 24);
        if (nlen >= REG_NAME_MAX) { reg_file_close(h); return -1; }
        int slot = -1;
        for (int j = 0; j < REG_MAX_KEYS; j++) { if (!reg_keys[j].used) { slot = j; break; } }
        if (slot < 0) { reg_file_close(h); return -1; }
        ADVAPI_KEY *k = &reg_keys[slot];
        k->used = 1; k->parent = parent; k->access_mask = access; k->value_count = (int)vcount;
        if (reg_file_read(h, k->name, nlen * 2) != (int)(nlen * 2)) { reg_file_close(h); return -1; }
        k->name[nlen] = 0;
        for (DWORD j = 0; j < nlen; j++) { g_cksum += (BYTE)k->name[j]; g_cksum += (BYTE)(k->name[j] >> 8); }
    }
    for (DWORD i = 0; i < nvalues; i++) {
        BYTE rec[20];
        if (reg_file_read(h, rec, 20) != 20) { reg_file_close(h); return -1; }
        int owner = (int)((DWORD)rec[0] | ((DWORD)rec[1] << 8) | ((DWORD)rec[2] << 16) | ((DWORD)rec[3] << 24));
        DWORD type = (DWORD)rec[4] | ((DWORD)rec[5] << 8) | ((DWORD)rec[6] << 16) | ((DWORD)rec[7] << 24);
        DWORD data_size = (DWORD)rec[8] | ((DWORD)rec[9] << 8) | ((DWORD)rec[10] << 16) | ((DWORD)rec[11] << 24);
        DWORD nlen = (DWORD)rec[16] | ((DWORD)rec[17] << 8) | ((DWORD)rec[18] << 16) | ((DWORD)rec[19] << 24);
        if (nlen >= REG_NAME_MAX) { reg_file_close(h); return -1; }
        int slot = -1;
        for (int j = 0; j < REG_MAX_VALUES; j++) { if (!reg_values[j].used) { slot = j; break; } }
        if (slot < 0) { reg_file_close(h); return -1; }
        ADVAPI_VALUE *v = &reg_values[slot];
        v->used = 1; v->owner = owner; v->type = type; v->data_size = data_size;
        v->data_offset = 0xFFFFFFFF;
        if (reg_file_read(h, v->name, nlen * 2) != (int)(nlen * 2)) { reg_file_close(h); return -1; }
        v->name[nlen] = 0;
        for (DWORD j = 0; j < nlen; j++) { g_cksum += (BYTE)v->name[j]; g_cksum += (BYTE)(v->name[j] >> 8); }
        if (data_size) {
            if (reg_heap_used + data_size > REG_HEAP_SIZE) { reg_file_close(h); return -1; }
            v->data_offset = reg_heap_used;
            if (reg_file_read(h, reg_heap + reg_heap_used, data_size) != (int)data_size) {
                reg_file_close(h); return -1;
            }
            for (DWORD j = 0; j < data_size; j++) g_cksum += reg_heap[reg_heap_used + j];
            reg_heap_used += data_size;
        }
    }
    reg_file_close(h);
    if (g_cksum != stored_ck) return -1; /* corrupted hive -> caller starts empty */
    reg_establish_roots();
    return 0;
}

static int reg_alloc_key(void) {
    for (int i = 0; i < REG_MAX_KEYS; i++) {
        if (!reg_keys[i].used) return i;
    }
    return -1;
}
static int reg_alloc_value(void) {
    for (int i = 0; i < REG_MAX_VALUES; i++) {
        if (!reg_values[i].used) return i;
    }
    return -1;
}
/* Initialize a default security descriptor for a key. Forward declared here;
 * defined below after InitializeSecurityDescriptor. */
static int reg_init_default_sd(BYTE *sd_buf);
static int reg_create_root(const WCHAR *name) {
    int slot = reg_alloc_key();
    if (slot < 0) return -1;
    reg_keys[slot].used = 1;
    reg_keys[slot].parent = -1;
    reg_keys[slot].access_mask = KEY_ALL_ACCESS;
    reg_keys[slot].value_count = 0;
    reg_init_default_sd(reg_keys[slot].sd);
    reg_wcscpy(reg_keys[slot].name, name);
    return slot;
}

static int reg_count_children(int parent_index) {
    int n = 0;
    for (int i = 0; i < REG_MAX_KEYS; i++)
        if (reg_keys[i].used && reg_keys[i].parent == parent_index) n++;
    return n;
}
static int reg_child_at(int parent_index, DWORD index) {
    int seen = 0;
    for (int i = 0; i < REG_MAX_KEYS; i++) {
        if (reg_keys[i].used && reg_keys[i].parent == parent_index) {
            if (seen == (int)index) return i;
            seen++;
        }
    }
    return -1;
}
static int reg_count_values(int owner) {
    int n = 0;
    for (int i = 0; i < REG_MAX_VALUES; i++)
        if (reg_values[i].used && reg_values[i].owner == owner) n++;
    return n;
}
static int reg_value_at(int owner, DWORD index) {
    int seen = 0;
    for (int i = 0; i < REG_MAX_VALUES; i++) {
        if (reg_values[i].used && reg_values[i].owner == owner) {
            if (seen == (int)index) return i;
            seen++;
        }
    }
    return -1;
}

/* delete a value and all values/keys under a key recursively */
static void reg_drop_values_of_key(int key_index) {
    for (int i = 0; i < REG_MAX_VALUES; i++) {
        if (reg_values[i].used && reg_values[i].owner == key_index)
            reg_values[i].used = 0;
    }
}
static void reg_delete_tree_recursive(int key_index) {
    /* delete children first */
    for (int i = 0; i < REG_MAX_KEYS; i++) {
        if (reg_keys[i].used && reg_keys[i].parent == key_index)
            reg_delete_tree_recursive(i);
    }
    reg_drop_values_of_key(key_index);
    reg_keys[key_index].used = 0;
}

/* ---------- registry APIs ---------- */

__attribute__((dllexport)) LONG RegCloseKey(HKEY key) {
    if (!reg_key_from_handle(key)) return ERROR_INVALID_HANDLE;
    return ERROR_SUCCESS;
}

__attribute__((dllexport)) LONG RegCreateKeyExW(HKEY key, const WCHAR *sub, DWORD reserved, WCHAR *class,
                                                  DWORD options, DWORD desired, void *security,
                                                  HKEY *result, DWORD *disposition) {
    (void)reserved; (void)class; (void)options; (void)security;
    if (!result) return ERROR_INVALID_PARAMETER;
    ADVAPI_KEY *p = reg_key_from_handle(key);
    if (!p) return ERROR_INVALID_HANDLE;
    if (!sub || !*sub) {
        *result = key;
        if (disposition) *disposition = REG_OPENED_EXISTING_KEY;
        return ERROR_SUCCESS;
    }
    if ((p->access_mask & KEY_CREATE_SUB_KEY) != KEY_CREATE_SUB_KEY) return ERROR_ACCESS_DENIED;
    int idx = reg_find_child(p, sub);
    if (idx >= 0) {
        ADVAPI_KEY *c = &reg_keys[idx];
        if (desired && (c->access_mask & desired) != desired) return ERROR_ACCESS_DENIED;
        *result = (HKEY)(unsigned long long)(idx + 1);
        if (disposition) *disposition = REG_OPENED_EXISTING_KEY;
        reg_hive_save();
        return ERROR_SUCCESS;
    }
    int slot = reg_alloc_key();
    if (slot < 0) return ERROR_NOT_SUPPORTED;
    ADVAPI_KEY *c = &reg_keys[slot];
    c->used = 1;
    c->parent = reg_key_index(p);
    c->access_mask = desired ? desired : (KEY_READ | KEY_WRITE | KEY_CREATE_SUB_KEY | KEY_ENUMERATE_SUB_KEYS);
    c->value_count = 0;
    reg_init_default_sd(c->sd);
    reg_wcscpy(c->name, sub);
    *result = (HKEY)(unsigned long long)(slot + 1);
    if (disposition) *disposition = REG_CREATED_NEW_KEY;
    reg_hive_save();
    return ERROR_SUCCESS;
}

__attribute__((dllexport)) LONG RegCreateKeyExA(HKEY key, const char *sub, DWORD reserved, char *class,
                                                 DWORD options, DWORD desired, void *security,
                                                 HKEY *result, DWORD *disposition) {
    (void)class;
    if (!result) return ERROR_INVALID_PARAMETER;
    if (!sub || !*sub) {
        *result = key;
        if (disposition) *disposition = REG_OPENED_EXISTING_KEY;
        return ERROR_SUCCESS;
    }
    reg_mbstowcs(reg_wbuf, 2048, sub);
    return RegCreateKeyExW(key, reg_wbuf, reserved, 0, options, desired, security, result, disposition);
}

__attribute__((dllexport)) LONG RegOpenKeyExW(HKEY key, const WCHAR *sub, DWORD opt, DWORD access, HKEY *out) {
    (void)opt;
    if (!out) return ERROR_INVALID_PARAMETER;
    ADVAPI_KEY *p = reg_key_from_handle(key);
    if (!p) return ERROR_INVALID_HANDLE;
    if (!sub || !*sub) { *out = key; return ERROR_SUCCESS; }
    if ((p->access_mask & KEY_ENUMERATE_SUB_KEYS) != KEY_ENUMERATE_SUB_KEYS) return ERROR_ACCESS_DENIED;
    int idx = reg_find_child(p, sub);
    if (idx < 0) return ERROR_FILE_NOT_FOUND;
    ADVAPI_KEY *c = &reg_keys[idx];
    if (access && (c->access_mask & access) != access) return ERROR_ACCESS_DENIED;
    *out = (HKEY)(unsigned long long)(idx + 1);
    return ERROR_SUCCESS;
}

__attribute__((dllexport)) LONG RegOpenKeyExA(HKEY key, const char *sub, DWORD opt, DWORD access, HKEY *out) {
    if (!out) return ERROR_INVALID_PARAMETER;
    if (!sub || !*sub) { *out = key; return ERROR_SUCCESS; }
    int n = reg_mbstowcs(reg_wbuf, 2048, sub);
    (void)n;
    return RegOpenKeyExW(key, reg_wbuf, opt, access, out);
}

__attribute__((dllexport)) LONG RegDeleteKeyW(HKEY key, const WCHAR *sub) {
    ADVAPI_KEY *p = reg_key_from_handle(key);
    if (!p) return ERROR_INVALID_HANDLE;
    if (!sub || !*sub) return ERROR_INVALID_PARAMETER;
    if ((p->access_mask & KEY_SET_VALUE) != KEY_SET_VALUE) return ERROR_ACCESS_DENIED;
    int idx = reg_find_child(p, sub);
    if (idx < 0) return ERROR_FILE_NOT_FOUND;
    ADVAPI_KEY *c = &reg_keys[idx];
    if (reg_count_children(idx) > 0) return ERROR_ACCESS_DENIED; /* non-empty */
    reg_drop_values_of_key(idx);
    c->used = 0;
    reg_hive_save();
    return ERROR_SUCCESS;
}

__attribute__((dllexport)) LONG RegDeleteKeyA(HKEY key, const char *sub) {
    if (!sub || !*sub) return ERROR_INVALID_PARAMETER;
    reg_mbstowcs(reg_wbuf, 2048, sub);
    return RegDeleteKeyW(key, reg_wbuf);
}

__attribute__((dllexport)) LONG RegDeleteKeyExW(HKEY key, const WCHAR *sub, DWORD sam, DWORD reserved) {
    (void)sam; (void)reserved;
    return RegDeleteKeyW(key, sub);
}
__attribute__((dllexport)) LONG RegDeleteKeyExA(HKEY key, const char *sub, DWORD sam, DWORD reserved) {
    (void)sam; (void)reserved;
    return RegDeleteKeyA(key, sub);
}

__attribute__((dllexport)) LONG RegDeleteTreeW(HKEY key, const WCHAR *sub) {
    ADVAPI_KEY *p = reg_key_from_handle(key);
    if (!p) return ERROR_INVALID_HANDLE;
    if (!sub || !*sub) return ERROR_INVALID_PARAMETER;
    if ((p->access_mask & KEY_SET_VALUE) != KEY_SET_VALUE) return ERROR_ACCESS_DENIED;
    int idx = reg_find_child(p, sub);
    if (idx < 0) return ERROR_FILE_NOT_FOUND;
    reg_delete_tree_recursive(idx);
    reg_hive_save();
    return ERROR_SUCCESS;
}
__attribute__((dllexport)) LONG RegDeleteTreeA(HKEY key, const char *sub) {
    if (!sub || !*sub) return ERROR_INVALID_PARAMETER;
    reg_mbstowcs(reg_wbuf, 2048, sub);
    return RegDeleteTreeW(key, reg_wbuf);
}

/* Delete a single named value from a key. Follows the RegDeleteKey pattern:
 * validate handle/access, locate the value, mark it unused, persist the hive. */
static void reg_drop_value(int value_index) {
    if (value_index < 0 || value_index >= REG_MAX_VALUES) return;
    reg_values[value_index].used = 0;
}

__attribute__((dllexport)) LONG RegDeleteValueW(HKEY key, const WCHAR *name) {
    ADVAPI_KEY *k = reg_key_from_handle(key);
    if (!k) return ERROR_INVALID_HANDLE;
    if ((k->access_mask & KEY_SET_VALUE) != KEY_SET_VALUE) return ERROR_ACCESS_DENIED;
    if (!name || !*name) return ERROR_INVALID_PARAMETER;
    int kidx = reg_key_index(k);
    int v = reg_find_value(kidx, name);
    if (v < 0) return ERROR_FILE_NOT_FOUND;
    reg_drop_value(v);
    reg_hive_save();
    return ERROR_SUCCESS;
}
__attribute__((dllexport)) LONG RegDeleteValueA(HKEY key, const char *name) {
    if (!name || !*name) return ERROR_INVALID_PARAMETER;
    reg_mbstowcs(reg_wbuf, 2048, name);
    return RegDeleteValueW(key, reg_wbuf);
}

__attribute__((dllexport)) LONG RegEnumKeyExW(HKEY key, DWORD index, WCHAR *name, DWORD *name_size,
                                                DWORD *reserved, WCHAR *class, DWORD *class_size,
                                                FILETIME *last_write) {
    (void)reserved; (void)class; (void)class_size; (void)last_write;
    ADVAPI_KEY *k = reg_key_from_handle(key);
    if (!k) return ERROR_INVALID_HANDLE;
    if ((k->access_mask & KEY_ENUMERATE_SUB_KEYS) != KEY_ENUMERATE_SUB_KEYS) return ERROR_ACCESS_DENIED;
    int pidx = reg_key_index(k);
    int child = reg_child_at(pidx, index);
    if (child < 0) return ERROR_NO_MORE_ITEMS;
    DWORD nlen = (DWORD)reg_wcslen(reg_keys[child].name);
    if (name) {
        if (!name_size || *name_size == 0) return ERROR_MORE_DATA;
        DWORD need = nlen + 1;
        if (*name_size < need) { *name_size = need; return ERROR_MORE_DATA; }
        reg_wcscpy(name, reg_keys[child].name);
        *name_size = nlen;
    }
    return ERROR_SUCCESS;
}
__attribute__((dllexport)) LONG RegEnumKeyExA(HKEY key, DWORD index, char *name, DWORD *name_size,
                                                DWORD *reserved, char *class, DWORD *class_size,
                                                FILETIME *last_write) {
    (void)reserved; (void)class; (void)class_size; (void)last_write;
    if (!name_size) return ERROR_INVALID_PARAMETER;
    DWORD wsize = *name_size;
    LONG r = RegEnumKeyExW(key, index, reg_wbuf, &wsize, 0, 0, 0, 0);
    if (r == ERROR_SUCCESS) {
        reg_wcstombs(name, (int)*name_size, reg_wbuf);
        *name_size = (DWORD)reg_strlen(name);
    } else if (r == ERROR_MORE_DATA) {
        *name_size = wsize;
    }
    return r;
}

__attribute__((dllexport)) LONG RegEnumValueW(HKEY key, DWORD index, WCHAR *name, DWORD *name_size,
                                                DWORD *reserved, DWORD *type, BYTE *data, DWORD *data_size) {
    (void)reserved;
    ADVAPI_KEY *k = reg_key_from_handle(key);
    if (!k) return ERROR_INVALID_HANDLE;
    if ((k->access_mask & KEY_ENUMERATE_SUB_KEYS) != KEY_ENUMERATE_SUB_KEYS) return ERROR_ACCESS_DENIED;
    int kidx = reg_key_index(k);
    int v = reg_value_at(kidx, index);
    if (v < 0) return ERROR_NO_MORE_ITEMS;
    ADVAPI_VALUE *val = &reg_values[v];
    DWORD nlen = (DWORD)reg_wcslen(val->name);
    if (name) {
        if (!name_size || *name_size == 0) return ERROR_MORE_DATA;
        DWORD need = nlen + 1;
        if (*name_size < need) { *name_size = need; return ERROR_MORE_DATA; }
        reg_wcscpy(name, val->name);
        *name_size = nlen;
    }
    if (type) *type = val->type;
    if (data) {
        if (!data_size) return ERROR_INVALID_PARAMETER;
        if (*data_size < val->data_size) { *data_size = val->data_size; return ERROR_MORE_DATA; }
        if (val->data_size && val->data_offset != 0xFFFFFFFF)
            reg_memcpy(data, reg_heap + val->data_offset, val->data_size);
        *data_size = val->data_size;
    }
    return ERROR_SUCCESS;
}
__attribute__((dllexport)) LONG RegEnumValueA(HKEY key, DWORD index, char *name, DWORD *name_size,
                                                DWORD *reserved, DWORD *type, BYTE *data, DWORD *data_size) {
    (void)reserved;
    if (!name_size) return ERROR_INVALID_PARAMETER;
    DWORD wsize = *name_size;
    LONG r = RegEnumValueW(key, index, reg_wbuf, &wsize, 0, type, data, data_size);
    if (r == ERROR_SUCCESS) {
        reg_wcstombs(name, (int)*name_size, reg_wbuf);
        *name_size = (DWORD)reg_strlen(name);
    } else if (r == ERROR_MORE_DATA) {
        *name_size = wsize;
    }
    return r;
}

__attribute__((dllexport)) LONG RegQueryValueExW(HKEY key, const WCHAR *name, DWORD *reserved, DWORD *type,
                                                   BYTE *data, DWORD *size) {
    (void)reserved;
    ADVAPI_KEY *k = reg_key_from_handle(key);
    if (!k) return ERROR_INVALID_HANDLE;
    if ((k->access_mask & KEY_QUERY_VALUE) != KEY_QUERY_VALUE) return ERROR_ACCESS_DENIED;
    int v = reg_find_value(reg_key_index(k), name);
    if (v < 0) { if (type) *type = 0; return ERROR_FILE_NOT_FOUND; }
    ADVAPI_VALUE *val = &reg_values[v];
    if (type) *type = val->type;
    if (!data) {
        if (size) *size = val->data_size;
        return ERROR_SUCCESS;
    }
    if (!size) return ERROR_INVALID_PARAMETER;
    if (*size < val->data_size) { *size = val->data_size; return ERROR_MORE_DATA; }
    if (val->data_size && val->data_offset != 0xFFFFFFFF)
        reg_memcpy(data, reg_heap + val->data_offset, val->data_size);
    *size = val->data_size;
    return ERROR_SUCCESS;
}
__attribute__((dllexport)) LONG RegQueryValueExA(HKEY key, const char *name, DWORD *reserved, DWORD *type,
                                                   BYTE *data, DWORD *size) {
    (void)reserved;
    if (!name || !*name) return ERROR_INVALID_PARAMETER;
    reg_mbstowcs(reg_wbuf, 2048, name);
    return RegQueryValueExW(key, reg_wbuf, reserved, type, data, size);
}

__attribute__((dllexport)) LONG RegSetValueExW(HKEY key, const WCHAR *name, DWORD reserved, DWORD type,
                                                 const BYTE *data, DWORD size) {
    (void)reserved;
    ADVAPI_KEY *k = reg_key_from_handle(key);
    if (!k) return ERROR_INVALID_HANDLE;
    if ((k->access_mask & KEY_SET_VALUE) != KEY_SET_VALUE) return ERROR_ACCESS_DENIED;
    if (!name || !*name) return ERROR_INVALID_PARAMETER;
    int kidx = reg_key_index(k);
    int v = reg_find_value(kidx, name);
    ADVAPI_VALUE *val;
    if (v >= 0) {
        val = &reg_values[v];
        val->type = type;
    } else {
        int slot = reg_alloc_value();
        if (slot < 0) return ERROR_NOT_SUPPORTED;
        val = &reg_values[slot];
        val->used = 1;
        val->owner = kidx;
        val->data_offset = 0xFFFFFFFF;
        val->data_size = 0;
        reg_wcscpy(val->name, name);
        reg_keys[kidx].value_count++;
    }
    if (size) {
        DWORD off = reg_heap_alloc(size);
        if (off == 0xFFFFFFFF) return ERROR_NOT_SUPPORTED;
        val->data_offset = off;
        val->data_size = size;
        reg_memcpy(reg_heap + off, data, size);
    } else {
        val->data_offset = 0xFFFFFFFF;
        val->data_size = 0;
    }
    reg_hive_save();
    return ERROR_SUCCESS;
}
__attribute__((dllexport)) LONG RegSetValueExA(HKEY key, const char *name, DWORD reserved, DWORD type,
                                                 const BYTE *data, DWORD size) {
    (void)reserved;
    if (!name || !*name) return ERROR_INVALID_PARAMETER;
    reg_mbstowcs(reg_wbuf, 2048, name);
    return RegSetValueExW(key, reg_wbuf, reserved, type, data, size);
}

__attribute__((dllexport)) LONG RegGetValueW(HKEY key, const WCHAR *sub, const WCHAR *name, DWORD flags,
                                                DWORD *type, void *data, DWORD *size) {
    (void)flags;
    HKEY h = key;
    if (sub && *sub) {
        HKEY found = 0;
        LONG r = RegOpenKeyExW(key, sub, 0, KEY_READ, &found);
        if (r != ERROR_SUCCESS) return r;
        h = found;
    }
    LONG r = RegQueryValueExW(h, name, 0, type, data, size);
    if (h != key) RegCloseKey(h);
    return r;
}
__attribute__((dllexport)) LONG RegGetValueA(HKEY key, const char *sub, const char *name, DWORD flags,
                                                DWORD *type, void *data, DWORD *size) {
    (void)flags;
    if (sub && *sub) reg_mbstowcs(reg_wbuf, 2048, sub);
    /* wbuf holds sub (if present); reg_namebuf holds name if present. */
    static WCHAR reg_namebuf[2048];
    const WCHAR *wname = 0;
    if (name && *name) {
        reg_mbstowcs(reg_namebuf, 2048, name);
        wname = reg_namebuf;
    }
    if (sub && *sub) {
        return RegGetValueW(key, reg_wbuf, wname, flags, type, data, size);
    }
    return RegGetValueW(key, 0, wname, flags, type, data, size);
}

__attribute__((dllexport)) LONG RegQueryInfoKeyW(HKEY key, WCHAR *class, DWORD *class_size, DWORD *reserved,
                                                   DWORD *subkeys, DWORD *max_subkey_len, DWORD *max_class_len,
                                                   DWORD *values, DWORD *max_value_name_len, DWORD *max_value_len,
                                                   PVOID sa, PFILETIME last_write) {
    (void)class; (void)class_size; (void)reserved; (void)max_class_len; (void)sa; (void)last_write;
    ADVAPI_KEY *k = reg_key_from_handle(key);
    if (!k) return ERROR_INVALID_HANDLE;
    if ((k->access_mask & KEY_ENUMERATE_SUB_KEYS) != KEY_ENUMERATE_SUB_KEYS) return ERROR_ACCESS_DENIED;
    int kidx = reg_key_index(k);
    if (subkeys) *subkeys = (DWORD)reg_count_children(kidx);
    if (max_subkey_len) {
        DWORD m = 0;
        for (int i = 0; i < REG_MAX_KEYS; i++)
            if (reg_keys[i].used && reg_keys[i].parent == kidx) {
                DWORD n = (DWORD)reg_wcslen(reg_keys[i].name);
                if (n > m) m = n;
            }
        *max_subkey_len = m;
    }
    if (values) *values = (DWORD)reg_count_values(kidx);
    if (max_value_name_len) {
        DWORD m = 0;
        for (int i = 0; i < REG_MAX_VALUES; i++)
            if (reg_values[i].used && reg_values[i].owner == kidx) {
                DWORD n = (DWORD)reg_wcslen(reg_values[i].name);
                if (n > m) m = n;
            }
        *max_value_name_len = m;
    }
    if (max_value_len) {
        DWORD m = 0;
        for (int i = 0; i < REG_MAX_VALUES; i++)
            if (reg_values[i].used && reg_values[i].owner == kidx) {
                if ((DWORD)reg_values[i].data_size > m) m = reg_values[i].data_size;
            }
        *max_value_len = m;
    }
    return ERROR_SUCCESS;
}
__attribute__((dllexport)) LONG RegQueryInfoKeyA(HKEY key, char *class, DWORD *class_size, DWORD *reserved,
                                                   DWORD *subkeys, DWORD *max_subkey_len, DWORD *max_class_len,
                                                   DWORD *values, DWORD *max_value_name_len, DWORD *max_value_len,
                                                   PVOID sa, PFILETIME last_write) {
    (void)class; (void)class_size; (void)reserved; (void)max_class_len; (void)sa; (void)last_write;
    return RegQueryInfoKeyW(key, 0, 0, 0, subkeys, max_subkey_len, max_class_len, values,
                            max_value_name_len, max_value_len, sa, last_write);
}

__attribute__((dllexport)) LONG RegFlushKey(HKEY key) {
    if (!reg_key_from_handle(key)) return ERROR_INVALID_HANDLE;
    reg_hive_save();
    return ERROR_SUCCESS;
}

__attribute__((dllexport)) LONG RegLoadKeyW(HKEY key, const WCHAR *subKey, const WCHAR *file) {
    (void)key; (void)subKey; (void)file;
    return ERROR_CALL_NOT_IMPLEMENTED;
}
__attribute__((dllexport)) LONG RegLoadKeyA(HKEY key, const char *subKey, const char *file) {
    (void)key; (void)subKey; (void)file;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

__attribute__((dllexport)) LONG RegSaveKeyW(HKEY key, const WCHAR *file, void *security) {
    (void)key; (void)file; (void)security;
    return ERROR_CALL_NOT_IMPLEMENTED;
}
__attribute__((dllexport)) LONG RegSaveKeyA(HKEY key, const char *file, void *security) {
    (void)key; (void)file; (void)security;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

__attribute__((dllexport)) LONG RegConnectRegistryW(const WCHAR *machine, HKEY key, HKEY *result) {
    if (!result) return ERROR_INVALID_PARAMETER;
    if (!machine || !*machine) { *result = key; return ERROR_SUCCESS; }
    /* SentinelOS is a single machine; remote registry not supported. */
    return ERROR_CALL_NOT_IMPLEMENTED;
}
__attribute__((dllexport)) LONG RegConnectRegistryA(const char *machine, HKEY key, HKEY *result) {
    if (!result) return ERROR_INVALID_PARAMETER;
    if (!machine || !*machine) { *result = key; return ERROR_SUCCESS; }
    return ERROR_CALL_NOT_IMPLEMENTED;
}

__attribute__((dllexport)) LONG RegCopyTreeW(HKEY srcKey, const WCHAR *srcSub, HKEY dstKey,
                                               const WCHAR *dstSub, DWORD flags) {
    (void)flags;
    if (!srcSub || !*srcSub || !dstSub || !*dstSub) return ERROR_INVALID_PARAMETER;
    ADVAPI_KEY *sp = reg_key_from_handle(srcKey);
    ADVAPI_KEY *dp = reg_key_from_handle(dstKey);
    if (!sp || !dp) return ERROR_INVALID_HANDLE;
    if ((dp->access_mask & KEY_CREATE_SUB_KEY) != KEY_CREATE_SUB_KEY) return ERROR_ACCESS_DENIED;
    int sidx = reg_find_child(sp, srcSub);
    if (sidx < 0) return ERROR_FILE_NOT_FOUND;
    /* create/open destination */
    HKEY dst = 0;
    DWORD disp = 0;
    LONG r = RegCreateKeyExW(dstKey, dstSub, 0, 0, 0, KEY_ALL_ACCESS, 0, &dst, &disp);
    if (r != ERROR_SUCCESS) return r;
    /* copy values */
    int nv = reg_count_values(sidx);
    for (int i = 0; i < nv; i++) {
        int vi = reg_value_at(sidx, i);
        ADVAPI_VALUE *v = &reg_values[vi];
        WCHAR vname[REG_NAME_MAX];
        reg_wcscpy(vname, v->name);
        RegSetValueExW(dst, vname, 0, v->type,
                       (v->data_size && v->data_offset != 0xFFFFFFFF) ? (reg_heap + v->data_offset) : 0,
                       v->data_size);
    }
    /* recurse children */
    int nc = reg_count_children(sidx);
    for (int i = 0; i < nc; i++) {
        int ci = reg_child_at(sidx, i);
        WCHAR cname[REG_NAME_MAX];
        reg_wcscpy(cname, reg_keys[ci].name);
        RegCopyTreeW(dst, cname, dst, cname, 0);
    }
    RegCloseKey(dst);
    reg_hive_save();
    return ERROR_SUCCESS;
}
__attribute__((dllexport)) LONG RegCopyTreeA(HKEY srcKey, const char *srcSub, HKEY dstKey,
                                               const char *dstSub, DWORD flags) {
    (void)flags;
    if (!srcSub || !*srcSub || !dstSub || !*dstSub) return ERROR_INVALID_PARAMETER;
    reg_mbstowcs(reg_wbuf, 2048, srcSub);
    reg_wcstombs(reg_abuf, 4096, reg_wbuf); /* not used, keep */
    /* Need two wide buffers; do src then dst. */
    static WCHAR reg_dstbuf[2048];
    reg_mbstowcs(reg_dstbuf, 2048, dstSub);
    return RegCopyTreeW(srcKey, reg_wbuf, dstKey, reg_dstbuf, flags);
}

__attribute__((dllexport)) LONG RegNotifyChangeKeyValue(HKEY key, BOOL watch_subtree, DWORD filter,
                                                          HANDLE event, BOOL async) {
    (void)key; (void)watch_subtree; (void)filter; (void)event; (void)async;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/* ---------- last error ---------- */

static DWORD advapi_last_error;
__attribute__((dllexport)) void SetLastError(DWORD err) { advapi_last_error = err; }
__attribute__((dllexport)) DWORD GetLastError(void) { return advapi_last_error; }

/* Security descriptor / ACL error codes. */
#define ERROR_INVALID_SECURITY_DESCR 1336
#define ERROR_INVALID_ACL 1332
#define ERROR_INVALID_SID 1337

/* Revisions. */
#define SECURITY_DESCRIPTOR_REVISION 1
#define ACL_REVISION 2
#define ACL_REVISION_DS 4

/* Security information components (for RegGetSecurityInfo/RegSetSecurityInfo). */
typedef DWORD SECURITY_INFORMATION;
#define OWNER_SECURITY_INFORMATION 0x00000001u
#define GROUP_SECURITY_INFORMATION 0x00000002u
#define DACL_SECURITY_INFORMATION  0x00000004u
#define SACL_SECURITY_INFORMATION  0x00000008u

/* Security descriptor control flags. */
#define SE_OWNER_DEFAULTED          0x0001
#define SE_GROUP_DEFAULTED          0x0002
#define SE_DACL_PRESENT             0x0004
#define SE_DACL_DEFAULTED           0x0008
#define SE_SACL_PRESENT             0x0010
#define SE_SACL_DEFAULTED           0x0020
#define SE_DACL_PROTECTED           0x1000
#define SE_SACL_PROTECTED           0x2000
#define SE_SELF_RELATIVE            0x8000

typedef unsigned short WORD;
typedef unsigned short SECURITY_DESCRIPTOR_CONTROL, *PSECURITY_DESCRIPTOR_CONTROL;

typedef struct _SID_IDENTIFIER_AUTHORITY {
    BYTE Value[6];
} SID_IDENTIFIER_AUTHORITY, *PSID_IDENTIFIER_AUTHORITY;

typedef struct _SID {
    BYTE Revision;
    BYTE SubAuthorityCount;
    SID_IDENTIFIER_AUTHORITY IdentifierAuthority;
    DWORD SubAuthority[1];
} SID, *PSID;

typedef struct _ACL {
    BYTE AclRevision;
    BYTE Sbz1;
    WORD AclSize;
    WORD AceCount;
    WORD Sbz2;
} ACL, *PACL;

typedef struct _ACL_SIZE_INFORMATION {
    DWORD AceCount;
    DWORD AclBytesInUse;
    DWORD AclBytesFree;
} ACL_SIZE_INFORMATION, *PACL_SIZE_INFORMATION;

typedef struct _SECURITY_DESCRIPTOR {
    BYTE Revision;
    BYTE Sbz1;
    SECURITY_DESCRIPTOR_CONTROL Control;
    PSID Owner;
    PSID Group;
    PACL Sacl;
    PACL Dacl;
} SECURITY_DESCRIPTOR, *PSECURITY_DESCRIPTOR;

static int sec_desc_valid(PSECURITY_DESCRIPTOR psd) {
    if (!psd) return 0;
    if (psd->Revision != SECURITY_DESCRIPTOR_REVISION) return 0;
    if (psd->Sbz1 != 0) return 0;
    return 1;
}

__attribute__((dllexport)) BOOL GetSecurityDescriptorDacl(PSECURITY_DESCRIPTOR psd, BOOL *pbDaclPresent, PACL *pDacl, BOOL *pbDaclDefaulted) {
    if (!sec_desc_valid(psd)) { SetLastError(ERROR_INVALID_SECURITY_DESCR); return 0; }
    if (pbDaclPresent) *pbDaclPresent = (psd->Control & SE_DACL_PRESENT) ? 1 : 0;
    if (pDacl) *pDacl = psd->Dacl;
    if (pbDaclDefaulted) *pbDaclDefaulted = (psd->Control & SE_DACL_DEFAULTED) ? 1 : 0;
    return 1;
}

__attribute__((dllexport)) BOOL SetSecurityDescriptorDacl(PSECURITY_DESCRIPTOR psd, BOOL bDaclPresent, PACL pDacl, BOOL bDaclDefaulted) {
    if (!sec_desc_valid(psd)) { SetLastError(ERROR_INVALID_SECURITY_DESCR); return 0; }
    if (psd->Control & SE_SELF_RELATIVE) { SetLastError(ERROR_INVALID_SECURITY_DESCR); return 0; }
    if (bDaclPresent) {
        if (psd->Control & SE_DACL_PROTECTED) { SetLastError(ERROR_ACCESS_DENIED); return 0; }
        psd->Control |= SE_DACL_PRESENT;
        psd->Dacl = pDacl;
    } else {
        psd->Control &= ~SE_DACL_PRESENT;
        psd->Dacl = 0;
    }
    if (bDaclDefaulted) psd->Control |= SE_DACL_DEFAULTED;
    else psd->Control &= ~SE_DACL_DEFAULTED;
    return 1;
}

__attribute__((dllexport)) BOOL GetSecurityDescriptorSacl(PSECURITY_DESCRIPTOR psd, BOOL *pbSaclPresent, PACL *pSacl, BOOL *pbSaclDefaulted) {
    if (!sec_desc_valid(psd)) { SetLastError(ERROR_INVALID_SECURITY_DESCR); return 0; }
    if (pbSaclPresent) *pbSaclPresent = (psd->Control & SE_SACL_PRESENT) ? 1 : 0;
    if (pSacl) *pSacl = psd->Sacl;
    if (pbSaclDefaulted) *pbSaclDefaulted = (psd->Control & SE_SACL_DEFAULTED) ? 1 : 0;
    return 1;
}

__attribute__((dllexport)) BOOL SetSecurityDescriptorSacl(PSECURITY_DESCRIPTOR psd, BOOL bSaclPresent, PACL pSacl, BOOL bSaclDefaulted) {
    if (!sec_desc_valid(psd)) { SetLastError(ERROR_INVALID_SECURITY_DESCR); return 0; }
    if (psd->Control & SE_SELF_RELATIVE) { SetLastError(ERROR_INVALID_SECURITY_DESCR); return 0; }
    if (bSaclPresent) {
        if (psd->Control & SE_SACL_PROTECTED) { SetLastError(ERROR_ACCESS_DENIED); return 0; }
        psd->Control |= SE_SACL_PRESENT;
        psd->Sacl = pSacl;
    } else {
        psd->Control &= ~SE_SACL_PRESENT;
        psd->Sacl = 0;
    }
    if (bSaclDefaulted) psd->Control |= SE_SACL_DEFAULTED;
    else psd->Control &= ~SE_SACL_DEFAULTED;
    return 1;
}

__attribute__((dllexport)) BOOL GetSecurityDescriptorOwner(PSECURITY_DESCRIPTOR psd, PSID *pOwner, BOOL *pbOwnerDefaulted) {
    if (!sec_desc_valid(psd)) { SetLastError(ERROR_INVALID_SECURITY_DESCR); return 0; }
    if (pOwner) *pOwner = psd->Owner;
    if (pbOwnerDefaulted) *pbOwnerDefaulted = (psd->Control & SE_OWNER_DEFAULTED) ? 1 : 0;
    return 1;
}

__attribute__((dllexport)) BOOL SetSecurityDescriptorOwner(PSECURITY_DESCRIPTOR psd, PSID pOwner, BOOL bOwnerDefaulted) {
    if (!sec_desc_valid(psd)) { SetLastError(ERROR_INVALID_SECURITY_DESCR); return 0; }
    if (psd->Control & SE_SELF_RELATIVE) { SetLastError(ERROR_INVALID_SECURITY_DESCR); return 0; }
    psd->Owner = pOwner;
    if (bOwnerDefaulted) psd->Control |= SE_OWNER_DEFAULTED;
    else psd->Control &= ~SE_OWNER_DEFAULTED;
    return 1;
}

__attribute__((dllexport)) BOOL GetSecurityDescriptorGroup(PSECURITY_DESCRIPTOR psd, PSID *pGroup, BOOL *pbGroupDefaulted) {
    if (!sec_desc_valid(psd)) { SetLastError(ERROR_INVALID_SECURITY_DESCR); return 0; }
    if (pGroup) *pGroup = psd->Group;
    if (pbGroupDefaulted) *pbGroupDefaulted = (psd->Control & SE_GROUP_DEFAULTED) ? 1 : 0;
    return 1;
}

__attribute__((dllexport)) BOOL SetSecurityDescriptorGroup(PSECURITY_DESCRIPTOR psd, PSID pGroup, BOOL bGroupDefaulted) {
    if (!sec_desc_valid(psd)) { SetLastError(ERROR_INVALID_SECURITY_DESCR); return 0; }
    if (psd->Control & SE_SELF_RELATIVE) { SetLastError(ERROR_INVALID_SECURITY_DESCR); return 0; }
    psd->Group = pGroup;
    if (bGroupDefaulted) psd->Control |= SE_GROUP_DEFAULTED;
    else psd->Control &= ~SE_GROUP_DEFAULTED;
    return 1;
}

__attribute__((dllexport)) DWORD GetSecurityDescriptorLength(PSECURITY_DESCRIPTOR psd) {
    if (!sec_desc_valid(psd)) return 0;
    return sizeof(SECURITY_DESCRIPTOR);
}

__attribute__((dllexport)) BOOL IsValidSecurityDescriptor(PSECURITY_DESCRIPTOR psd) {
    return sec_desc_valid(psd);
}

__attribute__((dllexport)) BOOL IsValidAcl(PACL pacl) {
    if (!pacl) return 0;
    if (pacl->AclRevision != ACL_REVISION && pacl->AclRevision != ACL_REVISION_DS) return 0;
    if (pacl->AclSize < sizeof(ACL)) return 0;
    return 1;
}

__attribute__((dllexport)) BOOL GetAclInformation(PACL pacl, void *info, DWORD count, DWORD cls) {
    (void)pacl; (void)info; (void)count; (void)cls;
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__attribute__((dllexport)) BOOL SetAclInformation(PACL pacl, const void *info, DWORD count, DWORD cls) {
    (void)pacl; (void)info; (void)count; (void)cls;
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__attribute__((dllexport)) DWORD GetLengthSid(PSID sid) {
    if (!sid) return 0;
    return 8 + (DWORD)sid->SubAuthorityCount * 4;
}

__attribute__((dllexport)) BOOL IsValidSid(PSID sid) {
    if (!sid) return 0;
    if (sid->Revision != 1) return 0;
    if (sid->SubAuthorityCount == 0 || sid->SubAuthorityCount > 16) return 0;
    return 1;
}

__attribute__((dllexport)) BOOL EqualSid(PSID sid1, PSID sid2) {
    int i;
    if (!sid1 || !sid2) return 0;
    if (!IsValidSid(sid1) || !IsValidSid(sid2)) return 0;
    if (sid1->SubAuthorityCount != sid2->SubAuthorityCount) return 0;
    for (i = 0; i < 6; i++)
        if (sid1->IdentifierAuthority.Value[i] != sid2->IdentifierAuthority.Value[i]) return 0;
    for (i = 0; i < sid1->SubAuthorityCount; i++)
        if (sid1->SubAuthority[i] != sid2->SubAuthority[i]) return 0;
    return 1;
}

__attribute__((dllexport)) BOOL EqualPrefixSid(PSID sid1, PSID sid2) {
    DWORD n;
    int i;
    if (!sid1 || !sid2) return 0;
    if (!IsValidSid(sid1) || !IsValidSid(sid2)) return 0;
    if (sid1->Revision != sid2->Revision) return 0;
    for (i = 0; i < 6; i++)
        if (sid1->IdentifierAuthority.Value[i] != sid2->IdentifierAuthority.Value[i]) return 0;
    n = sid1->SubAuthorityCount < sid2->SubAuthorityCount ? sid1->SubAuthorityCount : sid2->SubAuthorityCount;
    for (i = 0; i < (int)n; i++)
        if (sid1->SubAuthority[i] != sid2->SubAuthority[i]) return 0;
    return 1;
}

__attribute__((dllexport)) DWORD GetLengthSecurityDescriptor(PSECURITY_DESCRIPTOR psd) {
    if (!sec_desc_valid(psd)) return 0;
    return sizeof(SECURITY_DESCRIPTOR);
}

__attribute__((dllexport)) BOOL InitializeSecurityDescriptor(PSECURITY_DESCRIPTOR psd, DWORD revision) {
    if (!psd) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    if (revision != SECURITY_DESCRIPTOR_REVISION) { SetLastError(ERROR_INVALID_SECURITY_DESCR); return 0; }
    psd->Revision = SECURITY_DESCRIPTOR_REVISION;
    psd->Sbz1 = 0;
    psd->Control = 0;
    psd->Owner = 0;
    psd->Group = 0;
    psd->Sacl = 0;
    psd->Dacl = 0;
    return 1;
}

/* Initialize a default security descriptor in a key's sd buffer.
 * The descriptor is non-self-relative: Owner/Group/Dacl/Sacl are pointers
 * into the same 256-byte buffer. The DACL is present but contains no ACEs
 * (placeholder until a real SID/user DB exists). */
static int reg_init_default_sd(BYTE *sd_buf) {
    if (!sd_buf) return 0;
    reg_memset(sd_buf, 0, 256);
    PSECURITY_DESCRIPTOR psd = (PSECURITY_DESCRIPTOR)sd_buf;
    if (!InitializeSecurityDescriptor(psd, SECURITY_DESCRIPTOR_REVISION)) return 0;
    /* Place a minimal ACL right after the descriptor within the same buffer. */
    PACL pDacl = (PACL)(sd_buf + sizeof(SECURITY_DESCRIPTOR));
    pDacl->AclRevision = ACL_REVISION;
    pDacl->Sbz1 = 0;
    pDacl->AclSize = (WORD)sizeof(ACL);
    pDacl->AceCount = 0;
    pDacl->Sbz2 = 0;
    if (!SetSecurityDescriptorDacl(psd, 1, pDacl, 0)) return 0;
    return 1;
}

/* RegGetSecurityInfo - query a key's security descriptor.
 * Returns the requested security information components (owner, group, DACL,
 * SACL) and optionally the full security descriptor pointer. */
__attribute__((dllexport)) LONG RegGetSecurityInfo(HKEY key, SECURITY_INFORMATION secinfo,
                                                    PSID *ppsidOwner, PSID *ppsidGroup,
                                                    PACL *ppDacl, PACL *ppSacl,
                                                    PSECURITY_DESCRIPTOR *ppSecurityDescriptor) {
    ADVAPI_KEY *k = reg_key_from_handle(key);
    if (!k) return ERROR_INVALID_HANDLE;
    PSECURITY_DESCRIPTOR psd = (PSECURITY_DESCRIPTOR)k->sd;
    if (!sec_desc_valid(psd)) {
        /* Key loaded from a hive written before security descriptors existed;
         * lazily initialize a default descriptor. */
        if (!reg_init_default_sd(k->sd)) { SetLastError(ERROR_INVALID_SECURITY_DESCR); return ERROR_INVALID_SECURITY_DESCR; }
        psd = (PSECURITY_DESCRIPTOR)k->sd;
    }
    DWORD valid = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                  DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION;
    if (secinfo & ~valid) { SetLastError(ERROR_INVALID_PARAMETER); return ERROR_INVALID_PARAMETER; }
    if (secinfo & OWNER_SECURITY_INFORMATION) {
        if (ppsidOwner) *ppsidOwner = psd->Owner;
    }
    if (secinfo & GROUP_SECURITY_INFORMATION) {
        if (ppsidGroup) *ppsidGroup = psd->Group;
    }
    if (secinfo & DACL_SECURITY_INFORMATION) {
        if (ppDacl) *ppDacl = psd->Dacl;
    }
    if (secinfo & SACL_SECURITY_INFORMATION) {
        if (ppSacl) *ppSacl = psd->Sacl;
    }
    if (ppSecurityDescriptor) *ppSecurityDescriptor = psd;
    return ERROR_SUCCESS;
}

/* RegSetSecurityInfo - set a key's security descriptor components.
 * Replaces the owner, group, DACL, and/or SACL as indicated by secinfo.
 * Changes are persisted to the hive file. */
__attribute__((dllexport)) LONG RegSetSecurityInfo(HKEY key, SECURITY_INFORMATION secinfo,
                                                    PSID pOwner, PSID pGroup,
                                                    PACL pDacl, PACL pSacl) {
    ADVAPI_KEY *k = reg_key_from_handle(key);
    if (!k) return ERROR_INVALID_HANDLE;
    PSECURITY_DESCRIPTOR psd = (PSECURITY_DESCRIPTOR)k->sd;
    if (!sec_desc_valid(psd)) {
        if (!reg_init_default_sd(k->sd)) { SetLastError(ERROR_INVALID_SECURITY_DESCR); return ERROR_INVALID_SECURITY_DESCR; }
        psd = (PSECURITY_DESCRIPTOR)k->sd;
    }
    DWORD valid = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                  DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION;
    if (secinfo & ~valid) { SetLastError(ERROR_INVALID_PARAMETER); return ERROR_INVALID_PARAMETER; }
    if (secinfo & OWNER_SECURITY_INFORMATION) {
        if (psd->Control & SE_SELF_RELATIVE) { SetLastError(ERROR_INVALID_SECURITY_DESCR); return ERROR_INVALID_SECURITY_DESCR; }
        psd->Owner = pOwner;
        psd->Control = pOwner ? (psd->Control | SE_OWNER_DEFAULTED) : (psd->Control & ~SE_OWNER_DEFAULTED);
    }
    if (secinfo & GROUP_SECURITY_INFORMATION) {
        if (psd->Control & SE_SELF_RELATIVE) { SetLastError(ERROR_INVALID_SECURITY_DESCR); return ERROR_INVALID_SECURITY_DESCR; }
        psd->Group = pGroup;
        psd->Control = pGroup ? (psd->Control | SE_GROUP_DEFAULTED) : (psd->Control & ~SE_GROUP_DEFAULTED);
    }
    if (secinfo & DACL_SECURITY_INFORMATION) {
        if (psd->Control & SE_SELF_RELATIVE) { SetLastError(ERROR_INVALID_SECURITY_DESCR); return ERROR_INVALID_SECURITY_DESCR; }
        if (psd->Control & SE_DACL_PROTECTED) { SetLastError(ERROR_ACCESS_DENIED); return ERROR_ACCESS_DENIED; }
        if (pDacl) psd->Control |= SE_DACL_PRESENT;
        else psd->Control &= ~SE_DACL_PRESENT;
        psd->Dacl = pDacl;
    }
    if (secinfo & SACL_SECURITY_INFORMATION) {
        if (psd->Control & SE_SELF_RELATIVE) { SetLastError(ERROR_INVALID_SECURITY_DESCR); return ERROR_INVALID_SECURITY_DESCR; }
        if (psd->Control & SE_SACL_PROTECTED) { SetLastError(ERROR_ACCESS_DENIED); return ERROR_ACCESS_DENIED; }
        if (pSacl) psd->Control |= SE_SACL_PRESENT;
        else psd->Control &= ~SE_SACL_PRESENT;
        psd->Sacl = pSacl;
    }
    reg_hive_save();
    return ERROR_SUCCESS;
}

__attribute__((dllexport)) DWORD ConvertToAutoInheritSecurityObject(void *object, int type, void *generic_mapping, void *default_descriptor) {
    (void)object; (void)type; (void)generic_mapping; (void)default_descriptor;
    return ERROR_NOT_SUPPORTED;
}

__attribute__((dllexport)) BOOL SystemFunction036(void *buffer, ULONG length) {
    if (!buffer && length) return 0;
    unsigned int a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    if (!(c & (1u << 30))) return 0;
    BYTE *p = (BYTE *)buffer;
    for (ULONG i = 0; i < length;) {
        unsigned long long v;
        unsigned char ok;
        __asm__ volatile("rdrand %0; setc %1" : "=r"(v), "=qm"(ok));
        if (!ok) return 0;
        for (unsigned j = 0; j < 8 && i < length; j++, i++)
            p[i] = (BYTE)(v >> (j * 8));
    }
    return 1;
}

/* CryptoAPI -- SentinelOS has no real cryptographic subsystem.
 * These entry points provide compatible exports but report limitations
 * honestly instead of fabricating keys, hashes or signatures.
 * CryptGenRandom delegates to SystemFunction036 (RDRAND) where available. */
#define NTE_BAD_ALGID 5010
#define NTE_BAD_KEYSET 5012
#define NTE_BAD_PROV_TYPE 5017
typedef unsigned int ALG_ID;
typedef void *HCRYPTPROV;
typedef void *HCRYPTKEY;
typedef void *HCRYPTHASH;
#define CALG_RSA_KEYX 0x0000a400u
#define CALG_RSA_SIGN 0x0000a200u
#define CALG_DES 0x00006601u
#define CALG_3DES 0x00006603u
#define CALG_RC2 0x00006602u
#define CALG_RC4 0x00006801u
#define CALG_MD5 0x00008003u
#define CALG_SHA 0x00008004u
#define PROV_RSA_FULL 1
#define PROV_RSA_SIGN 2
#define CRYPTO_MAX_PROVS 16
#define CRYPTO_MAX_KEYS 64
#define CRYPTO_MAX_HASHES 64
typedef struct { int used; } CRYPTO_PROV;
typedef struct { int used; HCRYPTPROV prov; ALG_ID algid; BYTE key[256]; DWORD key_len; } CRYPTO_KEY;
typedef struct { int used; HCRYPTPROV prov; ALG_ID algid; BYTE hash[64]; DWORD hash_len; } CRYPTO_HASH;
static CRYPTO_PROV crypto_provs[CRYPTO_MAX_PROVS];
static CRYPTO_KEY crypto_keys[CRYPTO_MAX_KEYS];
static CRYPTO_HASH crypto_hashes[CRYPTO_MAX_HASHES];
static int crypto_prov_slot(void){for(int i=0;i<CRYPTO_MAX_PROVS;i++)if(!crypto_provs[i].used){crypto_provs[i].used=1;return i+1;}return 0;}
static void crypto_prov_free(int i){if(i>0&&i<=CRYPTO_MAX_PROVS)crypto_provs[i-1].used=0;}
static int crypto_key_slot(void){for(int i=0;i<CRYPTO_MAX_KEYS;i++)if(!crypto_keys[i].used){crypto_keys[i].used=1;return i+1;}return 0;}
static void crypto_key_free(int i){if(i>0&&i<=CRYPTO_MAX_KEYS)crypto_keys[i-1].used=0;}
static void crypto_hash_free(int i){if(i>0&&i<=CRYPTO_MAX_HASHES)crypto_hashes[i-1].used=0;}
__attribute__((dllexport)) BOOL CryptAcquireContextA(HCRYPTPROV *prov, const char *container, const char *provider, DWORD type, DWORD flags){(void)container;(void)provider;(void)type;(void)flags;if(!prov){SetLastError(ERROR_INVALID_PARAMETER);return 0;}if(type!=PROV_RSA_FULL&&type!=PROV_RSA_SIGN){SetLastError(NTE_BAD_PROV_TYPE);return 0;}int i=crypto_prov_slot();if(!i){SetLastError(NTE_BAD_KEYSET);return 0;}*prov=(HCRYPTPROV)(unsigned long long)i;return 1;}
__attribute__((dllexport)) BOOL CryptAcquireContextW(HCRYPTPROV *prov, const WCHAR *container, const WCHAR *provider, DWORD type, DWORD flags){(void)container;(void)provider;(void)type;(void)flags;return CryptAcquireContextA(prov,0,0,type,flags);}
__attribute__((dllexport)) BOOL CryptReleaseContext(HCRYPTPROV prov, DWORD flags){(void)flags;crypto_prov_free((int)(unsigned long long)prov);return 1;}
__attribute__((dllexport)) BOOL CryptGenKey(HCRYPTPROV prov, ALG_ID algid, DWORD flags, HCRYPTKEY *key){(void)prov;(void)flags;if(!key){SetLastError(ERROR_INVALID_PARAMETER);return 0;}if(algid!=CALG_RSA_KEYX&&algid!=CALG_RSA_SIGN&&algid!=CALG_DES&&algid!=CALG_3DES&&algid!=CALG_RC2&&algid!=CALG_RC4){SetLastError(NTE_BAD_ALGID);return 0;}int i=crypto_key_slot();if(!i){SetLastError(NTE_BAD_KEYSET);return 0;}crypto_keys[i-1].prov=prov;crypto_keys[i-1].algid=algid;if(!SystemFunction036(crypto_keys[i-1].key,sizeof(crypto_keys[i-1].key))){crypto_key_free(i);return 0;}crypto_keys[i-1].key_len=sizeof(crypto_keys[i-1].key);*key=(HCRYPTKEY)(unsigned long long)i;return 1;}
__attribute__((dllexport)) BOOL CryptDestroyKey(HCRYPTKEY key){crypto_key_free((int)(unsigned long long)key);return 1;}
__attribute__((dllexport)) BOOL CryptGenRandom(HCRYPTPROV prov, DWORD len, BYTE *buf){(void)prov;if(!buf&&len){SetLastError(ERROR_INVALID_PARAMETER);return 0;}if(!len){SetLastError(0);return 1;}return SystemFunction036(buf,len);}
__attribute__((dllexport)) BOOL CryptCreateHash(HCRYPTPROV prov, ALG_ID algid, HCRYPTKEY key, DWORD flags, HCRYPTHASH *hash){(void)prov;(void)algid;(void)key;(void)flags;if(!hash){SetLastError(ERROR_INVALID_PARAMETER);return 0;}SetLastError(NTE_BAD_ALGID);return 0;}
__attribute__((dllexport)) BOOL CryptHashData(HCRYPTHASH hash, const BYTE *data, DWORD len, DWORD flags){(void)hash;(void)data;(void)len;(void)flags;SetLastError(ERROR_NOT_SUPPORTED);return 0;}
__attribute__((dllexport)) BOOL CryptDestroyHash(HCRYPTHASH hash){crypto_hash_free((int)(unsigned long long)hash);return 1;}
__attribute__((dllexport)) BOOL CryptDeriveKey(HCRYPTPROV prov, ALG_ID algid, HCRYPTHASH hash, DWORD flags, HCRYPTKEY *key){(void)prov;(void)algid;(void)hash;(void)flags;(void)key;SetLastError(ERROR_NOT_SUPPORTED);return 0;}
__attribute__((dllexport)) BOOL CryptEncrypt(HCRYPTKEY key, HCRYPTHASH hash, BOOL final, DWORD flags, BYTE *buf, DWORD *len, DWORD buflen){(void)key;(void)hash;(void)final;(void)flags;(void)buf;(void)len;(void)buflen;SetLastError(ERROR_NOT_SUPPORTED);return 0;}
__attribute__((dllexport)) BOOL CryptDecrypt(HCRYPTKEY key, HCRYPTHASH hash, BOOL final, DWORD flags, BYTE *buf, DWORD *len){(void)key;(void)hash;(void)final;(void)flags;(void)buf;(void)len;SetLastError(ERROR_NOT_SUPPORTED);return 0;}
__attribute__((dllexport)) BOOL CryptSignHashA(HCRYPTHASH hash, DWORD dwKeySpec, const char *desc, DWORD flags, BYTE *sig, DWORD *siglen){(void)hash;(void)dwKeySpec;(void)desc;(void)flags;(void)sig;(void)siglen;SetLastError(ERROR_NOT_SUPPORTED);return 0;}
__attribute__((dllexport)) BOOL CryptSignHashW(HCRYPTHASH hash, DWORD dwKeySpec, const WCHAR *desc, DWORD flags, BYTE *sig, DWORD *siglen){(void)hash;(void)dwKeySpec;(void)desc;(void)flags;(void)sig;(void)siglen;SetLastError(ERROR_NOT_SUPPORTED);return 0;}
__attribute__((dllexport)) BOOL CryptVerifySignatureA(HCRYPTHASH hash, const BYTE *sig, DWORD siglen, const char *desc, DWORD flags){(void)hash;(void)sig;(void)siglen;(void)desc;(void)flags;SetLastError(ERROR_NOT_SUPPORTED);return 0;}
__attribute__((dllexport)) BOOL CryptVerifySignatureW(HCRYPTHASH hash, const BYTE *sig, DWORD siglen, const WCHAR *desc, DWORD flags){(void)hash;(void)sig;(void)siglen;(void)desc;(void)flags;SetLastError(ERROR_NOT_SUPPORTED);return 0;}
__attribute__((dllexport)) BOOL CryptGetHashParam(HCRYPTHASH hash, DWORD param, BYTE *data, DWORD *len, DWORD flags){(void)hash;(void)param;(void)data;(void)len;(void)flags;SetLastError(ERROR_NOT_SUPPORTED);return 0;}
__attribute__((dllexport)) BOOL CryptSetHashParam(HCRYPTHASH hash, DWORD param, const BYTE *data, DWORD flags){(void)hash;(void)param;(void)data;(void)flags;SetLastError(ERROR_NOT_SUPPORTED);return 0;}
__attribute__((dllexport)) BOOL CryptGetKeyParam(HCRYPTKEY key, DWORD param, BYTE *data, DWORD *len, DWORD flags){(void)key;(void)param;(void)data;(void)len;(void)flags;SetLastError(ERROR_NOT_SUPPORTED);return 0;}
__attribute__((dllexport)) BOOL CryptSetKeyParam(HCRYPTKEY key, DWORD param, const BYTE *data, DWORD flags){(void)key;(void)param;(void)data;(void)flags;SetLastError(ERROR_NOT_SUPPORTED);return 0;}
__attribute__((dllexport)) BOOL CryptExportKey(HCRYPTKEY key, HCRYPTKEY expkey, DWORD blobtype, DWORD flags, BYTE *buf, DWORD *len){(void)key;(void)expkey;(void)blobtype;(void)flags;(void)buf;(void)len;SetLastError(ERROR_NOT_SUPPORTED);return 0;}
__attribute__((dllexport)) BOOL CryptImportKey(HCRYPTPROV prov, const BYTE *buf, DWORD len, HCRYPTKEY pubkey, DWORD flags, HCRYPTKEY *key){(void)prov;(void)buf;(void)len;(void)pubkey;(void)flags;(void)key;SetLastError(ERROR_NOT_SUPPORTED);return 0;}
__attribute__((dllexport)) BOOL CryptSetProvParam(HCRYPTPROV prov, DWORD param, const BYTE *data){(void)prov;(void)param;(void)data;SetLastError(ERROR_NOT_SUPPORTED);return 0;}
__attribute__((dllexport)) BOOL CryptGetProvParam(HCRYPTPROV prov, DWORD param, BYTE *data, DWORD *len, DWORD flags){(void)prov;(void)param;(void)data;(void)len;(void)flags;SetLastError(ERROR_NOT_SUPPORTED);return 0;}

/* Initialize predefined root keys on first use, then load the disk hive if present. */
static int advapi_initialized;
__attribute__((constructor)) static void advapi_init(void) {
    if (advapi_initialized) return;
    advapi_initialized = 1;
    /* Load persistent hive from disk (roots come with it if a hive exists). */
    reg_hive_load();
    /* Ensure all five roots exist (fresh registry or missing/corrupt hive). */
    if (g_root_idx[0] < 0) g_root_idx[0] = reg_create_root(L"Classes");
    if (g_root_idx[1] < 0) g_root_idx[1] = reg_create_root(L"CurrentUser");
    if (g_root_idx[2] < 0) g_root_idx[2] = reg_create_root(L"Machine");
    if (g_root_idx[3] < 0) g_root_idx[3] = reg_create_root(L"Users");
    if (g_root_idx[4] < 0) g_root_idx[4] = reg_create_root(L"CurrentConfig");
}
