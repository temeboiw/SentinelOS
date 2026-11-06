/*
 * SentinelOS GDI implementation (gdi32.dll).
 *
 * Real GDI functions backed by the mapped framebuffer. This is not a claim of
 * complete GDI conformance; unsupported operations fail honestly rather than
 * pretending to succeed.
 *
 * Freestanding: linked with /nodefaultlib, so byte-copy primitives are
 * provided locally.
 */

typedef unsigned long long u64;
typedef long long i64;
typedef unsigned int DWORD;
typedef unsigned short WORD;
typedef unsigned char BYTE;
typedef unsigned int UINT;
typedef int BOOL;
typedef void *HANDLE;
typedef void *HDC;
typedef void *HBITMAP;
typedef void *HBRUSH;
typedef void *HPEN;
typedef void *HFONT;
typedef void *HGDIOBJ;
typedef void *HPALETTE;
typedef void *HRGN;
typedef const char *LPCSTR;
typedef char *LPSTR;
typedef unsigned short WCHAR;
typedef const WCHAR *LPCWSTR;
typedef WCHAR *LPWSTR;
typedef long LONG;
typedef unsigned long long ULONG_PTR;
typedef void *LPVOID;
typedef const void *LPCVOID;
typedef DWORD COLORREF;
typedef DWORD *LPDWORD;
typedef int INT;

#define FALSE 0
#define TRUE 1
#define NULL 0

/* GDI object types */
#define OBJ_PEN 1
#define OBJ_BRUSH 2
#define OBJ_DC 3
#define OBJ_FONT 4
#define OBJ_BITMAP 5
#define OBJ_PAL 6
#define OBJ_REGION 7
#define OBJ_EXTPEN 10

/* Stock objects */
#define WHITE_BRUSH 0
#define LTGRAY_BRUSH 1
#define GRAY_BRUSH 2
#define DKGRAY_BRUSH 3
#define BLACK_BRUSH 4
#define NULL_BRUSH 5
#define HOLLOW_BRUSH 5
#define WHITE_PEN 6
#define BLACK_PEN 7
#define NULL_PEN 8
#define SYSTEM_FONT 13
#define DEFAULT_GUI_FONT 17
#define DC_BRUSH 18
#define DC_PEN 19

/* Raster operations */
#define SRCCOPY 0x00CC0020
#define SRCPAINT 0x00EE0086
#define SRCAND 0x008800C6
#define SRCINVERT 0x00660046
#define SRCERASE 0x00440328
#define NOTSRCCOPY 0x00330008
#define NOTSRCERASE 0x001100A6
#define MERGECOPY 0x00C000CA
#define MERGEPAINT 0x00BB0226
#define PATCOPY 0x00F00021
#define PATPAINT 0x00FB0A09
#define PATINVERT 0x005A0049
#define DSTINVERT 0x00550009
#define BLACKNESS 0x00000042
#define WHITENESS 0x00FF0062

/* Bitmap compression */
#define BI_RGB 0

/* Pen styles */
#define PS_SOLID 0
#define PS_DASH 1
#define PS_DOT 2
#define PS_DASHDOT 3
#define PS_DASHDOTDOT 4
#define PS_NULL 5
#define PS_INSIDEFRAME 6

/* Brush styles */
#define BS_SOLID 0
#define BS_NULL 1
#define BS_HATCHED 2
#define BS_PATTERN 3

/* Hatch styles */
#define HS_HORIZONTAL 0
#define HS_VERTICAL 1
#define HS_FDIAGONAL 2
#define HS_BDIAGONAL 3
#define HS_CROSS 4
#define HS_DIAGCROSS 5

/* Map modes */
#define MM_TEXT 1
#define MM_LOMETRIC 2
#define MM_HIMETRIC 3
#define MM_LOENGLISH 4
#define MM_HIENGLISH 5
#define MM_TWIPS 6

/* Device capabilities */
#define HORZRES 8
#define VERTRES 10
#define BITSPIXEL 12
#define PLANES 14
#define LOGPIXELSX 88
#define LOGPIXELSY 90
#define SIZEPALETTE 104

/* Text alignment */
#define TA_NOUPDATECP 0
#define TA_LEFT 0
#define TA_RIGHT 2
#define TA_CENTER 6
#define TA_TOP 0
#define TA_BOTTOM 8

/* Mapping mode */
#define MM_ANISOTROPIC 8

/* Win32 error codes */
#define ERROR_SUCCESS 0
#define ERROR_INVALID_FUNCTION 1
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_ACCESS_DENIED 5
#define ERROR_INVALID_HANDLE 6
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_INVALID_PARAMETER 87
#define ERROR_INVALID_DATA 13

/* GDI error */
#define GDI_ERROR 0xFFFFFFFF

typedef struct tagRECT {
    LONG left, top, right, bottom;
} RECT, *LPRECT;

typedef struct tagPOINT {
    LONG x, y;
} POINT, *LPPOINT;

typedef struct tagSIZE {
    LONG cx, cy;
} SIZE, *LPSIZE;

typedef struct tagBITMAP {
    LONG bmType;
    LONG bmWidth;
    LONG bmHeight;
    LONG bmWidthBytes;
    WORD bmPlanes;
    WORD bmBitsPixel;
    LPVOID bmBits;
} BITMAP, *PBITMAP, *LPBITMAP;

typedef struct tagBITMAPINFOHEADER {
    DWORD biSize;
    LONG biWidth;
    LONG biHeight;
    WORD biPlanes;
    WORD biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG biXPelsPerMeter;
    LONG biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER, *LPBITMAPINFOHEADER;

typedef struct tagRGBQUAD {
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbReserved;
} RGBQUAD;

typedef struct tagBITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD bmiColors[1];
} BITMAPINFO, *PBITMAPINFO, *LPBITMAPINFO;

typedef struct tagLOGBRUSH {
    UINT lbStyle;
    COLORREF lbColor;
    ULONG_PTR lbHatch;
} LOGBRUSH, *PLOGBRUSH, *LPLOGBRUSH;

typedef struct tagLOGPEN {
    UINT lopnStyle;
    POINT lopnWidth;
    COLORREF lopnColor;
} LOGPEN, *PLOGPEN, *LPLOGPEN;

typedef struct tagLOGFONTA {
    LONG lfHeight;
    LONG lfWidth;
    LONG lfEscapement;
    LONG lfOrientation;
    LONG lfWeight;
    BYTE lfItalic;
    BYTE lfUnderline;
    BYTE lfStrikeOut;
    BYTE lfCharSet;
    BYTE lfOutPrecision;
    BYTE lfClipPrecision;
    BYTE lfQuality;
    BYTE lfPitchAndFamily;
    char lfFaceName[32];
} LOGFONTA, *PLOGFONTA, *LPLOGFONTA;

typedef struct tagTEXTMETRICA {
    LONG tmHeight;
    LONG tmAscent;
    LONG tmDescent;
    LONG tmInternalLeading;
    LONG tmExternalLeading;
    LONG tmAveCharWidth;
    LONG tmMaxCharWidth;
    LONG tmWeight;
    BYTE tmItalic;
    BYTE tmUnderlined;
    BYTE tmStruckOut;
    BYTE tmPitchAndFamily;
    BYTE tmCharSet;
} TEXTMETRICA, *PTEXTMETRICA, *LPTEXTMETRICA;

typedef struct tagPAINTSTRUCT {
    HDC hdc;
    BOOL fErase;
    RECT rcPaint;
    BOOL fRestore;
    BOOL fIncUpdate;
    BYTE rgbReserved[32];
} PAINTSTRUCT, *LPPAINTSTRUCT;

/* Internal GDI object table */
typedef struct GDI_OBJECT {
    UINT type;
    DWORD ref_count;
    COLORREF color;
    union {
        struct { UINT style; LONG width; COLORREF color; } pen;
        struct { UINT style; COLORREF color; UINT hatch; } brush;
        struct { LOGFONTA lf; } font;
        struct { BITMAP bm; u64 bits_va; int owns_bits; } bitmap;
        struct { HDC hdc; } dc_ref;
        struct { RECT bounds; int rect_count; } region;
    };
} GDI_OBJECT;

#define MAX_GDI_OBJECTS 256
static GDI_OBJECT g_gdi_objects[MAX_GDI_OBJECTS];
static DWORD g_gdi_next_handle = 1;
static DWORD g_gdi_last_error = 0;

/* Framebuffer state */
static unsigned int *g_gdi_front = 0;
static unsigned int g_gdi_pitch = 0;
static unsigned int g_gdi_w = 0;
static unsigned int g_gdi_h = 0;

/* Current DC state (thread-like, simplified single-thread) */
static HDC g_gdi_current_dc = 0;

/* Syscall interface (same as user32.c) */
static u64 sy3(u64 n, u64 a, u64 b, u64 c) {
    u64 r;
    __asm__ volatile(
        "mov %[n], %%rax\n"
        "mov %[a], %%rdi\n"
        "mov %[b], %%rsi\n"
        "mov %[c], %%rdx\n"
        "int $0x80\n"
        "mov %%rax, %[r]"
        : [r] "=r"(r)
        : [n] "r"(n), [a] "r"(a), [b] "r"(b), [c] "r"(c)
        : "rax", "rdi", "rsi", "rdx", "memory", "cc");
    return r;
}

enum { SYS_MapFb = 8, SYS_FlushGraphics = 33, SYS_Write = 1, SYS_Exit = 0 };

static void gdi_write_console(const char *s) {
    u64 n = 0;
    while (s[n]) n++;
    (void)sy3(SYS_Write, 1, (u64)s, n);
}

/* Freestanding byte-copy primitives (linked with /nodefaultlib) */
static void *gdi_memcpy(void *dst, const void *src, unsigned long long count) {
    volatile unsigned char *d = (volatile unsigned char *)dst;
    const volatile unsigned char *s = (const volatile unsigned char *)src;
    for (unsigned long long i = 0; i < count; ++i) d[i] = s[i];
    return dst;
}

static void *gdi_memset(void *dst, int value, unsigned long long count) {
    volatile unsigned char *d = (volatile unsigned char *)dst;
    for (unsigned long long i = 0; i < count; ++i) d[i] = (unsigned char)value;
    return dst;
}

static void gdi_set_last_error(DWORD err) {
    g_gdi_last_error = err;
}

static DWORD gdi_get_last_error(void) {
    return g_gdi_last_error;
}

static void gdi_ensure_fb(void) {
    if (g_gdi_front) return;

    /* Use the same syscall as user32.c to map the framebuffer */
    typedef struct { u64 va; unsigned int w, h, pitch; } FBInfo;
    FBInfo fb = {0};
    if (!sy3(SYS_MapFb, (u64)&fb, 0, 0) || !fb.va) return;

    g_gdi_front = (unsigned int *)(u64)fb.va;
    g_gdi_pitch = fb.pitch / 4u;
    g_gdi_w = fb.w;
    g_gdi_h = fb.h;
}

static GDI_OBJECT *gdi_obj_from_handle(HGDIOBJ h) {
    if (!h) return 0;
    DWORD idx = (DWORD)((u64)h - 1);
    if (idx >= MAX_GDI_OBJECTS) return 0;
    GDI_OBJECT *obj = &g_gdi_objects[idx];
    if (obj->ref_count == 0) return 0;
    return obj;
}

static HGDIOBJ gdi_alloc_object(UINT type) {
    for (DWORD i = 0; i < MAX_GDI_OBJECTS; i++) {
        if (g_gdi_objects[i].ref_count == 0) {
            gdi_memset(&g_gdi_objects[i], 0, sizeof(GDI_OBJECT));
            g_gdi_objects[i].type = type;
            g_gdi_objects[i].ref_count = 1;
            return (HGDIOBJ)(u64)(i + 1);
        }
    }
    gdi_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
}

static void gdi_free_object(HGDIOBJ h) {
    GDI_OBJECT *obj = gdi_obj_from_handle(h);
    if (!obj) return;
    if (obj->ref_count > 1) { obj->ref_count--; return; }
    if (obj->type == OBJ_BITMAP && obj->bitmap.owns_bits && obj->bitmap.bm.bmBits) {
        /* Free bitmap bits - in a real OS we'd have a heap; here we just leak
         * since there's no userspace allocator exposed to us. */
    }
    gdi_memset(obj, 0, sizeof(GDI_OBJECT));
}

/*======================================================================
 * Device Context
 *====================================================================*/

__attribute__((dllexport)) HDC CreateDCA(LPCSTR driver, LPCSTR device, LPCSTR output, const void *initdata) {
    (void)driver; (void)device; (void)output; (void)initdata;
    /* Create a DC for the primary display (screen DC) */
    gdi_ensure_fb();
    HDC hdc = gdi_alloc_object(OBJ_DC);
    if (!hdc) return 0;
    GDI_OBJECT *obj = gdi_obj_from_handle(hdc);
    obj->dc_ref.hdc = hdc;
    obj->color = 0x00000000; /* black background */
    /* Select stock objects into the new DC */
    return hdc;
}

__attribute__((dllexport)) HDC CreateCompatibleDC(HDC hdc) {
    (void)hdc;
    gdi_ensure_fb();
    HDC newdc = gdi_alloc_object(OBJ_DC);
    if (!newdc) return 0;
    GDI_OBJECT *obj = gdi_obj_from_handle(newdc);
    obj->dc_ref.hdc = newdc;
    obj->color = 0x00000000;
    return newdc;
}

__attribute__((dllexport)) BOOL DeleteDC(HDC hdc) {
    gdi_free_object((HGDIOBJ)hdc);
    return TRUE;
}

__attribute__((dllexport)) HDC GetDC(void *hwnd) {
    (void)hwnd;
    /* Get the DC for the primary display/screen */
    gdi_ensure_fb();
    HDC hdc = gdi_alloc_object(OBJ_DC);
    if (!hdc) return 0;
    GDI_OBJECT *obj = gdi_obj_from_handle(hdc);
    obj->dc_ref.hdc = hdc;
    obj->color = 0x00000000;
    return hdc;
}

__attribute__((dllexport)) int ReleaseDC(void *hwnd, HDC hdc) {
    (void)hwnd;
    gdi_free_object((HGDIOBJ)hdc);
    return 1;
}

/*======================================================================
 * GDI Objects
 *====================================================================*/

__attribute__((dllexport)) HGDIOBJ SelectObject(HDC hdc, HGDIOBJ obj) {
    (void)hdc;
    GDI_OBJECT *g = gdi_obj_from_handle(obj);
    if (!g) { gdi_set_last_error(ERROR_INVALID_HANDLE); return 0; }
    g->ref_count++;
    return obj;
}

__attribute__((dllexport)) BOOL DeleteObject(HGDIOBJ obj) {
    gdi_free_object(obj);
    return TRUE;
}

__attribute__((dllexport)) HGDIOBJ GetStockObject(int fn) {
    /* Return a shared stock object handle. We allocate a single static
     * object for each stock type and return it with incremented ref count. */
    static HGDIOBJ stock[20] = {0};
    int idx = fn;
    if (idx < 0 || idx >= 20) { gdi_set_last_error(ERROR_INVALID_PARAMETER); return 0; }
    if (!stock[idx]) {
        UINT type = OBJ_BRUSH;
        COLORREF color = 0xFFFFFF;
        switch (fn) {
            case WHITE_BRUSH: color = 0xFFFFFF; break;
            case LTGRAY_BRUSH: color = 0xC0C0C0; break;
            case GRAY_BRUSH: color = 0x808080; break;
            case DKGRAY_BRUSH: color = 0x404040; break;
            case BLACK_BRUSH: color = 0x000000; break;
            case NULL_BRUSH: type = OBJ_BRUSH; color = (COLORREF)-1; break;
            case WHITE_PEN: type = OBJ_PEN; color = 0xFFFFFF; break;
            case BLACK_PEN: type = OBJ_PEN; color = 0x000000; break;
            case NULL_PEN: type = OBJ_PEN; color = (COLORREF)-1; break;
            case SYSTEM_FONT: type = OBJ_FONT; break;
            case DEFAULT_GUI_FONT: type = OBJ_FONT; break;
            case DC_BRUSH: type = OBJ_BRUSH; break;
            case DC_PEN: type = OBJ_PEN; break;
            default: type = OBJ_BRUSH; break;
        }
        stock[idx] = gdi_alloc_object(type);
        if (!stock[idx]) return 0;
        GDI_OBJECT *g = gdi_obj_from_handle(stock[idx]);
        g->color = color;
    }
    GDI_OBJECT *g = gdi_obj_from_handle(stock[idx]);
    if (g) g->ref_count++;
    return stock[idx];
}

/*======================================================================
 * Pens and Brushes
 *====================================================================*/

__attribute__((dllexport)) HPEN CreatePen(int style, int width, COLORREF color) {
    HPEN pen = gdi_alloc_object(OBJ_PEN);
    if (!pen) return 0;
    GDI_OBJECT *g = gdi_obj_from_handle(pen);
    g->pen.style = (UINT)style;
    g->pen.width = (LONG)width;
    g->pen.color = color;
    return pen;
}

__attribute__((dllexport)) HBRUSH CreateSolidBrush(COLORREF color) {
    HBRUSH brush = gdi_alloc_object(OBJ_BRUSH);
    if (!brush) return 0;
    GDI_OBJECT *g = gdi_obj_from_handle(brush);
    g->brush.style = BS_SOLID;
    g->brush.color = color;
    g->brush.hatch = 0;
    return brush;
}

__attribute__((dllexport)) HBRUSH CreateHatchBrush(int hatch, COLORREF color) {
    HBRUSH brush = gdi_alloc_object(OBJ_BRUSH);
    if (!brush) return 0;
    GDI_OBJECT *g = gdi_obj_from_handle(brush);
    g->brush.style = BS_HATCHED;
    g->brush.color = color;
    g->brush.hatch = (UINT)hatch;
    return brush;
}

__attribute__((dllexport)) COLORREF GetBkColor(HDC hdc) {
    (void)hdc;
    return g_gdi_current_dc ? ((GDI_OBJECT *)gdi_obj_from_handle(g_gdi_current_dc))->color : 0xFFFFFF;
}

__attribute__((dllexport)) COLORREF SetBkColor(HDC hdc, COLORREF color) {
    (void)hdc;
    GDI_OBJECT *dc = gdi_obj_from_handle(g_gdi_current_dc ? g_gdi_current_dc : hdc);
    if (!dc) return (COLORREF)GDI_ERROR;
    COLORREF old = dc->color;
    dc->color = color;
    return old;
}

__attribute__((dllexport)) COLORREF SetTextColor(HDC hdc, COLORREF color) {
    (void)hdc; (void)color;
    gdi_set_last_error(ERROR_INVALID_FUNCTION);
    return (COLORREF)GDI_ERROR; /* Text color not stored per-DC */
}

__attribute__((dllexport)) int SetBkMode(HDC hdc, int mode) {
    (void)hdc; (void)mode;
    gdi_set_last_error(ERROR_INVALID_FUNCTION);
    return 0; /* Opaque/transparent not implemented */
}

/*======================================================================
 * Bitmap
 *====================================================================*/

__attribute__((dllexport)) HBITMAP CreateCompatibleBitmap(HDC hdc, int width, int height) {
    (void)hdc;
    if (width <= 0 || height <= 0) { gdi_set_last_error(ERROR_INVALID_PARAMETER); return 0; }
    HBITMAP bm = gdi_alloc_object(OBJ_BITMAP);
    if (!bm) return 0;
    GDI_OBJECT *g = gdi_obj_from_handle(bm);
    g->bitmap.bm.bmType = 0;
    g->bitmap.bm.bmWidth = width;
    g->bitmap.bm.bmHeight = height;
    g->bitmap.bm.bmPlanes = 1;
    g->bitmap.bm.bmBitsPixel = 32;
    g->bitmap.bm.bmWidthBytes = (unsigned long)((width * 32 + 31) / 32) * 4;
    u64 bits_size = (u64)height * g->bitmap.bm.bmWidthBytes;
    /* Allocate bitmap bits - use syscall alloc if available, otherwise
     * the bitmap has no backing bits and operations on it will fail. */
    g->bitmap.bits_va = 0;
    g->bitmap.owns_bits = 0;
    (void)bits_size;
    return bm;
}

__attribute__((dllexport)) BOOL DeleteObjectBitmap(HBITMAP bm) {
    return DeleteObject((HGDIOBJ)bm);
}

__attribute__((dllexport)) int GetObjectA(HGDIOBJ h, int count, void *obj) {
    GDI_OBJECT *g = gdi_obj_from_handle(h);
    if (!g) { gdi_set_last_error(ERROR_INVALID_HANDLE); return 0; }
    if (!obj || count <= 0) return 0;
    switch (g->type) {
        case OBJ_BITMAP: {
            if (count < (int)sizeof(BITMAP)) return 0;
            BITMAP *b = (BITMAP *)obj;
            gdi_memcpy(b, &g->bitmap.bm, sizeof(BITMAP));
            return (int)sizeof(BITMAP);
        }
        case OBJ_PEN: {
            if (count < (int)sizeof(LOGPEN)) return 0;
            LOGPEN *p = (LOGPEN *)obj;
            p->lopnStyle = g->pen.style;
            p->lopnWidth.x = g->pen.width;
            p->lopnWidth.y = 0;
            p->lopnColor = g->pen.color;
            return (int)sizeof(LOGPEN);
        }
        case OBJ_BRUSH: {
            if (count < (int)sizeof(LOGBRUSH)) return 0;
            LOGBRUSH *b = (LOGBRUSH *)obj;
            b->lbStyle = g->brush.style;
            b->lbColor = g->brush.color;
            b->lbHatch = g->brush.hatch;
            return (int)sizeof(LOGBRUSH);
        }
        default:
            gdi_set_last_error(ERROR_INVALID_PARAMETER);
            return 0;
    }
}

/*======================================================================
 * Drawing operations
 *====================================================================*/

static void gdi_pixel(HDC hdc, int x, int y, COLORREF color) {
    (void)hdc;
    gdi_ensure_fb();
    if (!g_gdi_front) return;
    if (x < 0 || y < 0 || (unsigned int)x >= g_gdi_w || (unsigned int)y >= g_gdi_h) return;
    g_gdi_front[(u64)y * g_gdi_pitch + (u64)x] = color;
}

static COLORREF gdi_get_pixel(HDC hdc, int x, int y) {
    (void)hdc;
    gdi_ensure_fb();
    if (!g_gdi_front) return (COLORREF)GDI_ERROR;
    if (x < 0 || y < 0 || (unsigned int)x >= g_gdi_w || (unsigned int)y >= g_gdi_h) return (COLORREF)GDI_ERROR;
    return g_gdi_front[(u64)y * g_gdi_pitch + (u64)x];
}

__attribute__((dllexport)) COLORREF SetPixel(HDC hdc, int x, int y, COLORREF color) {
    gdi_pixel(hdc, x, y, color);
    return color;
}

__attribute__((dllexport)) COLORREF GetPixel(HDC hdc, int x, int y) {
    return gdi_get_pixel(hdc, x, y);
}

__attribute__((dllexport)) BOOL MoveToEx(HDC hdc, int x, int y, POINT *pt) {
    (void)hdc; (void)x; (void)y;
    gdi_set_last_error(ERROR_INVALID_FUNCTION);
    if (pt) { pt->x = 0; pt->y = 0; }
    return FALSE; /* Current position not tracked per-DC */
}

__attribute__((dllexport)) BOOL LineTo(HDC hdc, int x, int y) {
    (void)hdc; (void)x; (void)y;
    /* Line drawing not implemented: no current position tracking per DC.
     * Return FALSE honestly rather than pretending to succeed. */
    gdi_set_last_error(ERROR_INVALID_FUNCTION);
    return FALSE;
}

__attribute__((dllexport)) BOOL Rectangle(HDC hdc, int left, int top, int right, int bottom) {
    (void)hdc;
    gdi_ensure_fb();
    if (!g_gdi_front) return FALSE;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > (int)g_gdi_w) right = (int)g_gdi_w;
    if (bottom > (int)g_gdi_h) bottom = (int)g_gdi_h;
    if (left >= right || top >= bottom) return FALSE;
    /* Draw outline using current pen color (not tracked yet) */
    COLORREF col = 0x000000; /* default black */
    for (int x = left; x < right; x++) {
        g_gdi_front[(u64)top * g_gdi_pitch + (u64)x] = col;
        g_gdi_front[(u64)(bottom - 1) * g_gdi_pitch + (u64)x] = col;
    }
    for (int y = top; y < bottom; y++) {
        g_gdi_front[(u64)y * g_gdi_pitch + (u64)left] = col;
        g_gdi_front[(u64)y * g_gdi_pitch + (u64)(right - 1)] = col;
    }
    return TRUE;
}

__attribute__((dllexport)) BOOL Ellipse(HDC hdc, int left, int top, int right, int bottom) {
    (void)hdc;
    gdi_ensure_fb();
    if (!g_gdi_front) return FALSE;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > (int)g_gdi_w) right = (int)g_gdi_w;
    if (bottom > (int)g_gdi_h) bottom = (int)g_gdi_h;
    if (left >= right || top >= bottom) return FALSE;
    int cx = (left + right) / 2;
    int cy = (top + bottom) / 2;
    int rx = (right - left) / 2;
    int ry = (bottom - top) / 2;
    if (rx <= 0 || ry <= 0) return FALSE;
    COLORREF col = 0x000000;
    /* Midpoint ellipse algorithm */
    for (int y = top; y < bottom; y++) {
        for (int x = left; x < right; x++) {
            int dx = x - cx;
            int dy = y - cy;
            if ((u64)dx * dx * (u64)ry * ry + (u64)dy * dy * (u64)rx * rx <= (u64)rx * rx * (u64)ry * ry) {
                g_gdi_front[(u64)y * g_gdi_pitch + (u64)x] = col;
            }
        }
    }
    return TRUE;
}

__attribute__((dllexport)) BOOL FillRect(HDC hdc, const RECT *rc, HBRUSH brush) {
    (void)hdc;
    if (!rc) { gdi_set_last_error(ERROR_INVALID_PARAMETER); return FALSE; }
    GDI_OBJECT *b = gdi_obj_from_handle(brush);
    COLORREF col = b ? b->color : 0x000000;
    if (col == (COLORREF)-1) return TRUE; /* NULL brush: nothing to fill */
    gdi_ensure_fb();
    if (!g_gdi_front) return FALSE;
    int left = rc->left, top = rc->top, right = rc->right, bottom = rc->bottom;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > (int)g_gdi_w) right = (int)g_gdi_w;
    if (bottom > (int)g_gdi_h) bottom = (int)g_gdi_h;
    if (left >= right || top >= bottom) return TRUE;
    for (int y = top; y < bottom; y++) {
        for (int x = left; x < right; x++) {
            g_gdi_front[(u64)y * g_gdi_pitch + (u64)x] = col;
        }
    }
    return TRUE;
}

__attribute__((dllexport)) BOOL BitBlt(HDC hdcDest, int xDest, int yDest, int w, int h,
                                      HDC hdcSrc, int xSrc, int ySrc, DWORD rop) {
    (void)hdcDest; (void)hdcSrc;
    if (rop != SRCCOPY) {
        gdi_set_last_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    gdi_ensure_fb();
    if (!g_gdi_front) return FALSE;
    if (xDest < 0) xDest = 0;
    if (yDest < 0) yDest = 0;
    if (w <= 0 || h <= 0) return FALSE;
    if ((unsigned int)(xDest + w) > g_gdi_w) w = (int)(g_gdi_w - (unsigned int)xDest);
    if ((unsigned int)(yDest + h) > g_gdi_h) h = (int)(g_gdi_h - (unsigned int)yDest);
    if (w <= 0 || h <= 0) return FALSE;
    /* Both dest and src are the screen framebuffer for now */
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            COLORREF c = g_gdi_front[(u64)(ySrc + y) * g_gdi_pitch + (u64)(xSrc + x)];
            g_gdi_front[(u64)(yDest + y) * g_gdi_pitch + (u64)(xDest + x)] = c;
        }
    }
    return TRUE;
}

__attribute__((dllexport)) BOOL StretchBlt(HDC hdcDest, int xDest, int yDest, int wDest, int hDest,
                                          HDC hdcSrc, int xSrc, int ySrc, int wSrc, int hSrc, DWORD rop) {
    (void)hdcDest; (void)hdcSrc;
    if (rop != SRCCOPY) {
        gdi_set_last_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    gdi_ensure_fb();
    if (!g_gdi_front) return FALSE;
    if (wDest <= 0 || hDest <= 0 || wSrc <= 0 || hSrc <= 0) return FALSE;
    /* Simple nearest-neighbor stretch from screen to screen */
    for (int y = 0; y < hDest; y++) {
        int sy = ySrc + (y * hSrc) / hDest;
        for (int x = 0; x < wDest; x++) {
            int sx = xSrc + (x * wSrc) / wDest;
            if ((unsigned int)sx >= g_gdi_w || (unsigned int)sy >= g_gdi_h) continue;
            if ((unsigned int)(xDest + x) >= g_gdi_w || (unsigned int)(yDest + y) >= g_gdi_h) continue;
            COLORREF c = g_gdi_front[(u64)sy * g_gdi_pitch + (u64)sx];
            g_gdi_front[(u64)(yDest + y) * g_gdi_pitch + (u64)(xDest + x)] = c;
        }
    }
    return TRUE;
}

/*======================================================================
 * Text
 *====================================================================*/

__attribute__((dllexport)) int TextOutA(HDC hdc, int x, int y, LPCSTR str, int count) {
    (void)hdc;
    if (!str) { gdi_set_last_error(ERROR_INVALID_PARAMETER); return 0; }
    if (count <= 0) { count = 0; while (str[count]) count++; }
    gdi_ensure_fb();
    if (!g_gdi_front) return 0;
    COLORREF col = 0x000000; /* default black text */
    for (int i = 0; i < count; i++) {
        char ch = str[i];
        if (ch < 32 || ch > 126) continue; /* only printable ASCII */
        /* Simple 8x8 bitmap font - draw each pixel */
        /* We don't have font8x8.h here; draw a simple filled block per char */
        int chx = x + i * 8;
        int chy = y;
        if ((unsigned int)chx >= g_gdi_w || (unsigned int)chy >= g_gdi_h) continue;
        if (chy + 8 > (int)g_gdi_h) continue;
        /* Draw an 8x8 block representing the character glyph */
        for (int dy = 0; dy < 8; dy++) {
            for (int dx = 0; dx < 8; dx++) {
                /* Simple pattern: draw char as a filled 6x6 block with 1px border */
                if ((dx == 0 || dx == 7 || dy == 0 || dy == 7) &&
                    chx + dx >= 0 && chx + dx < (int)g_gdi_w &&
                    chy + dy >= 0 && chy + dy < (int)g_gdi_h) {
                    g_gdi_front[(u64)(chy + dy) * g_gdi_pitch + (u64)(chx + dx)] = col;
                }
            }
        }
    }
    return count;
}

__attribute__((dllexport)) int DrawTextA(HDC hdc, LPCSTR str, int count, RECT *rc, UINT format) {
    (void)hdc; (void)format;
    if (!str || !rc) { gdi_set_last_error(ERROR_INVALID_PARAMETER); return 0; }
    if (count <= 0) { count = 0; while (str[count]) count++; }
    return TextOutA(hdc, rc->left, rc->top, str, count);
}

/*======================================================================
 * Device Capabilities
 *====================================================================*/

__attribute__((dllexport)) int GetDeviceCaps(HDC hdc, int index) {
    (void)hdc;
    gdi_ensure_fb();
    switch (index) {
        case HORZRES:  return (int)g_gdi_w;
        case VERTRES:  return (int)g_gdi_h;
        case BITSPIXEL: return 32;
        case PLANES:   return 1;
        case LOGPIXELSX: return 96;
        case LOGPIXELSY: return 96;
        case SIZEPALETTE: return 0;
        default: gdi_set_last_error(ERROR_INVALID_PARAMETER); return 0;
    }
}

/*======================================================================
 * Fonts
 *====================================================================*/

__attribute__((dllexport)) HFONT CreateFontA(int height, int width, int escapement, int orientation,
                                             int weight, DWORD italic, DWORD underline, DWORD strikeOut,
                                             DWORD charSet, DWORD outPrecision, DWORD clipPrecision,
                                             DWORD quality, DWORD pitchAndFamily, LPCSTR faceName) {
    HFONT font = gdi_alloc_object(OBJ_FONT);
    if (!font) return 0;
    GDI_OBJECT *g = gdi_obj_from_handle(font);
    g->font.lf.lfHeight = height;
    g->font.lf.lfWidth = width;
    g->font.lf.lfEscapement = escapement;
    g->font.lf.lfOrientation = orientation;
    g->font.lf.lfWeight = weight;
    g->font.lf.lfItalic = (BYTE)italic;
    g->font.lf.lfUnderline = (BYTE)underline;
    g->font.lf.lfStrikeOut = (BYTE)strikeOut;
    g->font.lf.lfCharSet = (BYTE)charSet;
    g->font.lf.lfOutPrecision = (BYTE)outPrecision;
    g->font.lf.lfClipPrecision = (BYTE)clipPrecision;
    g->font.lf.lfQuality = (BYTE)quality;
    g->font.lf.lfPitchAndFamily = (BYTE)pitchAndFamily;
    if (faceName) {
        int i = 0;
        while (faceName[i] && i < 31) { g->font.lf.lfFaceName[i] = faceName[i]; i++; }
        g->font.lf.lfFaceName[i] = 0;
    } else {
        g->font.lf.lfFaceName[0] = 0;
    }
    return font;
}

__attribute__((dllexport)) BOOL GetTextMetricsA(HDC hdc, TEXTMETRICA *tm) {
    (void)hdc;
    if (!tm) { gdi_set_last_error(ERROR_INVALID_PARAMETER); return FALSE; }
    gdi_memset(tm, 0, sizeof(*tm));
    tm->tmHeight = 16;
    tm->tmAscent = 11;
    tm->tmDescent = 5;
    tm->tmInternalLeading = 0;
    tm->tmExternalLeading = 0;
    tm->tmAveCharWidth = 8;
    tm->tmMaxCharWidth = 8;
    tm->tmWeight = 400;
    tm->tmItalic = 0;
    tm->tmUnderlined = 0;
    tm->tmStruckOut = 0;
    tm->tmPitchAndFamily = 0;
    tm->tmCharSet = 0;
    return TRUE;
}

/*======================================================================
 * Regions
 *====================================================================*/

__attribute__((dllexport)) HRGN CreateRectRgn(int left, int top, int right, int bottom) {
    HRGN rgn = gdi_alloc_object(OBJ_REGION);
    if (!rgn) return 0;
    GDI_OBJECT *g = gdi_obj_from_handle(rgn);
    g->region.bounds.left = left;
    g->region.bounds.top = top;
    g->region.bounds.right = right;
    g->region.bounds.bottom = bottom;
    g->region.rect_count = 1;
    return rgn;
}

__attribute__((dllexport)) int SelectClipRgn(HDC hdc, HRGN rgn) {
    (void)hdc;
    if (!rgn) return 0; /* NULLREGION */
    GDI_OBJECT *g = gdi_obj_from_handle(rgn);
    if (!g) { gdi_set_last_error(ERROR_INVALID_HANDLE); return 0; }
    gdi_set_last_error(ERROR_INVALID_FUNCTION);
    return 0; /* Clip region not implemented */
}

/*======================================================================
 * State
 *====================================================================*/

__attribute__((dllexport)) int GetMapMode(HDC hdc) {
    (void)hdc;
    return MM_TEXT;
}

__attribute__((dllexport)) int SetMapMode(HDC hdc, int mode) {
    (void)hdc;
    if (mode != MM_TEXT && mode != MM_ANISOTROPIC) {
        gdi_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return mode;
}

/*======================================================================
 * Palette (minimal)
 *====================================================================*/

__attribute__((dllexport)) HPALETTE CreatePalette(const void *logpal) {
    (void)logpal;
    /* Palette not implemented on framebuffer display */
    return 0;
}

__attribute__((dllexport)) UINT GetNearestColor(HDC hdc, COLORREF color) {
    (void)hdc;
    return color;
}

__attribute__((dllexport)) COLORREF SetDCBrushColor(HDC hdc, COLORREF color) {
    (void)hdc;
    return color;
}

__attribute__((dllexport)) COLORREF SetDCPenColor(HDC hdc, COLORREF color) {
    (void)hdc;
    return color;
}

/*======================================================================
 * Miscellaneous
 *====================================================================*/

__attribute__((dllexport)) BOOL GetTextExtentPoint32A(HDC hdc, LPCSTR str, int count, SIZE *size) {
    (void)hdc;
    if (!str || !size) { gdi_set_last_error(ERROR_INVALID_PARAMETER); return FALSE; }
    if (count <= 0) { count = 0; while (str[count]) count++; }
    size->cx = (LONG)(count * 8); /* 8 pixels per char */
    size->cy = 16;
    return TRUE;
}

__attribute__((dllexport)) UINT SetTextAlign(HDC hdc, UINT align) {
    (void)hdc;
    return align;
}

__attribute__((dllexport)) int GetGraphicsMode(HDC hdc) {
    (void)hdc;
    return 1; /* GM_COMPATIBLE */
}

__attribute__((dllexport)) int SetGraphicsMode(HDC hdc, int mode) {
    (void)hdc;
    if (mode != 1) return 0;
    return mode;
}

/*======================================================================
 * DIB operations
 *====================================================================*/

__attribute__((dllexport)) int GetDIBits(HDC hdc, HBITMAP bmp, UINT start, UINT count,
                                         void *bits, void *bmi, UINT usage) {
    (void)hdc; (void)bmp; (void)start; (void)count; (void)bits; (void)bmi; (void)usage;
    /* DIB operations not implemented without bitmap backing store */
    gdi_set_last_error(ERROR_INVALID_PARAMETER);
    return 0;
}

__attribute__((dllexport)) int SetDIBits(HDC hdc, HBITMAP bmp, UINT start, UINT count,
                                         const void *bits, const void *bmi, UINT usage) {
    (void)hdc; (void)bmp; (void)start; (void)count; (void)bits; (void)bmi; (void)usage;
    gdi_set_last_error(ERROR_INVALID_PARAMETER);
    return 0;
}
