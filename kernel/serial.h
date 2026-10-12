#pragma once
// Kernel debug output.  DbgPrint formats text and hands it to the HAL (hal.dll owns the COM1 port).
#include <stdint.h>
#include <stdarg.h>
#include "ntdef.h"
#include "hal.h"

static void serial_putc(char c) { char s[2] = { c, 0 }; HalDisplayString(s); }   // the HAL turns \n into \r\n
static void kputs(const char *s) { HalDisplayString(s); }

static void put_num(uint64_t v, unsigned base, int upper, int width, int zero) {
    char buf[24]; int n = 0;
    if (v == 0) buf[n++] = '0';
    while (v && n < 23) {
        unsigned d = (unsigned)(v % base);
        buf[n++] = (char)(d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10);
        v /= base;
    }
    while (n < width && n < 23) buf[n++] = zero ? '0' : ' ';
    while (n--) serial_putc(buf[n]);
}

// supports: %s %c %d %u %x %X %p %%   flags: 0, width, l / ll / z (64-bit)
static ULONG DbgPrint(PCSTR fmt, ...) {
    va_list ap; va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') { serial_putc(*fmt); continue; }
        fmt++;
        int zero = 0, width = 0, lng = 0;
        if (*fmt == '0') { zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l' || *fmt == 'z') { lng = 1; fmt++; }
        switch (*fmt) {
        case 's': { const char *s = va_arg(ap, const char *); kputs(s ? s : "(null)"); break; }
        case 'c': serial_putc((char)va_arg(ap, int)); break;
        case 'd': {
            long long v = lng ? va_arg(ap, long long) : va_arg(ap, int);
            if (v < 0) { serial_putc('-'); v = -v; }
            put_num((uint64_t)v, 10, 0, width, zero); break;
        }
        case 'u': put_num(lng ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned), 10, 0, width, zero); break;
        case 'x': put_num(lng ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned), 16, 0, width, zero); break;
        case 'X': put_num(lng ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned), 16, 1, width, zero); break;
        case 'p': kputs("0x"); put_num(va_arg(ap, uint64_t), 16, 0, 16, 1); break;
        case '%': serial_putc('%'); break;
        default:  serial_putc('%'); serial_putc(*fmt); break;
        }
    }
    va_end(ap);
    return 0;
}
