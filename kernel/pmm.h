#pragma once
// Memory-map dump + physical page allocator (bitmap, 4 KiB pages).
// Conventional memory is handed out from the start.  Boot-services memory is added later by
// pmm_reclaim_boot_services(), which must only run after cr3 points at our own page tables.
#include <stdint.h>
#include "bootinfo.h"
#include "serial.h"

#define PAGE_SIZE 4096ULL

// EFI_MEMORY_DESCRIPTOR (stride is bi->desc_size, not sizeof)
typedef struct {
    uint32_t type, pad;
    uint64_t phys, virt, pages, attr;
} EfiMemDesc;

static const char *const efi_type[15] = {
    "Reserved", "LoaderCode", "LoaderData", "BootServicesCode", "BootServicesData",
    "RuntimeCode", "RuntimeData", "Conventional", "Unusable", "ACPIReclaim",
    "ACPINVS", "MMIO", "MMIOPortSpace", "PalCode", "Persistent",
};
#define EFI_CONVENTIONAL 7

static const EfiMemDesc *md_at(const BootInfo *bi, uint64_t i) {
    return (const EfiMemDesc *)(bi->mmap + i * bi->desc_size);
}

static void mmap_emit(uint64_t s, uint64_t e, uint32_t t) {
    DbgPrint("  %016llx - %016llx  %s (%llu KiB)\n", s, e, t < 15 ? efi_type[t] : "?", (e - s) / 1024);
}

// print the map with adjacent same-type regions merged, then totals
static void mmap_dump(const BootInfo *bi) {
    uint64_t n = bi->mmap_size / bi->desc_size;
    uint64_t tot[16] = {0};
    uint64_t rs = 0, re = 0; uint32_t rt = 0; int open = 0;

    for (uint64_t i = 0; i < n; i++) {
        const EfiMemDesc *d = md_at(bi, i);
        uint64_t s = d->phys, e = s + d->pages * PAGE_SIZE;
        tot[d->type < 15 ? d->type : 15] += d->pages;
        if (open && d->type == rt && s == re) { re = e; continue; }
        if (open) mmap_emit(rs, re, rt);
        rs = s; re = e; rt = d->type; open = 1;
    }
    if (open) mmap_emit(rs, re, rt);

    DbgPrint("  totals: conventional=%llu MiB  boot-services=%llu MiB  loader=%llu MiB  runtime=%llu MiB  acpi=%llu MiB\n",
            tot[7] * 4 / 1024, (tot[3] + tot[4]) * 4 / 1024, (tot[1] + tot[2]) * 4 / 1024,
            (tot[5] + tot[6]) * 4 / 1024, (tot[9] + tot[10]) * 4 / 1024);
}

// ------------------------------------------------------------ allocator
static uint64_t *pm_bits;                 // bit set = page in use
static uint64_t  pm_pages, pm_free, pm_hint;
static volatile uint32_t pm_lock;
static uint64_t pmm_lock(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    while (__atomic_exchange_n(&pm_lock,1,__ATOMIC_ACQUIRE))
        while (__atomic_load_n(&pm_lock,__ATOMIC_RELAXED)) __asm__ volatile("pause");
    return flags;
}
static void pmm_unlock(uint64_t flags) {
    __atomic_store_n(&pm_lock,0,__ATOMIC_RELEASE);
    if (flags & 0x200) __asm__ volatile("sti" ::: "memory");
}

static inline int  bit_get(uint64_t i) { return (pm_bits[i >> 6] >> (i & 63)) & 1; }
static inline void bit_set(uint64_t i) { pm_bits[i >> 6] |=  (1ULL << (i & 63)); }
static inline void bit_clr(uint64_t i) { pm_bits[i >> 6] &= ~(1ULL << (i & 63)); }

static void pmm_panic(const char *why) {
    DbgPrint("pmm: %s\n", why);
    for (;;) __asm__ volatile("cli; hlt");
}

static void pmm_init(const BootInfo *bi) {
    uint64_t n = bi->mmap_size / bi->desc_size, top = 0;

    for (uint64_t i = 0; i < n; i++) {               // highest usable-or-reclaimable address = size of the bitmap's world
        const EfiMemDesc *d = md_at(bi, i);
        uint64_t e = d->phys + d->pages * PAGE_SIZE;
        if ((d->type == EFI_CONVENTIONAL || d->type == 3 || d->type == 4) && e > top) top = e;   // + boot services: reclaimed later
    }
    if (!top) pmm_panic("no conventional memory");

    pm_pages = top / PAGE_SIZE;
    uint64_t words    = (pm_pages + 63) / 64;
    uint64_t bm_pages = (words * 8 + PAGE_SIZE - 1) / PAGE_SIZE;

    uint64_t at = 0;                                 // first conventional region (>= 1 MiB) big enough for the bitmap
    for (uint64_t i = 0; i < n && !at; i++) {
        const EfiMemDesc *d = md_at(bi, i);
        if (d->type != EFI_CONVENTIONAL) continue;
        uint64_t s = d->phys < 0x100000 ? 0x100000 : d->phys, e = d->phys + d->pages * PAGE_SIZE;
        if (e > s && (e - s) / PAGE_SIZE >= bm_pages) at = s;
    }
    if (!at) pmm_panic("no room for bitmap");

    pm_bits = (uint64_t *)at;
    for (uint64_t w = 0; w < words; w++) pm_bits[w] = ~0ULL;      // everything "used" ...

    pm_free = 0;
    for (uint64_t i = 0; i < n; i++) {                            // ... then free the conventional pages above 1 MiB
        const EfiMemDesc *d = md_at(bi, i);
        if (d->type != EFI_CONVENTIONAL) continue;
        for (uint64_t p = 0; p < d->pages; p++) {
            uint64_t a = d->phys + p * PAGE_SIZE;
            if (a < 0x100000) continue;
            bit_clr(a / PAGE_SIZE); pm_free++;
        }
    }
    for (uint64_t p = 0; p < bm_pages; p++) {                     // the bitmap itself is in use
        uint64_t idx = at / PAGE_SIZE + p;
        if (!bit_get(idx)) { bit_set(idx); pm_free--; }
    }
    pm_hint = 0;
    DbgPrint("pmm: bitmap at %p (%llu pages), covers %llu pages (%llu MiB)\n",
            (void *)at, bm_pages, pm_pages, pm_pages * 4 / 1024);
}

static uint64_t pmm_free_pages(void) { return pm_free; }
static uint64_t pmm_total_pages(void) { return pm_pages; }

static int pmm_is_used(uint64_t pa) {
    uint64_t i = pa / PAGE_SIZE;
    return i >= pm_pages ? 1 : bit_get(i);
}

static uint64_t find_run(uint64_t from, uint64_t to, uint64_t n) {
    uint64_t run = 0;
    for (uint64_t i = from; i < to; i++) {
        if ((i & 63) == 0 && pm_bits[i >> 6] == ~0ULL) { run = 0; i += 63; continue; }   // skip a full word
        if (bit_get(i)) { run = 0; continue; }
        if (++run == n) return i + 1 - n;
    }
    return ~0ULL;
}

// n contiguous pages; returns physical address or 0
static uint64_t pmm_alloc_n(uint64_t n) {
    uint64_t pmfl = pmm_lock();
    if (!n || n > pm_free) { pmm_unlock(pmfl); return 0; }
    uint64_t s = find_run(pm_hint, pm_pages, n);
    if (s == ~0ULL) s = find_run(0, pm_pages, n);
    if (s == ~0ULL) { pmm_unlock(pmfl); return 0; }
    for (uint64_t i = 0; i < n; i++) bit_set(s + i);
    pm_free -= n;
    pm_hint = s + n;
    uint64_t result = s * PAGE_SIZE;
    pmm_unlock(pmfl);
    return result;
}

static uint64_t pmm_alloc(void) { return pmm_alloc_n(1); }

static uint64_t pmm_alloc_zeroed(void) {          // reached by physical address (identity map)
    uint64_t pa = pmm_alloc();
    if (pa) { uint64_t *p = (uint64_t *)pa; for (int i = 0; i < 512; i++) p[i] = 0; }
    return pa;
}

static void pmm_free(uint64_t pa) {
    uint64_t pmfl = pmm_lock();
    uint64_t i = pa / PAGE_SIZE;
    if ((pa & (PAGE_SIZE - 1)) || i >= pm_pages) { pmm_unlock(pmfl); DbgPrint("pmm: bad free %p\n", (void *)pa); return; }
    if (!bit_get(i)) { pmm_unlock(pmfl); DbgPrint("pmm: double free %p\n", (void *)pa); return; }
    bit_clr(i); pm_free++;
    if (i < pm_hint) pm_hint = i;
    pmm_unlock(pmfl);
}

static void pmm_selftest(uint64_t kernel_addr, uint64_t stack_addr) {
    uint64_t before = pm_free;
    uint64_t a = pmm_alloc(), b = pmm_alloc(), c = pmm_alloc();
    DbgPrint("pmm test: alloc %p %p %p\n", (void *)a, (void *)b, (void *)c);
    pmm_free(b);
    uint64_t d = pmm_alloc();
    DbgPrint("pmm test: freed middle page, next alloc %s\n", d == b ? "reused it (ok)" : "did NOT reuse it");
    uint64_t r = pmm_alloc_n(16);
    DbgPrint("pmm test: 16 contiguous pages at %p\n", (void *)r);
    DbgPrint("pmm test: kernel image page marked used: %s\n", pmm_is_used(kernel_addr) ? "yes (ok)" : "NO (bug)");
    DbgPrint("pmm test: kernel stack page marked used: %s\n", pmm_is_used(stack_addr) ? "yes (ok)" : "NO (bug)");
    DbgPrint("pmm test: allocated pages are distinct: %s\n",
            (a != c && a != d && c != d && (r + 16 * PAGE_SIZE <= a || r > a)) ? "ok" : "BUG");
    pmm_free(a); pmm_free(c); pmm_free(d);
    for (uint64_t i = 0; i < 16; i++) pmm_free(r + i * PAGE_SIZE);
    DbgPrint("pmm test: no leak after freeing everything: %s\n", pm_free == before ? "ok" : "LEAK");
}

// Boot-services code/data is free once the firmware's page tables are gone.  Call only AFTER switching cr3.
static uint64_t pmm_reclaim_boot_services(const BootInfo *bi) {
    uint64_t n = bi->mmap_size / bi->desc_size, got = 0;
    for (uint64_t i = 0; i < n; i++) {
        const EfiMemDesc *d = md_at(bi, i);
        if (d->type != 3 && d->type != 4) continue;
        for (uint64_t p = 0; p < d->pages; p++) {
            uint64_t a = d->phys + p * PAGE_SIZE;
            if (a < 0x100000 || a / PAGE_SIZE >= pm_pages) continue;
            if (bit_get(a / PAGE_SIZE)) { bit_clr(a / PAGE_SIZE); pm_free++; got++; }
        }
    }
    pm_hint = 0;
    return got;
}

// write an address-derived pattern to every cache line of every free page (via the direct map), then verify
static void pmm_touch_test(uint64_t hhdm) {
    uint64_t bad = 0, touched = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (uint64_t pg = 0; pg < pm_pages; pg++) {
            if (bit_get(pg)) continue;
            volatile uint64_t *q = (volatile uint64_t *)(hhdm + pg * PAGE_SIZE);
            for (int k = 0; k < 512; k += 8) {
                uint64_t want = 0xA5A5000000000000ULL ^ (pg * PAGE_SIZE + k * 8);
                if (pass == 0) { q[k] = want; touched++; }
                else if (q[k] != want && bad++ < 3) DbgPrint("  mismatch at page %p\n", (void *)(pg * PAGE_SIZE));
            }
        }
    }
    DbgPrint("pmm touch test: %llu cache lines written and verified, %llu errors: %s\n",
            touched, bad, bad ? "FAIL" : "ok");
}
