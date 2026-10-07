#pragma once
// Device-path helpers (no EDK2 libraries needed). Include AFTER common.h.
#include <Protocol/LoadedImage.h>
#include <Protocol/DevicePath.h>
#include <Protocol/SimpleFileSystem.h>

EFI_GUID gEfiLoadedImageProtocolGuid      = {0x5B1B31A1,0x9562,0x11d2,{0x8E,0x3F,0x00,0xA0,0xC9,0x69,0x72,0x3B}};
EFI_GUID gEfiDevicePathProtocolGuid       = {0x09576e91,0x6d3f,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
EFI_GUID gEfiSimpleFileSystemProtocolGuid = {0x964e5b22,0x6459,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};

// total size of a device path in bytes, including the end node
static UINTN dp_len(EFI_DEVICE_PATH_PROTOCOL *dp) {
    UINT8 *p = (UINT8 *)dp;
    for (;;) {
        UINT16 l = p[2] | (p[3] << 8);
        if (l < 4) break;
        if (p[0] == 0x7F && p[1] == 0xFF) { p += l; break; }   // end of entire path
        p += l;
    }
    return (UINTN)(p - (UINT8 *)dp);
}

// copy of `dev` with a file-path node (and a new end node) appended
static EFI_DEVICE_PATH_PROTOCOL *dp_append_file(EFI_DEVICE_PATH_PROTOCOL *dev, const CHAR16 *file) {
    UINTN dl = dp_len(dev) - 4;                       // everything but the end node
    UINTN fl = 0; while (file[fl]) fl++;
    UINTN nodelen = 4 + (fl + 1) * 2;
    UINT8 *out;
    if (EFI_ERROR(bs->AllocatePool(EfiLoaderData, dl + nodelen + 4, (VOID **)&out))) return NULL;
    memcpy(out, dev, dl);
    UINT8 *n = out + dl;
    n[0] = 4; n[1] = 4; n[2] = (UINT8)(nodelen & 0xFF); n[3] = (UINT8)(nodelen >> 8);   // MEDIA / FILEPATH
    memcpy(n + 4, file, (fl + 1) * 2);
    n += nodelen;
    n[0] = 0x7F; n[1] = 0xFF; n[2] = 4; n[3] = 0;                                       // END
    return (EFI_DEVICE_PATH_PROTOCOL *)out;
}

static CHAR16 lower16(CHAR16 c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

// does the last file-path node of `dp` end with `name`? (case-insensitive)
static BOOLEAN dp_file_is(EFI_DEVICE_PATH_PROTOCOL *dp, const CHAR16 *name) {
    UINT8 *p = (UINT8 *)dp, *last = NULL;
    for (;;) {
        UINT16 l = p[2] | (p[3] << 8);
        if (l < 4 || (p[0] == 0x7F && p[1] == 0xFF)) break;
        if (p[0] == 4 && p[1] == 4) last = p;
        p += l;
    }
    if (!last) return FALSE;
    CHAR16 *f = (CHAR16 *)(last + 4);
    UINTN fl = 0, nl = 0;
    while (f[fl]) fl++;
    while (name[nl]) nl++;
    if (fl < nl) return FALSE;
    f += fl - nl;
    for (UINTN i = 0; i < nl; i++) if (lower16(f[i]) != lower16(name[i])) return FALSE;
    return TRUE;
}