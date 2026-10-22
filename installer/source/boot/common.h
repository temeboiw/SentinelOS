#pragma once
// Shared by bootx64.c, bootuni.c and loadufi.c (each is its own freestanding EFI app).
#include <Uefi.h>

static EFI_SYSTEM_TABLE  *st;
static EFI_BOOT_SERVICES *bs;

void *memcpy(void *d, const void *s, UINTN n) { UINT8 *a = d; const UINT8 *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, UINTN n)          { UINT8 *a = d; while (n--) *a++ = (UINT8)c; return d; }

/* Early boot diagnostics go directly to COM1 so the boot path remains
 * observable even when GOP/framebuffer initialization is broken. */
static int serial_ready;
static void serial_out8(UINT16 port, UINT8 value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
static UINT8 serial_in8(UINT16 port) {
    UINT8 value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static void serial_init(void) {
    if (serial_ready) return;
    serial_out8(0x3F9, 0x00); /* disable UART interrupts */
    serial_out8(0x3FB, 0x80); /* divisor latch */
    serial_out8(0x3F8, 0x01); /* 115200 baud */
    serial_out8(0x3F9, 0x00);
    serial_out8(0x3FB, 0x03); /* 8N1 */
    serial_out8(0x3FA, 0xC7); /* FIFO */
    serial_out8(0x3FC, 0x0B); /* DTR, RTS, OUT2 */
    serial_ready = 1;
}
static void serial_putc(char c) {
    serial_init();
    if (c == '\\n') {
        while (!(serial_in8(0x3FD) & 0x20)) {}
        serial_out8(0x3F8, '\\r');
    }
    while (!(serial_in8(0x3FD) & 0x20)) {}
    serial_out8(0x3F8, (UINT8)c);
}
static void say(CHAR16 *s) {
    for (; *s; ++s) serial_putc((char)(*s <= 0x7F ? *s : '?'));
}

static int same(const void *a, const void *b, UINTN n) {
    const UINT8 *x = a, *y = b;
    while (n--) if (*x++ != *y++) return 0;
    return 1;
}