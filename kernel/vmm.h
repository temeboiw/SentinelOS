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
#define PTE_U    (1ULL << 2)          // user-accessible
#define PTE_PWT  (1ULL << 3)
#define PTE_PCD  (1ULL << 4)
#define PTE_PS   (1ULL << 7)
#define PTE_ADDR 0x000FFFFFFFFFF000ULL
#define LARGE    (2ULL << 20)

// Canonical user virtual range we use for the demo process
#define USER_CODE_BASE  0x0000000040000000ULL   // 1 GiB – above typical RAM identity map
#define USER_STACK_TOP  0x00007FFFFFFFE000ULL   // near top of lower half

#define KERNEL_VA_BASE   0xFFFFFFFF80200000ULL
#define HAL_VA_BASE      0xFFFFFFFF81000000ULL


#define PHYS_TO_VIRT(pa) ((uint64_t *)(HHDM_BASE + (uint64_t)(pa)))

typedef struct _KPROCESS {
    uint64_t  DirectoryTableBase;
    uint64_t *Pml4;
    ULONG     Pid;
    char      ImageName[16];
} KPROCESS;

static uint64_t *pml4;
static uint64_t  vmm_table_pages;
static KPROCESS *PspSystemProcess;
static KPROCESS *MmCurrentProcess;
static ULONG     MmNextPid = 1;

static void vmm_panic(const char *why) {
    DbgPrint("vmm: %s\n", why);
    for (;;) __asm__ volatile("cli; hlt");
}

static uint64_t *table_next(uint64_t *tbl, unsigned idx) {
    if (!(tbl[idx] & PTE_P)) {
        uint64_t pa = pmm_alloc_zeroed();
        if (!pa) vmm_panic("out of memory for page tables");
        vmm_table_pages++;
        tbl[idx] = pa | PTE_P | PTE_RW | PTE_U;
    }
    uint64_t pa = tbl[idx] & PTE_ADDR;
    if (MmCurrentProcess && MmCurrentProcess != PspSystemProcess)
        return PHYS_TO_VIRT(pa);
    return (uint64_t *)pa;
}

static void map_2m(uint64_t virt, uint64_t phys, uint64_t attr) {
    uint64_t *pdpt = table_next(pml4, (virt >> 39) & 511);
    uint64_t *pd   = table_next(pdpt, (virt >> 30) & 511);
    unsigned i = (virt >> 21) & 511;
    if (pd[i] & PTE_P) return;                      // first mapping wins
    pd[i] = phys | PTE_P | PTE_RW | PTE_PS | attr;
}

// 4 KiB mapping into a specific address space.
static uint64_t *pte_page_va(uint64_t pa) {
    if (!MmCurrentProcess || MmCurrentProcess == PspSystemProcess)
        return (uint64_t *)pa;
    return PHYS_TO_VIRT(pa);
}

static void map_4k_in(uint64_t *root, uint64_t virt, uint64_t phys, uint64_t attr) {
    uint64_t *pdpt = table_next(root, (virt >> 39) & 511);
    uint64_t *pd   = table_next(pdpt, (virt >> 30) & 511);
    uint64_t *pt;
    unsigned pdi = (virt >> 21) & 511;
    if (pd[pdi] & PTE_P) {
        if (pd[pdi] & PTE_PS) vmm_panic("map_4k_in: 2 MiB page already present");
        pt = pte_page_va(pd[pdi] & PTE_ADDR);
    } else {
        uint64_t pa = pmm_alloc_zeroed();
        if (!pa) vmm_panic("out of memory for PT");
        vmm_table_pages++;
        pd[pdi] = pa | PTE_P | PTE_RW | (attr & PTE_U);
        pt = pte_page_va(pa);
    }
    if (attr & PTE_U) {
        root[(virt >> 39) & 511] |= PTE_U;
        pdpt[(virt >> 30) & 511] |= PTE_U;
        pd[pdi] |= PTE_U;
    }
    unsigned pti = (virt >> 12) & 511;
    pt[pti] = (phys & PTE_ADDR) | PTE_P | attr;
}

static void map_4k(uint64_t virt, uint64_t phys, uint64_t attr) {
    map_4k_in(pml4, virt, phys, attr);
}

static uint64_t MmMapUserPage(KPROCESS *proc, uint64_t virt, int writable) {
    uint64_t pa = pmm_alloc_zeroed();
    if (!pa) vmm_panic("MmMapUserPage: OOM");
    map_4k_in(proc->Pml4, virt, pa, PTE_U | (writable ? PTE_RW : 0));
    return pa;
}

static uint64_t vmm_map_user_page(uint64_t virt, int writable) {
    return MmMapUserPage(PspSystemProcess ? PspSystemProcess : MmCurrentProcess, virt, writable);
}

static void MmSwitchToProcess(KPROCESS *p) {
    if (!p || p == MmCurrentProcess) return;
    uint64_t cr3 = p->DirectoryTableBase & ~0xfffULL;
    MmCurrentProcess = p;
    __asm__ volatile("cli");
    __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

static void MmInitSystemProcess(void) {
    static KPROCESS sys;
    uint64_t live;
    __asm__ volatile("mov %%cr3, %0" : "=r"(live));
    sys.DirectoryTableBase = live & ~0xfffULL;
    sys.Pml4 = pml4;
    sys.Pid = 0;
    sys.ImageName[0]='S'; sys.ImageName[1]='y'; sys.ImageName[2]='s';
    sys.ImageName[3]='t'; sys.ImageName[4]='e'; sys.ImageName[5]='m';
    PspSystemProcess = &sys;
    MmCurrentProcess = &sys;
    DbgPrint("mm: System process cr3=%p pml4=%p\n",
             (void *)sys.DirectoryTableBase, (void *)pml4);
}

#define MM_MAX_PROCESSES 16
static KPROCESS MmProcessPool[MM_MAX_PROCESSES];
static int MmProcessUsed[MM_MAX_PROCESSES];


static void map_2m_in(uint64_t *root, uint64_t virt, uint64_t phys, uint64_t attr) {
    uint64_t *pdpt, *pd;
    int i4 = (virt >> 39) & 511, i3 = (virt >> 30) & 511, i2 = (virt >> 21) & 511;
    if (!(root[i4] & PTE_P)) {
        uint64_t x = pmm_alloc_zeroed();
        root[i4] = x | PTE_P | PTE_RW;
    }
    pdpt = pte_page_va(root[i4] & PTE_ADDR);
    if (!(pdpt[i3] & PTE_P)) {
        uint64_t x = pmm_alloc_zeroed();
        pdpt[i3] = x | PTE_P | PTE_RW;
    }
    pd = pte_page_va(pdpt[i3] & PTE_ADDR);
    if (!(pd[i2] & PTE_P))
        pd[i2] = (phys & ~0x1FFFFFULL) | PTE_P | PTE_RW | PTE_PS | attr;
}

static KPROCESS *MmCreateAddressSpace(const char *name) {
    KPROCESS *p = NULL;
    for (int i = 0; i < MM_MAX_PROCESSES; i++)
        if (!MmProcessUsed[i]) { MmProcessUsed[i]=1; p=&MmProcessPool[i]; break; }
    if (!p) return NULL;
    for (int i = 0; i < (int)sizeof(KPROCESS); i++) ((char*)p)[i]=0;
    uint64_t pml4_pa = pmm_alloc_zeroed();
    if (!pml4_pa) {
        for (int i=0;i<MM_MAX_PROCESSES;i++) if (p==&MmProcessPool[i]) MmProcessUsed[i]=0;
        return NULL;
    }
    uint64_t *np = (uint64_t *)pml4_pa;  /* System CR3: identity */
    for (int i = 256; i < 512; i++) np[i] = PspSystemProcess->Pml4[i];
    /* APIC/IOAPIC so HAL works under process CR3 */
    for (uint64_t pa = 0xFEC00000ULL; pa < 0x100000000ULL; pa += 0x200000ULL)
        map_2m_in(np, pa, pa, PTE_PCD | PTE_PWT);

    p->DirectoryTableBase = pml4_pa;
    p->Pml4 = np;
    p->Pid = MmNextPid++;
    if (name) for (int i=0;i<15&&name[i];i++) p->ImageName[i]=name[i];
    DbgPrint("mm: created process '%s' pid=%lu cr3=%p\n",
             p->ImageName, (unsigned long)p->Pid, (void*)pml4_pa);
    return p;
}

static void MmDeleteAddressSpace(KPROCESS *p) {
    if (!p || p == PspSystemProcess) return;
    pmm_free(p->DirectoryTableBase);
    for (int i=0;i<MM_MAX_PROCESSES;i++)
        if (p==&MmProcessPool[i]) { MmProcessUsed[i]=0; break; }
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
            attr = 0; break;                                        // RAM: write-back
        case 11: case 12:
            attr = PTE_PCD | PTE_PWT; break;                        // MMIO: uncached
        case 0:
            if (s >= (4ULL << 30)) continue;                        // skip huge reserved windows above 4 GiB
            attr = PTE_PCD | PTE_PWT; break;
        default:
            continue;                                               // unusable, PAL code
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

    /* Higher-half module mappings */
    for (uint32_t i = 0; i < bi->module_count; i++) {
        uint64_t virt = bi->modules[i].base;
        uint64_t phys = bi->modules[i].phys;
        uint64_t size = bi->modules[i].size;
        if (!virt || !phys || !size) continue;
        if (virt < 0xFFFF800000000000ULL) continue;
        for (uint64_t off = 0; off < size; off += 0x1000)
            map_4k(virt + off, phys + off, PTE_RW);
        DbgPrint("vmm: mapped module '%s' %p -> %p (%llu KiB)\n",
                 bi->modules[i].name, (void *)phys, (void *)virt, size / 1024);
    }

    __asm__ volatile("movq %0, %%cr3" :: "r"((uint64_t)pml4) : "memory");
    DbgPrint("vmm: running on our own page tables (higher-half)\n");
    MmInitSystemProcess();
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
    DbgPrint("vmm test: kernel code higher-half mapped: %s\n", vmm_translate(code_addr) != ~0ULL ? "ok" : "FAIL");
    DbgPrint("vmm test: kernel stack higher-half mapped: %s\n", vmm_translate(stack_addr) != ~0ULL ? "ok" : "FAIL");
    DbgPrint("vmm test: framebuffer mapped (both aliases): %s\n",
            (vmm_translate(bi->fb_base) == bi->fb_base && vmm_translate(HHDM_BASE + bi->fb_base) == bi->fb_base) ? "ok" : "FAIL");
    DbgPrint("vmm test: unmapped address is really unmapped: %s\n",
            vmm_translate(0x00007F0000000000ULL) == ~0ULL ? "ok" : "FAIL");
    pmm_free(pa);
}
