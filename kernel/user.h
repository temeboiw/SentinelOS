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

#define SYS_NtTerminateProcess      0
#define SYS_NtWriteFile             1
#define SYS_NtGetCurrentProcessId   2
#define SYS_NtAllocateVirtualMemory 3
#define SYS_NtCreateProcess         4
#define SYS_NtOpenFile              5
#define SYS_NtWriteExistingFile     23
#define SYS_NtSeekFile               24
#define SYS_NtCreateDirectory        25
#define SYS_NtDeleteFile             26
#define SYS_NtRemoveDirectory        27
#define SYS_NtPathType                28
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

typedef struct {
    BootModule module;
    KPROCESS *owner;
    uint64_t source_phys;
    uint64_t source_pages;
    int used;
} UserDiskModule;
static UserDiskModule user_disk_modules[UP_MAX];


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


/* Read-only VFS file handles used by the initial Win32 file APIs. */
#define VFS_USER_HANDLES 32
#define SYS_NtCloseFile 22
typedef struct {
    uint32_t ino;
    uint64_t pos, size;
    int used;
    int writable;
} VfsUserHandle;
static VfsUserHandle vfs_user_handles[VFS_USER_HANDLES];

/* Path lookup for file handles. This is deliberately separate from the
 * interactive folder listing/navigation state (vfs_ents/vfs_depth). */
static int e4_name_equal_ci(const uint8_t *a, uint8_t alen, const char *b) {
    uint32_t blen = 0;
    while (b[blen] && blen < 256) blen++;
    if (alen != blen) return 0;
    for (uint32_t i = 0; i < alen; i++) {
        uint8_t x = a[i], y = (uint8_t)b[i];
        if (x >= 'A' && x <= 'Z') x = (uint8_t)(x + ('a' - 'A'));
        if (y >= 'A' && y <= 'Z') y = (uint8_t)(y + ('a' - 'A'));
        if (x != y) return 0;
    }
    return 1;
}

static uint32_t e4_lookup_ci(Ext4k *fs, uint32_t dir, const char *name) {
    uint8_t inode[256];
    if (!name || !name[0] || e4_inode(fs, dir, inode) < 0) return 0;
    uint64_t size = e4_size(inode);
    static uint8_t buf[4096];
    for (uint64_t pos = 0; pos < size; pos += fs->blksz) {
        uint64_t pb = e4_map(fs, inode, (uint32_t)(pos / fs->blksz));
        if (!pb || e4_read(fs, pb * fs->blksz, buf, fs->blksz) < 0) continue;
        for (uint32_t o = 0; o + 8 <= fs->blksz; ) {
            uint16_t rl = E16(buf, o + 4);
            if (rl < 8 || o + rl > fs->blksz) break;
            uint32_t ino = E32(buf, o);
            uint8_t nl = buf[o + 6];
            if (ino && nl && nl <= 255 && o + 8u + nl <= fs->blksz &&
                e4_name_equal_ci(buf + o + 8, nl, name)) return ino;
            o += rl;
        }
    }
    return 0;
}

static uint32_t vfs_resolve_user_path(const char *path) {
    if (!g_ext4_ok || !path || !path[0]) return 0;
    uint32_t dir = vfs_cwd_ino ? vfs_cwd_ino : 2;
    const char *p = path;
    /* Accept Win32 drive-rooted paths and either slash style. */
    if (p[0] && p[1] == ':' && (p[0] == 'C' || p[0] == 'c')) {
        p += 2;
        dir = 2;
    }
    if (*p == '/' || *p == '\\') {
        dir = 2;
        while (*p == '/' || *p == '\\') p++;
    }
    while (*p) {
        char part[256];
        uint32_t n = 0;
        while (*p && *p != '/' && *p != '\\') {
            if (n + 1 >= sizeof part) return 0;
            part[n++] = *p++;
        }
        while (*p == '/' || *p == '\\') p++;
        if (!n) continue;
        part[n] = 0;
        if (n == 1 && part[0] == '.') continue;
        uint32_t next = e4_lookup_ci(&g_ext4, dir, part);
        if (!next) return 0;
        uint8_t inode[256];
        if (e4_inode(&g_ext4, next, inode) < 0) return 0;
        if (*p) {
            if ((E16(inode, 0) & 0xF000) != 0x4000) return 0;
            dir = next;
        } else {
            return next;
        }
    }
    return dir;
}

static int e4_dir_has_space(uint32_t dir_ino, uint32_t name_len) {
    if (!name_len || name_len > 255) return 0;
    uint8_t inode[256];
    if (e4_inode(&g_ext4,dir_ino,inode)<0 ||
        (E16(inode,0)&0xF000)!=0x4000 ||
        (E32(inode,32)&0x1000u)) return 0;
    uint64_t size=e4_size(inode);
    uint32_t need=(8u+name_len+3u)&~3u;
    static uint8_t block[4096];
    for(uint64_t pos=0;pos<size;pos+=g_ext4.blksz) {
        uint64_t pb=e4_map(&g_ext4,inode,(uint32_t)(pos/g_ext4.blksz));
        if(!pb || e4_read(&g_ext4,pb*g_ext4.blksz,block,g_ext4.blksz)<0) continue;
        for(uint32_t o=0;o+8<=g_ext4.blksz;) {
            uint16_t rec=E16(block,o+4);
            uint32_t eino=E32(block,o);
            uint8_t nl=block[o+6];
            if(rec<8 || (rec&3) || o+rec>g_ext4.blksz || nl>rec-8) break;
            if((!eino && rec>=need) ||
               (eino && rec-((8u+nl+3u)&~3u)>=need)) return 1;
            o+=rec;
        }
    }
    return 0;
}

/* Directory growth is isolated from the directory-browser state. It only
 * supports an unindexed directory whose extent tree fits in the inode root. */
static int e4_dir_can_grow(uint32_t dir_ino) {
    uint8_t inode[256];
    if (!e4_can_mutate(&g_ext4) ||
        e4_inode(&g_ext4, dir_ino, inode) < 0 ||
        (E16(inode, 0) & 0xF000) != 0x4000 ||
        (E32(inode, 32) & 0x1000u)) return 0;
    uint8_t *root = inode + 40;
    if (E16(root, 0) != 0xF30A || E16(root, 6) != 0) return 0;
    uint16_t n = E16(root, 2), max = E16(root, 4);
    return max <= 4 && n <= max && n < max &&
           e4_size(inode) <= 0xFFFFFFFFULL - g_ext4.blksz &&
           E32(inode, 28) <= 0xFFFFFFFFu - g_ext4.blksz / 512u;
}

static int e4_grow_dir_one_block(uint32_t dir_ino) {
    uint8_t inode[256];
    if (!e4_dir_can_grow(dir_ino) ||
        e4_inode(&g_ext4, dir_ino, inode) < 0) return -1;
    uint64_t old_size = e4_size(inode);
    if (old_size % g_ext4.blksz) return -1;
    uint32_t logical = (uint32_t)(old_size / g_ext4.blksz);
    if (e4_map(&g_ext4, inode, logical)) return -1;
    uint64_t physical = e4_alloc_block(&g_ext4);
    if (!physical) return -1;
    static uint8_t block[4096];
    for (uint32_t i = 0; i < g_ext4.blksz; ++i) block[i] = 0;
    E16(block, 4) = (uint16_t)g_ext4.blksz; /* one reusable free dirent */
    if (e4_write(&g_ext4, physical * g_ext4.blksz, block, g_ext4.blksz) < 0) {
        (void)e4_free_block(&g_ext4, physical);
        return -1;
    }
    if (e4_add_inline_extent(&g_ext4, inode, logical, physical) < 0) {
        (void)e4_free_block(&g_ext4, physical);
        return -1;
    }
    E32(inode, 4) = (uint32_t)(old_size + g_ext4.blksz);
    E32(inode, 28) += g_ext4.blksz / 512u;
    if (e4_write_inode(&g_ext4, dir_ino, inode) < 0) return -1;
    return 0;
}

static int e4_insert_dirent(uint32_t dir_ino, const char *name, uint32_t ino, uint8_t file_type) {
    if (!e4_can_mutate(&g_ext4) || !name || !name[0]) return -1;
    uint32_t nl = 0;
    while (name[nl] && nl < 256) nl++;
    if (!nl || nl > 255 || e4_lookup_ci(&g_ext4, dir_ino, name)) return -1;
    uint8_t dinode[256];
    if (e4_inode(&g_ext4, dir_ino, dinode) < 0 ||
        (E16(dinode, 0) & 0xF000) != 0x4000 ||
        (E32(dinode, 32) & 0x00001000u)) return -1; /* indexed dirs need htree updates */
    uint64_t dsize = e4_size(dinode);
    static uint8_t block[4096];
    for (uint64_t pos = 0; pos < dsize; pos += g_ext4.blksz) {
        uint64_t pb = e4_map(&g_ext4, dinode, (uint32_t)(pos / g_ext4.blksz));
        if (!pb || e4_read(&g_ext4, pb * g_ext4.blksz, block, g_ext4.blksz) < 0) continue;
        for (uint32_t o = 0; o + 8 <= g_ext4.blksz; ) {
            uint32_t entry_ino = E32(block, o);
            uint16_t rec = E16(block, o + 4);
            uint8_t oldnl = block[o + 6];
            if (rec < 8 || (rec & 3) || o + rec > g_ext4.blksz ||
                oldnl > rec - 8) break;
            uint32_t need = (8u + nl + 3u) & ~3u;
            uint32_t slot = 0, at = o;
            if (!entry_ino && rec >= need) {
                slot = rec;
            } else {
                uint32_t used = (8u + oldnl + 3u) & ~3u;
                if (entry_ino && rec >= used && rec - used >= need) {
                    E16(block, o + 4) = (uint16_t)used;
                    at = o + used;
                    slot = rec - used;
                }
            }
            if (slot) {
                for (uint32_t j = 0; j < slot; ++j) block[at + j] = 0;
                E32(block, at) = ino;
                E16(block, at + 4) = (uint16_t)slot;
                block[at + 6] = (uint8_t)nl;
                block[at + 7] = file_type;
                for (uint32_t j = 0; j < nl; ++j) block[at + 8 + j] = (uint8_t)name[j];
                if (e4_write(&g_ext4, pb * g_ext4.blksz, block, g_ext4.blksz) < 0)
                    return -1;
                return 0;
            }
            o += rec;
        }
    }
    /* If all existing blocks are full, grow only a simple inline-extent
       directory. Refuse indexed or external extent trees rather than corrupt them. */
    if (e4_grow_dir_one_block(dir_ino) < 0) return -1;
    return e4_insert_dirent(dir_ino, name, ino, file_type);
}

static uint32_t vfs_create_empty_regular(const char *path) {
    if (!g_ext4_ok || !e4_can_mutate(&g_ext4) || !path || !path[0]) return 0;
    char full[256];
    uint32_t len = 0;
    while (path[len] && len + 1 < sizeof full) { full[len] = path[len]; len++; }
    if (path[len] || !len || full[len-1]=='/' || full[len-1]=='\\') return 0;
    while (len > 1 && (full[len - 1] == '/' || full[len - 1] == '\\')) len--;
    full[len] = 0;
    int last_sep = -1;
    for (uint32_t i = 0; i < len; ++i)
        if (full[i] == '/' || full[i] == '\\') last_sep = (int)i;
    char base[256], parent[256];
    uint32_t bn = 0, pn = 0;
    uint32_t start = (uint32_t)(last_sep + 1);
    if (start >= len) return 0;
    bn = len - start;
    if (!bn || bn > 255) return 0;
    for (uint32_t i = 0; i < bn; ++i) base[i] = full[start + i];
    base[bn] = 0;
    if ((bn == 1 && base[0] == '.') ||
        (bn == 2 && base[0] == '.' && base[1] == '.')) return 0;
    uint32_t parent_ino = vfs_cwd_ino ? vfs_cwd_ino : 2;
    if (last_sep >= 0) {
        /* Keep a drive prefix such as C: intact for the path resolver. */
        pn = (uint32_t)last_sep;
        if (pn == 0) { parent[0] = '/'; parent[1] = 0; }
        else {
            for (uint32_t i = 0; i < pn; ++i) parent[i] = full[i];
            parent[pn] = 0;
            if (pn == 2 && full[1] == ':') parent[pn] = 0;
        }
        parent_ino = vfs_resolve_user_path(parent);
        if (!parent_ino) return 0;
    }
    uint8_t pino[256];
    if (e4_inode(&g_ext4, parent_ino, pino) < 0 ||
        (E16(pino, 0) & 0xF000) != 0x4000 ||
        (E32(pino,32) & 0x1000u) ||
        e4_lookup_ci(&g_ext4, parent_ino, base)) return 0;

    /* Preflight directory slack before changing allocation metadata. */
    int has_space = 0;
    uint64_t dsize = e4_size(pino);
    static uint8_t dbuf[4096];
    uint32_t need = (8u + bn + 3u) & ~3u;
    for (uint64_t pos = 0; pos < dsize && !has_space; pos += g_ext4.blksz) {
        uint64_t pb = e4_map(&g_ext4, pino, (uint32_t)(pos / g_ext4.blksz));
        if (!pb || e4_read(&g_ext4, pb * g_ext4.blksz, dbuf, g_ext4.blksz) < 0) continue;
        for (uint32_t o = 0; o + 8 <= g_ext4.blksz; ) {
            uint16_t rec = E16(dbuf, o + 4);
            uint32_t eino = E32(dbuf, o);
            uint8_t en = dbuf[o + 6];
            if (rec < 8 || (rec & 3) || o + rec > g_ext4.blksz || en > rec - 8) break;
            if ((!eino && rec >= need) ||
                (eino && rec - ((8u + en + 3u) & ~3u) >= need)) {
                has_space = 1; break;
            }
            o += rec;
        }
    }
    if (!has_space && !e4_dir_can_grow(parent_ino)) return 0;

    uint32_t ino = e4_alloc_inode(&g_ext4, 0);
    if (!ino) return 0;
    uint8_t fresh[256];
    for (uint32_t i = 0; i < g_ext4.isize; ++i) fresh[i] = 0;
    E16(fresh, 0) = 0x81A4; /* regular file, mode 0644 */
    E16(fresh, 26) = 1;     /* link count */
    E32(fresh, 32) = 0x00080000; /* EXT4_EXTENTS_FL */
    E16(fresh, 40) = 0xF30A; /* empty extent tree */
    E16(fresh, 42) = 0;
    E16(fresh, 44) = (uint16_t)((60 - 12) / 12);
    E16(fresh, 46) = 0;
    if (g_ext4.isize >= 160) E16(fresh, 128) = 32;
    if (e4_write_inode(&g_ext4, ino, fresh) < 0) return 0;
    if (e4_insert_dirent(parent_ino, base, ino, 1) < 0) {
        /* Do not clear the inode bit after a possibly partial directory write:
           leaking an unreachable inode is safer than freeing a referenced one. */
        return 0;
    }
    return ino;
}

static uint32_t vfs_create_directory(const char *path) {
    if (!g_ext4_ok || !e4_can_mutate(&g_ext4) || !path || !path[0]) return 0;
    char full[256], base[256], parent[256];
    uint32_t len = 0;
    while (path[len] && len + 1 < sizeof full) { full[len] = path[len]; len++; }
    if (path[len]) return 0;
    while (len > 1 && (full[len-1] == '/' || full[len-1] == '\\')) len--;
    full[len] = 0;
    int sep = -1;
    for (uint32_t i = 0; i < len; ++i)
        if (full[i] == '/' || full[i] == '\\') sep = (int)i;
    uint32_t start = (uint32_t)(sep + 1);
    if (start >= len || len - start > 255) return 0;
    uint32_t bn = len - start;
    for (uint32_t i = 0; i < bn; ++i) base[i] = full[start+i];
    base[bn] = 0;
    if ((bn == 1 && base[0] == '.') ||
        (bn == 2 && base[0] == '.' && base[1] == '.') ||
        (bn == 0)) return 0;
    uint32_t parent_ino = vfs_cwd_ino ? vfs_cwd_ino : 2;
    if (sep >= 0) {
        if (sep == 0) { parent[0]='/'; parent[1]=0; }
        else {
            for (uint32_t i=0;i<(uint32_t)sep;i++) parent[i]=full[i];
            parent[sep]=0;
        }
        parent_ino = vfs_resolve_user_path(parent);
        if (!parent_ino) return 0;
    }
    uint8_t pino[256];
    if (e4_inode(&g_ext4, parent_ino, pino) < 0 ||
        (E16(pino,0) & 0xF000) != 0x4000 ||
        (E32(pino,32) & 0x1000u) ||
        E16(pino,26) == 0xFFFFu ||
        e4_lookup_ci(&g_ext4,parent_ino,base) ||
        (!e4_dir_has_space(parent_ino,bn) && !e4_dir_can_grow(parent_ino))) return 0;

    uint32_t ino = e4_alloc_inode(&g_ext4, 1);
    if (!ino) return 0;
    uint64_t pb = e4_alloc_block(&g_ext4);
    if (!pb) return 0;
    static uint8_t block[4096];
    for (uint32_t i=0;i<g_ext4.blksz;i++) block[i]=0;
    E32(block,0)=ino; E16(block,4)=12; block[6]=1; block[7]=2; block[8]='.';
    E32(block,12)=parent_ino; E16(block,16)=(uint16_t)(g_ext4.blksz-12);
    block[18]=2; block[19]=2; block[20]='.'; block[21]='.';
    if (e4_write(&g_ext4,pb*g_ext4.blksz,block,g_ext4.blksz)<0) return 0;

    uint8_t fresh[256];
    for (uint32_t i=0;i<g_ext4.isize;i++) fresh[i]=0;
    E16(fresh,0)=0x41ED; /* directory, mode 0755 */
    E32(fresh,4)=g_ext4.blksz;
    E16(fresh,26)=2;
    E32(fresh,28)=g_ext4.blksz/512;
    E32(fresh,32)=0x00080000;
    E16(fresh,40)=0xF30A; E16(fresh,42)=1; E16(fresh,44)=4; E16(fresh,46)=0;
    E32(fresh,52)=0; E16(fresh,56)=1;
    E16(fresh,58)=(uint16_t)(pb>>32); E32(fresh,60)=(uint32_t)pb;
    if (g_ext4.isize >= 160) E16(fresh,128)=32;
    if (e4_write_inode(&g_ext4,ino,fresh)<0) return 0;
    if (e4_insert_dirent(parent_ino,base,ino,2)<0) return 0;
    E16(pino,26)=(uint16_t)(E16(pino,26)+1);
    if (e4_write_inode(&g_ext4,parent_ino,pino)<0) return 0;
    return ino;
}

static int vfs_split_user_path(const char *path, uint32_t *parent_ino, char base[256]) {
    if (!path || !path[0] || !parent_ino) return 0;
    char full[256], parent[256];
    uint32_t len=0;
    while(path[len] && len+1<sizeof full){full[len]=path[len];len++;}
    if(path[len] || !len) return 0;
    while(len>1 && (full[len-1]=='/' || full[len-1]=='\\')) len--;
    full[len]=0;
    int sep=-1;
    for(uint32_t i=0;i<len;i++) if(full[i]=='/' || full[i]=='\\') sep=(int)i;
    uint32_t start=(uint32_t)(sep+1), n=len-start;
    if(!n || n>255) return 0;
    for(uint32_t i=0;i<n;i++) base[i]=full[start+i];
    base[n]=0;
    if((n==1 && base[0]=='.') || (n==2 && base[0]=='.' && base[1]=='.')) return 0;
    *parent_ino=vfs_cwd_ino?vfs_cwd_ino:2;
    if(sep>=0) {
        if(sep==0){parent[0]='/';parent[1]=0;}
        else {for(uint32_t i=0;i<(uint32_t)sep;i++)parent[i]=full[i];parent[sep]=0;}
        *parent_ino=vfs_resolve_user_path(parent);
        if(!*parent_ino) return 0;
    }
    return 1;
}

static int e4_remove_dirent(uint32_t parent_ino, const char *name, uint32_t target_ino) {
    uint8_t dir[256];
    if(e4_inode(&g_ext4,parent_ino,dir)<0 ||
       (E16(dir,0)&0xF000)!=0x4000 || (E32(dir,32)&0x1000u)) return -1;
    uint64_t size=e4_size(dir);
    static uint8_t block[4096];
    for(uint64_t pos=0;pos<size;pos+=g_ext4.blksz) {
        uint64_t pb=e4_map(&g_ext4,dir,(uint32_t)(pos/g_ext4.blksz));
        if(!pb || e4_read(&g_ext4,pb*g_ext4.blksz,block,g_ext4.blksz)<0) continue;
        for(uint32_t o=0;o+8<=g_ext4.blksz;) {
            uint16_t rec=E16(block,o+4); uint32_t ino=E32(block,o);
            uint8_t nl=block[o+6];
            if(rec<8 || (rec&3) || o+rec>g_ext4.blksz || nl>rec-8) break;
            if(ino==target_ino && e4_name_equal_ci(block+o+8,nl,name)) {
                E32(block,o)=0; block[o+6]=0; block[o+7]=0;
                for(uint32_t j=8;j<rec;j++) block[o+j]=0;
                return e4_write(&g_ext4,pb*g_ext4.blksz,block,g_ext4.blksz);
            }
            o+=rec;
        }
    }
    return -1;
}

static int vfs_delete_regular_file(const char *path) {
    if(!g_ext4_ok || !e4_can_mutate(&g_ext4)) return 0;
    uint32_t parent=0; char base[256];
    if(!vfs_split_user_path(path,&parent,base)) return 0;
    uint32_t ino=vfs_resolve_user_path(path);
    if(!ino) return 0;
    for(int i=0;i<VFS_USER_HANDLES;i++)
        if(vfs_user_handles[i].used && vfs_user_handles[i].ino==ino) return 0;
    uint8_t inode[256];
    if(e4_inode(&g_ext4,ino,inode)<0 ||
       (E16(inode,0)&0xF000)!=0x8000 || E16(inode,26)!=1 ||
       E16(inode+40,6)!=0 || E16(inode+40,2)>E16(inode+40,4) ||
       E16(inode+40,2)>4) return 0;
    if(e4_remove_dirent(parent,base,ino)<0) return 0;
    uint64_t size=e4_size(inode);
    uint32_t blocks=(uint32_t)((size+g_ext4.blksz-1)/g_ext4.blksz);
    for(uint32_t lb=0;lb<blocks;lb++) {
        uint64_t pb=e4_map(&g_ext4,inode,lb);
        if(pb && e4_free_block(&g_ext4,pb)<0) return 0;
    }
    for(uint32_t i=0;i<g_ext4.isize;i++) inode[i]=0;
    if(e4_write_inode(&g_ext4,ino,inode)<0) return 0;
    return e4_free_inode_bit(&g_ext4,ino,0)==0;
}

static int vfs_remove_empty_directory(const char *path) {
    if(!g_ext4_ok || !e4_can_mutate(&g_ext4)) return 0;
    uint32_t parent=0; char base[256];
    if(!vfs_split_user_path(path,&parent,base)) return 0;
    uint32_t ino=vfs_resolve_user_path(path);
    if(!ino || ino==2) return 0;
    uint8_t inode[256], pino[256];
    if(e4_inode(&g_ext4,ino,inode)<0 || e4_inode(&g_ext4,parent,pino)<0 ||
       (E16(inode,0)&0xF000)!=0x4000 ||
       (E32(inode,32)&0x1000u) || E16(pino,26)<=2 ||
       E16(inode+40,6)!=0 || E16(inode+40,2)>4) return 0;
    uint64_t size=e4_size(inode);
    static uint8_t block[4096];
    for(uint64_t pos=0;pos<size;pos+=g_ext4.blksz) {
        uint64_t pb=e4_map(&g_ext4,inode,(uint32_t)(pos/g_ext4.blksz));
        if(!pb || e4_read(&g_ext4,pb*g_ext4.blksz,block,g_ext4.blksz)<0) return 0;
        for(uint32_t o=0;o+8<=g_ext4.blksz;) {
            uint16_t rec=E16(block,o+4); uint32_t ent=E32(block,o);
            uint8_t nl=block[o+6];
            if(rec<8 || (rec&3) || o+rec>g_ext4.blksz || nl>rec-8) return 0;
            if(ent && !((nl==1 && block[o+8]=='.') ||
                        (nl==2 && block[o+8]=='.' && block[o+9]=='.'))) return 0;
            o+=rec;
        }
    }
    if(e4_remove_dirent(parent,base,ino)<0) return 0;
    for(uint32_t lb=0;lb<(uint32_t)((size+g_ext4.blksz-1)/g_ext4.blksz);lb++) {
        uint64_t pb=e4_map(&g_ext4,inode,lb);
        if(pb && e4_free_block(&g_ext4,pb)<0) return 0;
    }
    for(uint32_t i=0;i<g_ext4.isize;i++) inode[i]=0;
    if(e4_write_inode(&g_ext4,ino,inode)<0 ||
       e4_free_inode_bit(&g_ext4,ino,1)<0) return 0;
    E16(pino,26)=(uint16_t)(E16(pino,26)-1);
    return e4_write_inode(&g_ext4,parent,pino)==0;
}

static int vfs_open_user_file(const char *name, uint64_t access, uint64_t disposition) {
    if (!g_ext4_ok || !name || !name[0]) return -1;
    uint32_t ino = vfs_resolve_user_path(name);
    if (!ino && (disposition == 1 || disposition == 4)) {
        if (!(access & 0x40000000u)) return -1;
        ino = vfs_create_empty_regular(name);
    } else if (ino && disposition == 1) {
        return -1; /* CREATE_NEW must not overwrite an existing path */
    } else if (disposition != 3 && disposition != 4) {
        return -1; /* no truncate/replace semantics until implemented */
    }
    if (!ino) return -1;
    uint8_t inode[256];
    if (e4_inode(&g_ext4, ino, inode) < 0 ||
        (E16(inode, 0) & 0xF000) != 0x8000) return -1;
    for (int i = 0; i < VFS_USER_HANDLES; i++) {
        if (!vfs_user_handles[i].used) {
            vfs_user_handles[i].ino = ino;
            vfs_user_handles[i].pos = 0;
            vfs_user_handles[i].size = e4_size(inode);
            vfs_user_handles[i].writable = (access & 0x40000000u) != 0;
            vfs_user_handles[i].used = 1;
            return 0x100 + i;
        }
    }
    return -1;
}

static int vfs_read_user_file(uint64_t handle, uint64_t dst, uint64_t len) {
    if (handle < 0x100 || handle >= 0x100 + VFS_USER_HANDLES ||
        len > 1024 * 1024) return -1;
    if (!len) return 0;
    VfsUserHandle *h = &vfs_user_handles[handle - 0x100];
    KPROCESS *proc = MmCurrentProcess;
    if (!h->used || !proc ||
        !MmValidateUserRange(proc, dst, len, 1)) return -1;
    if (h->pos >= h->size) return 0;
    if (len > h->size - h->pos) len = h->size - h->pos;

    uint8_t inode[256];
    if (e4_inode(&g_ext4, h->ino, inode) < 0) return -1;
    uint64_t done = 0;
    while (done < len) {
        uint64_t pos = h->pos + done;
        uint32_t block_off = (uint32_t)(pos % g_ext4.blksz);
        uint64_t chunk = g_ext4.blksz - block_off;
        if (chunk > len - done) chunk = len - done;
        uint64_t phys_block = e4_map(&g_ext4, inode, (uint32_t)(pos / g_ext4.blksz));
        if (!phys_block) {
            for (uint64_t j = 0; j < chunk; j++) ((uint8_t *)dst)[done + j] = 0;
        } else if (e4_read(&g_ext4, phys_block * g_ext4.blksz + block_off,
                           (void *)(dst + done), chunk) < 0) {
            return done ? (int)done : -1;
        }
        done += chunk;
    }
    h->pos += done;
    return (int)done;
}

static int vfs_write_user_file(uint64_t handle, uint64_t src, uint64_t len) {
    if (handle < 0x100 || handle >= 0x100 + VFS_USER_HANDLES ||
        !len || len > 1024 * 1024) return -1;
    VfsUserHandle *h = &vfs_user_handles[handle - 0x100];
    KPROCESS *proc = MmCurrentProcess;
    if (!h->used || !h->writable || !proc ||
        !MmValidateUserRange(proc, src, len, 0)) return -1;
    uint64_t end = h->pos + len;
    if (end < h->pos || end > 0xFFFFFFFFULL || !e4_can_mutate(&g_ext4)) return -1;
    uint8_t inode[256];
    if (e4_inode(&g_ext4, h->ino, inode) < 0 ||
        (E16(inode, 0) & 0xF000) != 0x8000 ||
        !(E32(inode, 32) & 0x80000)) return -1;

    uint64_t old_size = e4_size(inode);
    uint32_t last_lb = (uint32_t)((end - 1) / g_ext4.blksz);
    uint16_t en = E16(inode + 40, 2), em = E16(inode + 40, 4);
    if (E16(inode + 40, 6) != 0 || em > 4 || en > em) return -1;
    static uint8_t zero[4096];
    /* Preflight extent capacity conservatively before allocating anything. */
    uint32_t first_needed = (uint32_t)((h->pos < old_size ? h->pos : old_size) /
                                       g_ext4.blksz);
    uint32_t missing = 0;
    for (uint32_t lb = first_needed; lb <= last_lb; ++lb)
        if (!e4_map(&g_ext4, inode, lb)) missing++;
    /* Blocks may coalesce into a single extent; only refuse up front when the
       inline root is already full. Fragmentation can still exhaust it safely. */
    if (missing && en >= em) return -1;
    for (uint32_t lb = first_needed; lb <= last_lb; ++lb) {
        if (e4_map(&g_ext4, inode, lb)) continue;
        uint64_t pb = e4_alloc_block(&g_ext4);
        if (!pb || e4_write(&g_ext4, pb * g_ext4.blksz, zero, g_ext4.blksz) < 0 ||
            e4_add_inline_extent(&g_ext4, inode, lb, pb) < 0) return -1;
    }

    /* If seeking beyond EOF, zero the old partial block's gap explicitly. */
    if (h->pos > old_size && old_size && (old_size % g_ext4.blksz)) {
        uint64_t pb = e4_map(&g_ext4, inode, (uint32_t)(old_size / g_ext4.blksz));
        uint64_t gap_end = h->pos;
        uint64_t block_end = ((old_size / g_ext4.blksz) + 1) * g_ext4.blksz;
        if (gap_end > block_end) gap_end = block_end;
        if (pb && gap_end > old_size &&
            e4_write(&g_ext4, pb * g_ext4.blksz + old_size % g_ext4.blksz,
                     zero, gap_end - old_size) < 0) return -1;
    }

    uint64_t done = 0;
    while (done < len) {
        uint64_t pos = h->pos + done;
        uint32_t block_off = (uint32_t)(pos % g_ext4.blksz);
        uint64_t chunk = g_ext4.blksz - block_off;
        if (chunk > len - done) chunk = len - done;
        uint64_t pb = e4_map(&g_ext4, inode, (uint32_t)(pos / g_ext4.blksz));
        if (!pb || e4_write(&g_ext4, pb * g_ext4.blksz + block_off,
                            (const void *)(src + done), chunk) < 0)
            return done ? (int)done : -1;
        done += chunk;
    }
    if (end > old_size) {
        E32(inode, 4) = (uint32_t)end;
        E32(inode, 108) = 0;
        if (e4_write_inode(&g_ext4, h->ino, inode) < 0) return done ? (int)done : -1;
        h->size = end;
    }
    h->pos += done;
    return (int)done;
}

static int64_t vfs_seek_user_file(uint64_t handle, int64_t offset, uint64_t method) {
    if (handle < 0x100 || handle >= 0x100 + VFS_USER_HANDLES || method > 2) return -1;
    VfsUserHandle *h = &vfs_user_handles[handle - 0x100];
    if (!h->used) return -1;
    int64_t base = method == 0 ? 0 : (method == 1 ? (int64_t)h->pos : (int64_t)h->size);
    if ((offset < 0 && offset < -base) || (offset > 0 && base > INT64_MAX - offset)) return -1;
    int64_t next = base + offset;
    if (next < 0 || (uint64_t)next > 0xFFFFFFFFULL) return -1;
    h->pos = (uint64_t)next;
    return next;
}

static void vfs_close_user_file(uint64_t handle) {
    if (handle >= 0x100 && handle < 0x100 + VFS_USER_HANDLES)
        vfs_user_handles[handle - 0x100].used = 0;
}


/* Read a file from the current ext4 directory into a kernel buffer. */
static int vfs_find_regular_file(const char *name, uint32_t *ino_out,
                                 uint64_t *size_out) {
    if (!g_ext4_ok || !name || !name[0]) return -1;
    for (int i=0;i<vfs_nent;i++) {
        const char *a=vfs_ents[i].name,*b=name;
        int same=1;
        while (*a || *b) {
            char ca=*a++,cb=*b++;
            if(ca>='A'&&ca<='Z')ca=(char)(ca+32);
            if(cb>='A'&&cb<='Z')cb=(char)(cb+32);
            if(ca!=cb){same=0;break;}
        }
        if(!same || vfs_ents[i].is_dir) continue;
        uint8_t inode[256];
        if(e4_inode(&g_ext4,vfs_ents[i].ino,inode)<0 ||
           (E16(inode,0)&0xF000)!=0x8000) return -1;
        if(ino_out)*ino_out=vfs_ents[i].ino;
        if(size_out)*size_out=e4_size(inode);
        return 0;
    }
    return -1;
}
static int vfs_read_inode_at(uint32_t ino,uint64_t pos,void *dst,uint64_t len) {
    uint8_t inode[256];
    if(!dst || e4_inode(&g_ext4,ino,inode)<0) return -1;
    uint64_t size=e4_size(inode);
    if(pos>=size || !len) return 0;
    if(len>size-pos)len=size-pos;
    uint64_t done=0;
    while(done<len) {
        uint64_t off=pos+done;
        uint32_t inblock=(uint32_t)(off%g_ext4.blksz);
        uint64_t chunk=g_ext4.blksz-inblock;
        if(chunk>len-done)chunk=len-done;
        uint64_t pb=e4_map(&g_ext4,inode,(uint32_t)(off/g_ext4.blksz));
        if(!pb) {
            uint8_t *d=(uint8_t *)dst;
            for(uint64_t j=0;j<chunk;j++)d[done+j]=0;
        } else if(e4_read(&g_ext4,pb*g_ext4.blksz+inblock,
                           (uint8_t *)dst+done,chunk)<0) {
            return done?(int)done:-1;
        }
        done+=chunk;
    }
    return (int)done;
}
static void PspFreeDiskModule(UserDiskModule *d) {
    if(!d || !d->used)return;
    for(uint64_t i=0;i<d->source_pages;i++)
        pmm_free(d->source_phys+i*USER_IMAGE_PAGE);
    d->module.phys=0; d->module.size=0; d->module.entry=0;
    d->module.base=0; d->owner=0; d->source_phys=0;
    d->source_pages=0; d->used=0;
}
static void PspForgetDiskModule(KPROCESS *owner) {
    if(!owner)return;
    for(int i=0;i<UP_MAX;i++)
        if(user_disk_modules[i].used && user_disk_modules[i].owner==owner)
            PspFreeDiskModule(&user_disk_modules[i]);
}
/*
 * Load an on-disk x64 PE32+ file into image layout. This handles PE headers
 * and section copies; imports still resolve only against boot-loaded DLLs.
 */
static UserDiskModule *PspLoadDiskModule(const char *name) {
    uint32_t ino=0;
    uint64_t raw_size=0;
    if(vfs_find_regular_file(name,&ino,&raw_size)<0 ||
       raw_size<0x100 || raw_size>16ull*1024*1024) return 0;
    uint64_t raw_pages=(raw_size+0xFFFULL)>>12;
    uint64_t raw_pa=pmm_alloc_n(raw_pages);
    if(!raw_pa)return 0;
    uint8_t *raw=(uint8_t *)(HHDM_BASE+raw_pa);
    for(uint64_t i=0;i<raw_pages*USER_IMAGE_PAGE;i++)raw[i]=0;
    if(vfs_read_inode_at(ino,0,raw,raw_size)!=(int)raw_size) {
        for(uint64_t i=0;i<raw_pages;i++)pmm_free(raw_pa+i*USER_IMAGE_PAGE);
        return 0;
    }
    if(raw[0]!='M'||raw[1]!='Z')goto bad_raw;
    int32_t lfanew=*(int32_t *)(raw+0x3C);
    if(lfanew<0 || (uint64_t)lfanew+24>raw_size ||
       *(uint32_t *)(raw+lfanew)!=0x00004550)goto bad_raw;
    uint8_t *fh=raw+lfanew+4;
    uint16_t machine=*(uint16_t *)fh;
    uint16_t sections=*(uint16_t *)(fh+2);
    uint16_t opt_size=*(uint16_t *)(fh+16);
    uint64_t opt_off=(uint64_t)lfanew+24;
    if(machine!=0x8664 || opt_size<160 || opt_off+opt_size>raw_size ||
       *(uint16_t *)(raw+opt_off)!=0x20B || !sections || sections>96)goto bad_raw;
    uint8_t *opt=raw+opt_off;
    uint32_t entry_rva=*(uint32_t *)(opt+16);
    uint64_t preferred=*(uint64_t *)(opt+24);
    uint32_t image_size=*(uint32_t *)(opt+56);
    uint32_t headers_size=*(uint32_t *)(opt+60);
    uint64_t sect_off=opt_off+opt_size;
    if(image_size<0x1000 || image_size>16u*1024*1024 ||
       !headers_size || headers_size>raw_size || headers_size>image_size ||
       entry_rva>=image_size ||
       sect_off+(uint64_t)sections*40>raw_size)goto bad_raw;
    uint64_t image_pages=((uint64_t)image_size+0xFFFULL)>>12;
    uint64_t image_pa=pmm_alloc_n(image_pages);
    if(!image_pa)goto bad_raw;
    uint8_t *image=(uint8_t *)(HHDM_BASE+image_pa);
    for(uint64_t i=0;i<image_pages*USER_IMAGE_PAGE;i++)image[i]=0;
    for(uint32_t i=0;i<headers_size;i++)image[i]=raw[i];
    for(uint16_t i=0;i<sections;i++) {
        uint8_t *sh=raw+sect_off+(uint64_t)i*40;
        uint32_t vsize=*(uint32_t *)(sh+8);
        uint32_t va=*(uint32_t *)(sh+12);
        uint32_t rawsz=*(uint32_t *)(sh+16);
        uint32_t rawoff=*(uint32_t *)(sh+20);
        uint32_t copy=rawsz;
        if(vsize && copy>vsize)copy=vsize;
        if(va>image_size || copy>image_size-va ||
           rawoff>raw_size || copy>raw_size-rawoff) {
            for(uint64_t j=0;j<image_pages;j++)pmm_free(image_pa+j*USER_IMAGE_PAGE);
            goto bad_raw;
        }
        for(uint32_t j=0;j<copy;j++)image[va+j]=raw[rawoff+j];
    }
    for(uint64_t i=0;i<raw_pages;i++)pmm_free(raw_pa+i*USER_IMAGE_PAGE);
    for(int i=0;i<UP_MAX;i++)if(!user_disk_modules[i].used) {
        UserDiskModule *d=&user_disk_modules[i];
        d->used=1; d->owner=0; d->source_phys=image_pa;
        d->source_pages=image_pages;
        d->module.phys=image_pa; d->module.size=image_size;
        d->module.base=preferred?preferred:0x42000000ULL;
        d->module.entry=d->module.base+entry_rva;
        int n=0; for(;name[n]&&n<31;n++)d->module.name[n]=name[n];
        d->module.name[n]=0;
        for(n++;n<32;n++)d->module.name[n]=0;
        return d;
    }
    for(uint64_t i=0;i<image_pages;i++)pmm_free(image_pa+i*USER_IMAGE_PAGE);
    return 0;
bad_raw:
    for(uint64_t i=0;i<raw_pages;i++)pmm_free(raw_pa+i*USER_IMAGE_PAGE);
    return 0;
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
            PspForgetDiskModule(dying);
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
        char name[64];
        char child_arg[64];
        child_arg[0] = 0;
        int has_child_arg = 0;

        if (copy_user_str(f->rdi, name, sizeof name) != 0) {
            f->rax = (uint64_t)-1LL;
            return f;
        }
        if (f->rsi && copy_user_str(f->rsi, child_arg, sizeof child_arg) == 0)
            has_child_arg = 1;

        KPROCESS *caller = MmCurrentProcess;

        /* Kernel allocation and page-table work under the system CR3. */
        MmSwitchToProcess(PspSystemProcess);

        BootModule *mod = PspFindModule(user_bootinfo, name);
        UserDiskModule *disk_mod = 0;
        if (!mod) {
            disk_mod = PspLoadDiskModule(name);
            if (disk_mod) mod = &disk_mod->module;
        }

        if (!mod) {
            MmSwitchToProcess(caller);
            f->rax = (uint64_t)-1LL;
            return f;
        }

        KPROCESS *proc = MmCreateAddressSpace(name);

        if (!proc) {
            if (disk_mod) PspFreeDiskModule(disk_mod);
            MmSwitchToProcess(caller);
            f->rax = (uint64_t)-1LL;
            return f;
        }
        if (disk_mod) disk_mod->owner = proc;
        if (PspMapUserImage(proc, mod) != 0) {
            PspForgetUserImages(proc);
            MmDeleteAddressSpace(proc);
            if (disk_mod) PspFreeDiskModule(disk_mod);
            MmSwitchToProcess(caller);
            f->rax = (uint64_t)-1LL;
            return f;
        }

        pe_resolve_imports(proc, mod);

        UserImage *main_image = PspGetUserImage(proc, mod);

        if (!main_image) {
            PspForgetUserImages(proc);
            MmDeleteAddressSpace(proc);
            if (disk_mod) PspFreeDiskModule(disk_mod);
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
                    if (disk_mod) PspFreeDiskModule(disk_mod);
                    MmSwitchToProcess(caller);
                    f->rax = (uint64_t)-1LL;
                    return f;
                }

                map_4k_in(proc->Pml4,
                          stack_lo + off, pa, PTE_U | PTE_RW | PTE_OWNED);
            }
        }

        /* Optional single command-line argument at a stable user VA. */
        if (has_child_arg && MmUserRangeUnmapped(proc, 0x6FF00000ULL, USER_IMAGE_PAGE)) {
            uint64_t arg_pa = pmm_alloc_zeroed();
            if (arg_pa) {
                map_4k_in(proc->Pml4, 0x6FF00000ULL, arg_pa,
                          PTE_U | PTE_RW | PTE_OWNED);
                char *arg_dst = (char *)(uintptr_t)arg_pa;
                for (int ai = 0; ai < 63 && child_arg[ai]; ai++)
                    arg_dst[ai] = child_arg[ai];
                arg_dst[63] = 0;
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

    case SYS_NtCreateDirectory: {
        char name[256];
        if (copy_user_str(f->rdi, name, sizeof name) != 0)
            f->rax = 0;
        else
            f->rax = vfs_create_directory(name) ? 1 : 0;
        return f;
    }

    case SYS_NtPathType: {
        char name[256];
        if (copy_user_str(f->rdi,name,sizeof name)!=0) { f->rax=0; return f; }
        uint32_t ino=vfs_resolve_user_path(name);
        uint8_t inode[256];
        if (!ino || e4_inode(&g_ext4,ino,inode)<0) f->rax=0;
        else {
            uint16_t mode=E16(inode,0)&0xF000;
            f->rax=mode==0x4000?2:(mode==0x8000?1:0);
        }
        return f;
    }
    case SYS_NtDeleteFile: {
        char name[256];
        f->rax = copy_user_str(f->rdi,name,sizeof name)==0 &&
                 vfs_delete_regular_file(name) ? 1 : 0;
        return f;
    }
    case SYS_NtRemoveDirectory: {
        char name[256];
        f->rax = copy_user_str(f->rdi,name,sizeof name)==0 &&
                 vfs_remove_empty_directory(name) ? 1 : 0;
        return f;
    }

    case SYS_NtOpenFile: {
        char name[256];
        if (copy_user_str(f->rdi, name, sizeof name) != 0) {
            f->rax = (uint64_t)-1LL;
            return f;
        }
        f->rax = (uint64_t)(int64_t)vfs_open_user_file(name, f->rsi, f->rdx);
        return f;
    }

    case SYS_NtReadFile:
        f->rax = (uint64_t)(int64_t)vfs_read_user_file(f->rdi, f->rsi, f->rdx);
        return f;

    case SYS_NtSeekFile:
        f->rax = (uint64_t)vfs_seek_user_file(f->rdi, (int64_t)f->rsi, f->rdx);
        return f;

    case SYS_NtWriteExistingFile:
        f->rax = (uint64_t)(int64_t)vfs_write_user_file(f->rdi, f->rsi, f->rdx);
        return f;

    case SYS_NtCloseFile:
        vfs_close_user_file(f->rdi);
        f->rax = 1;
        return f;

    case SYS_NtQueryDirectoryFile: {
        uint32_t index = (uint32_t)f->rdi;
        char *name = (char *)f->rsi;
        uint64_t capacity = f->rdx;
        if (capacity < 2 || capacity > 256 ||
            !name || !MmValidateUserRange(MmCurrentProcess, (uint64_t)name, capacity, 1)) {
            f->rax = 0;
            return f;
        }

        if (g_ext4_ok) {
            /* A mounted filesystem is the sole source of directory entries.
               Never append boot modules after the current directory ends. */
            if (index < (uint32_t)vfs_nent) {
                const char *n = vfs_ents[index].name;
                uint64_t i = 0;
                for (; n[i] && i + 1 < capacity; i++)
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
            uint64_t i = 0;
            for (; n[i] && i + 1 < capacity; i++)
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

        static UIn pending_click;
        static int pending_click_valid;
        static int pending_click_pid;

        int mepid = MmCurrentProcess ? (int)MmCurrentProcess->Pid : 0;

        if (!out || !MmValidateUserRange(MmCurrentProcess, (uint64_t)out, sizeof *out, 1)) {
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