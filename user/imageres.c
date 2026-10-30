/* SentinelOS image resource catalog.
 * Bitmap payloads are staged beside this DLL in System32\\icons.
 * IDs are stable and map directly to icon_001.bmp ... icon_085.bmp.
 */
typedef unsigned int UINT;
typedef unsigned long long SIZE_T;
static const char *const g_icon_paths[] = {
    "icons/icon_001.bmp",
    "icons/icon_002.bmp",
    "icons/icon_003.bmp",
    "icons/icon_004.bmp",
    "icons/icon_005.bmp",
    "icons/icon_006.bmp",
    "icons/icon_007.bmp",
    "icons/icon_008.bmp",
    "icons/icon_009.bmp",
    "icons/icon_010.bmp",
    "icons/icon_011.bmp",
    "icons/icon_012.bmp",
    "icons/icon_013.bmp",
    "icons/icon_014.bmp",
    "icons/icon_015.bmp",
    "icons/icon_016.bmp",
    "icons/icon_017.bmp",
    "icons/icon_018.bmp",
    "icons/icon_019.bmp",
    "icons/icon_020.bmp",
    "icons/icon_021.bmp",
    "icons/icon_022.bmp",
    "icons/icon_023.bmp",
    "icons/icon_024.bmp",
    "icons/icon_025.bmp",
    "icons/icon_026.bmp",
    "icons/icon_027.bmp",
    "icons/icon_028.bmp",
    "icons/icon_029.bmp",
    "icons/icon_030.bmp",
    "icons/icon_031.bmp",
    "icons/icon_032.bmp",
    "icons/icon_033.bmp",
    "icons/icon_034.bmp",
    "icons/icon_035.bmp",
    "icons/icon_036.bmp",
    "icons/icon_037.bmp",
    "icons/icon_038.bmp",
    "icons/icon_039.bmp",
    "icons/icon_040.bmp",
    "icons/icon_041.bmp",
    "icons/icon_042.bmp",
    "icons/icon_043.bmp",
    "icons/icon_044.bmp",
    "icons/icon_045.bmp",
    "icons/icon_046.bmp",
    "icons/icon_047.bmp",
    "icons/icon_048.bmp",
    "icons/icon_049.bmp",
    "icons/icon_050.bmp",
    "icons/icon_051.bmp",
    "icons/icon_052.bmp",
    "icons/icon_053.bmp",
    "icons/icon_054.bmp",
    "icons/icon_055.bmp",
    "icons/icon_056.bmp",
    "icons/icon_057.bmp",
    "icons/icon_058.bmp",
    "icons/icon_059.bmp",
    "icons/icon_060.bmp",
    "icons/icon_061.bmp",
    "icons/icon_062.bmp",
    "icons/icon_063.bmp",
    "icons/icon_064.bmp",
    "icons/icon_065.bmp",
    "icons/icon_066.bmp",
    "icons/icon_067.bmp",
    "icons/icon_068.bmp",
    "icons/icon_069.bmp",
    "icons/icon_070.bmp",
    "icons/icon_071.bmp",
    "icons/icon_072.bmp",
    "icons/icon_073.bmp",
    "icons/icon_074.bmp",
    "icons/icon_075.bmp",
    "icons/icon_076.bmp",
    "icons/icon_077.bmp",
    "icons/icon_078.bmp",
    "icons/icon_079.bmp",
    "icons/icon_080.bmp",
    "icons/icon_081.bmp",
    "icons/icon_082.bmp",
    "icons/icon_083.bmp",
    "icons/icon_084.bmp",
    "icons/icon_085.bmp"
};
__attribute__((dllexport)) UINT GetImageResourceCount(void) {
    return (UINT)(sizeof(g_icon_paths) / sizeof(g_icon_paths[0]));
}
__attribute__((dllexport)) const char *GetImageResourcePathA(UINT id) {
    if (id == 0 || id > GetImageResourceCount()) return (const char *)0;
    return g_icon_paths[id - 1];
}
__attribute__((dllexport)) const char *GetImageResourceNameA(UINT id) {
    if (id == 0 || id > GetImageResourceCount()) return (const char *)0;
    return g_icon_paths[id - 1] + 6;
}
static int imageres_equal_name(const char *path, const char *name) {
    if (!path || !name) return 0;

    /* Accept either the catalog path (icons/icon_001.bmp) or just the
       basename (icon_001.bmp). Windows callers may use either slash. */
    const char *base = path;
    for (const char *p = path; *p; ++p)
        if (*p == '/' || *p == '\\\\') base = p + 1;

    const char *candidate = name;
    for (const char *p = name; *p; ++p)
        if (*p == '/' || *p == '\\\\') candidate = p + 1;

    while (*base && *candidate) {
        char a = *base++, b = *candidate++;
        if (a == '/') a = '\\\\';
        if (b == '/') b = '\\\\';
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return 0;
    }
    return !*base && !*candidate;
}

__attribute__((dllexport)) UINT FindImageResourceA(const char *name) {
    if (!name || !name[0]) return 0;
    for (UINT i = 0; i < GetImageResourceCount(); ++i)
        if (imageres_equal_name(g_icon_paths[i], name)) return i + 1;
    return 0;
}

/* Alias with an explicit ID-oriented name for callers that treat this DLL
   as a resource index rather than a filesystem path table. */
__attribute__((dllexport)) UINT GetImageResourceIdA(const char *name) {
    return FindImageResourceA(name);
}


/* Read a catalog bitmap into caller-owned memory. The returned byte count is
 * zero for an invalid ID, unreadable file, non-BMP payload, or short buffer.
 * The API deliberately returns the encoded BMP so USER32 and applications
 * can decode it without depending on the process working directory. */
typedef unsigned long long U64;
enum { IMAGERES_SYS_OPEN_FILE=5, IMAGERES_SYS_READ_FILE=6, IMAGERES_SYS_CLOSE_FILE=22 };
static U64 imageres_syscall(U64 number,U64 a,U64 b,U64 c) {
    U64 result;
    __asm__ volatile(
        "mov %[n], %%rax\n"
        "mov %[a], %%rdi\n"
        "mov %[b], %%rsi\n"
        "mov %[c], %%rdx\n"
        "int $0x80\n"
        "mov %%rax, %[r]"
        : [r]"=r"(result)
        : [n]"r"(number),[a]"r"(a),[b]"r"(b),[c]"r"(c)
        : "rax","rdi","rsi","rdx","memory","cc");
    return result;
}
__attribute__((dllexport)) UINT GetImageResourceDataA(UINT id, void *buffer, UINT capacity) {
    if (!buffer || capacity < 54u || id == 0 || id > GetImageResourceCount())
        return 0;
    char path[64] = "/magic/System32/";
    unsigned int at = 16;
    const char *relative = GetImageResourcePathA(id);
    for (unsigned int i = 0; relative[i] && at + 1 < sizeof(path); ++i)
        path[at++] = relative[i];
    path[at] = 0;
    U64 handle = imageres_syscall(IMAGERES_SYS_OPEN_FILE, (U64)path, 0, 0);
    if (!handle || handle == (U64)-1) return 0;
    U64 got = imageres_syscall(IMAGERES_SYS_READ_FILE, handle, (U64)buffer, capacity);
    (void)imageres_syscall(IMAGERES_SYS_CLOSE_FILE, handle, 0, 0);
    if (got < 54u || got > capacity) return 0;
    const unsigned char *bytes = (const unsigned char *)buffer;
    if (bytes[0] != 'B' || bytes[1] != 'M') return 0;
    return (UINT)got;
}
