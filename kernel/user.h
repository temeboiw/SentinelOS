#pragma once
#include <stdint.h>
#include "serial.h"
#include "cpu.h"
#include "vmm.h"
#include "pmm.h"
#include "heap.h"
#include "bootinfo.h"
#include "ps2.h"
#include "ext4k.h"
#include "virtio_input.h"
#include "uthread.h"
#include "wm.h"

#define SYS_NtTerminateProcess      0
#define SYS_NtWriteFile             1
#define SYS_NtGetCurrentProcessId   2
#define SYS_NtAllocateVirtualMemory 3
#define SYS_NtCreateProcess         4
#define SYS_NtOpenFile              5
#define SYS_NtReadFile              6
#define SYS_NtQueryDirectoryFile    7
#define SYS_NtMapFramebuffer        8
#define SYS_NtGetProcAddress        9
#define SYS_NtGetInput              10
#define SYS_NtShutdown              11
#define SYS_NtSetCurrentDirectory   12
#define SYS_NtCreateThread          13
#define SYS_NtWaitForSingleObject   14
#define SYS_WmRegister              15
#define SYS_WmSetMin                16
#define SYS_WmUnregister            17
#define SYS_WmQuery                 18
#define SYS_WmRestore               19
#define SYS_WmSetRect              20
#define SYS_NtYield               21

static uint8_t user_kstack[16 * 1024] __attribute__((aligned(16)));
static uint64_t user_saved_ksp, user_saved_rip;
static BootInfo *user_bootinfo;
static KPROCESS *user_pending;
static uint64_t user_pending_entry;

#define UP_MAX 8
typedef struct {
    KPROCESS *proc;
    uint64_t entry;
    uint64_t rip, rsp, rflags, rax;
    int alive;
    int fresh;
    int parent;
    uint64_t gpr[14];   /* r15 r14 r13 r12 r11 r10 r9 r8 rbp rdi rsi rdx rcx rbx */
} UserProc;
static UserProc up_tab[UP_MAX];
static int up_n;
static int up_cur = -1;

/* A process switch must carry the whole register file, not just RIP/RSP. */
static void up_snap(Frame *f, UserProc *u) {
    u->gpr[0]  = f->r15; u->gpr[1]  = f->r14; u->gpr[2]  = f->r13;
    u->gpr[3]  = f->r12; u->gpr[4]  = f->r11; u->gpr[5]  = f->r10;
    u->gpr[6]  = f->r9;  u->gpr[7]  = f->r8;  u->gpr[8]  = f->rbp;
    u->gpr[9]  = f->rdi; u->gpr[10] = f->rsi; u->gpr[11] = f->rdx;
    u->gpr[12] = f->rcx; u->gpr[13] = f->rbx;
}
static void up_restore_gpr(Frame *f, const UserProc *u) {
    f->r15 = u->gpr[0];  f->r14 = u->gpr[1];  f->r13 = u->gpr[2];
    f->r12 = u->gpr[3];  f->r11 = u->gpr[4];  f->r10 = u->gpr[5];
    f->r9  = u->gpr[6];  f->r8  = u->gpr[7];  f->rbp = u->gpr[8];
    f->rdi = u->gpr[9];  f->rsi = u->gpr[10]; f->rdx = u->gpr[11];
    f->rcx = u->gpr[12]; f->rbx = u->gpr[13];
}
static void up_clear_gpr(Frame *f) {
    f->r15 = f->r14 = f->r13 = f->r12 = f->r11 = f->r10 = 0;
    f->r9 = f->r8 = f->rbp = f->rdi = f->rsi = f->rdx = f->rcx = f->rbx = 0;
}

static void up_save(Frame *f, int idx) {
    if (idx < 0 || idx >= UP_MAX || !up_tab[idx].alive) return;
    up_tab[idx].rip = f->rip;
    up_tab[idx].rsp = f->rsp;
    up_tab[idx].rflags = f->rflags;
    up_snap(f, &up_tab[idx]);
    /* keep rax - syscall return value must survive GetInput RR */
}
static void up_save_full(Frame *f, int idx) {
    if (idx < 0 || idx >= UP_MAX || !up_tab[idx].alive) return;
    up_tab[idx].rip = f->rip;
    up_tab[idx].rsp = f->rsp;
    up_tab[idx].rflags = f->rflags;
    up_tab[idx].rax = f->rax;
    up_snap(f, &up_tab[idx]);
}
static void up_load(Frame *f, int idx) {
    UserProc *u = &up_tab[idx];
    if (!u->alive || !u->proc) return;
    if (ki_current) ki_current->Process = u->proc;
    MmSwitchToProcess(u->proc);
    f->cs = USER_CS;
    f->ss = USER_DS;
    if (u->fresh || u->rip < 0x10000) {
        f->rip = u->entry ? u->entry : u->rip;
        f->rsp = USER_STACK_TOP - 8; /* Win64 entry RSP is 8 mod 16 */
        f->rflags = 0x202;
        f->rax = 0;
        up_clear_gpr(f);
        u->fresh = 0;
    } else {
        f->rip = u->rip;
        f->rsp = u->rsp;
        f->rflags = u->rflags ? u->rflags : 0x202;
        f->rax = u->rax;
        up_restore_gpr(f, u);
    }
    up_cur = idx;
}
static int up_find_proc(KPROCESS *p) {
    for (int i = 0; i < UP_MAX; i++)
        if (up_tab[i].alive && up_tab[i].proc == p) return i;
    return -1;
}
static int up_pick_next(void) {
    int start = (up_cur < 0) ? 0 : up_cur;
    for (int k = 1; k <= UP_MAX; k++) {
        int i = (start + k) % UP_MAX;
        if (!up_tab[i].alive || !up_tab[i].proc) continue;

        /* Keep a parent suspended while one of its children is alive. */
        int has_live_child = 0;
        for (int j = 0; j < UP_MAX; j++) {
            if (up_tab[j].alive && up_tab[j].proc &&
                up_tab[j].parent == i) {
                has_live_child = 1;
                break;
            }
        }
        if (!has_live_child) return i;
    }
    return -1;
}

static uint64_t user_fb_base;
static uint32_t user_fb_w, user_fb_h, user_fb_pitch;
static int fb_map_logged;

/*
 * PE32+ image loader v2.
 *
 * BootModule describes a file-layout PE image in physical memory. Each process
 * receives a private, image-layout copy, so relocations and IAT writes never
 * mutate the boot module or another process's image.
 */
#define PE_MAX_INSTANCES (MM_MAX_PROCESSES * BOOT_MAX_MODULES)
#define PE_MAX_IMAGE_SIZE (64ULL * 1024 * 1024)
#define PE_DOS_HEADER_SIZE 0x40u
#define PE_NT_HEADER_SIZE 24u
#define PE_OPT64_MIN_SIZE 240u
#define PE_SECTION_SIZE 40u
#define PE_DIRECTORY_BASE 112u
#define PE_DIR_EXPORT 0u
#define PE_DIR_IMPORT 1u
#define PE_DIR_BASERELOC 5u

typedef struct {
    uint32_t pe_off, section_off, section_count, opt_size;
    uint32_t image_size, headers_size, entry_rva;
    uint64_t preferred_base;
    uint32_t export_rva, export_size;
    uint32_t import_rva, import_size;
    uint32_t reloc_rva, reloc_size;
} PEHeaderView;

typedef struct {
    KPROCESS *proc;
    BootModule *source;
    uint64_t image_phys, base, entry, alloc_size;
    uint32_t image_size;
    uint8_t state; /* 0 free, 1 mapped, 2 resolving, 3 resolved, 4 failed */
    uint8_t used;
} PEImageInstance;

static PEImageInstance pe_instances[PE_MAX_INSTANCES];

static int pe_span(uint64_t total, uint64_t off, uint64_t len) {
    return off <= total && len <= total - off;
}

static int pe_read_headers(BootModule *mod, PEHeaderView *v) {
    if (!mod || !mod->phys || !mod->size || mod->size < PE_DOS_HEADER_SIZE || !v)
        return -1;
    const uint8_t *file = (const uint8_t *)(HHDM_BASE + mod->phys);
    if (file[0] != 'M' || file[1] != 'Z') return -1;
    uint32_t peoff = *(const uint32_t *)(file + 0x3C);
    if (!pe_span(mod->size, peoff, PE_NT_HEADER_SIZE)) return -1;
    if (file[peoff] != 'P' || file[peoff + 1] != 'E' ||
        file[peoff + 2] != 0 || file[peoff + 3] != 0) return -1;

    const uint8_t *coff = file + peoff + 4;
    uint16_t machine = *(const uint16_t *)(coff + 0);
    uint16_t sections = *(const uint16_t *)(coff + 2);
    uint16_t opt_size = *(const uint16_t *)(coff + 16);
    if (machine != 0x8664 || !sections || sections > 96 ||
        opt_size < PE_OPT64_MIN_SIZE) return -1;
    uint64_t optoff = (uint64_t)peoff + 24;
    if (!pe_span(mod->size, optoff, opt_size)) return -1;
    const uint8_t *opt = file + optoff;
    if (*(const uint16_t *)opt != 0x20B) return -1;

    uint32_t image_size = *(const uint32_t *)(opt + 56);
    uint32_t headers_size = *(const uint32_t *)(opt + 60);
    uint32_t entry_rva = *(const uint32_t *)(opt + 16);
    uint64_t preferred = *(const uint64_t *)(opt + 24);
    uint32_t dirs = *(const uint32_t *)(opt + 108);
    if (!image_size || image_size > PE_MAX_IMAGE_SIZE ||
        !headers_size || headers_size > image_size ||
        headers_size > mod->size || !entry_rva || entry_rva >= image_size ||
        dirs < 6 || opt_size < PE_DIRECTORY_BASE + 6 * 8) return -1;

    uint64_t section_off = optoff + opt_size;
    uint64_t section_bytes = (uint64_t)sections * PE_SECTION_SIZE;
    if (section_off > UINT32_MAX ||
        !pe_span(mod->size, section_off, section_bytes)) return -1;

    v->pe_off = peoff;
    v->section_off = (uint32_t)section_off;
    v->section_count = sections;
    v->opt_size = opt_size;
    v->image_size = image_size;
    v->headers_size = headers_size;
    v->entry_rva = entry_rva;
    v->preferred_base = preferred;
    v->export_rva = *(const uint32_t *)(opt + PE_DIRECTORY_BASE + PE_DIR_EXPORT * 8);
    v->export_size = *(const uint32_t *)(opt + PE_DIRECTORY_BASE + PE_DIR_EXPORT * 8 + 4);
    v->import_rva = *(const uint32_t *)(opt + PE_DIRECTORY_BASE + PE_DIR_IMPORT * 8);
    v->import_size = *(const uint32_t *)(opt + PE_DIRECTORY_BASE + PE_DIR_IMPORT * 8 + 4);
    v->reloc_rva = *(const uint32_t *)(opt + PE_DIRECTORY_BASE + PE_DIR_BASERELOC * 8);
    v->reloc_size = *(const uint32_t *)(opt + PE_DIRECTORY_BASE + PE_DIR_BASERELOC * 8 + 4);

    for (uint32_t i = 0; i < sections; i++) {
        const uint8_t *sh = file + section_off + (uint64_t)i * PE_SECTION_SIZE;
        uint32_t virtual_size = *(const uint32_t *)(sh + 8);
        uint32_t virtual_address = *(const uint32_t *)(sh + 12);
        uint32_t raw_size = *(const uint32_t *)(sh + 16);
        uint32_t raw_off = *(const uint32_t *)(sh + 20);
        uint32_t mapped = virtual_size > raw_size ? virtual_size : raw_size;
        if (mapped && !pe_span(image_size, virtual_address, mapped)) return -1;
        if (raw_size && !pe_span(mod->size, raw_off, raw_size)) return -1;
    }
    if ((v->export_rva || v->export_size) &&
        (!v->export_rva || !v->export_size ||
         !pe_span(image_size, v->export_rva, v->export_size))) return -1;
    if ((v->import_rva || v->import_size) &&
        (!v->import_rva || !v->import_size ||
         !pe_span(image_size, v->import_rva, v->import_size))) return -1;
    if ((v->reloc_rva || v->reloc_size) &&
        (!v->reloc_rva || !v->reloc_size ||
         !pe_span(image_size, v->reloc_rva, v->reloc_size))) return -1;
    return 0;
}

static BootModule *PspFindModule(BootInfo *bi, const char *name) {
    if (!bi || !name) return 0;
    for (uint32_t i = 0; i < bi->module_count && i < BOOT_MAX_MODULES; i++) {
        const char *n = bi->modules[i].name;
        int match = 1;
        uint32_t k = 0;
        for (; k < sizeof bi->modules[i].name && name[k] && n[k]; k++) {
            char a = name[k], b = n[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) { match = 0; break; }
        }
        if (!match || k >= sizeof bi->modules[i].name || name[k] || n[k]) continue;
        return &bi->modules[i];
    }
    return 0;
}

static PEImageInstance *pe_find_instance(KPROCESS *proc, BootModule *mod) {
    for (uint32_t i = 0; i < PE_MAX_INSTANCES; i++)
        if (pe_instances[i].used && pe_instances[i].proc == proc &&
            pe_instances[i].source == mod) return &pe_instances[i];
    return 0;
}

static int pe_overlap(uint64_t a, uint64_t as, uint64_t b, uint64_t bs) {
    return a < b + bs && b < a + as;
}

static int pe_base_available(KPROCESS *proc, uint64_t base, uint64_t size) {
    if (!base || base >= 0x0000800000000000ULL ||
        size > 0x0000800000000000ULL - base) return 0;
    for (uint32_t i = 0; i < PE_MAX_INSTANCES; i++) {
        PEImageInstance *x = &pe_instances[i];
        if (x->used && x->proc == proc &&
            pe_overlap(base, size, x->base, x->alloc_size)) return 0;
    }
    return 1;
}

static int pe_apply_relocs(uint8_t *image, uint32_t image_size,
                           uint32_t rva, uint32_t size, int64_t delta) {
    if (!delta) return 0;
    if (!rva || !size || !pe_span(image_size, rva, size)) return -1;
    uint32_t off = rva, end = rva + size;
    while (off < end) {
        if (!pe_span(end, off, 8)) return -1;
        uint32_t page_rva = *(uint32_t *)(image + off);
        uint32_t block_size = *(uint32_t *)(image + off + 4);
        if (block_size < 8 || block_size > end - off ||
            ((block_size - 8) & 1)) return -1;
        uint32_t count = (block_size - 8) / 2;
        for (uint32_t i = 0; i < count; i++) {
            uint16_t e = *(uint16_t *)(image + off + 8 + i * 2);
            uint16_t type = e >> 12;
            uint64_t target64 = (uint64_t)page_rva + (e & 0x0FFF);
            if (target64 > UINT32_MAX) return -1;
            uint32_t target = (uint32_t)target64;
            if (type == 0) continue; /* IMAGE_REL_BASED_ABSOLUTE */
            if (type == 10) {
                if (!pe_span(image_size, target, 8)) return -1;
                *(uint64_t *)(image + target) += (uint64_t)delta;
            } else if (type == 3) {
                if (!pe_span(image_size, target, 4)) return -1;
                *(uint32_t *)(image + target) += (uint32_t)delta;
            } else {
                return -1; /* unknown relocation type */
            }
        }
        off += block_size;
    }
    return off == end ? 0 : -1;
}

static int PspMapUserImage(KPROCESS *proc, BootModule *mod) {
    if (!proc || !mod) return -1;
    if (pe_find_instance(proc, mod)) return 0;

    PEHeaderView h;
    if (pe_read_headers(mod, &h) != 0) {
        DbgPrint("pe: rejected invalid/unsupported PE32+ image '%s'\n", mod->name);
        return -1;
    }

    uint64_t alloc_size = ((uint64_t)h.image_size + 0xFFFULL) & ~0xFFFULL;
    uint64_t base = h.preferred_base;
    if (!pe_base_available(proc, base, alloc_size)) {
        if (!h.reloc_rva || !h.reloc_size) {
            DbgPrint("pe: preferred base collision and no relocations: %s\n", mod->name);
            return -1;
        }
        base = 0x50000000ULL;
        while (!pe_base_available(proc, base, alloc_size)) {
            if (base > 0x00007F0000000000ULL - 0x04000000ULL) return -1;
            base += 0x04000000ULL;
        }
    }

    uint64_t pa = pmm_alloc_n(alloc_size / PAGE_SIZE);
    if (!pa) {
        DbgPrint("pe: cannot allocate %llu bytes for %s\n", alloc_size, mod->name);
        return -1;
    }
    uint8_t *image = (uint8_t *)(HHDM_BASE + pa);
    const uint8_t *file = (const uint8_t *)(HHDM_BASE + mod->phys);
    for (uint64_t i = 0; i < alloc_size; i++) image[i] = 0;
    for (uint32_t i = 0; i < h.headers_size; i++) image[i] = file[i];

    for (uint32_t i = 0; i < h.section_count; i++) {
        const uint8_t *sh = file + h.section_off + (uint64_t)i * PE_SECTION_SIZE;
        uint32_t va = *(const uint32_t *)(sh + 12);
        uint32_t raw_size = *(const uint32_t *)(sh + 16);
        uint32_t raw_off = *(const uint32_t *)(sh + 20);
        for (uint32_t j = 0; j < raw_size; j++) image[va + j] = file[raw_off + j];
    }

    int64_t delta = (int64_t)base - (int64_t)h.preferred_base;
    if (pe_apply_relocs(image, h.image_size, h.reloc_rva, h.reloc_size, delta) != 0) {
        DbgPrint("pe: relocation failure in %s\n", mod->name);
        for (uint64_t i = 0; i < alloc_size / PAGE_SIZE; i++) pmm_free(pa + i * PAGE_SIZE);
        return -1;
    }

    PEImageInstance *slot = 0;
    for (uint32_t i = 0; i < PE_MAX_INSTANCES; i++)
        if (!pe_instances[i].used) { slot = &pe_instances[i]; break; }
    if (!slot) {
        for (uint64_t i = 0; i < alloc_size / PAGE_SIZE; i++) pmm_free(pa + i * PAGE_SIZE);
        return -1;
    }

    for (uint64_t off = 0; off < alloc_size; off += PAGE_SIZE)
        map_4k_in(proc->Pml4, base + off, pa + off, PTE_U | PTE_RW);

    slot->proc = proc;
    slot->source = mod;
    slot->image_phys = pa;
    slot->base = base;
    slot->entry = base + h.entry_rva;
    slot->alloc_size = alloc_size;
    slot->image_size = h.image_size;
    slot->state = 1;
    slot->used = 1;
    DbgPrint("pe: mapped private %s base=%p size=%u entry=%p\n",
             mod->name, (void *)base, h.image_size, (void *)slot->entry);
    return 0;
}

static int pe_image_cstr(PEImageInstance *im, uint32_t rva, const char **out) {
    if (!im || !out || rva >= im->image_size) return -1;
    const char *s = (const char *)(HHDM_BASE + im->image_phys + rva);
    uint32_t n = rva;
    while (n < im->image_size && s[n - rva]) n++;
    if (n == im->image_size) return -1;
    *out = s;
    return 0;
}

static uint64_t pe_get_export(KPROCESS *proc, BootModule *mod,
                              const char *func, uint16_t ordinal, int by_ordinal) {
    PEImageInstance *im = pe_find_instance(proc, mod);
    if (!im) return 0;
    uint8_t *image = (uint8_t *)(HHDM_BASE + im->image_phys);
    PEHeaderView h;
    if (pe_read_headers(mod, &h) != 0 || !h.export_rva ||
        !pe_span(im->image_size, h.export_rva, 40)) return 0;
    uint8_t *exp = image + h.export_rva;
    uint32_t ordinal_base = *(uint32_t *)(exp + 16);
    uint32_t nfunc = *(uint32_t *)(exp + 20);
    uint32_t nnames = *(uint32_t *)(exp + 24);
    uint32_t funcs = *(uint32_t *)(exp + 28);
    uint32_t names = *(uint32_t *)(exp + 32);
    uint32_t ords = *(uint32_t *)(exp + 36);
    uint32_t index = 0;
    if (by_ordinal) {
        if (ordinal < ordinal_base || (uint32_t)ordinal - ordinal_base >= nfunc) return 0;
        index = (uint32_t)ordinal - ordinal_base;
    } else {
        if (!func || !pe_span(im->image_size, names, (uint64_t)nnames * 4) ||
            !pe_span(im->image_size, ords, (uint64_t)nnames * 2)) return 0;
        int found = 0;
        for (uint32_t i = 0; i < nnames; i++) {
            uint32_t nrva = *(uint32_t *)(image + names + i * 4);
            const char *n;
            if (pe_image_cstr(im, nrva, &n) != 0) return 0;
            uint32_t k = 0;
            while (func[k] && n[k] && func[k] == n[k]) k++;
            if (func[k] || n[k]) continue;
            index = *(uint16_t *)(image + ords + i * 2);
            found = 1;
            break;
        }
        if (!found) return 0;
    }
    if (index >= nfunc || !pe_span(im->image_size, funcs, (uint64_t)nfunc * 4)) return 0;
    uint32_t frva = *(uint32_t *)(image + funcs + index * 4);
    if (!frva || frva >= im->image_size) return 0;
    /* Forwarded exports are strings inside the export directory; not executable RVAs. */
    if (frva >= h.export_rva && frva < h.export_rva + h.export_size) return 0;
    return im->base + frva;
}

static int pe_resolve_imports(KPROCESS *proc, BootModule *mod) {
    PEImageInstance *im = pe_find_instance(proc, mod);
    if (!im) return -1;
    if (im->state == 3 || im->state == 2) return 0;
    if (im->state == 4) return -1;
    im->state = 2;

    PEHeaderView h;
    if (pe_read_headers(mod, &h) != 0) { im->state = 4; return -1; }
    if (!h.import_rva || !h.import_size) { im->state = 3; return 0; }
    uint8_t *image = (uint8_t *)(HHDM_BASE + im->image_phys);
    uint32_t max_desc = h.import_size / 20;
    int terminated = 0;
    for (uint32_t d = 0; d < max_desc; d++) {
        uint32_t off = h.import_rva + d * 20;
        if (!pe_span(im->image_size, off, 20)) break;
        uint8_t *desc = image + off;
        uint32_t oft = *(uint32_t *)(desc + 0);
        uint32_t name_rva = *(uint32_t *)(desc + 12);
        uint32_t ft = *(uint32_t *)(desc + 16);
        if (!oft && !name_rva && !ft) { terminated = 1; break; }
        const char *dll;
        if (!name_rva || !ft || pe_image_cstr(im, name_rva, &dll) != 0) {
            im->state = 4; return -1;
        }
        BootModule *dep = PspFindModule(user_bootinfo, dll);
        if (!dep || PspMapUserImage(proc, dep) != 0 ||
            pe_resolve_imports(proc, dep) != 0) {
            DbgPrint("pe: unresolved DLL dependency %s imported by %s\n", dll, mod->name);
            im->state = 4; return -1;
        }
        if (!pe_find_instance(proc, dep)) { im->state = 4; return -1; }
        uint32_t lookup = oft ? oft : ft;
        int thunk_terminated = 0;
        for (uint32_t i = 0; i < im->image_size / 8; i++) {
            uint64_t thunk_off = (uint64_t)lookup + (uint64_t)i * 8;
            uint64_t iat_off = (uint64_t)ft + (uint64_t)i * 8;
            if (!pe_span(im->image_size, thunk_off, 8) ||
                !pe_span(im->image_size, iat_off, 8)) break;
            uint64_t thunk = *(uint64_t *)(image + thunk_off);
            if (!thunk) { thunk_terminated = 1; break; }
            uint64_t addr = 0;
            if (thunk & 0x8000000000000000ULL) {
                addr = pe_get_export(proc, dep, 0, (uint16_t)thunk, 1);
            } else {
                if (thunk > UINT32_MAX || !pe_span(im->image_size, thunk, 3)) {
                    im->state = 4; return -1;
                }
                const char *fn;
                if (pe_image_cstr(im, (uint32_t)thunk + 2, &fn) != 0) {
                    im->state = 4; return -1;
                }
                addr = pe_get_export(proc, dep, fn, 0, 0);
            }
            if (!addr) {
                DbgPrint("pe: unresolved import in %s from %s\n", mod->name, dll);
                im->state = 4; return -1;
            }
            *(uint64_t *)(image + iat_off) = addr;
        }
        if (!thunk_terminated) { im->state = 4; return -1; }
    }
    if (!terminated) { im->state = 4; return -1; }
    im->state = 3;
    return 0;
}

static int copy_user_str(uint64_t uaddr, char *out, int max) {
    if (uaddr < 0x10000 || uaddr >= 0x0000800000000000ULL) return -1;
    for (int i = 0; i < max - 1; i++) {
        char c = ((const char *)uaddr)[i];
        out[i] = c;
        if (!c) return 0;
    }
    out[max - 1] = 0;
    return 0;
}

static Frame *syscall_handler(Frame *f) {
    switch (f->rax) {
    case SYS_NtTerminateProcess: {
        DbgPrint("[user] exit");
        KPROCESS *dying = MmCurrentProcess;
        int dead_idx = up_find_proc(dying);
        int parent_idx = (dead_idx >= 0) ? up_tab[dead_idx].parent : -1;
        ut_exit_current();
        if (dying && dying != PspSystemProcess) {
            wm_unregister((int)dying->Pid);
            for (int i = 0; i < UP_MAX; i++) {
                if (up_tab[i].proc == dying) {
                    up_tab[i].alive = 0;
                    up_tab[i].parent = -1;
                }
            }
        }
        int nxt = -1;
        if (parent_idx >= 0 && parent_idx < UP_MAX &&
            up_tab[parent_idx].alive && up_tab[parent_idx].proc)
            nxt = parent_idx;
        if (nxt < 0) nxt = up_pick_next();
        if (nxt >= 0) {
            DbgPrint("user: switch process");
            up_load(f, nxt);
            return f;
        }
        DbgPrint("user: no process left");
        MmSwitchToProcess(PspSystemProcess);
        for (;;) __asm__ volatile("cli; hlt");
    }
    case SYS_NtWriteFile: {
        uint64_t uaddr = f->rsi, len = f->rdx;
        if (uaddr >= 0x10000 && uaddr + len >= uaddr &&
            uaddr + len < 0x0000800000000000ULL && len <= 4096) {
            for (uint64_t i = 0; i < len; i++) {
                char c = ((const char *)uaddr)[i];
                if (!c) break;
                serial_putc(c);
            }
            f->rax = len;
        } else f->rax = (uint64_t)-1LL;
        return f;
    }
    case SYS_NtGetCurrentProcessId:
        f->rax = MmCurrentProcess ? MmCurrentProcess->Pid : 0;
        return f;
    case SYS_NtAllocateVirtualMemory: {
        uint64_t prefer = f->rdi, size = (f->rsi + 0xFFF) & ~0xFFFULL;
        if (!size || size > 16ull * 1024 * 1024) { f->rax = 0; return f; }
        if (!prefer) prefer = 0x50000000ULL;
        for (uint64_t off = 0; off < size; off += 0x1000) {
            uint64_t pa = pmm_alloc_zeroed();
            if (!pa) { f->rax = 0; return f; }
            map_4k_in(MmCurrentProcess->Pml4, prefer + off, pa, PTE_U | PTE_RW);
        }
        f->rax = prefer;
        return f;
    }
    case SYS_NtCreateProcess: {
        char name[64];
        if (copy_user_str(f->rdi, name, sizeof name) != 0) {
            f->rax = (uint64_t)-1LL;
            return f;
        }
        KPROCESS *caller = MmCurrentProcess;
        /* kernel work under system CR3 (page tables / pmm use low identity) */
        MmSwitchToProcess(PspSystemProcess);

        BootModule *mod = PspFindModule(user_bootinfo, name);
        if (!mod) {
            MmSwitchToProcess(caller);
            f->rax = (uint64_t)-1LL;
            return f;
        }
        KPROCESS *proc = MmCreateAddressSpace(name);
        if (!proc || PspMapUserImage(proc, mod) != 0 ||
            pe_resolve_imports(proc, mod) != 0) {
            MmSwitchToProcess(caller);
            f->rax = (uint64_t)-1LL;
            return f;
        }
        PEImageInstance *main_image = pe_find_instance(proc, mod);
        if (!main_image) {
            MmSwitchToProcess(caller);
            f->rax = (uint64_t)-1LL;
            return f;
        }
        uint64_t entry = main_image->entry;

        /* map child stack */
        {
            uint64_t stack_sz = 0x10000ULL;
            uint64_t stack_lo = USER_STACK_TOP - stack_sz;
            for (uint64_t off = 0; off < stack_sz; off += 0x1000) {
                uint64_t pa = pmm_alloc_zeroed();
                if (!pa) {
                    MmSwitchToProcess(caller);
                    f->rax = (uint64_t)-1LL;
                    return f;
                }
                map_4k_in(proc->Pml4, stack_lo + off, pa, PTE_U | PTE_RW);
            }
        }

        if (up_find_proc(caller) < 0 && up_n < UP_MAX) {
            up_tab[up_n].parent = -1;
            up_tab[up_n].proc = caller;
            up_tab[up_n].entry = f->rip;
            up_tab[up_n].alive = 1;
            up_tab[up_n].fresh = 0;
            up_tab[up_n].rip = f->rip;
            up_tab[up_n].rsp = f->rsp;
            up_tab[up_n].rflags = f->rflags ? f->rflags : 0x202;
            up_tab[up_n].rax = proc->Pid;
            up_snap(f, &up_tab[up_n]);
            up_n++;
        } else {
            int me = up_find_proc(caller);
            if (me >= 0) {
                up_save(f, me);
                up_tab[me].rax = proc->Pid;
            }
        }
        if (up_n < UP_MAX) {
            up_tab[up_n].parent = up_find_proc(caller);
            up_tab[up_n].proc = proc;
            up_tab[up_n].entry = entry;
            up_tab[up_n].alive = 1;
            up_tab[up_n].fresh = 1;
            up_tab[up_n].rip = entry;
            up_tab[up_n].rsp = USER_STACK_TOP;
            up_tab[up_n].rflags = 0x202;
            up_tab[up_n].rax = 0;
            up_n++;
            DbgPrint("user: NtCreateProcess concurrent");
        }
        {
            int ci = up_find_proc(proc);
            if (ci >= 0) {
                up_load(f, ci);
                f->rax = proc->Pid;
                return f;
            }
        }
        MmSwitchToProcess(caller);
        f->rax = proc->Pid;
        return f;
    }
    case SYS_NtOpenFile: {
        char name[64];
        if (copy_user_str(f->rdi, name, sizeof name) != 0) {
            f->rax = (uint64_t)-1LL; return f;
        }
        f->rax = 1;
        return f;
    }
    case SYS_NtReadFile: {
        f->rax = 0;
        return f;
    }
    case SYS_NtQueryDirectoryFile: {
        uint32_t index = (uint32_t)f->rdi;
        char *name = (char *)f->rsi;
        if (!name) { f->rax = 0; return f; }
        if (g_ext4_ok && index < (uint32_t)vfs_nent) {
            const char *n = vfs_ents[index].name;
            int i = 0; for (; n[i] && i < 63; i++) name[i] = n[i];
            name[i] = 0;
            f->rax = vfs_ents[index].is_dir ? 2 : 1;
        } else if (user_bootinfo && index < user_bootinfo->module_count) {
            const char *n = user_bootinfo->modules[index].name;
            int i = 0; for (; n[i] && i < 63; i++) name[i] = n[i];
            name[i] = 0;
            f->rax = 1;
        } else f->rax = 0;
        return f;
    }
    case SYS_NtMapFramebuffer: {
        if (!user_fb_base) { f->rax = 0; return f; }
        KPROCESS *cur = MmCurrentProcess;
        MmSwitchToProcess(PspSystemProcess);
        uint64_t uva = 0x70000000ULL;
        uint64_t pages = ((uint64_t)user_fb_pitch * user_fb_h + 0xFFF) >> 12;
        for (uint64_t i = 0; i < pages; i++)
            map_4k_in(cur->Pml4, uva + (i << 12),
                      (user_fb_base + (i << 12)) & ~0xFFFULL, PTE_U | PTE_RW);
        MmSwitchToProcess(cur);
        if (f->rdi) {
            *(uint64_t *)f->rdi = uva;
            *(uint32_t *)(f->rdi + 8) = user_fb_w;
            *(uint32_t *)(f->rdi + 12) = user_fb_h;
            *(uint32_t *)(f->rdi + 16) = user_fb_pitch;
        }
        if (!fb_map_logged) {
            DbgPrint("user: FB mapped %ux%u", user_fb_w, user_fb_h);
            fb_map_logged = 1;
        }
        f->rax = uva;
        return f;
    }
    case SYS_NtGetProcAddress: {
        char dll[64], func[64];
        if (copy_user_str(f->rdi, dll, sizeof dll) != 0) dll[0] = 0;
        if (copy_user_str(f->rsi, func, sizeof func) != 0) func[0] = 0;
        BootModule *mod = PspFindModule(user_bootinfo, dll);
        uint64_t addr = 0;
        if (mod) {
            KPROCESS *cur = MmCurrentProcess;
            MmSwitchToProcess(PspSystemProcess);
            if (PspMapUserImage(cur, mod) == 0 &&
                pe_resolve_imports(cur, mod) == 0)
                addr = pe_get_export(cur, mod, func, 0, 0);
            MmSwitchToProcess(cur);
            DbgPrint("user: GetProcAddress %s!%s = %p", dll, func, (void *)addr);
        }
        f->rax = addr;
        return f;
    }
    case SYS_NtGetInput: {
        typedef struct { int type, x, y, btn; char key; } UIn;
        UIn *out = (UIn *)f->rdi;
        static UIn pending_click;
        static int pending_click_valid;
        static int pending_click_pid;
        int mepid = MmCurrentProcess ? (int)MmCurrentProcess->Pid : 0;
        if (!out) { f->rax = 0; return f; }

        /* A click already assigned to another process must remain queued. */
        if (pending_click_valid) {
            if (pending_click_pid == mepid) {
                *out = pending_click;
                pending_click_valid = 0;
                f->rax = 1;
                return f;
            }
            goto no_input_event;
        }

        ps2_poll();
        if (vi_ok) virtio_input_poll();

        /* tablet owns absolute position when present */
        if (vi_ok && (vi_moved || vi_clicked)) {
            mouse_x = vi_x;
            mouse_y = vi_y;
            mouse_btn = vi_btn;
            out->type = vi_clicked ? 3 : 2;
            out->x = mouse_x;
            out->y = mouse_y;
            out->btn = mouse_btn;
            vi_moved = 0;
            vi_clicked = 0;
            if (out->type == 3) {
                int target = wm_hit_test(out->x, out->y);
                if (target <= 0) target = mepid;
                pending_click = *out;
                pending_click_pid = target;
                pending_click_valid = 1;
                if (target != mepid) goto no_input_event;
                pending_click_valid = 0;
            }
            f->rax = 1;
            return f;
        }

        char k = 0;
        if (ps2_read_key(&k)) {
            out->type = 1;
            out->key = k;
            out->x = mouse_x;
            out->y = mouse_y;
            out->btn = mouse_btn;
            f->rax = 1;
            return f;
        }

        /* PS/2 relative – only if no tablet */
        if (!vi_ok) {
            static int lx = -1, ly = -1, lb = -1;
            if (mouse_x != lx || mouse_y != ly || mouse_btn != lb) {
                int pressed = (mouse_btn & 1) && !(lb & 1);
                out->type = pressed ? 3 : 2;
                out->x = mouse_x;
                out->y = mouse_y;
                out->btn = mouse_btn;
                lx = mouse_x; ly = mouse_y; lb = mouse_btn;
                if (out->type == 3) {
                    int target = wm_hit_test(out->x, out->y);
                    if (target <= 0) target = mepid;
                    pending_click = *out;
                    pending_click_pid = target;
                    pending_click_valid = 1;
                    if (target != mepid) goto no_input_event;
                    pending_click_valid = 0;
                }
                f->rax = 1;
                return f;
            }
        }

    no_input_event:
        /* RR at most every 32 empty polls so pointer stays responsive */
        {
            static int empty;
            empty++;
            if ((empty & 31) == 0) {
                int me = up_find_proc(MmCurrentProcess);
                if (me >= 0) up_save(f, me);
                int nxt = up_pick_next();
                if (nxt >= 0 && nxt != me) {
                    if (me >= 0) up_tab[me].rax = 0;
                    up_load(f, nxt);
                    return f;
                }
            }
        }
        f->rax = 0;
        return f;
    }
    case SYS_NtSetCurrentDirectory: {
        char name[128];
        if (copy_user_str(f->rdi, name, sizeof name) != 0) {
            f->rax = (uint64_t)-1LL; return f;
        }
        int r = g_ext4_ok ? vfs_chdir(name) : -1;
        f->rax = (uint64_t)(int64_t)r;
        return f;
    }
    case SYS_NtCreateThread: {
        f->rax = ut_create(f->rdi, f->rsi);
        return f;
    }
    case SYS_NtWaitForSingleObject: {
        f->rax = ut_wait(f, (int)f->rdi, (uint32_t)f->rsi);
        return f;
    }
    case SYS_WmRegister: {
        char title[40];
        if (copy_user_str(f->rsi, title, sizeof title) != 0) title[0] = 0;
        int pid = MmCurrentProcess ? (int)MmCurrentProcess->Pid : 0;
        wm_register(pid, title);
        f->rax = 0;
        return f;
    }
    case SYS_WmSetMin: {
        int pid = MmCurrentProcess ? (int)MmCurrentProcess->Pid : 0;
        wm_set_min(pid, (int)f->rdi);
        f->rax = 0;
        return f;
    }
    case SYS_WmUnregister: {
        int pid = MmCurrentProcess ? (int)MmCurrentProcess->Pid : 0;
        wm_unregister(pid);
        f->rax = 0;
        return f;
    }
    case SYS_WmQuery: {
        char tmp[40];
        int minv = 0, pidv = 0;
        if (!wm_query((int)f->rdi, tmp, &minv, &pidv)) { f->rax = 0; return f; }
        if (f->rsi) {
            char *d = (char *)f->rsi;
            int k = 0; for (; tmp[k]; k++) d[k] = tmp[k]; d[k] = 0;
        }
        if (f->rdx) *(int *)f->rdx = minv;
        f->rax = (uint64_t)(uint32_t)pidv;
        return f;
    }
    case SYS_WmSetRect: {
        int rect[4];
        int *src = (int *)f->rdi;
        if (!src) { f->rax = 0; return f; }
        for (int i = 0; i < 4; i++) rect[i] = src[i];
        int pid = MmCurrentProcess ? (int)MmCurrentProcess->Pid : 0;
        wm_set_rect(pid, rect[0], rect[1], rect[2], rect[3]);
        f->rax = 0;
        return f;
    }
    case SYS_WmRestore: {
        wm_restore_pid((int)f->rdi);
        f->rax = 0;
        return f;
    }
    case SYS_NtYield: {
        int me = up_find_proc(MmCurrentProcess);
        if (me >= 0) {
            up_save(f, me);
            up_tab[me].rax = 0;
        }
        int nxt = up_pick_next();
        if (nxt >= 0 && nxt != me) {
            up_load(f, nxt);
            return f;
        }
        f->rax = 0;
        return f;
    }
    case SYS_NtShutdown: {
        if (f->rdi == 0)
            HalReturnToFirmware(HalPowerDownRoutine);
        else
            HalReturnToFirmware(HalRebootRoutine);
        f->rax = 0;
        return f;
    }
    case 40:
        f->rax = (uint64_t)-1LL;
        return f;
    default:
        f->rax = (uint64_t)-1LL;
        return f;
    }
}


/* Diagnostic-only page-table walk; called before switching to the process CR3. */
static void user_dbg_walk(KPROCESS *proc, uint64_t va) {
    uint64_t e4 = proc->Pml4[(va >> 39) & 511];
    if (!(e4 & PTE_P)) {
        DbgPrint("ptwalk va=%p: PML4=%p not-present\n", (void *)va, (void *)e4);
        return;
    }
    uint64_t *pdpt = (uint64_t *)(e4 & PTE_ADDR);
    uint64_t e3 = pdpt[(va >> 30) & 511];
    if (!(e3 & PTE_P)) {
        DbgPrint("ptwalk va=%p: PML4=%p PDPT=%p not-present\n",
                 (void *)va, (void *)e4, (void *)e3);
        return;
    }
    if (e3 & PTE_PS) {
        DbgPrint("ptwalk va=%p: PML4=%p PDPT(1G)=%p effective_user=%u\n",
                 (void *)va, (void *)e4, (void *)e3,
                 (unsigned)(((e4 & e3 & PTE_U) != 0)));
        return;
    }
    uint64_t *pd = (uint64_t *)(e3 & PTE_ADDR);
    uint64_t e2 = pd[(va >> 21) & 511];
    if (!(e2 & PTE_P)) {
        DbgPrint("ptwalk va=%p: PML4=%p PDPT=%p PD=%p not-present\n",
                 (void *)va, (void *)e4, (void *)e3, (void *)e2);
        return;
    }
    if (e2 & PTE_PS) {
        DbgPrint("ptwalk va=%p: PML4=%p PDPT=%p PD(2M)=%p effective_user=%u\n",
                 (void *)va, (void *)e4, (void *)e3, (void *)e2,
                 (unsigned)(((e4 & e3 & e2 & PTE_U) != 0)));
        return;
    }
    uint64_t *pt = (uint64_t *)(e2 & PTE_ADDR);
    uint64_t e1 = pt[(va >> 12) & 511];
    DbgPrint("ptwalk va=%p: PML4=%p PDPT=%p PD=%p PT=%p effective_user=%u\n",
             (void *)va, (void *)e4, (void *)e3, (void *)e2, (void *)e1,
             (unsigned)(((e4 & e3 & e2 & e1 & PTE_U) != 0)));
}

static void user_enter(BootInfo *bi) {
    ps2_init();
    ata_init();
    virtio_blk_init();
    virtio_input_init();
    vfs_mount_c();
    DbgPrint("\n-- usermode (smss) --\n");
    user_bootinfo = (BootInfo *)(HHDM_BASE + (uint64_t)bi);
    user_fb_base = bi->fb_base;
    user_fb_w = bi->width;
    user_fb_h = bi->height;
    user_fb_pitch = bi->pitch;
    user_pending = 0;

    DbgPrint("user: BootInfo modules (%u)\n", user_bootinfo->module_count);
    for (uint32_t i = 0; i < user_bootinfo->module_count; i++)
        DbgPrint("  [%u] '%s'\n", i, user_bootinfo->modules[i].name);

    BootModule *smss = PspFindModule(user_bootinfo, "smss.exe");
    if (!smss) { DbgPrint("user: smss.exe not loaded\n"); return; }

    KPROCESS *proc = MmCreateAddressSpace("smss");
    if (!proc || PspMapUserImage(proc, smss) != 0) {
        DbgPrint("user: map smss failed\n");
        return;
    }

    uint64_t ktop = ((uint64_t)user_kstack + sizeof user_kstack) & ~15ULL;
    user_set_kernel_stack(ktop);
    if (ki_current) ki_current->Process = proc;
    up_tab[0].parent = -1;
    up_tab[0].proc = proc;
    up_tab[0].entry = smss->entry;
    up_tab[0].alive = 1;
    up_tab[0].fresh = 0;
    up_tab[0].rip = smss->entry;
    up_tab[0].rsp = USER_STACK_TOP;
    up_tab[0].rflags = 0x202;
    up_n = 1;
    up_cur = 0;

    uint64_t entry = smss->entry;
    DbgPrint("user: pid=%lu enter smss %p\n", (unsigned long)proc->Pid, (void *)entry);

    uint64_t *s = (uint64_t *)ktop;
    *--s = USER_DS;
    *--s = USER_STACK_TOP;
    *--s = 0x202;
    *--s = USER_CS;
    *--s = entry;

    /* map user stack: 64 KiB below USER_STACK_TOP */
    {
        uint64_t stack_sz = 0x10000ULL;
        uint64_t stack_lo = USER_STACK_TOP - stack_sz;
        for (uint64_t off = 0; off < stack_sz; off += 0x1000) {
            uint64_t pa = pmm_alloc_zeroed();
            if (!pa) { DbgPrint("user: stack alloc fail\n"); return; }
            map_4k_in(proc->Pml4, stack_lo + off, pa, PTE_U | PTE_RW);
        }
    }
    user_dbg_walk(proc, entry);
    user_dbg_walk(proc, USER_STACK_TOP - 8);
    user_dbg_walk(proc, (uint64_t)s);
    user_dbg_walk(proc, (uint64_t)user_kstack);
    DbgPrint("user: iret frame=%p kernel_stack=%p..%p rsp0=%p user_rsp=%p cr3=%p\n",
             (void *)s, (void *)user_kstack, (void *)(user_kstack + sizeof user_kstack),
             (void *)tss.rsp[0], (void *)USER_STACK_TOP, (void *)proc->DirectoryTableBase);
    MmSwitchToProcess(proc);

    __asm__ volatile(
        "lea 1f(%%rip), %%rcx\n"
        "mov %%rcx, %[rip]\n"
        "mov %%rsp, %[rsp]\n"
        "mov %[frame], %%rsp\n"
        "iretq\n"
        "1:\n"
        : [rip] "=m"(user_saved_rip), [rsp] "=m"(user_saved_ksp)
        : [frame] "r"(s)
        : "rcx", "memory"
    );

    MmSwitchToProcess(PspSystemProcess);
    if (ki_current) ki_current->Process = PspSystemProcess;
    DbgPrint("user: all user processes exited, back in kernel\n");
}
