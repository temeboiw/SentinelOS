#pragma once
// Own page tables: identity map (kernel still lives there) + direct map of physical memory
// at HHDM_BASE.  2 MiB pages.  RAM = write-back, MMIO/reserved = uncached, framebuffer = write-through.
#include <stdint.h>
#include "bootinfo.h"
#include "serial.h"
#include "pmm.h"

#define HHDM_BASE 0xFFFF800000000000ULL

#define PTE_P    (1ULL << 0)
#define PTE_RW   (1ULL << 1)
#define PTE_PWT  (1ULL << 3)
#define PTE_PCD  (1ULL << 4)
#define PTE_PS   (1ULL << 7)
#define PTE_ADDR 0x000FFFFFFFFFF000ULL
#define LARGE    (2ULL << 20)

static uint64_t *pml4;
static uint64_t  vmm_table_pages;

static void vmm_panic(const char *why) {
    DbgPrint("vmm: %s\n", why);
    for (;;) __asm__ volatile("cli; hlt");
}

static uint64_t *table_next(uint64_t *tbl, unsigned idx) {
    if (!(tbl[idx] & PTE_P)) {
        uint64_t pa = pmm_alloc_zeroed();
        if (!pa) vmm_panic("out of memory for page tables");
        vmm_table_pages++;
        tbl[idx] = pa | PTE_P | PTE_RW;
    }
    return (uint64_t *)(tbl[idx] & PTE_ADDR);       // tables are reached by physical address (identity map)
}

static void map_2m(uint64_t virt, uint64_t phys, uint64_t attr) {
    uint64_t *pdpt = table_next(pml4, (virt >> 39) & 511);
    uint64_t *pd   = table_next(pdpt, (virt >> 30) & 511);
    unsigned i = (virt >> 21) & 511;
    if (pd[i] & PTE_P) return;                      // first mapping wins
    pd[i] = phys | PTE_P | PTE_RW | PTE_PS | attr;
}

// map physical [s, e) at virt = voff + phys, rounded out to 2 MiB
static void map_range(uint64_t voff, uint64_t s, uint64_t e, uint64_t attr) {
    s &= ~(LARGE - 1);
    e = (e + LARGE - 1) & ~(LARGE - 1);
    for (uint64_t p = s; p < e; p += LARGE) map_2m(voff + p, p, attr);
}

// software page-table walk; returns physical address or ~0 if unmapped
static uint64_t vmm_translate(uint64_t v) {
    uint64_t e = pml4[(v >> 39) & 511];
    if (!(e & PTE_P)) return ~0ULL;
    e = ((uint64_t *)(e & PTE_ADDR))[(v >> 30) & 511];
    if (!(e & PTE_P)) return ~0ULL;
    if (e & PTE_PS) return (e & 0x000FFFFFC0000000ULL) | (v & 0x3FFFFFFFULL);
    e = ((uint64_t *)(e & PTE_ADDR))[(v >> 21) & 511];
    if (!(e & PTE_P)) return ~0ULL;
    if (e & PTE_PS) return (e & 0x000FFFFFFFE00000ULL) | (v & 0x1FFFFFULL);
    e = ((uint64_t *)(e & PTE_ADDR))[(v >> 12) & 511];
    if (!(e & PTE_P)) return ~0ULL;
    return (e & PTE_ADDR) | (v & 0xFFFULL);
}

static void vmm_init(const BootInfo *bi) {
    uint64_t free_before = pmm_free_pages();
    pml4 = (uint64_t *)pmm_alloc_zeroed();
    if (!pml4) vmm_panic("no page for PML4");
    vmm_table_pages = 1;

    uint64_t n = bi->mmap_size / bi->desc_size;
    for (uint64_t i = 0; i < n; i++) {
        const EfiMemDesc *d = md_at(bi, i);
        uint64_t s = d->phys, e = s + d->pages * PAGE_SIZE, attr;
        switch (d->type) {
        case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 9: case 10: case 14:
            attr = 0; break;                                    // RAM-like: write-back
        case 11: case 12:
            attr = PTE_PCD | PTE_PWT; break;                    // MMIO: uncached
        case 0:
            if (s >= (4ULL << 30)) continue;                    // skip huge reserved windows above 4 GiB
            attr = PTE_PCD | PTE_PWT; break;
        default:
            continue;                                           // unusable, PAL code
        }
        map_range(0, s, e, attr);
        map_range(HHDM_BASE, s, e, attr);
    }

    // not in the EFI map: framebuffer (write-through) and the APIC / HPET window (uncached)
    uint64_t fb = bi->fb_base, fbe = fb + (uint64_t)bi->pitch * bi->height;
    map_range(0, fb, fbe, PTE_PWT);
    map_range(HHDM_BASE, fb, fbe, PTE_PWT);
    map_range(0, 0xFEC00000ULL, 0xFF000000ULL, PTE_PCD | PTE_PWT);
    map_range(HHDM_BASE, 0xFEC00000ULL, 0xFF000000ULL, PTE_PCD | PTE_PWT);

    uint64_t old_cr3;
    __asm__ volatile("movq %%cr3, %0" : "=r"(old_cr3));
    DbgPrint("vmm: pml4 at %p, %llu page-table pages (%llu KiB); cr3 %p -> %p\n",
            (void *)pml4, vmm_table_pages, vmm_table_pages * 4, (void *)old_cr3, (void *)pml4);
    (void)free_before;

    __asm__ volatile("movq %0, %%cr3" :: "r"((uint64_t)pml4) : "memory");
    DbgPrint("vmm: running on our own page tables\n");
}

static void vmm_selftest(const BootInfo *bi, uint64_t code_addr, uint64_t stack_addr) {
    uint64_t pa = pmm_alloc();
    volatile uint64_t *lo = (volatile uint64_t *)pa;
    volatile uint64_t *hi = (volatile uint64_t *)(HHDM_BASE + pa);

    *lo = 0x1122334455667788ULL;
    DbgPrint("vmm test: write via identity, read via direct map: %s\n", *hi == 0x1122334455667788ULL ? "ok" : "FAIL");
    *hi = 0xAABBCCDDEEFF0011ULL;
    DbgPrint("vmm test: write via direct map, read via identity: %s\n", *lo == 0xAABBCCDDEEFF0011ULL ? "ok" : "FAIL");
    DbgPrint("vmm test: translate(direct map + page) == page: %s\n", vmm_translate(HHDM_BASE + pa) == pa ? "ok" : "FAIL");
    DbgPrint("vmm test: kernel code identity-mapped: %s\n", vmm_translate(code_addr) == code_addr ? "ok" : "FAIL");
    DbgPrint("vmm test: kernel stack identity-mapped: %s\n", vmm_translate(stack_addr) == stack_addr ? "ok" : "FAIL");
    DbgPrint("vmm test: framebuffer mapped (both aliases): %s\n",
            (vmm_translate(bi->fb_base) == bi->fb_base && vmm_translate(HHDM_BASE + bi->fb_base) == bi->fb_base) ? "ok" : "FAIL");
    DbgPrint("vmm test: unmapped address is really unmapped: %s\n",
            vmm_translate(0x00007F0000000000ULL) == ~0ULL ? "ok" : "FAIL");
    pmm_free(pa);
}
