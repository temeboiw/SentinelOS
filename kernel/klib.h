#pragma once
// Freestanding libc bits.  clang may emit calls to memcpy/memset/memmove/memcmp at any time,
// so the kernel must define them.  (Included once, from kernel.c.)
#include <stddef.h>
#include <stdint.h>

void *memcpy(void *d, const void *s, size_t n) {
    void *r = d;
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) :: "memory");
    return r;
}

void *memmove(void *d, const void *s, size_t n) {
    if ((uintptr_t)d <= (uintptr_t)s || (uintptr_t)d >= (uintptr_t)s + n) return memcpy(d, s, n);
    void *r = d;                                     // overlapping, destination above source: copy backwards
    uint8_t *dd = (uint8_t *)d + n - 1; const uint8_t *ss = (const uint8_t *)s + n - 1;
    __asm__ volatile("std; rep movsb; cld" : "+D"(dd), "+S"(ss), "+c"(n) :: "memory");
    return r;
}

void *memset(void *d, int c, size_t n) {
    void *r = d;
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"((uint8_t)c) : "memory");
    return r;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    for (; n; n--, x++, y++) if (*x != *y) return *x < *y ? -1 : 1;
    return 0;
}

size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }