#pragma once
// Shared by bootx64.c, bootuni.c and loadufi.c (each is its own freestanding EFI app).
#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

static EFI_SYSTEM_TABLE  *st;
static EFI_BOOT_SERVICES *bs;

void *memcpy(void *d, const void *s, UINTN n) { UINT8 *a = d; const UINT8 *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, UINTN n)          { UINT8 *a = d; while (n--) *a++ = (UINT8)c; return d; }

/* UEFI diagnostics go to COM1 only. Never print firmware text on the display:
 * the firmware stage paints the same clean blue/cyan background as bootvid. */
static void serial_out8(UINT16 port, UINT8 value) {
    __asm__ volatile("outb %0, %1" :: "a"(value), "Nd"(port));
}
static UINT8 serial_in8(UINT16 port) {
    UINT8 value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static void serial_init(void) {
    static int ready;
    if (ready) return;
    serial_out8(0x3F9, 0x00);
    serial_out8(0x3FB, 0x80);
    serial_out8(0x3F8, 0x01);
    serial_out8(0x3F9, 0x00);
    serial_out8(0x3FB, 0x03);
    serial_out8(0x3FA, 0xC7);
    serial_out8(0x3FC, 0x0B);
    ready = 1;
}
static void serial_char(char c) {
    serial_init();
    for (UINTN spin=0; spin<100000u; ++spin)
        if (serial_in8(0x3FD) & 0x20u) { serial_out8(0x3F8, (UINT8)c); return; }
}
static void say(CHAR16 *s) {
    if (!s) return;
    for (; *s; ++s) {
        UINT32 c = (UINT32)*s;
        if (c == L'\r' && s[1] == L'\n') continue;
        if (c == L'\n') serial_char('\r');
        if (c < 0x80u) serial_char((char)c);
        else serial_char('?');
    }
}
static void paint_firmware_background(void) {
    static EFI_GUID gop_guid = {0x9042a9de,0x23dc,0x4a38,{0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    if (!st || !bs || EFI_ERROR(bs->LocateProtocol(&gop_guid, NULL, (VOID **)&gop)) ||
        !gop || !gop->Mode || !gop->Mode->Info) return;
    UINT32 width = gop->Mode->Info->HorizontalResolution;
    UINT32 height = gop->Mode->Info->VerticalResolution;
    if (!width || !height) return;
    /* Keep firmware handoff visually quiet until the OS boot video takes over. */
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL px;
    px.Red = 0;
    px.Green = 0;
    px.Blue = 0;
    px.Reserved = 0;
    (void)gop->Blt(gop, &px, EfiBltVideoFill, 0, 0, 0, 0, width, height, 0);
}

static int same(const void *a, const void *b, UINTN n) {
    const UINT8 *x = a, *y = b;
    while (n--) if (*x++ != *y++) return 0;
    return 1;
}