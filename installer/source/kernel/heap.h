#pragma once
// Kernel heap.  Small blocks: size-class slabs (32..2048 bytes including the 16-byte header),
// carved from single pages.  Large blocks: contiguous pages straight from the page allocator.
// Memory is reached through the direct map.  KHEAP_DEBUG adds poison-on-free and use-after-free checks.
#include <stddef.h>
#include <stdint.h>
#include "klib.h"
#include "pmm.h"
#include "vmm.h"

#define KHEAP_DEBUG 1
#define K_HDR    16
#define K_ALLOC  0xA110CA7Eu
#define K_FREE   0xF4EEDEADu
#define K_LARGE  0xFFu
#define K_POISON 0xDD

typedef struct { uint32_t magic, cls; uint64_t size; } KHdr;   // size: bytes requested (allocated) / next-free link (free slab slot)

static const uint32_t k_cls[7] = { 32, 64, 128, 256, 512, 1024, 2048 };
static KHdr *k_free[7];
static uint64_t h_in_use, h_allocs, h_frees, h_errors, h_slab_pages, h_large_pages;
static volatile uint32_t h_lock;
static void h_lock_acquire(void) { while (__atomic_exchange_n(&h_lock,1,__ATOMIC_ACQUIRE)) while (__atomic_load_n(&h_lock,__ATOMIC_RELAXED)) __asm__ volatile("pause"); }
static void h_lock_release(void) { __atomic_store_n(&h_lock,0,__ATOMIC_RELEASE); }

static void h_err(const char *what, void *p) {
    h_errors++;
    DbgPrint("heap: %s at %p\n", what, p);
}

// pages needed by a large block that holds n user bytes plus the header
static uint64_t k_pages(size_t n) { return (n + K_HDR + PAGE_SIZE - 1) / PAGE_SIZE; }

static int k_class_for(size_t need) {
    for (int c = 0; c < 7; c++) if (need <= k_cls[c]) return c;
    return -1;
}

static int k_refill(int c) {
    uint64_t pa = pmm_alloc();
    if (!pa) return 0;
    uint8_t *base = (uint8_t *)(HHDM_BASE + pa);
    uint32_t slot = k_cls[c];
    for (int i = (int)(PAGE_SIZE / slot) - 1; i >= 0; i--) {         // build the free list back to front
        KHdr *h = (KHdr *)(base + (uint64_t)i * slot);
        h->magic = K_FREE; h->cls = (uint32_t)c; h->size = (uint64_t)k_free[c];
#if KHEAP_DEBUG
        memset(h + 1, K_POISON, slot - K_HDR);
#endif
        k_free[c] = h;
    }
    h_slab_pages++;
    return 1;
}

static void *hp_malloc(size_t n) {
    if (!n) n = 1;
    size_t need = n + K_HDR;
    int c = need <= 2048 ? k_class_for(need) : -1;

    if (c >= 0) {
        if (!k_free[c] && !k_refill(c)) return NULL;
        KHdr *h = k_free[c];
        if (h->magic != K_FREE) {                                    // free list got corrupted: abandon it
            h_err("corrupt free list", h);
            k_free[c] = NULL;
            return hp_malloc(n);
        }
        k_free[c] = (KHdr *)h->size;
#if KHEAP_DEBUG
        uint8_t *u = (uint8_t *)(h + 1);
        for (uint32_t i = 0; i < k_cls[c] - K_HDR; i++)
            if (u[i] != K_POISON) { h_err("write after free detected in block", u); break; }
#endif
        h->magic = K_ALLOC; h->cls = (uint32_t)c; h->size = n;
        h_in_use += n; h_allocs++;
        return h + 1;
    }

    uint64_t pages = k_pages(n);
    uint64_t pa = pmm_alloc_n(pages);
    if (!pa) return NULL;
    KHdr *h = (KHdr *)(HHDM_BASE + pa);
    h->magic = K_ALLOC; h->cls = K_LARGE; h->size = n;
    h_in_use += n; h_allocs++; h_large_pages += pages;
    return h + 1;
}

static void *hp_zalloc(size_t n) {
    void *p = hp_malloc(n);
    if (p) memset(p, 0, n);
    return p;
}

static size_t k_capacity(KHdr *h) {
    return h->cls == K_LARGE ? k_pages(h->size) * PAGE_SIZE - K_HDR : k_cls[h->cls] - K_HDR;
}

static void hp_free(void *p) {
    if (!p) return;
    KHdr *h = (KHdr *)p - 1;
    if (((uint64_t)h & 15) || (uint64_t)h < HHDM_BASE) { h_err("bad free (not a heap pointer)", p); return; }
    if (h->magic == K_FREE) { h_err("double free", p); return; }
    if (h->magic != K_ALLOC) { h_err("bad free (header corrupt)", p); return; }

    if (h->cls == K_LARGE) {
        uint64_t pages = k_pages(h->size), pa = (uint64_t)h - HHDM_BASE;
        h_in_use -= h->size;
        h->magic = K_FREE;                                           // catches an immediate second free
        h_large_pages -= pages; h_frees++;
        for (uint64_t i = 0; i < pages; i++) pmm_free(pa + i * PAGE_SIZE);
        return;
    }
    uint32_t c = h->cls;
    if (c >= 7) { h_err("bad free (class corrupt)", p); return; }
    h_in_use -= h->size; h_frees++;
#if KHEAP_DEBUG
    memset(h + 1, K_POISON, k_cls[c] - K_HDR);
#endif
    h->magic = K_FREE; h->size = (uint64_t)k_free[c];
    k_free[c] = h;
}

static void *hp_realloc(void *p, size_t n) {
    if (!p) return hp_malloc(n);
    if (!n) { hp_free(p); return NULL; }
    KHdr *h = (KHdr *)p - 1;
    if (h->magic != K_ALLOC) { h_err("realloc of invalid block", p); return NULL; }
    size_t cur = h->size;                                            // bytes the caller asked for before
    int fits = h->cls == K_LARGE ? (k_pages(n) == k_pages(cur)) : (n <= k_capacity(h));
    if (fits) { h_in_use += n; h_in_use -= cur; h->size = n; return p; }
    void *q = hp_malloc(n);
    if (!q) return NULL;
    memcpy(q, p, cur < n ? cur : n);
    hp_free(p);
    return q;
}

static void heap_stats(void) {
    DbgPrint("heap: %llu bytes in %llu live blocks (%llu allocs, %llu frees), slab pages %llu, large pages %llu, errors %llu\n",
            h_in_use, h_allocs - h_frees, h_allocs, h_frees, h_slab_pages, h_large_pages, h_errors);
}

// ------------------------------------------------------------------ self test
static uint64_t t_rng = 0x2545F4914F6CDD1DULL;
static uint64_t t_rand(void) { t_rng ^= t_rng << 13; t_rng ^= t_rng >> 7; t_rng ^= t_rng << 17; return t_rng; }

#define T_N 1000
static void  *t_ptr[T_N];
static size_t t_len[T_N];

static int t_fill_check(void *p, size_t n, uint32_t seed, int fill) {
    uint8_t *b = p;
    for (size_t j = 0; j < n; j++) {
        uint8_t want = (uint8_t)(seed * 131 + j);
        if (fill) b[j] = want; else if (b[j] != want) return 0;
    }
    return 1;
}

static void heap_selftest(void) {
    uint64_t pmm_start = pmm_free_pages();
    uint64_t err0 = h_errors;

    // 1. assorted sizes: aligned, writable, no overlap with the block header
    static const size_t sizes[] = { 1, 16, 17, 100, 1000, 2000, 2016, 2017, 4096, 100000 };
    int ok = 1;
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        void *p = hp_malloc(sizes[i]);
        if (!p || ((uint64_t)p & 15) || !t_fill_check(p, sizes[i], i, 1) || !t_fill_check(p, sizes[i], i, 0)) ok = 0;
        hp_free(p);
    }
    DbgPrint("heap test: sizes 1..100000 aligned and writable: %s\n", ok ? "ok" : "FAIL");

    // 2. many mixed blocks alive at once, patterns must survive, then free in scrambled order
    int fails = 0;
    for (int i = 0; i < T_N; i++) {
        size_t n = (t_rand() % 20 == 0) ? 1 + t_rand() % 40000 : 1 + t_rand() % 3000;
        t_ptr[i] = hp_malloc(n); t_len[i] = n;
        if (!t_ptr[i]) { fails++; continue; }
        t_fill_check(t_ptr[i], n, (uint32_t)i, 1);
    }
    for (int i = 0; i < T_N; i++) if (t_ptr[i] && !t_fill_check(t_ptr[i], t_len[i], (uint32_t)i, 0)) fails++;
    DbgPrint("heap test: %d mixed blocks alive, %d pattern/alloc failures: %s\n", T_N, fails, fails ? "FAIL" : "ok");
    for (int i = 0; i < T_N; i++) hp_free(t_ptr[(i * 7919) % T_N]);
    DbgPrint("heap test: all freed, in-use bytes %llu, large pages %llu: %s\n", h_in_use, h_large_pages,
            (h_in_use == 0 && h_large_pages == 0) ? "ok" : "FAIL");

    // 3. a 1 MiB block comes from and returns to the page allocator exactly
    uint64_t before = pmm_free_pages();
    void *big = hp_malloc(1 << 20);
    int big_ok = big && t_fill_check(big, 1 << 20, 7, 1) && t_fill_check(big, 1 << 20, 7, 0);
    hp_free(big);
    DbgPrint("heap test: 1 MiB block write/verify: %s, pages returned exactly: %s\n",
            big_ok ? "ok" : "FAIL", pmm_free_pages() == before ? "ok" : "FAIL");

    // 4. hp_zalloc is zeroed even over recycled memory, hp_realloc keeps contents
    uint8_t *z = hp_malloc(200); memset(z, 0xAB, 200); hp_free(z);
    z = hp_zalloc(200); int zero = 1; for (int i = 0; i < 200; i++) if (z[i]) zero = 0;
    hp_free(z);
    uint8_t *r = hp_malloc(20); for (int i = 0; i < 20; i++) r[i] = (uint8_t)(i + 1);
    r = hp_realloc(r, 5000); int keep = r != NULL; for (int i = 0; keep && i < 20; i++) if (r[i] != i + 1) keep = 0;
    r = hp_realloc(r, 10);   for (int i = 0; keep && i < 10; i++) if (r[i] != i + 1) keep = 0;
    hp_free(r);
    DbgPrint("heap test: hp_zalloc zeroed recycled memory: %s, hp_realloc keeps contents (grow+shrink): %s\n",
            zero ? "ok" : "FAIL", keep ? "ok" : "FAIL");

    DbgPrint("heap test: no errors so far: %s\n", h_errors == err0 ? "ok" : "FAIL");

    // 5. the debug checks must actually fire (these produce the "heap:" messages below on purpose)
    DbgPrint("heap test: now provoking errors on purpose...\n");
    uint64_t e = h_errors;
    void *d = hp_malloc(40); hp_free(d); hp_free(d);
    DbgPrint("heap test: double free detected: %s\n", h_errors == e + 1 ? "ok" : "FAIL");
    e = h_errors;
    uint8_t *u = hp_malloc(40); hp_free(u); u[3] = 0x55; uint8_t *u2 = hp_malloc(40);      // same slot comes back (LIFO)
    DbgPrint("heap test: write-after-free detected: %s\n", (h_errors == e + 1 && u2 == u) ? "ok" : "FAIL");
    hp_free(u2);
    e = h_errors;
    hp_free((void *)0x1000);
    DbgPrint("heap test: wild pointer free rejected: %s\n", h_errors == e + 1 ? "ok" : "FAIL");

    // 6. nothing leaked except slab pages the heap keeps cached
    DbgPrint("heap test: pmm pages not returned = %llu (slab cache = %llu): %s\n",
            pmm_start - pmm_free_pages(), h_slab_pages, (pmm_start - pmm_free_pages()) == h_slab_pages ? "ok" : "FAIL");
}


// ---- public, interrupt-safe interface (single CPU: disabling interrupts is the lock) ----
static inline uint64_t KeDisableInterrupts(void) { uint64_t fl; __asm__ volatile("pushfq; cli; popq %0" : "=r"(fl) :: "memory"); return fl; }
static inline void KeRestoreInterrupts(uint64_t fl) { if (fl & 0x200) __asm__ volatile("sti" ::: "memory"); }

static void *kmalloc(size_t n)           { uint64_t f = KeDisableInterrupts(); h_lock_acquire(); void *p = hp_malloc(n);     h_lock_release(); KeRestoreInterrupts(f); return p; }
static void *kzalloc(size_t n)           { uint64_t f = KeDisableInterrupts(); h_lock_acquire(); void *p = hp_zalloc(n);     h_lock_release(); KeRestoreInterrupts(f); return p; }
static void *krealloc(void *p, size_t n) { uint64_t f = KeDisableInterrupts(); h_lock_acquire(); void *q = hp_realloc(p, n); h_lock_release(); KeRestoreInterrupts(f); return q; }
static void  kfree(void *p)              { uint64_t f = KeDisableInterrupts(); h_lock_acquire(); hp_free(p);                 h_lock_release(); KeRestoreInterrupts(f); }
