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
__attribute__((dllexport)) UINT FindImageResourceA(const char *name) {
    if (!name || !name[0]) return 0;
    for (UINT i=0; i<GetImageResourceCount(); ++i) {
        const char *a=g_icon_paths[i], *b=name;
        while (*a && *b) {
            char x=*a++, y=*b++;
            if (x>='A'&&x<='Z') x=(char)(x+32);
            if (y>='A'&&y<='Z') y=(char)(y+32);
            if (x!=y) break;
        }
        if (!*a && !*b) return i+1;
    }
    return 0;
}
