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
#define SYS_WmSetRect                20
#define SYS_NtYield                  21

#define UP_MAX 8
#define USER_IMAGE_MAX (UP_MAX * BOOT_MAX_MODULES)
#define USER_IMAGE_PAGE 0x1000ULL

static uint8_t user_kstack[16 * 1024] __attribute__((aligned(16)));
static uint64_t user_saved_ksp, user_saved_rip;
static BootInfo *user_bootinfo;
static KPROCESS *user_pending;
static uint64_t user_pending_entry;

typedef struct {
    KPROCESS *proc;
    uint64_t entry;
    uint64_t rip, rsp, rflags, rax;
    int alive;
    int fresh;
    int parent;
    uint64_t gpr[14];
} UserProc;

static UserProc up_tab[UP_MAX];
static int up_n;
static int up_cur = -1;

/*
 * Each process/module pair has its own physical image allocation.
 * BootModule is shared boot metadata and must never be modified by
 * user image relocation or import resolution.
 */
typedef struct {
    KPROCESS *proc;
    BootModule *source;
    uint64_t phys;
    uint64_t base;
    uint64_t entry;
    uint64_t size;
    int resolving;
    int resolved;
} UserImage;

static UserImage user_images[USER_IMAGE_MAX];

static uint64_t user_fb_base;
static uint32_t user_fb_w, user_fb_h, user_fb_pitch;
static int fb_map_logged;

/* ------------------------------------------------------------ scheduler */

static void up_snap(Frame *f, UserProc *u) {
    u->gpr[0]  = f->r15;
    u->gpr[1]  = f->r14;
    u->gpr[2]  = f->r13;
    u->gpr[3]  = f->r12;
    u->gpr[4]  = f->r11;
    u->gpr[5]  = f->r10;
    u->gpr[6]  = f->r9;
    u->gpr[7]  = f->r8;
    u->gpr[8]  = f->rbp;
    u->gpr[9]  = f->rdi;
    u->gpr[10] = f->rsi;
    u->gpr[11] = f->rdx;
    u->gpr[12] = f->rcx;
    u->gpr[13] = f->rbx;
}

static void up_restore_gpr(Frame *f, const UserProc *u) {
    f->r15 = u->gpr[0];
    f->r14 = u->gpr[1];
    f->r13 = u->gpr[2];
    f->r12 = u->gpr[3];
    f->r11 = u->gpr[4];
    f->r10 = u->gpr[5];
    f->r9  = u->gpr[6];
    f->r8  = u->gpr[7];
    f->rbp = u->gpr[8];
    f->rdi = u->gpr[9];
    f->rsi = u->gpr[10];
    f->rdx = u->gpr[11];
    f->rcx = u->gpr[12];
    f->rbx = u->gpr[13];
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
        f->rsp = USER_STACK_TOP;
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
    for (int i = 0; i < UP_MAX; i++) {
        if (up_tab[i].alive && up_tab[i].proc == p)
            return i;
    }
    return -1;
}

static int up_pick_next(void) {
    int start = (up_cur < 0) ? 0 : up_cur;

    for (int k = 1; k <= UP_MAX; k++) {
        int i = (start + k) % UP_MAX;

        if (!up_tab[i].alive || !up_tab[i].proc)
            continue;

        int has_live_child = 0;

        for (int j = 0; j < UP_MAX; j++) {
            if (up_tab[j].alive && up_tab[j].proc &&
                up_tab[j].parent == i) {
                has_live_child = 1;
                break;
            }
        }

        if (!has_live_child)
            return i;
    }

    return -1;
}

/* ------------------------------------------------------------ PE helpers */

static int pe_is_image(const uint8_t *image, uint64_t size) {
    if (!image || size < 0x40) return 0;
    if (image[0] != 'M' || image[1] != 'Z') return 0;

    int32_t lfanew = *(const int32_t *)(image + 0x3C);
    if (lfanew < 0 || (uint64_t)lfanew + 4 + 20 + 2 > size)
        return 0;

    if (*(const uint32_t *)(image + lfanew) != 0x00004550)
        return 0;

    const uint8_t *opt = image + lfanew + 4 + 20;
    return *(const uint16_t *)opt == 0x20B;
}

static uint8_t *PspImageBytes(UserImage *image) {
    if (!image || !image->phys) return 0;
    return (uint8_t *)(HHDM_BASE + image->phys);
}

static UserImage *PspGetUserImage(KPROCESS *proc, BootModule *mod) {
    if (!proc || !mod) return 0;

    for (int i = 0; i < USER_IMAGE_MAX; i++) {
        if (user_images[i].proc == proc &&
            user_images[i].source == mod &&
            user_images[i].phys) {
            return &user_images[i];
        }
    }

    return 0;
}

/*
 * Apply PE base relocations to the private copy only.
 * The source BootModule bytes are never changed.
 */
static void pe_apply_relocs(uint8_t *image, uint64_t size, int64_t delta) {
    if (!pe_is_image(image, size) || !delta) return;

    int32_t lfanew = *(int32_t *)(image + 0x3C);
    uint8_t *opt = image + lfanew + 4 + 20;

    uint32_t reloc_rva = *(uint32_t *)(opt + 112 + 5 * 8);
    uint32_t reloc_sz  = *(uint32_t *)(opt + 112 + 5 * 8 + 4);

    if (!reloc_rva || !reloc_sz ||
        reloc_rva >= size || reloc_sz > size - reloc_rva) {
        return;
    }

    uint8_t *rel = image + reloc_rva;
    uint8_t *end = rel + reloc_sz;

    while (rel + 8 <= end) {
        uint32_t page_rva = *(uint32_t *)rel;
        uint32_t block_sz = *(uint32_t *)(rel + 4);

        if (block_sz < 8 || block_sz > (uint64_t)(end - rel))
            break;

        uint32_t count = (block_sz - 8) / 2;
        uint16_t *entries = (uint16_t *)(rel + 8);

        for (uint32_t i = 0; i < count; i++) {
            uint16_t e = entries[i];
            uint16_t type = e >> 12;
            uint16_t off = e & 0xFFF;
            uint64_t target = (uint64_t)page_rva + off;

            if (type == 10) {
                if (target <= size && sizeof(uint64_t) <= size - target)
                    *(uint64_t *)(image + target) += (uint64_t)delta;
            } else if (type == 3) {
                if (target <= size && sizeof(uint32_t) <= size - target)
                    *(uint32_t *)(image + target) += (uint32_t)delta;
            }
        }

        rel += block_sz;
    }
}

static BootModule *PspFindModule(BootInfo *bi, const char *name) {
    if (!bi || !name) return 0;

    for (uint32_t i = 0; i < bi->module_count && i < BOOT_MAX_MODULES; i++) {
        const char *n = bi->modules[i].name;
        int match = 1;

        for (int k = 0; name[k] || n[k]; k++) {
            char a = name[k];
            char b = n[k];

            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);

            if (a != b) {
                match = 0;
                break;
            }
        }

        if (match) return &bi->modules[i];
    }

    return 0;
}

/*
 * Map a private image for this process.
 *
 * IMPORTANT: call this while the system/kernel address space is active,
 * because pmm_alloc_n() returns physical addresses and map_4k_in() relies
 * on the current address space for page-table access.
 */
static int PspMapUserImage(KPROCESS *proc, BootModule *mod) {
    if (!proc || !mod || !mod->phys || !mod->size)
        return -1;

    UserImage *existing = PspGetUserImage(proc, mod);
    if (existing)
        return 0;

    uint64_t size = mod->size;
    if (size > UINT64_MAX - (USER_IMAGE_PAGE - 1))
        return -1;

    uint64_t rounded = (size + USER_IMAGE_PAGE - 1) & ~(USER_IMAGE_PAGE - 1);
    uint64_t pages = rounded / USER_IMAGE_PAGE;

    if (!pages)
        return -1;

    int slot = -1;
    for (int i = 0; i < USER_IMAGE_MAX; i++) {
        if (!user_images[i].phys && !user_images[i].proc &&
            !user_images[i].source) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        DbgPrint("user: image table full for %s\n", mod->name);
        return -1;
    }

    uint64_t private_pa = pmm_alloc_n(pages);
    if (!private_pa) {
        DbgPrint("user: image allocation failed for %s (%llu pages)\n",
                 mod->name, (unsigned long long)pages);
        return -1;
    }

    uint8_t *src = (uint8_t *)(HHDM_BASE + mod->phys);
    uint8_t *dst = (uint8_t *)(HHDM_BASE + private_pa);

    for (uint64_t i = 0; i < size; i++)
        dst[i] = src[i];

    for (uint64_t i = size; i < rounded; i++)
        dst[i] = 0;

    uint64_t base = mod->base ? mod->base : 0x40000000ULL;
    uint64_t entry = base;

    /*
     * Save the record before resolving imports. This lets recursive DLL
     * dependencies detect that the image is already mapped.
     */
    UserImage *image = &user_images[slot];
    image->proc = proc;
    image->source = mod;
    image->phys = private_pa;
    image->base = base;
    image->entry = entry;
    image->size = rounded;
    image->resolving = 0;
    image->resolved = 0;

    /*
     * The allocation is contiguous, so every virtual image page maps to
     * the corresponding private physical page.
     */
    for (uint64_t off = 0; off < rounded; off += USER_IMAGE_PAGE) {
        map_4k_in(proc->Pml4, base + off,
                  private_pa + off, PTE_U | PTE_RW);
    }

    if (pe_is_image(dst, size)) {
        int32_t lfanew = *(int32_t *)(dst + 0x3C);
        uint8_t *opt = dst + lfanew + 4 + 20;

        uint64_t preferred_base = *(uint64_t *)(opt + 24);
        int64_t delta = (int64_t)base - (int64_t)preferred_base;

        if (delta)
            pe_apply_relocs(dst, size, delta);

        uint32_t ep_rva = *(uint32_t *)(opt + 16);

        if (ep_rva >= size) {
            DbgPrint("user: invalid PE entry point in %s\n", mod->name);
            for (uint64_t i = 0; i < pages; i++)
                pmm_free(private_pa + i * USER_IMAGE_PAGE);

            image->proc = 0;
            image->source = 0;
            image->phys = 0;
            image->base = 0;
            image->entry = 0;
            image->size = 0;
            return -1;
        }

        image->entry = base + ep_rva;
    }

    DbgPrint("user: private image %s pid=%lu va=%p pa=%p size=%llu\n",
             mod->name,
             (unsigned long)proc->Pid,
             (void *)image->base,
             (void *)image->phys,
             (unsigned long long)image->size);

    return 0;
}

/*
 * Return an export address from the requesting process's private DLL.
 */
static uint64_t pe_get_export(KPROCESS *proc, BootModule *mod,
                              const char *func) {
    if (!proc || !mod || !func)
        return 0;

    UserImage *image = PspGetUserImage(proc, mod);
    if (!image)
        return 0;

    uint8_t *phys = PspImageBytes(image);
    uint64_t size = image->size;

    if (!pe_is_image(phys, size))
        return 0;

    int32_t lfanew = *(int32_t *)(phys + 0x3C);
    uint8_t *opt = phys + lfanew + 4 + 20;

    uint32_t exp_rva = *(uint32_t *)(opt + 112);
    if (!exp_rva || exp_rva > size || size - exp_rva < 40)
        return 0;

    uint8_t *exp = phys + exp_rva;
    uint32_t nfuncs = *(uint32_t *)(exp + 20);
    uint32_t nnames = *(uint32_t *)(exp + 24);
    uint32_t funcs_rva = *(uint32_t *)(exp + 28);
    uint32_t names_rva = *(uint32_t *)(exp + 32);
    uint32_t ords_rva  = *(uint32_t *)(exp + 36);

    if (funcs_rva >= size || names_rva >= size || ords_rva >= size)
        return 0;

    if ((uint64_t)nnames * 4 > size - names_rva ||
        (uint64_t)nnames * 2 > size - ords_rva ||
        (uint64_t)nfuncs * 4 > size - funcs_rva) {
        return 0;
    }

    for (uint32_t i = 0; i < nnames; i++) {
        uint32_t nrva = *(uint32_t *)(phys + names_rva + i * 4);

        if (nrva >= size)
            continue;

        const char *n = (const char *)(phys + nrva);
        uint64_t remaining = size - nrva;
        int match = 1;
        int terminated = 0;

        for (uint64_t k = 0; k < remaining; k++) {
            if (func[k] != n[k]) {
                match = 0;
                break;
            }

            if (!func[k] && !n[k]) {
                terminated = 1;
                break;
            }

            if (!func[k] || !n[k]) {
                match = 0;
                break;
            }
        }

        if (!match || !terminated)
            continue;

        uint16_t ord = *(uint16_t *)(phys + ords_rva + i * 2);
        if (ord >= nfuncs)
            return 0;

        uint32_t frva = *(uint32_t *)(phys + funcs_rva + ord * 4);
        if (!frva || frva >= size)
            return 0;

        /*
         * Forwarded exports are not handled here. A forwarded export's
         * RVA points back inside the export directory.
         */
        if (frva >= exp_rva &&
            frva < exp_rva + 40) {
            return 0;
        }

        return image->base + frva;
    }

    return 0;
}

/*
 * Resolve imports in the process-local image.
 *
 * resolving/resolved flags prevent dependency cycles such as A -> B -> A.
 * DLL images and their import address tables are process-private too.
 */
static void pe_resolve_imports(KPROCESS *proc, BootModule *mod) {
    if (!proc || !mod)
        return;

    UserImage *image = PspGetUserImage(proc, mod);
    if (!image || image->resolved || image->resolving)
        return;

    uint8_t *phys = PspImageBytes(image);
    uint64_t size = image->size;

    if (!pe_is_image(phys, size)) {
        image->resolved = 1;
        return;
    }

    image->resolving = 1;

    int32_t lfanew = *(int32_t *)(phys + 0x3C);
    uint8_t *opt = phys + lfanew + 4 + 20;

    uint32_t imp_rva = *(uint32_t *)(opt + 112 + 1 * 8);
    uint32_t imp_sz  = *(uint32_t *)(opt + 112 + 1 * 8 + 4);

    if (!imp_rva || imp_rva >= size ||
        (imp_sz && imp_sz > size - imp_rva)) {
        image->resolving = 0;
        image->resolved = 1;
        return;
    }

    uint64_t max_desc = imp_sz ? imp_sz / 20 : (size - imp_rva) / 20;

    for (uint64_t d = 0; d < max_desc; d++) {
        uint64_t desc_off = (uint64_t)imp_rva + d * 20;

        if (desc_off > size || size - desc_off < 20)
            break;

        uint8_t *desc = phys + desc_off;

        uint32_t oft_rva  = *(uint32_t *)(desc + 0);
        uint32_t name_rva = *(uint32_t *)(desc + 12);
        uint32_t ft_rva   = *(uint32_t *)(desc + 16);

        if (!name_rva && !ft_rva && !oft_rva)
            break;

        if (!name_rva || !ft_rva ||
            name_rva >= size || ft_rva >= size)
            continue;

        const char *dll = (const char *)(phys + name_rva);
        BootModule *dep = PspFindModule(user_bootinfo, dll);

        if (!dep)
            continue;

        if (PspMapUserImage(proc, dep) != 0)
            continue;

        pe_resolve_imports(proc, dep);

        uint32_t look_rva = oft_rva ? oft_rva : ft_rva;

        if (look_rva >= size)
            continue;

        uint64_t max_thunks = (size - look_rva) / 8;
        uint64_t max_iat = (size - ft_rva) / 8;
        if (max_thunks > max_iat)
            max_thunks = max_iat;

        for (uint64_t i = 0; i < max_thunks; i++) {
            uint64_t look_off = (uint64_t)look_rva + i * 8;
            uint64_t iat_off = (uint64_t)ft_rva + i * 8;

            uint64_t look = *(uint64_t *)(phys + look_off);
            if (!look)
                break;

            uint64_t addr = 0;

            /* Ordinal imports are not implemented by this loader. */
            if (!(look & 0x8000000000000000ULL)) {
                uint32_t name_off = (uint32_t)look;

                if (name_off <= size && size - name_off >= 3) {
                    const char *fn = (const char *)(phys + name_off + 2);
                    addr = pe_get_export(proc, dep, fn);
                }
            }

            *(uint64_t *)(phys + iat_off) = addr;
        }
    }

    image->resolving = 0;
    image->resolved = 1;
}

/* ------------------------------------------------------------ user memory */

static int copy_user_str(uint64_t uaddr, char *out, int max) {
    if (!out || max <= 0 ||
        uaddr < 0x10000 ||
        uaddr >= 0x0000800000000000ULL) {
        return -1;
    }

    for (int i = 0; i < max - 1; i++) {
        uint64_t at = uaddr + (uint64_t)i;

        if (at < uaddr || at >= 0x0000800000000000ULL)
            return -1;

        char c = ((const char *)at)[0];
        out[i] = c;

        if (!c)
            return 0;
    }

    out[max - 1] = 0;
    return 0;
}

/* ------------------------------------------------------------ syscalls */

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
            up_tab[parent_idx].alive && up_tab[parent_idx].proc) {
            nxt = parent_idx;
        }

        if (nxt < 0)
            nxt = up_pick_next();

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
        uint64_t uaddr = f->rsi;
        uint64_t len = f->rdx;

        if (uaddr >= 0x10000 &&
            uaddr + len >= uaddr &&
            uaddr + len < 0x0000800000000000ULL &&
            len <= 4096) {
            for (uint64_t i = 0; i < len; i++) {
                char c = ((const char *)uaddr)[i];
                if (!c) break;
                serial_putc(c);
            }

            f->rax = len;
        } else {
            f->rax = (uint64_t)-1LL;
        }

        return f;
    }

    case SYS_NtGetCurrentProcessId:
        f->rax = MmCurrentProcess ? MmCurrentProcess->Pid : 0;
        return f;

    case SYS_NtAllocateVirtualMemory: {
        uint64_t prefer = f->rdi;

        if (f->rsi > UINT64_MAX - 0xFFFULL) {
            f->rax = 0;
            return f;
        }

        uint64_t size = (f->rsi + 0xFFFULL) & ~0xFFFULL;

        if (!size || size > 16ull * 1024 * 1024) {
            f->rax = 0;
            return f;
        }

        if (!prefer)
            prefer = 0x50000000ULL;

        for (uint64_t off = 0; off < size; off += USER_IMAGE_PAGE) {
            uint64_t pa = pmm_alloc_zeroed();

            if (!pa) {
                f->rax = 0;
                return f;
            }

            map_4k_in(MmCurrentProcess->Pml4,
                      prefer + off, pa, PTE_U | PTE_RW);
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

        /* Kernel allocation and page-table work under the system CR3. */
        MmSwitchToProcess(PspSystemProcess);

        BootModule *mod = PspFindModule(user_bootinfo, name);

        if (!mod) {
            MmSwitchToProcess(caller);
            f->rax = (uint64_t)-1LL;
            return f;
        }

        KPROCESS *proc = MmCreateAddressSpace(name);

        if (!proc || PspMapUserImage(proc, mod) != 0) {
            MmSwitchToProcess(caller);
            f->rax = (uint64_t)-1LL;
            return f;
        }

        pe_resolve_imports(proc, mod);

        UserImage *main_image = PspGetUserImage(proc, mod);

        if (!main_image) {
            MmSwitchToProcess(caller);
            f->rax = (uint64_t)-1LL;
            return f;
        }

        uint64_t entry = main_image->entry;

        /* Map the child's 64 KiB stack. */
        {
            uint64_t stack_sz = 0x10000ULL;
            uint64_t stack_lo = USER_STACK_TOP - stack_sz;

            for (uint64_t off = 0; off < stack_sz; off += USER_IMAGE_PAGE) {
                uint64_t pa = pmm_alloc_zeroed();

                if (!pa) {
                    MmSwitchToProcess(caller);
                    f->rax = (uint64_t)-1LL;
                    return f;
                }

                map_4k_in(proc->Pml4,
                          stack_lo + off, pa, PTE_U | PTE_RW);
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
            f->rax = (uint64_t)-1LL;
            return f;
        }

        f->rax = 1;
        return f;
    }

    case SYS_NtReadFile:
        f->rax = 0;
        return f;

    case SYS_NtQueryDirectoryFile: {
        uint32_t index = (uint32_t)f->rdi;
        char *name = (char *)f->rsi;

        if (!name) {
            f->rax = 0;
            return f;
        }

        if (g_ext4_ok && index < (uint32_t)vfs_nent) {
            const char *n = vfs_ents[index].name;
            int i = 0;

            for (; n[i] && i < 63; i++)
                name[i] = n[i];

            name[i] = 0;
            f->rax = vfs_ents[index].is_dir ? 2 : 1;
        } else if (user_bootinfo &&
                   index < user_bootinfo->module_count &&
                   index < BOOT_MAX_MODULES) {
            const char *n = user_bootinfo->modules[index].name;
            int i = 0;

            for (; n[i] && i < 63; i++)
                name[i] = n[i];

            name[i] = 0;
            f->rax = 1;
        } else {
            f->rax = 0;
        }

        return f;
    }

    case SYS_NtMapFramebuffer: {
        if (!user_fb_base) {
            f->rax = 0;
            return f;
        }

        KPROCESS *cur = MmCurrentProcess;
        MmSwitchToProcess(PspSystemProcess);

        uint64_t uva = 0x70000000ULL;
        uint64_t pages =
            ((uint64_t)user_fb_pitch * user_fb_h + 0xFFFULL) >> 12;

        for (uint64_t i = 0; i < pages; i++) {
            map_4k_in(cur->Pml4,
                      uva + (i << 12),
                      (user_fb_base + (i << 12)) & ~0xFFFULL,
                      PTE_U | PTE_RW);
        }

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

        if (copy_user_str(f->rdi, dll, sizeof dll) != 0)
            dll[0] = 0;

        if (copy_user_str(f->rsi, func, sizeof func) != 0)
            func[0] = 0;

        BootModule *mod = PspFindModule(user_bootinfo, dll);
        uint64_t addr = 0;

        if (mod) {
            KPROCESS *cur = MmCurrentProcess;

            MmSwitchToProcess(PspSystemProcess);

            if (PspMapUserImage(cur, mod) == 0) {
                pe_resolve_imports(cur, mod);
                addr = pe_get_export(cur, mod, func);
            }

            MmSwitchToProcess(cur);

            DbgPrint("user: GetProcAddress %s!%s = %p",
                     dll, func, (void *)addr);
        }

        f->rax = addr;
        return f;
    }

    case SYS_NtGetInput: {
        typedef struct {
            int type, x, y, btn;
            char key;
        } UIn;

        UIn *out = (UIn *)f->rdi;

        static UIn pending_click;
        static int pending_click_valid;
        static int pending_click_pid;

        int mepid = MmCurrentProcess ? (int)MmCurrentProcess->Pid : 0;

        if (!out) {
            f->rax = 0;
            return f;
        }

        /* A click assigned to another process remains queued. */
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

        if (vi_ok)
            virtio_input_poll();

        /* Tablet owns absolute position when present. */
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

                if (target != mepid)
                    goto no_input_event;

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

        /* PS/2 relative mouse input, only when no tablet exists. */
        if (!vi_ok) {
            static int lx = -1, ly = -1, lb = -1;

            if (mouse_x != lx || mouse_y != ly || mouse_btn != lb) {
                int pressed = (mouse_btn & 1) && !(lb & 1);

                out->type = pressed ? 3 : 2;
                out->x = mouse_x;
                out->y = mouse_y;
                out->btn = mouse_btn;

                lx = mouse_x;
                ly = mouse_y;
                lb = mouse_btn;

                if (out->type == 3) {
                    int target = wm_hit_test(out->x, out->y);
                    if (target <= 0) target = mepid;

                    pending_click = *out;
                    pending_click_pid = target;
                    pending_click_valid = 1;

                    if (target != mepid)
                        goto no_input_event;

                    pending_click_valid = 0;
                }

                f->rax = 1;
                return f;
            }
        }

    no_input_event:
        /*
         * Round-robin at most every 32 empty polls.
         * Scheduler behavior intentionally unchanged.
         */
        {
            static int empty;
            empty++;

            if ((empty & 31) == 0) {
                int me = up_find_proc(MmCurrentProcess);

                if (me >= 0)
                    up_save(f, me);

                int nxt = up_pick_next();

                if (nxt >= 0 && nxt != me) {
                    if (me >= 0)
                        up_tab[me].rax = 0;

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
            f->rax = (uint64_t)-1LL;
            return f;
        }

        int r = g_ext4_ok ? vfs_chdir(name) : -1;
        f->rax = (uint64_t)(int64_t)r;
        return f;
    }

    case SYS_NtCreateThread:
        f->rax = ut_create(f->rdi, f->rsi);
        return f;

    case SYS_NtWaitForSingleObject:
        f->rax = ut_wait(f, (int)f->rdi, (uint32_t)f->rsi);
        return f;

    case SYS_WmRegister: {
        char title[40];

        if (copy_user_str(f->rsi, title, sizeof title) != 0)
            title[0] = 0;

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

        if (!wm_query((int)f->rdi, tmp, &minv, &pidv)) {
            f->rax = 0;
            return f;
        }

        if (f->rsi) {
            char *d = (char *)f->rsi;
            int k = 0;

            for (; tmp[k]; k++)
                d[k] = tmp[k];

            d[k] = 0;
        }

        if (f->rdx)
            *(int *)f->rdx = minv;

        f->rax = (uint64_t)(uint32_t)pidv;
        return f;
    }

    case SYS_WmSetRect: {
        int rect[4];
        int *src = (int *)f->rdi;

        if (!src) {
            f->rax = 0;
            return f;
        }

        for (int i = 0; i < 4; i++)
            rect[i] = src[i];

        int pid = MmCurrentProcess ? (int)MmCurrentProcess->Pid : 0;

        wm_set_rect(pid, rect[0], rect[1], rect[2], rect[3]);

        f->rax = 0;
        return f;
    }

    case SYS_WmRestore:
        wm_restore_pid((int)f->rdi);
        f->rax = 0;
        return f;

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

    case SYS_NtShutdown:
        if (f->rdi == 0)
            HalReturnToFirmware(HalPowerDownRoutine);
        else
            HalReturnToFirmware(HalRebootRoutine);

        f->rax = 0;
        return f;

    case 40:
        f->rax = (uint64_t)-1LL;
        return f;

    default:
        f->rax = (uint64_t)-1LL;
        return f;
    }
}

/* ------------------------------------------------------------ user entry */

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

    DbgPrint("user: BootInfo modules (%u)\n",
             user_bootinfo->module_count);

    for (uint32_t i = 0;
         i < user_bootinfo->module_count && i < BOOT_MAX_MODULES;
         i++) {
        DbgPrint("  [%u] '%s'\n",
                 i, user_bootinfo->modules[i].name);
    }

    BootModule *smss = PspFindModule(user_bootinfo, "smss.exe");

    if (!smss) {
        DbgPrint("user: smss.exe not loaded\n");
        return;
    }

    /*
     * Ensure page-table and physical allocator work happens in the
     * system address space. Keep the original system process for return.
     */
    MmSwitchToProcess(PspSystemProcess);

    KPROCESS *proc = MmCreateAddressSpace("smss");

    if (!proc || PspMapUserImage(proc, smss) != 0) {
        DbgPrint("user: map smss failed\n");
        MmSwitchToProcess(PspSystemProcess);
        return;
    }

    pe_resolve_imports(proc, smss);

    UserImage *smss_image = PspGetUserImage(proc, smss);

    if (!smss_image) {
        DbgPrint("user: smss private image record missing\n");
        MmSwitchToProcess(PspSystemProcess);
        return;
    }

    uint64_t entry = smss_image->entry;

    uint64_t ktop =
        ((uint64_t)user_kstack + sizeof user_kstack) & ~15ULL;

    user_set_kernel_stack(ktop);

    if (ki_current)
        ki_current->Process = proc;

    up_tab[0].parent = -1;
    up_tab[0].proc = proc;
    up_tab[0].entry = entry;
    up_tab[0].alive = 1;
    up_tab[0].fresh = 0;
    up_tab[0].rip = entry;
    up_tab[0].rsp = USER_STACK_TOP;
    up_tab[0].rflags = 0x202;
    up_tab[0].rax = 0;

    up_n = 1;
    up_cur = 0;

    DbgPrint("user: pid=%lu enter smss %p\n",
             (unsigned long)proc->Pid, (void *)entry);

    uint64_t *s = (uint64_t *)ktop;

    *--s = USER_DS;
    *--s = USER_STACK_TOP;
    *--s = 0x202;
    *--s = USER_CS;
    *--s = entry;

    /* Map the initial process's 64 KiB user stack. */
    {
        uint64_t stack_sz = 0x10000ULL;
        uint64_t stack_lo = USER_STACK_TOP - stack_sz;

        for (uint64_t off = 0; off < stack_sz; off += USER_IMAGE_PAGE) {
            uint64_t pa = pmm_alloc_zeroed();

            if (!pa) {
                DbgPrint("user: stack alloc fail\n");
                MmSwitchToProcess(PspSystemProcess);

                if (ki_current)
                    ki_current->Process = PspSystemProcess;

                return;
            }

            map_4k_in(proc->Pml4,
                      stack_lo + off, pa, PTE_U | PTE_RW);
        }
    }

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

    if (ki_current)
        ki_current->Process = PspSystemProcess;

    DbgPrint("user: all user processes exited, back in kernel\n");
}