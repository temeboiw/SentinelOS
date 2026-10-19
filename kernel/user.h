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
#include "ntabi.h"
#include "net.h"
#include "netstack.h"

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
#define SYS_NtQueryProcesses         28
#define SYS_NtTerminatePid           29
#define SYS_NtUdpSend                30
#define SYS_NtUdpBind                31
#define SYS_NtUdpReceive              32
#define SYS_NtGetTickCount            33
#define SYS_NtCloseFile               34

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

/* Disk-loaded PE images are normalized to image layout before entering the
 * existing per-process mapper. Keep their metadata stable for import resolution. */
#define DISK_PE_MODULE_MAX 8
#define DISK_PE_FILE_MAX (64ULL * 1024 * 1024)
static BootModule disk_pe_modules[DISK_PE_MODULE_MAX];
static uint8_t disk_pe_used[DISK_PE_MODULE_MAX];


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

static int up_find_free_slot(void) {
    for (int i = 0; i < UP_MAX; i++)
        if (!up_tab[i].alive)
            return i;
    return -1;
}

static int up_pick_next(void) {
    /* Round-robin every live process; parents remain schedulable while children run. */
    int start = (up_cur < 0) ? 0 : up_cur;
    for (int k = 1; k <= UP_MAX; k++) {
        int i = (start + k) % UP_MAX;
        if (up_tab[i].alive && up_tab[i].proc)
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

static void PspForgetUserImages(KPROCESS *proc) {
    if (!proc) return;
    for (int i = 0; i < USER_IMAGE_MAX; i++) {
        if (user_images[i].proc != proc) continue;
        user_images[i].proc = 0;
        user_images[i].source = 0;
        user_images[i].phys = 0;
        user_images[i].base = 0;
        user_images[i].entry = 0;
        user_images[i].size = 0;
        user_images[i].resolving = 0;
        user_images[i].resolved = 0;
    }
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
    uint64_t base = mod->base ? mod->base : 0x40000000ULL;

    if (!pages || (base & (USER_IMAGE_PAGE - 1)) ||
        base < 0x10000ULL || base >= 0x0000800000000000ULL ||
        rounded > 0x0000800000000000ULL - base ||
        !MmUserRangeUnmapped(proc, base, rounded)) {
        DbgPrint("user: image VA collision or invalid range for %s\n", mod->name);
        return -1;
    }

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
                  private_pa + off, PTE_U | PTE_RW | PTE_OWNED);
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
            for (uint64_t off = 0; off < rounded; off += USER_IMAGE_PAGE)
                MmUnmapAllocatedUserPage(proc, base + off);

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


/* Read an on-disk AMD64 PE32+ executable, convert raw section offsets to RVAs,
 * and register it as a stable synthetic module for the existing image/import
 * mapper. Called with the kernel address space active. */
static BootModule *PspLoadDiskPe(const char *path) {
    if (!path || !g_ext4_ok) return 0;
    uint64_t file_size = vfs_file_size(path);
    if (file_size < 0x100 || file_size > DISK_PE_FILE_MAX) {
        DbgPrint("pe: invalid disk image size for %s (%llu)\\n", path,
                 (unsigned long long)file_size);
        return 0;
    }

    int fh = vfs_open_read(path);
    if (fh < 0) return 0;
    uint64_t file_pages = (file_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t file_pa = pmm_alloc_n(file_pages);
    if (!file_pa) { vfs_close_open(fh); return 0; }
    uint8_t *file = (uint8_t *)(HHDM_BASE + file_pa);
    for (uint64_t off = 0; off < file_size; ) {
        uint32_t ask = (uint32_t)((file_size - off) > 4096 ? 4096 : (file_size - off));
        int64_t got = vfs_read_open(fh, file + off, ask);
        if (got <= 0) {
            vfs_close_open(fh);
            return 0;
        }
        off += (uint64_t)got;
    }
    vfs_close_open(fh);

    if (file[0] != 'M' || file[1] != 'Z') return 0;
    int32_t peoff = *(int32_t *)(file + 0x3c);
    if (peoff < 0 || (uint64_t)peoff + 24 > file_size ||
        *(uint32_t *)(file + peoff) != 0x00004550) return 0;
    uint8_t *coff = file + peoff + 4;
    uint16_t machine = *(uint16_t *)(coff + 0);
    uint16_t nsec = *(uint16_t *)(coff + 2);
    uint16_t optsz = *(uint16_t *)(coff + 16);
    uint64_t optoff = (uint64_t)peoff + 24;
    if (machine != 0x8664 || !nsec || nsec > 96 ||
        optsz < 112 + 16 * 8 || optoff + optsz > file_size ||
        *(uint16_t *)(file + optoff) != 0x20b) {
        DbgPrint("pe: unsupported PE format for %s\\n", path);
        return 0;
    }

    uint8_t *opt = file + optoff;
    uint32_t size_image = *(uint32_t *)(opt + 56);
    uint32_t size_headers = *(uint32_t *)(opt + 60);
    uint64_t image_base = *(uint64_t *)(opt + 24);
    uint32_t entry_rva = *(uint32_t *)(opt + 16);
    uint64_t sectoff = optoff + optsz;
    if (size_image < 0x1000 || size_image > DISK_PE_FILE_MAX ||
        size_headers > file_size || size_headers > size_image ||
        entry_rva >= size_image ||
        sectoff + (uint64_t)nsec * 40 > file_size ||
        image_base < 0x10000 || image_base >= 0x0000800000000000ULL ||
        size_image > 0x0000800000000000ULL - image_base) {
        DbgPrint("pe: invalid PE layout for %s\\n", path);
        return 0;
    }

    /* Find a free stable descriptor before allocating the image-layout buffer. */
    int slot = -1;
    for (int i = 0; i < DISK_PE_MODULE_MAX; i++) {
        if (!disk_pe_used[i]) { slot = i; break; }
    }
    if (slot < 0) {
        DbgPrint("pe: disk module table full\\n");
        return 0;
    }
    uint64_t image_pages = (size_image + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t image_pa = pmm_alloc_n(image_pages);
    if (!image_pa) {
        DbgPrint("pe: no memory for %s (%u bytes)\\n", path, size_image);
        return 0;
    }
    uint8_t *image = (uint8_t *)(HHDM_BASE + image_pa);
    for (uint64_t i = 0; i < image_pages * PAGE_SIZE; i++) image[i] = 0;
    for (uint32_t i = 0; i < size_headers; i++) image[i] = file[i];

    for (uint16_t s = 0; s < nsec; s++) {
        uint8_t *sec = file + sectoff + (uint64_t)s * 40;
        uint32_t virtual_size = *(uint32_t *)(sec + 8);
        uint32_t virtual_rva = *(uint32_t *)(sec + 12);
        uint32_t raw_size = *(uint32_t *)(sec + 16);
        uint32_t raw_off = *(uint32_t *)(sec + 20);
        uint32_t copy_size = raw_size;
        if (copy_size > virtual_size && virtual_size) copy_size = virtual_size;
        if ((uint64_t)raw_off + copy_size > file_size ||
            (uint64_t)virtual_rva + copy_size > size_image ||
            (virtual_size && (uint64_t)virtual_rva + virtual_size > size_image)) {
            DbgPrint("pe: section bounds invalid in %s\\n", path);
            return 0;
        }
        for (uint32_t j = 0; j < copy_size; j++)
            image[virtual_rva + j] = file[raw_off + j];
    }

    /* The normalized image owns its own pages now; release the temporary file buffer. */
    for (uint64_t p = 0; p < file_pages; p++) pmm_free(file_pa + p * PAGE_SIZE);

    BootModule *mod = &disk_pe_modules[slot];
    for (int i = 0; i < 32; i++) mod->name[i] = 0;
    int n = 0;
    while (path[n] && n < 31) { mod->name[n] = path[n]; n++; }
    mod->name[n] = 0;
    mod->base = image_base;
    mod->phys = image_pa;
    mod->size = size_image;
    mod->entry = image_base + entry_rva;
    disk_pe_used[slot] = 1;
    DbgPrint("pe: loaded disk image %s base=%p size=%u\\n", path,
             (void *)image_base, size_image);
    return mod;
}

/* ------------------------------------------------------------ user memory */

static int copy_user_str(uint64_t uaddr, char *out, int max) {
    if (!out || max <= 0 ||
        uaddr < 0x10000ULL ||
        uaddr >= 0x0000800000000000ULL) return -1;

    for (int i = 0; i < max - 1; i++) {
        uint64_t at = uaddr + (uint64_t)i;
        if (at < uaddr || !MmValidateUserRange(MmCurrentProcess, at, 1, 0))
            return -1;
        char c = ((const char *)at)[0];
        out[i] = c;
        if (!c) return 0;
    }

    out[max - 1] = 0;
    return -1; /* no terminator within the supplied bound */
}

/* ------------------------------------------------------------ syscalls */

static uint64_t NtStatusResult(NTSTATUS status) {
    return (uint64_t)(int64_t)status;
}

static Frame *syscall_handler(Frame *f) {
    switch (f->rax) {
    case NT_SYS_QUERY_INFORMATION_PROCESS: {
        if ((NT_HANDLE)f->rdi != NT_CURRENT_PROCESS ||
            f->rsi != NT_PROCESS_BASIC_INFORMATION_CLASS ||
            f->rcx < sizeof(NT_PROCESS_BASIC_INFORMATION)) {
            f->rax = NtStatusResult(
                (NT_HANDLE)f->rdi != NT_CURRENT_PROCESS
                    ? NT_STATUS_INVALID_HANDLE
                    : (f->rsi != NT_PROCESS_BASIC_INFORMATION_CLASS
                        ? NT_STATUS_INVALID_PARAMETER
                        : NT_STATUS_INFO_LENGTH_MISMATCH));
            return f;
        }

        if (!MmValidateUserRange(MmCurrentProcess, f->rdx,
                                 sizeof(NT_PROCESS_BASIC_INFORMATION), 1) ||
            (f->r8 && !MmValidateUserRange(MmCurrentProcess, f->r8,
                                           sizeof(NT_ULONG), 1))) {
            f->rax = NtStatusResult(NT_STATUS_ACCESS_VIOLATION);
            return f;
        }

        NT_PROCESS_BASIC_INFORMATION info;
        info.ExitStatus = NT_STATUS_SUCCESS;
        info.PebBaseAddress = 0;
        info.AffinityMask = 1;
        info.BasePriority = 8;
        info.UniqueProcessId = MmCurrentProcess ? MmCurrentProcess->Pid : 0;
        info.InheritedFromUniqueProcessId = 0;

        int me = up_find_proc(MmCurrentProcess);
        if (me >= 0) {
            int parent = up_tab[me].parent;
            if (parent >= 0 && parent < UP_MAX &&
                up_tab[parent].alive && up_tab[parent].proc)
                info.InheritedFromUniqueProcessId = up_tab[parent].proc->Pid;
        }

        *(NT_PROCESS_BASIC_INFORMATION *)f->rdx = info;
        if (f->r8) *(NT_ULONG *)f->r8 = sizeof info;
        f->rax = NtStatusResult(NT_STATUS_SUCCESS);
        return f;
    }

    case NT_SYS_ALLOCATE_VIRTUAL_MEMORY: {
        if ((NT_HANDLE)f->rdi != NT_CURRENT_PROCESS) {
            f->rax = NtStatusResult(NT_STATUS_INVALID_HANDLE);
            return f;
        }
        if (f->rdx != 0) {
            f->rax = NtStatusResult(NT_STATUS_INVALID_PARAMETER);
            return f;
        }
        if (!f->rsi || !f->rcx ||
            !MmValidateUserRange(MmCurrentProcess, f->rsi, sizeof(uint64_t), 1) ||
            !MmValidateUserRange(MmCurrentProcess, f->rcx, sizeof(uint64_t), 1)) {
            f->rax = NtStatusResult(NT_STATUS_ACCESS_VIOLATION);
            return f;
        }

        uint64_t base = *(uint64_t *)f->rsi;
        uint64_t requested = *(uint64_t *)f->rcx;
        uint64_t alloc_type = f->r8;
        uint64_t protect = f->r9;
        if (!requested || requested > UINT64_MAX - 0xFFFULL ||
            requested > 16ull * 1024 * 1024 ||
            (alloc_type & ~(NT_MEM_RESERVE | NT_MEM_COMMIT)) != 0 ||
            (alloc_type & (NT_MEM_RESERVE | NT_MEM_COMMIT)) !=
                (NT_MEM_RESERVE | NT_MEM_COMMIT) ||
            protect != NT_PAGE_READWRITE) {
            f->rax = NtStatusResult(NT_STATUS_INVALID_PARAMETER);
            return f;
        }

        uint64_t size = (requested + 0xFFFULL) & ~0xFFFULL;
        if (!base) base = 0x50000000ULL;
        if ((base & 0xFFFULL) || base < 0x10000ULL ||
            base >= 0x0000800000000000ULL ||
            size > 0x0000800000000000ULL - base) {
            f->rax = NtStatusResult(NT_STATUS_INVALID_ADDRESS);
            return f;
        }

        KPROCESS *caller = MmCurrentProcess;
        if (!caller || caller == PspSystemProcess) {
            f->rax = NtStatusResult(NT_STATUS_INVALID_HANDLE);
            return f;
        }

        MmSwitchToProcess(PspSystemProcess);
        if (!MmUserRangeUnmapped(caller, base, size)) {
            MmSwitchToProcess(caller);
            f->rax = NtStatusResult(NT_STATUS_INVALID_ADDRESS);
            return f;
        }

        uint64_t mapped = 0;
        for (; mapped < size; mapped += USER_IMAGE_PAGE) {
            if (!MmMapAllocatedUserPage(caller, base + mapped)) break;
        }
        if (mapped != size) {
            while (mapped) {
                mapped -= USER_IMAGE_PAGE;
                MmUnmapAllocatedUserPage(caller, base + mapped);
            }
            MmSwitchToProcess(caller);
            f->rax = NtStatusResult(NT_STATUS_NO_MEMORY);
            return f;
        }

        MmSwitchToProcess(caller);
        *(uint64_t *)f->rsi = base;
        *(uint64_t *)f->rcx = size;
        f->rax = NtStatusResult(NT_STATUS_SUCCESS);
        return f;
    }

    case NT_SYS_YIELD_EXECUTION: {
        int me = up_find_proc(MmCurrentProcess);
        if (me >= 0) {
            up_save(f, me);
            up_tab[me].rax = 0;
        }
        int next = up_pick_next();
        if (next >= 0 && next != me) {
            up_load(f, next);
            return f;
        }
        f->rax = NtStatusResult(NT_STATUS_SUCCESS);
        return f;
    }

    case NT_SYS_QUERY_VIRTUAL_MEMORY: {
        if ((NT_HANDLE)f->rdi != NT_CURRENT_PROCESS) {
            f->rax = NtStatusResult(NT_STATUS_INVALID_HANDLE);
            return f;
        }
        if (f->rdx != NT_MEMORY_BASIC_INFORMATION_CLASS ||
            f->r8 < sizeof(NT_MEMORY_BASIC_INFORMATION)) {
            f->rax = NtStatusResult(
                f->rdx != NT_MEMORY_BASIC_INFORMATION_CLASS
                    ? NT_STATUS_INVALID_PARAMETER
                    : NT_STATUS_INFO_LENGTH_MISMATCH);
            return f;
        }
        if (!MmValidateUserRange(MmCurrentProcess, f->rcx,
                                 sizeof(NT_MEMORY_BASIC_INFORMATION), 1) ||
            (f->r9 && !MmValidateUserRange(MmCurrentProcess, f->r9,
                                           sizeof(NT_ULONG), 1))) {
            f->rax = NtStatusResult(NT_STATUS_ACCESS_VIOLATION);
            return f;
        }

        uint64_t va = f->rsi;
        uint64_t pte = MmLookupUserPte(MmCurrentProcess, va, 0);
        if (!pte) {
            f->rax = NtStatusResult(NT_STATUS_INVALID_ADDRESS);
            return f;
        }

        NT_MEMORY_BASIC_INFORMATION info;
        info.BaseAddress = (NT_PVOID)(va & ~0xFFFULL);
        info.AllocationBase = info.BaseAddress;
        info.AllocationProtect = NT_PAGE_READWRITE;
        info.PartitionId = 0;
        info.RegionSize = 0x1000;
        info.State = NT_MEM_COMMITTED;
        info.Protect = (pte & PTE_RW) ? NT_PAGE_READWRITE : NT_PAGE_READONLY;
        info.Type = NT_MEM_PRIVATE;
        *(NT_MEMORY_BASIC_INFORMATION *)f->rcx = info;
        if (f->r9) *(NT_ULONG *)f->r9 = sizeof info;
        f->rax = NtStatusResult(NT_STATUS_SUCCESS);
        return f;
    }

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

            /* No thread may resume in an address space after its exit. */
            MmSwitchToProcess(PspSystemProcess);
            if (ki_current) ki_current->Process = PspSystemProcess;
            for (int i = 0; i < UT_MAX; i++) {
                if (ut_tab[i].proc == dying) {
                    ut_tab[i].alive = 0;
                    ut_tab[i].proc = 0;
                }
            }
            PspForgetUserImages(dying);
            MmDeleteAddressSpace(dying);
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

        if (len <= 4096 &&
            MmValidateUserRange(MmCurrentProcess, uaddr, len, 0)) {
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
        KPROCESS *caller = MmCurrentProcess;
        uint64_t prefer = f->rdi;
        uint64_t requested = f->rsi;

        if (!caller || caller == PspSystemProcess ||
            requested > UINT64_MAX - 0xFFFULL) {
            f->rax = 0;
            return f;
        }

        uint64_t size = (requested + 0xFFFULL) & ~0xFFFULL;
        if (!size || size > 16ull * 1024 * 1024) {
            f->rax = 0;
            return f;
        }

        if (!prefer) prefer = 0x50000000ULL;
        if ((prefer & 0xFFFULL) || prefer < 0x10000ULL ||
            prefer >= 0x0000800000000000ULL ||
            size > 0x0000800000000000ULL - prefer) {
            f->rax = 0;
            return f;
        }

        /* Validate and allocate while the system CR3 provides the physical
           identity map used by the PMM. Never replace an existing mapping. */
        MmSwitchToProcess(PspSystemProcess);
        if (!MmUserRangeUnmapped(caller, prefer, size)) {
            MmSwitchToProcess(caller);
            f->rax = 0;
            return f;
        }

        uint64_t mapped = 0;
        for (; mapped < size; mapped += USER_IMAGE_PAGE) {
            if (!MmMapAllocatedUserPage(caller, prefer + mapped)) break;
        }

        if (mapped != size) {
            while (mapped) {
                mapped -= USER_IMAGE_PAGE;
                MmUnmapAllocatedUserPage(caller, prefer + mapped);
            }
            MmSwitchToProcess(caller);
            f->rax = 0;
            return f;
        }

        MmSwitchToProcess(caller);
        f->rax = prefer;
        return f;
    }

    case SYS_NtCreateProcess: {
        char name[128];

        if (copy_user_str(f->rdi, name, sizeof name) != 0) {
            f->rax = (uint64_t)-1LL;
            return f;
        }

        KPROCESS *caller = MmCurrentProcess;

        /* Kernel allocation and page-table work under the system CR3. */
        MmSwitchToProcess(PspSystemProcess);

        BootModule *mod = PspFindModule(user_bootinfo, name);
        if (!mod) mod = PspLoadDiskPe(name);

        if (!mod) {
            MmSwitchToProcess(caller);
            f->rax = (uint64_t)-1LL;
            return f;
        }

        KPROCESS *proc = MmCreateAddressSpace(name);

        if (!proc) {
            MmSwitchToProcess(caller);
            f->rax = (uint64_t)-1LL;
            return f;
        }
        if (PspMapUserImage(proc, mod) != 0) {
            PspForgetUserImages(proc);
            MmDeleteAddressSpace(proc);
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
                    PspForgetUserImages(proc);
                    MmDeleteAddressSpace(proc);
                    MmSwitchToProcess(caller);
                    f->rax = (uint64_t)-1LL;
                    return f;
                }

                map_4k_in(proc->Pml4,
                          stack_lo + off, pa, PTE_U | PTE_RW | PTE_OWNED);
            }
        }

        int caller_slot = up_find_free_slot();
        if (up_find_proc(caller) < 0 && caller_slot >= 0) {
            up_tab[caller_slot].parent = -1;
            up_tab[caller_slot].proc = caller;
            up_tab[caller_slot].entry = f->rip;
            up_tab[caller_slot].alive = 1;
            up_tab[caller_slot].fresh = 0;
            up_tab[caller_slot].rip = f->rip;
            up_tab[caller_slot].rsp = f->rsp;
            up_tab[caller_slot].rflags = f->rflags ? f->rflags : 0x202;
            up_tab[caller_slot].rax = proc->Pid;
            up_snap(f, &up_tab[caller_slot]);
            up_n++;
        } else {
            int me = up_find_proc(caller);

            if (me >= 0) {
                up_save(f, me);
                up_tab[me].rax = proc->Pid;
            }
        }

        int child_slot = up_find_free_slot();
        if (child_slot >= 0) {
            up_tab[child_slot].parent = up_find_proc(caller);
            up_tab[child_slot].proc = proc;
            up_tab[child_slot].entry = entry;
            up_tab[child_slot].alive = 1;
            up_tab[child_slot].fresh = 1;
            up_tab[child_slot].rip = entry;
            up_tab[child_slot].rsp = USER_STACK_TOP;
            up_tab[child_slot].rflags = 0x202;
            up_tab[child_slot].rax = 0;
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
        char name[128];if(copy_user_str(f->rdi,name,sizeof name)!=0||!g_ext4_ok){f->rax=(uint64_t)-1LL;return f;}
        f->rax=(uint64_t)(int64_t)vfs_open_read(name);return f;
    }
    case SYS_NtReadFile: {
        int handle=(int)f->rdi;uint64_t buffer=f->rsi;uint32_t length=(uint32_t)f->rdx;
        if(!buffer||!length||length>4096||!MmValidateUserRange(MmCurrentProcess,buffer,length,1)){f->rax=(uint64_t)-1LL;return f;}
        uint8_t bounce[4096];int64_t got=vfs_read_open(handle,bounce,length);
        if(got>0)for(int64_t i=0;i<got;i++)((uint8_t*)buffer)[i]=bounce[i];f->rax=(uint64_t)got;return f;
    }
    case SYS_NtCloseFile:f->rax=(uint64_t)(int64_t)vfs_close_open((int)f->rdi);return f;
    case SYS_NtQueryProcesses: {
        uint64_t out=f->rdi,cap=f->rsi;
        if(!out||cap<sizeof(SENTINEL_PROCESS_INFORMATION)||!MmValidateUserRange(MmCurrentProcess,out,cap>UP_MAX*sizeof(SENTINEL_PROCESS_INFORMATION)?UP_MAX*sizeof(SENTINEL_PROCESS_INFORMATION):cap,1)){f->rax=(uint64_t)-1LL;return f;}
        uint64_t count=0,max=cap/sizeof(SENTINEL_PROCESS_INFORMATION);if(max>UP_MAX)max=UP_MAX;
        for(int i=0;i<UP_MAX&&count<max;i++)if(up_tab[i].alive&&up_tab[i].proc){
            SENTINEL_PROCESS_INFORMATION item;item.ProcessId=up_tab[i].proc->Pid;item.ParentProcessId=0;
            if(up_tab[i].parent>=0&&up_tab[i].parent<UP_MAX&&up_tab[up_tab[i].parent].alive&&up_tab[up_tab[i].parent].proc)item.ParentProcessId=up_tab[up_tab[i].parent].proc->Pid;
            item.State=up_tab[i].fresh?1u:2u;int j=0;const char*n=up_tab[i].proc->ImageName;
            while(j<31&&n&&n[j]){item.ImageName[j]=n[j];j++;}item.ImageName[j]=0;
            *(SENTINEL_PROCESS_INFORMATION*)(out+count*sizeof item)=item;count++;
        }f->rax=count;return f;
    }
    case SYS_NtTerminatePid: {
        KPROCESS *caller=MmCurrentProcess;
        uint64_t pid=f->rdi;KPROCESS*target=0;int idx=-1;
        for(int i=0;i<UP_MAX;i++)if(up_tab[i].alive&&up_tab[i].proc&&up_tab[i].proc->Pid==pid){target=up_tab[i].proc;idx=i;break;}
        if(!target||target==MmCurrentProcess||target==PspSystemProcess){f->rax=(uint64_t)-1LL;return f;}
        for(int i=0;i<UP_MAX;i++)if(up_tab[i].parent==idx)up_tab[i].parent=-1;
        up_tab[idx].alive=0;up_tab[idx].proc=0;wm_unregister((int)pid);
        MmSwitchToProcess(PspSystemProcess);if(ki_current)ki_current->Process=PspSystemProcess;
        for(int i=0;i<UT_MAX;i++)if(ut_tab[i].proc==target){ut_tab[i].alive=0;ut_tab[i].proc=0;}
        PspForgetUserImages(target);MmDeleteAddressSpace(target);MmSwitchToProcess(caller);f->rax=0;return f;
    }
    case SYS_NtUdpSend: {
        char payload[NS_UDP_RX_MAX];uint32_t len=(uint32_t)f->r8;
        if(!len||len>sizeof payload||!MmValidateUserRange(MmCurrentProcess,f->rcx,len,0)){f->rax=(uint64_t)-1LL;return f;}
        for(uint32_t i=0;i<len;i++)payload[i]=((const char*)f->rcx)[i];
        f->rax=(uint64_t)(int64_t)ns_udp_send_app((uint32_t)f->rdi,(uint16_t)f->rsi,(uint16_t)f->rdx,(const uint8_t*)payload,(uint16_t)len);return f;
    }
    case SYS_NtUdpBind:f->rax=(uint64_t)(int64_t)ns_udp_bind((uint16_t)f->rdi);return f;
    case SYS_NtUdpReceive: {
        uint64_t dp=f->rdi,cap=f->rsi,iptr=f->rdx,pptr=f->rcx;
        if(!dp||!cap||cap>NS_UDP_RX_MAX||!MmValidateUserRange(MmCurrentProcess,dp,cap,1)||(iptr&&!MmValidateUserRange(MmCurrentProcess,iptr,sizeof(uint32_t),1))||(pptr&&!MmValidateUserRange(MmCurrentProcess,pptr,sizeof(uint16_t),1))){f->rax=(uint64_t)-1LL;return f;}
        uint8_t data[NS_UDP_RX_MAX];uint32_t ip=0;uint16_t port=0;int n=ns_udp_recv_app(&ip,&port,data,(uint16_t)cap);
        if(n>=0){for(int i=0;i<n;i++)((uint8_t*)dp)[i]=data[i];if(iptr)*(uint32_t*)iptr=ip;if(pptr)*(uint16_t*)pptr=port;}
        f->rax=(uint64_t)(int64_t)n;return f;
    }
    case SYS_NtGetTickCount:f->rax=KeTickCount*10ULL;return f;

    case SYS_NtQueryDirectoryFile: {
        uint32_t index = (uint32_t)f->rdi;
        char *name = (char *)f->rsi;

        if (!name || !MmValidateUserRange(MmCurrentProcess, (uint64_t)name, 64, 1)) {
            f->rax = 0;
            return f;
        }

        if (g_ext4_ok) {
            /* Never mix boot modules into a mounted filesystem directory listing. */
            if (index < (uint32_t)vfs_nent) {
                const char *n = vfs_ents[index].name;
                int i = 0;

                for (; n[i] && i < 63; i++)
                    name[i] = n[i];

                name[i] = 0;
                f->rax = vfs_ents[index].is_dir ? 2 : 1;
            } else {
                f->rax = 0;
            }
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
        uint64_t uva = 0x70000000ULL;
        uint64_t bytes = (uint64_t)user_fb_pitch * user_fb_h;
        uint64_t pages = (bytes + 0xFFFULL) >> 12;
        if (!cur || cur == PspSystemProcess || !pages ||
            !MmValidateUserRange(cur, f->rdi, 20, 1) ||
            !MmUserRangeUnmapped(cur, uva, pages << 12)) {
            f->rax = 0;
            return f;
        }

        MmSwitchToProcess(PspSystemProcess);
        for (uint64_t i = 0; i < pages; i++) {
            map_4k_in(cur->Pml4,
                      uva + (i << 12),
                      (user_fb_base + (i << 12)) & ~0xFFFULL,
                      PTE_U | PTE_RW);
        }
        MmSwitchToProcess(cur);

        *(uint64_t *)f->rdi = uva;
        *(uint32_t *)(f->rdi + 8) = user_fb_w;
        *(uint32_t *)(f->rdi + 12) = user_fb_h;
        *(uint32_t *)(f->rdi + 16) = user_fb_pitch;

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

        static UIn pending_mouse;
        static int pending_mouse_valid;
        static int pending_mouse_pid;
        static UIn pending_key;
        static int pending_key_valid;
        static int pending_key_pid;

        int mepid = MmCurrentProcess ? (int)MmCurrentProcess->Pid : 0;

        if (!out || !MmValidateUserRange(MmCurrentProcess, (uint64_t)out, sizeof *out, 1)) {
            f->rax = 0;
            return f;
        }

        /* Deliver queued keyboard input only to the frontmost window. */
        if (pending_key_valid) {
            int target = wm_keyboard_target();
            if (target > 0) pending_key_pid = target;
            if (pending_key_pid == mepid) {
                *out = pending_key;
                pending_key_valid = 0;
                f->rax = 1;
                return f;
            }
            goto no_input_event;
        }

        /* Mouse motion and clicks belong only to the window under the pointer. */
        if (pending_mouse_valid) {
            if (pending_mouse_pid == mepid) {
                *out = pending_mouse;
                pending_mouse_valid = 0;
                f->rax = 1;
                return f;
            }

            goto no_input_event;
        }

        ps2_poll();

        if (vi_ok)
            virtio_input_poll();

        /* PS/2 wheel events remain available even when a virtio tablet owns XY. */
        if (mouse_wheel) {
            int wheel = mouse_wheel;
            mouse_wheel = 0;
            out->type = 4; /* mouse wheel */
            out->x = vi_ok ? vi_x : mouse_x;
            out->y = vi_ok ? vi_y : mouse_y;
            out->btn = vi_ok ? vi_btn : mouse_btn;
            out->key = (char)wheel;
            f->rax = 1;
            return f;
        }

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

            {
                int target = wm_hit_test(out->x, out->y);
                if (target <= 0) target = mepid;

                pending_mouse = *out;
                pending_mouse_pid = target;
                pending_mouse_valid = 1;

                if (target != mepid)
                    goto no_input_event;

                pending_mouse_valid = 0;
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

            int target = wm_keyboard_target();
            if (target > 0 && target != mepid) {
                pending_key = *out;
                pending_key_pid = target;
                pending_key_valid = 1;
                goto no_input_event;
            }

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

                {
                    int target = wm_hit_test(out->x, out->y);
                    if (target <= 0) target = mepid;

                    pending_mouse = *out;
                    pending_mouse_pid = target;
                    pending_mouse_valid = 1;

                    if (target != mepid)
                        goto no_input_event;

                    pending_mouse_valid = 0;
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

        if (f->rsi && !MmValidateUserRange(MmCurrentProcess, f->rsi, sizeof tmp, 1)) {
            f->rax = 0;
            return f;
        }
        if (f->rdx && !MmValidateUserRange(MmCurrentProcess, f->rdx, sizeof minv, 1)) {
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

        if (!src || !MmValidateUserRange(MmCurrentProcess, (uint64_t)src, sizeof rect, 0)) {
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

    case 22: { /* NtQueryNetworkConfig: SentinelOS extension */
        if (!f->rdi ||
            !MmValidateUserRange(MmCurrentProcess, f->rdi,
                                 sizeof(SENTINELOS_NET_CONFIG), 1)) {
            f->rax = (uint64_t)-1LL;
            return f;
        }
        SENTINELOS_NET_CONFIG cfg;
        ns_query_config(&cfg);
        *(SENTINELOS_NET_CONFIG *)f->rdi = cfg;
        f->rax = 0;
        return f;
    }

    case 23: /* Poll network RX and dispatch protocol packets. */
        f->rax = (uint64_t)ns_poll();
        return f;

    case 24: /* DHCPv4 discover/request; bounded polling. */
        f->rax = (uint64_t)(int64_t)ns_dhcp();
        return f;

    case 25: /* ICMP echo request; IPv4 address is in network byte order. */
        f->rax = (uint64_t)(int64_t)ns_ping((uint32_t)f->rdi);
        return f;

    case 26: { /* DNS A lookup: hostname pointer, uint32_t answer pointer. */
        char hostname[128];
        if (copy_user_str(f->rdi, hostname, sizeof hostname) != 0 ||
            !f->rsi || !MmValidateUserRange(MmCurrentProcess, f->rsi, sizeof(uint32_t), 1)) {
            f->rax = (uint64_t)-1LL;
            return f;
        }
        uint32_t answer = 0;
        int r = ns_dns_query(hostname, &answer);
        if (!r) *(uint32_t *)f->rsi = answer;
        f->rax = (uint64_t)(int64_t)r;
        return f;
    }

    case 27: /* TCP active-open handshake: IPv4 in RDI, port in RSI. */
        f->rax = (uint64_t)(int64_t)ns_tcp_connect((uint32_t)f->rdi,
                                                    (uint16_t)f->rsi);
        return f;

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
        if (f->rax >= 0x1000 && f->rax <= 0x10FF)
            f->rax = NtStatusResult(NT_STATUS_INVALID_SYSTEM_SERVICE);
        else
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
                if (ki_current) ki_current->Process = PspSystemProcess;
                PspForgetUserImages(proc);
                MmDeleteAddressSpace(proc);
                return;
            }
            map_4k_in(proc->Pml4,
                      stack_lo + off, pa, PTE_U | PTE_RW | PTE_OWNED);
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