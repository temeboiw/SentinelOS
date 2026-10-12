#pragma once
// Shared by bootx64.c, bootuni.c and loadufi.c (each is its own freestanding EFI app).
#include <Uefi.h>

static EFI_SYSTEM_TABLE  *st;
static EFI_BOOT_SERVICES *bs;

void *memcpy(void *d, const void *s, UINTN n) { UINT8 *a = d; const UINT8 *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, UINTN n)          { UINT8 *a = d; while (n--) *a++ = (UINT8)c; return d; }

static void say(CHAR16 *s) { st->ConOut->OutputString(st->ConOut, s); }

static int same(const void *a, const void *b, UINTN n) {
    const UINT8 *x = a, *y = b;
    while (n--) if (*x++ != *y++) return 0;
    return 1;
}