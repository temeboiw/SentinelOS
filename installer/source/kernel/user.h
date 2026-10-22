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
#include "gfx.h"
#include "virtio_blk.h"
#include "sha256.h"

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
#define SYS_NtGetSystemTime           23
#define SYS_NtGetTickCount             24
#define SYS_NtFreeVirtualMemory        27
#define SYS_NtProtectVirtualMemory     28
#define SYS_NtQueryVirtualMemory       29
#define SYS_NtQueryFileMetadata        30
#define SYS_NtQueryVolumeMetadata       31
#define SYS_NtInitializeGraphics        32
#define SYS_NtFlushGraphics              33
#define SYS_NtSubmit3D                    34
#define SYS_NtPresent3D                   35
#define SYS_NtUploadVirGLBuffer            36
#define SYS_NtGetGameKeyState               38
#define SYS_NtGetGameMouseDelta              39
#define SYS_NtQueryInstallerDisk              40
#define SYS_NtInstallerDiskIo                 41
#define SYS_NtReadInstallerPayload              42
#define SYS_NtQueryInstallerPayload              43
#define SYS_NtVerifyInstallerPayload              44
#define SYS_NtInstallerInstallImage                 45

typedef struct { uint64_t sectors; uint32_t sector_size; uint32_t present; } InstallerDiskInfo;

#define UP_MAX 8
#define USER_IMAGE_MAX (UP_MAX * BOOT_MAX_MODULES)
#define USER_IMAGE_PAGE 0x1000ULL
#define USER_VM_ALLOCATION_MAX 64
typedef struct { KPROCESS *process; uint64_t base,size; uint32_t allocation_protect; int used; } UserVmAllocation;
static UserVmAllocation user_vm_allocations[USER_VM_ALLOCATION_MAX];
static void user_vm_forget_process(KPROCESS *proc) {
    if(!proc)return;
    for(int i=0;i<USER_VM_ALLOCATION_MAX;i++)if(user_vm_allocations[i].used&&user_vm_allocations[i].process==proc) {
        user_vm_allocations[i].used=0;user_vm_allocations[i].process=0;user_vm_allocations[i].base=0;user_vm_allocations[i].size=0;user_vm_allocations[i].allocation_protect=0;
    }
}
static int user_vm_set_page_writable(KPROCESS *proc,uint64_t va,int writable) {
    if(!proc||va<0x10000ULL||va>=0x0000800000000000ULL||(va&0xFFFULL))return 0;
    uint64_t *root=PHYS_TO_VIRT(proc->DirectoryTableBase&PTE_ADDR);
    uint64_t e4=root[(va>>39)&511];if(!(e4&PTE_P)||!(e4&PTE_U))return 0;
    uint64_t *pdpt=PHYS_TO_VIRT(e4&PTE_ADDR);uint64_t e3=pdpt[(va>>30)&511];
    if(!(e3&PTE_P)||!(e3&PTE_U)||(e3&PTE_PS))return 0;
    uint64_t *pd=PHYS_TO_VIRT(e3&PTE_ADDR);uint64_t e2=pd[(va>>21)&511];
    if(!(e2&PTE_P)||!(e2&PTE_U)||(e2&PTE_PS))return 0;
    uint64_t *pt=PHYS_TO_VIRT(e2&PTE_ADDR);unsigned int idx=(unsigned int)((va>>12)&511);uint64_t e=pt[idx];
    if(!(e&PTE_P)||!(e&PTE_U)||!(e&PTE_OWNED))return 0;
    pt[idx]=writable?(e|PTE_RW):(e&~PTE_RW);
    if(MmCurrentProcess==proc)__asm__ volatile("invlpg (%0)"::"r"(va):"memory");
    return 1;
}

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
    int system_app;
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

/* System images are explicitly trusted session/boot components.
 * Ordinary user applications cannot create these processes. */
static int PspIsSystemAppName(const char *name) {
    static const char *const names[] = {
        "smss.exe", "bootvid.exe", "sentlogon.exe",
        "instlr.exe", "screen.exe"
    };
    if (!name) return 0;
    for (unsigned int i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        const char *a = name;
        const char *b = names[i];
        int equal = 1;
        while (*a || *b) {
            char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + ('a' - 'A'));
            if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + ('a' - 'A'));
            if (ca != cb) {
                equal = 0;
                break;
            }
            if (*a) a++;
            if (*b) b++;
        }
        if (equal) return 1;
    }
    return 0;
}

static int PspIsSystemProcess(KPROCESS *proc) {
    int idx = up_find_proc(proc);
    return idx >= 0 && up_tab[idx].alive && up_tab[idx].system_app;
}

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

/* Safely read the PE32+ Optional Header subsystem field. */
static int pe_get_subsystem(const uint8_t *image, uint64_t size) {
    if (!image || size < 0x40 || image[0] != 'M' || image[1] != 'Z')
        return -1;
    int32_t lfanew = *(const int32_t *)(image + 0x3C);
    if (lfanew < 0 || (uint64_t)lfanew > size ||
        size - (uint64_t)lfanew < 24)
        return -1;
    const uint8_t *nt = image + lfanew;
    if (*(const uint32_t *)nt != 0x00004550)
        return -1;
    uint16_t opt_size = *(const uint16_t *)(nt + 20);
    uint64_t opt_off = (uint64_t)lfanew + 24;
    if (opt_size < 70 || opt_off > size || opt_size > size - opt_off)
        return -1;
    const uint8_t *opt = image + opt_off;
    if (*(const uint16_t *)opt != 0x20B)
        return -1;
    return (int)*(const uint16_t *)(opt + 68);
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
    uint64_t preferred_base = 0;
    int relocatable = 0;

    /*
     * Boot DLLs can share the same recorded/default base. If no explicit
     * base is supplied, use the PE ImageBase. When that range is occupied,
     * move the image only if its PE has a bounded base-relocation directory.
     */
    if (pe_is_image((const uint8_t *)(HHDM_BASE + mod->phys), size)) {
        const uint8_t *raw = (const uint8_t *)(HHDM_BASE + mod->phys);
        int32_t peoff = *(const int32_t *)(raw + 0x3C);
        const uint8_t *opt = raw + peoff + 4 + 20;
        preferred_base = *(const uint64_t *)(opt + 24);
        uint32_t reloc_rva = *(const uint32_t *)(opt + 112 + 5 * 8);
        uint32_t reloc_size = *(const uint32_t *)(opt + 112 + 5 * 8 + 4);
        relocatable = reloc_rva && reloc_size &&
                      reloc_rva < size && reloc_size <= size - reloc_rva;
        if (!mod->base && preferred_base) base = preferred_base;
    }

    if (!pages || (base & (USER_IMAGE_PAGE - 1)) ||
        base < 0x10000ULL || base >= 0x0000800000000000ULL ||
        rounded > 0x0000800000000000ULL - base) {
        DbgPrint("user: invalid image range for %s\n", mod->name);
        return -1;
    }

    if (!MmUserRangeUnmapped(proc, base, rounded)) {
        if (!relocatable) {
            DbgPrint("user: image VA collision and no relocations for %s\n",
                     mod->name);
            return -1;
        }

        /* Probe complete image ranges, not just their first page. */
        uint64_t candidate = 0x50000000ULL;
        int found = 0;
        while (candidate < 0x0000800000000000ULL &&
               rounded <= 0x0000800000000000ULL - candidate) {
            if (MmUserRangeUnmapped(proc, candidate, rounded)) {
                base = candidate;
                found = 1;
                break;
            }
            if (candidate > UINT64_MAX - 0x02000000ULL) break;
            candidate += 0x02000000ULL;
        }
        if (!found) {
            DbgPrint("user: no free relocatable image range for %s\n",
                     mod->name);
            return -1;
        }
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
#define SYS_NtQueryFileSize 25
#define SYS_NtSetFilePosition 26
typedef struct {
    uint32_t ino;
    uint64_t pos, size;
    int used;
} VfsUserHandle;
typedef struct {
    uint64_t creation_time,access_time,write_time,change_time,size;
    uint32_t inode,mode,volume_serial;
    uint16_t links,reserved;
} UserFileMetadata;
typedef struct {
    uint64_t total_blocks,free_blocks,reserved_blocks;
    uint32_t block_size,volume_serial,total_inodes,free_inodes;
    char volume_label[17];
} UserVolumeMetadata;
static VfsUserHandle vfs_user_handles[VFS_USER_HANDLES];
static uint64_t user_filetime_from_unix(uint32_t seconds) {
    return ((uint64_t)seconds+11644473600ULL)*10000000ULL;
}

static int vfs_open_user_file(const char *name) {
    if (!g_ext4_ok || !name || !name[0]) return -1;
    uint32_t ino = 0;
    for (int i = 0; i < vfs_nent; i++) {
        const char *a = vfs_ents[i].name, *b = name;
        int same = 1;
        while (*a || *b) {
            char ca = *a++, cb = *b++;
            if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
            if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
            if (ca != cb) { same = 0; break; }
        }
        if (same && !vfs_ents[i].is_dir) { ino = vfs_ents[i].ino; break; }
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

typedef struct { uint16_t wYear,wMonth,wDayOfWeek,wDay,wHour,wMinute,wSecond,wMilliseconds; } UserSystemTime;
static uint8_t user_rtc_read(uint8_t reg) {
    uint8_t value;
    __asm__ volatile("outb %0, $0x70" : : "a"(reg));
    __asm__ volatile("inb $0x71, %0" : "=a"(value));
    return value;
}
static uint8_t user_rtc_decode(uint8_t value,int binary) {
    return binary?value:(uint8_t)((value&0x0Fu)+((value>>4)*10u));
}
static uint64_t user_boot_rtc_seconds;
static int user_boot_rtc_valid;
static int user_rtc_leap(uint32_t y){return (y%4u==0u&&y%100u!=0u)||y%400u==0u;}
static uint64_t user_rtc_epoch_seconds(const UserSystemTime *t){
    static const uint8_t md[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(!t||t->wYear<1970||t->wMonth<1||t->wMonth>12)return 0;
    uint64_t days=0;
    for(uint32_t y=1970;y<t->wYear;y++)days+=user_rtc_leap(y)?366u:365u;
    for(uint32_t m=1;m<t->wMonth;m++)days+=md[m-1]+(m==2&&user_rtc_leap(t->wYear));
    days+=t->wDay-1u;
    return days*86400u+(uint64_t)t->wHour*3600u+(uint64_t)t->wMinute*60u+t->wSecond;
}
static int user_rtc_system_time(UserSystemTime *out) {
    if(!out)return 0;
    int tries=100000;
    while((user_rtc_read(0x0A)&0x80u)&&--tries){}
    if(!tries)return 0;
    uint8_t sec=user_rtc_read(0x00),min=user_rtc_read(0x02),hour=user_rtc_read(0x04);
    uint8_t dow=user_rtc_read(0x06),day=user_rtc_read(0x07),month=user_rtc_read(0x08);
    uint8_t year=user_rtc_read(0x09),century=user_rtc_read(0x32),mode=user_rtc_read(0x0B);
    int binary=(mode&0x04u)!=0,pm=(hour&0x80u)!=0;
    hour&=0x7Fu;
    sec=user_rtc_decode(sec,binary);min=user_rtc_decode(min,binary);
    hour=user_rtc_decode(hour,binary);day=user_rtc_decode(day,binary);
    month=user_rtc_decode(month,binary);year=user_rtc_decode(year,binary);
    if(!(mode&0x02u)){hour=(uint8_t)(hour%12u+(pm?12u:0u));}
    if(century!=0&&century!=0xFFu){century=user_rtc_decode(century,binary);out->wYear=(uint16_t)(century*100u+year);}
    else out->wYear=(uint16_t)((year>=70u?1900u:2000u)+year);
    out->wMonth=month;out->wDay=day;out->wDayOfWeek=dow?((dow-1u)%7u):0;
    out->wHour=hour;out->wMinute=min;out->wSecond=sec;out->wMilliseconds=0;
    return month>=1&&month<=12&&day>=1&&day<=31&&hour<24&&min<60&&sec<60;
}


/* Shared software window compositor. USER32 surfaces are copied into kernel-owned
 * per-window backing stores; the shared GPU scanout is output-only. */
#define SYS_NtPresentLayer 37
#define WM_LAYER_BACKGROUND 0
#define WM_LAYER_DESKTOP    1
#define WM_LAYER_WINDOW     2
#define WM_LAYER_TASKBAR    3
#define WM_COMP_MAX_PIXELS (1920u * 1080u)
typedef struct {
    uint64_t pixels;
    uint32_t layer, x, y, w, h, width, height;
} WmPresentLayerArgs;
static uint32_t *wm_shared_background;
static uint32_t *wm_shared_desktop;
static uint32_t *wm_shared_taskbar;
static uint32_t *wm_shared_windows[WM_MAX];
static uint32_t wm_shared_width, wm_shared_height;
static uint32_t wm_shared_alpha(uint32_t dst, uint32_t src) {
    uint32_t a=src>>24;
    if(!a)return dst;
    if(a==255u)return src;
    uint32_t ia=255u-a;
    uint32_t r=(((src>>16)&255u)*a+((dst>>16)&255u)*ia)/255u;
    uint32_t g=(((src>>8)&255u)*a+((dst>>8)&255u)*ia)/255u;
    uint32_t b=((src&255u)*a+(dst&255u)*ia)/255u;
    return 0xFF000000u|(r<<16)|(g<<8)|b;
}
static int wm_shared_ensure(uint32_t width,uint32_t height) {
    if(!width||!height||width>1920u||height>1080u||
       (uint64_t)width*height>WM_COMP_MAX_PIXELS)return 0;
    if(wm_shared_width==width&&wm_shared_height==height&&wm_shared_background&&
       wm_shared_desktop&&wm_shared_taskbar)return 1;
    uint64_t bytes=(uint64_t)width*height*sizeof(uint32_t);
    if(wm_shared_background)kfree(wm_shared_background);
    if(wm_shared_desktop)kfree(wm_shared_desktop);
    if(wm_shared_taskbar)kfree(wm_shared_taskbar);
    wm_shared_background=kzalloc((size_t)bytes);
    wm_shared_desktop=kzalloc((size_t)bytes);
    wm_shared_taskbar=kzalloc((size_t)bytes);
    for(int i=0;i<WM_MAX;i++) {
        if(wm_shared_windows[i])kfree(wm_shared_windows[i]);
        wm_shared_windows[i]=0;
    }
    if(!wm_shared_background||!wm_shared_desktop||!wm_shared_taskbar) {
        if(wm_shared_background)kfree(wm_shared_background);
        if(wm_shared_desktop)kfree(wm_shared_desktop);
        if(wm_shared_taskbar)kfree(wm_shared_taskbar);
        wm_shared_background=wm_shared_desktop=wm_shared_taskbar=0;
        wm_shared_width=wm_shared_height=0;
        return 0;
    }
    wm_shared_width=width;wm_shared_height=height;
    return 1;
}
static int wm_shared_window_slot(int pid) {
    for(int i=0;i<WM_MAX;i++)
        if(wm_tab[i].alive&&wm_tab[i].pid==pid)return i;
    return -1;
}
/* Bounded serial diagnostics: detailed startup events plus periodic samples. */
static uint64_t wm_dbg_compose_calls, wm_dbg_present_calls;
static uint64_t wm_dbg_window_moves, wm_dbg_window_registers, wm_dbg_window_closes;
static int wm_dbg_sample(uint64_t n) { return n <= 48u || (n % 128u) == 0u; }
static uint64_t wm_dbg_input_events;
static void wm_dbg_input(int pid,int type,int x,int y,int btn,char key) {
    uint64_t n=++wm_dbg_input_events;
    if(!wm_dbg_sample(n))return;
    if(type==1)
        DbgPrint("[input] #%llu pid=%d key='%c' mouse=(%d,%d) buttons=%d\n",
                 (unsigned long long)n,pid,key,x,y,btn);
    else
        DbgPrint("[input] #%llu pid=%d %s mouse=(%d,%d) buttons=%d\n",
                 (unsigned long long)n,pid,type==3?"click":"move",x,y,btn);
}

static void wm_shared_compose(uint32_t x,uint32_t y,uint32_t w,uint32_t h) {
    if(!wm_shared_background||!gfx_surface_pa||!gfx_width||!gfx_height)return;
    uint32_t width=wm_shared_width,height=wm_shared_height;
    if(width!=gfx_width||height!=gfx_height)return;
    if(x>=width||y>=height||!w||!h)return;
    if(w>width-x)w=width-x;if(h>height-y)h=height-y;
    KPROCESS *old=MmCurrentProcess;
    if(old!=PspSystemProcess)MmSwitchToProcess(PspSystemProcess);
    uint32_t *out=(uint32_t*)PHYS_TO_VIRT(gfx_surface_pa);
    uint32_t pitch=gfx_pitch/4u;
    int order[WM_MAX],count=0;
    for(int i=0;i<WM_MAX;i++) {
        WmEntry *e=&wm_tab[i];
        if(!e->alive||e->minimized||!wm_shared_windows[i])continue;
        int j=count++;
        while(j>0&&wm_tab[order[j-1]].z>e->z){order[j]=order[j-1];j--;}
        order[j]=i;
    }
    uint64_t dbg_n=++wm_dbg_compose_calls;
    if(wm_dbg_sample(dbg_n)) {
        DbgPrint("[gfx] compose #%llu damage=(%u,%u %ux%u) windows=%d pitch=%u\n",
                 (unsigned long long)dbg_n,x,y,w,h,count,pitch);
        for(int oi=0;oi<count;oi++) {
            WmEntry *e=&wm_tab[order[oi]];
            DbgPrint("[gfx]   z[%d] pid=%d rect=(%d,%d %dx%d) minimized=%d\n",
                     oi,e->pid,e->x,e->y,e->w,e->h,e->minimized);
        }
    }
    for(uint32_t yy=y;yy<y+h;yy++)for(uint32_t xx=x;xx<x+w;xx++) {
        uint32_t at=yy*width+xx;
        uint32_t c=wm_shared_alpha(wm_shared_background[at],wm_shared_desktop[at]);
        /* Window surfaces are screen-sized buffers, but their valid drawing
         * area is bounded by the registered frame. Never composite stale
         * pixels outside that frame into the shared scanout. */
        for(int oi=0;oi<count;oi++) {
            WmEntry *e=&wm_tab[order[oi]];
            int left=e->x-12, top=e->y-12;
            int right=e->x+e->w+12, bottom=e->y+e->h+16;
            if((int)xx<left||(int)yy<top||(int)xx>=right||(int)yy>=bottom)
                continue;
            c=wm_shared_alpha(c,wm_shared_windows[order[oi]][at]);
        }
        c=wm_shared_alpha(c,wm_shared_taskbar[at]);
        out[yy*pitch+xx]=c;
    }
    int flush_ok=gfx_flush(x,y,w,h);
    if(wm_dbg_sample(dbg_n))
        DbgPrint("[gfx] flush damage=(%u,%u %ux%u) result=%d\n",x,y,w,h,flush_ok);
    if(old&&old!=PspSystemProcess)MmSwitchToProcess(old);
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


    case SYS_NtPresentLayer: {
        WmPresentLayerArgs a;
        if(!f->rdi||!MmValidateUserRange(MmCurrentProcess,f->rdi,sizeof(a),0)) {
            f->rax=0;return f;
        }
        a=*(WmPresentLayerArgs*)f->rdi;
        uint64_t dbg_n=++wm_dbg_present_calls;
        uint64_t pixels=(uint64_t)a.width*a.height;
        uint64_t bytes=pixels*sizeof(uint32_t);
        if(!a.pixels||!a.width||!a.height||a.width>1920u||a.height>1080u||
           pixels>WM_COMP_MAX_PIXELS||a.layer>WM_LAYER_TASKBAR||
           !MmValidateUserRange(MmCurrentProcess,a.pixels,bytes,0)||
           a.width!=gfx_width||a.height!=gfx_height) {
            if(wm_dbg_sample(dbg_n))
                DbgPrint("[gfx] present #%llu REJECT layer=%u rect=(%u,%u %ux%u) surface=%ux%u gfx=%ux%u pixels=%p\n",
                         (unsigned long long)dbg_n,a.layer,a.x,a.y,a.w,a.h,
                         a.width,a.height,gfx_width,gfx_height,(void*)a.pixels);
            f->rax=0;return f;
        }
        int pid=MmCurrentProcess?(int)MmCurrentProcess->Pid:0;
        if(wm_dbg_sample(dbg_n))
            DbgPrint("[gfx] present #%llu pid=%d layer=%u rect=(%u,%u %ux%u) surface=%ux%u\n",
                     (unsigned long long)dbg_n,pid,a.layer,a.x,a.y,a.w,a.h,a.width,a.height);
        int slot=wm_shared_window_slot(pid);
        KPROCESS *caller=MmCurrentProcess;
        if(caller!=PspSystemProcess)MmSwitchToProcess(PspSystemProcess);
        int ok=wm_shared_ensure(a.width,a.height);
        if(ok&&a.layer==WM_LAYER_WINDOW) {
            if(slot<0)ok=0;
            else if(!wm_shared_windows[slot])wm_shared_windows[slot]=kzalloc((size_t)bytes);
            if(slot>=0&&!wm_shared_windows[slot])ok=0;
        }
        if(caller!=PspSystemProcess)MmSwitchToProcess(caller);
        if(!ok){f->rax=0;return f;}
        if(a.x>=a.width||a.y>=a.height||!a.w||!a.h){f->rax=1;return f;}
        if(a.w>a.width-a.x)a.w=a.width-a.x;
        if(a.h>a.height-a.y)a.h=a.height-a.y;
        uint32_t *src=(uint32_t*)a.pixels;
        uint32_t *dst=0;
        if(a.layer==WM_LAYER_BACKGROUND)dst=wm_shared_background;
        else if(a.layer==WM_LAYER_DESKTOP)dst=wm_shared_desktop;
        else if(a.layer==WM_LAYER_TASKBAR)dst=wm_shared_taskbar;
        else if(slot>=0)dst=wm_shared_windows[slot];
        if(!dst){
            if(wm_dbg_sample(dbg_n))
                DbgPrint("[gfx] present #%llu REJECT no destination pid=%d layer=%u slot=%d\n",
                         (unsigned long long)dbg_n,pid,a.layer,slot);
            f->rax=0;return f;
        }
        for(uint32_t yy=a.y;yy<a.y+a.h;yy++)
            for(uint32_t xx=a.x;xx<a.x+a.w;xx++)
                dst[yy*a.width+xx]=src[yy*a.width+xx];
        /* The desktop publishes its desktop/window/taskbar layers in order.
         * Composing the window layer here and then composing the taskbar layer
         * immediately afterwards sends the same damaged rectangle to the GPU
         * twice. Defer only the Desktop owner's window-layer present; its
         * taskbar-layer present is the frame commit. Ordinary app windows still
         * present immediately, and the cursor/taskbar path remains unchanged. */
        int desktop_owner = slot >= 0 &&
            wm_tab[slot].title[0] == 'D' &&
            wm_tab[slot].title[1] == 'e' &&
            wm_tab[slot].title[2] == 's' &&
            wm_tab[slot].title[3] == 'k' &&
            wm_tab[slot].title[4] == 't' &&
            wm_tab[slot].title[5] == 'o' &&
            wm_tab[slot].title[6] == 'p' &&
            wm_tab[slot].title[7] == 0;
        if(a.layer==WM_LAYER_TASKBAR ||
           (a.layer==WM_LAYER_WINDOW && !desktop_owner))
            wm_shared_compose(a.x,a.y,a.w,a.h);
        if(wm_dbg_sample(dbg_n))
            DbgPrint("[gfx] present #%llu ACCEPT pid=%d layer=%u damage=(%u,%u %ux%u)\n",
                     (unsigned long long)dbg_n,pid,a.layer,a.x,a.y,a.w,a.h);
        f->rax=1;
        return f;
    }

    case SYS_NtTerminateProcess: {
        DbgPrint("[user] exit");

        KPROCESS *dying = MmCurrentProcess;
        int dead_idx = up_find_proc(dying);
        int parent_idx = (dead_idx >= 0) ? up_tab[dead_idx].parent : -1;

        ut_exit_current();

        if (dying && dying != PspSystemProcess) {
            int dead_pid=(int)dying->Pid;
            int dead_slot=wm_shared_window_slot(dead_pid);
            int dx=0,dy=0,dw=(int)wm_shared_width,dh=(int)wm_shared_height;
            if(dead_slot>=0) {
                dx=wm_tab[dead_slot].x-12;dy=wm_tab[dead_slot].y-12;
                dw=wm_tab[dead_slot].w+24;dh=wm_tab[dead_slot].h+28;
            }
            MmSwitchToProcess(PspSystemProcess);
            if(dead_slot>=0&&wm_shared_windows[dead_slot]) {
                kfree(wm_shared_windows[dead_slot]);wm_shared_windows[dead_slot]=0;
            }
            wm_unregister(dead_pid);
            if(dx<0){dw+=dx;dx=0;}if(dy<0){dh+=dy;dy=0;}
            if(dw>0&&dh>0)wm_shared_compose((uint32_t)dx,(uint32_t)dy,(uint32_t)dw,(uint32_t)dh);

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
            user_vm_forget_process(dying);
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
        uint64_t protect = f->rdx ? f->rdx : 0x04ULL;

        if (!caller || caller == PspSystemProcess ||
            requested > UINT64_MAX - 0xFFFULL) {
            f->rax = 0;
            return f;
        }

        uint64_t size = (requested + 0xFFFULL) & ~0xFFFULL;
        if(protect!=0x02ULL&&protect!=0x04ULL&&protect!=0x20ULL&&protect!=0x40ULL){f->rax=0;return f;}
        if (!size || size > 16ull * 1024 * 1024) {
            f->rax = 0;
            return f;
        }

        int automatic_address=(prefer==0);
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
        if(automatic_address) {
            uint64_t candidate=0x50000000ULL;
            uint64_t limit=0x60000000ULL-size;
            for(;candidate<=limit;candidate+=USER_IMAGE_PAGE)
                if(MmUserRangeUnmapped(caller,candidate,size))break;
            if(candidate>limit) {
                MmSwitchToProcess(caller);f->rax=0;return f;
            }
            prefer=candidate;
        }
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
        if(protect==0x02ULL||protect==0x20ULL) {
            for(uint64_t off=0;off<size;off+=USER_IMAGE_PAGE)user_vm_set_page_writable(caller,prefer+off,0);
        }

        int alloc_slot=-1;
        for(int i=0;i<USER_VM_ALLOCATION_MAX;i++)if(!user_vm_allocations[i].used){alloc_slot=i;break;}
        if(alloc_slot<0) {
            while(mapped){mapped-=USER_IMAGE_PAGE;MmUnmapAllocatedUserPage(caller,prefer+mapped);}
            MmSwitchToProcess(caller);f->rax=0;return f;
        }
        user_vm_allocations[alloc_slot].process=caller;
        user_vm_allocations[alloc_slot].base=prefer;
        user_vm_allocations[alloc_slot].size=size;
        user_vm_allocations[alloc_slot].allocation_protect=(uint32_t)protect;
        user_vm_allocations[alloc_slot].used=1;
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

        /*
         * System applications are a protected launch class. User applications
         * may launch ordinary applications, but only a system application may
         * launch smss.exe, bootvid.exe, or sentlogon.exe.
         */
        if (PspIsSystemAppName(name) && !PspIsSystemProcess(caller)) {
            DbgPrint("user: denied system-app launch: target=%s\n", name);
            /* Win32 callers get the subsystem-incompatibility dialog. */
            f->rax = (uint64_t)-2LL;
            return f;
        }

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

        /* Only the protected native boot applications may use subsystem 1.
           Normal Win32 applications must be GUI (2) or console (3) images. */
        int subsystem = pe_get_subsystem(
            (const uint8_t *)(HHDM_BASE + mod->phys), mod->size);
        int protected_system_app = PspIsSystemAppName(name);
        if (subsystem < 0 ||
            (protected_system_app && subsystem != 1) ||
            (!protected_system_app && subsystem != 2 && subsystem != 3)) {
            DbgPrint("user: incompatible PE subsystem: %s (subsystem=%d)\n",
                     name, subsystem);
            if (disk_mod) PspFreeDiskModule(disk_mod);
            MmSwitchToProcess(caller);
            f->rax = (uint64_t)-2LL;
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
            up_tab[up_n].system_app = PspIsSystemAppName(name);
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
        f->rax = (uint64_t)(int64_t)vfs_open_user_file(name);
        return f;
    }

    case SYS_NtReadFile:
        f->rax = (uint64_t)(int64_t)vfs_read_user_file(f->rdi, f->rsi, f->rdx);
        return f;

    case SYS_NtCloseFile:
        vfs_close_user_file(f->rdi);
        f->rax = 1;
        return f;

    case SYS_NtQueryFileSize: {
        uint64_t h=f->rdi;
        if(h<0x100||h>=0x100+VFS_USER_HANDLES||!vfs_user_handles[h-0x100].used){f->rax=(uint64_t)-1LL;return f;}
        f->rax=vfs_user_handles[h-0x100].size;
        return f;
    }

    case SYS_NtSetFilePosition: {
        uint64_t h=f->rdi;
        if(h<0x100||h>=0x100+VFS_USER_HANDLES||!vfs_user_handles[h-0x100].used){f->rax=(uint64_t)-1LL;return f;}
        VfsUserHandle *vh=&vfs_user_handles[h-0x100];
        int64_t offset=(int64_t)f->rsi;
        uint64_t base=0;
        if(f->rdx==1)base=vh->pos;
        else if(f->rdx==2)base=vh->size;
        else if(f->rdx!=0){f->rax=(uint64_t)-1LL;return f;}
        uint64_t target;
        if(offset<0){uint64_t mag=(uint64_t)(-(offset+1))+1;if(mag>base){f->rax=(uint64_t)-1LL;return f;}target=base-mag;}
        else {uint64_t mag=(uint64_t)offset;if(mag>0x7FFFFFFFFFFFFFFFULL-base){f->rax=(uint64_t)-1LL;return f;}target=base+mag;}
        vh->pos=target;f->rax=vh->pos;return f;
    }

    case SYS_NtFreeVirtualMemory: {
        KPROCESS *caller=MmCurrentProcess;
        uint64_t base=f->rdi,size=f->rsi,type=f->rdx;
        if(!caller||caller==PspSystemProcess||(base&0xFFFULL)||size!=0||type!=0x8000ULL) {f->rax=0;return f;}
        int slot=-1;
        for(int i=0;i<USER_VM_ALLOCATION_MAX;i++) {
            UserVmAllocation *a=&user_vm_allocations[i];
            if(a->used&&a->process==caller&&a->base==base){slot=i;break;}
        }
        if(slot<0){f->rax=0;return f;}
        UserVmAllocation *a=&user_vm_allocations[slot];
        MmSwitchToProcess(PspSystemProcess);
        int ok=1;
        for(uint64_t off=0;off<a->size;off+=USER_IMAGE_PAGE) {
            if(!MmUnmapAllocatedUserPage(caller,base+off)){ok=0;break;}
        }
        if(ok){a->used=0;a->process=0;a->base=0;a->size=0;}
        MmSwitchToProcess(caller);
        f->rax=ok?1:0;return f;
    }

    case SYS_NtProtectVirtualMemory: {
        KPROCESS *caller=MmCurrentProcess;
        uint64_t address=f->rdi,length=f->rsi,protect=f->rdx;
        if(!caller||caller==PspSystemProcess||!length||address>UINT64_MAX-length||
           (protect!=0x02ULL&&protect!=0x04ULL&&protect!=0x20ULL&&protect!=0x40ULL)) {f->rax=0;return f;}
        uint64_t start=address&~0xFFFULL,end=address+length;
        if(end>UINT64_MAX-0xFFFULL){f->rax=0;return f;}
        end=(end+0xFFFULL)&~0xFFFULL;
        if(end<=start){f->rax=0;return f;}
        UserVmAllocation *allocation=0;
        for(int i=0;i<USER_VM_ALLOCATION_MAX;i++) {
            UserVmAllocation *a=&user_vm_allocations[i];
            if(a->used&&a->process==caller&&start>=a->base&&end<=a->base+a->size){allocation=a;break;}
        }
        if(!allocation){f->rax=0;return f;}
        uint64_t first=MmLookupUserPte(caller,start,0);
        if(!(first&PTE_P)||!(first&PTE_U)||!(first&PTE_OWNED)){f->rax=0;return f;}
        uint64_t old_protect=(first&PTE_RW)?0x04ULL:0x02ULL;
        for(uint64_t page=start;page<end;page+=USER_IMAGE_PAGE) {
            uint64_t pte=MmLookupUserPte(caller,page,0);
            if(!(pte&PTE_P)||!(pte&PTE_U)||!(pte&PTE_OWNED)){f->rax=0;return f;}
        }
        int writable=(protect==0x04ULL||protect==0x40ULL);
        MmSwitchToProcess(PspSystemProcess);
        int ok=1;
        for(uint64_t page=start;page<end;page+=USER_IMAGE_PAGE)
            if(!user_vm_set_page_writable(caller,page,writable)){ok=0;break;}
        MmSwitchToProcess(caller);
        f->rax=ok?old_protect:0;return f;
    }

    case SYS_NtQueryVirtualMemory: {
        typedef struct {
            uint64_t BaseAddress,AllocationBase;
            uint32_t AllocationProtect;
            uint16_t PartitionId,Padding;
            uint64_t RegionSize;
            uint32_t State,Protect,Type,Padding2;
        } UserMemoryBasicInformation;
        KPROCESS *caller=MmCurrentProcess;
        uint64_t address=f->rdi,out=f->rsi,length=f->rdx;
        if(!caller||address>=0x0000800000000000ULL||
           length<sizeof(UserMemoryBasicInformation)||
           !MmValidateUserRange(caller,out,sizeof(UserMemoryBasicInformation),1)){f->rax=0;return f;}
        uint64_t page=address&~0xFFFULL,pte=MmLookupUserPte(caller,page,0);
        UserMemoryBasicInformation info={0};
        UserVmAllocation *allocation=0;
        for(int i=0;i<USER_VM_ALLOCATION_MAX;i++) {
            UserVmAllocation *a=&user_vm_allocations[i];
            if(a->used&&a->process==caller&&page>=a->base&&page<a->base+a->size){allocation=a;break;}
        }
        if(allocation&&pte) {
            uint32_t protect_now=(pte&PTE_RW)?0x04u:0x02u;
            uint64_t start=page,end=page+USER_IMAGE_PAGE,alloc_end=allocation->base+allocation->size;
            while(start>allocation->base) {
                uint64_t prev=MmLookupUserPte(caller,start-USER_IMAGE_PAGE,0);
                if(!prev||!(prev&PTE_OWNED)||(((prev&PTE_RW)?0x04u:0x02u)!=protect_now))break;
                start-=USER_IMAGE_PAGE;
            }
            while(end<alloc_end) {
                uint64_t next=MmLookupUserPte(caller,end,0);
                if(!next||!(next&PTE_OWNED)||(((next&PTE_RW)?0x04u:0x02u)!=protect_now))break;
                end+=USER_IMAGE_PAGE;
            }
            info.BaseAddress=start;info.AllocationBase=allocation->base;
            info.AllocationProtect=allocation->allocation_protect;info.RegionSize=end-start;
            info.State=0x1000u;info.Protect=protect_now;info.Type=0x20000u;
        } else if(pte) {
            info.BaseAddress=page;info.AllocationBase=page;info.AllocationProtect=(pte&PTE_RW)?0x04u:0x02u;
            info.RegionSize=USER_IMAGE_PAGE;info.State=0x1000u;info.Protect=(pte&PTE_RW)?0x04u:0x02u;
            info.Type=(pte&PTE_OWNED)?0x20000u:0x1000000u;
        } else {
            info.BaseAddress=page;info.AllocationBase=0;info.AllocationProtect=0;info.RegionSize=USER_IMAGE_PAGE;
            info.State=0x10000u;info.Protect=0;info.Type=0;
        }
        *(UserMemoryBasicInformation*)out=info;
        f->rax=sizeof(UserMemoryBasicInformation);return f;
    }

    case SYS_NtQueryVolumeMetadata: {
        uint64_t out=f->rdi,length=f->rsi;
        if(!g_ext4_ok||!g_ext4.blksz||length<sizeof(UserVolumeMetadata)||
           !MmValidateUserRange(MmCurrentProcess,out,sizeof(UserVolumeMetadata),1)){f->rax=0;return f;}
        UserVolumeMetadata meta={0};
        meta.total_blocks=g_ext4.total_blocks;meta.free_blocks=g_ext4.free_blocks;meta.reserved_blocks=g_ext4.reserved_blocks;
        meta.block_size=g_ext4.blksz;meta.volume_serial=g_ext4.volume_serial;meta.total_inodes=g_ext4.total_inodes;meta.free_inodes=g_ext4.free_inodes;
        for(int i=0;i<17;i++)meta.volume_label[i]=g_ext4.volume_label[i];
        *(UserVolumeMetadata*)out=meta;f->rax=1;return f;
    }

    case SYS_NtQueryFileMetadata: {
        uint64_t h=f->rdi,out=f->rsi,length=f->rdx;
        if(h<0x100||h>=0x100+VFS_USER_HANDLES||!vfs_user_handles[h-0x100].used||
           length<sizeof(UserFileMetadata)||!MmValidateUserRange(MmCurrentProcess,out,sizeof(UserFileMetadata),1)) {
            f->rax=0;return f;
        }
        VfsUserHandle *vh=&vfs_user_handles[h-0x100];uint8_t inode[256];
        if(!g_ext4_ok||e4_inode(&g_ext4,vh->ino,inode)<0){f->rax=0;return f;}
        UserFileMetadata meta={0};
        meta.creation_time=0; /* legacy ext4 inode has no birth-time field */
        meta.access_time=user_filetime_from_unix(E32(inode,8));
        meta.write_time=user_filetime_from_unix(E32(inode,16));
        meta.change_time=user_filetime_from_unix(E32(inode,12));
        meta.size=e4_size(inode);meta.inode=vh->ino;meta.mode=E16(inode,0);meta.volume_serial=g_ext4.volume_serial;meta.links=E16(inode,26);
        *(UserFileMetadata*)out=meta;f->rax=1;return f;
    }

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

    case SYS_NtUploadVirGLBuffer: {
        uint64_t bytes=f->rdi;
        if(!gfx_3d_ready||!bytes||bytes>GFX_VIRGL_BUFFER_BYTES) { f->rax=0;return f; }
        KPROCESS *caller=MmCurrentProcess;
        if(caller!=PspSystemProcess)MmSwitchToProcess(PspSystemProcess);
        int ok=gfx_upload_virgl_buffer((uint32_t)bytes);
        if(caller!=PspSystemProcess)MmSwitchToProcess(caller);
        f->rax=ok?1:0;
        return f;
    }

    case SYS_NtPresent3D: {
        if(!gfx_3d_ready) { f->rax=0;return f; }
        KPROCESS *caller=MmCurrentProcess;
        if(caller!=PspSystemProcess)MmSwitchToProcess(PspSystemProcess);
        int ok=gfx_present_virgl();
        if(caller!=PspSystemProcess)MmSwitchToProcess(caller);
        f->rax=ok?1:0;
        return f;
    }

    case SYS_NtSubmit3D: {
        uint64_t user_stream=f->rdi, length=f->rsi;
        if(!length || length>4064 || !gfx_3d_ready ||
           !MmValidateUserRange(MmCurrentProcess,user_stream,length,0)) {
            f->rax=0;return f;
        }
        uint8_t stream[4064];
        for(uint64_t i=0;i<length;i++)stream[i]=((const uint8_t*)user_stream)[i];
        KPROCESS *caller=MmCurrentProcess;
        if(caller!=PspSystemProcess)MmSwitchToProcess(PspSystemProcess);
        int ok=gfx_submit_virgl(stream,(uint32_t)length);
        if(caller!=PspSystemProcess)MmSwitchToProcess(caller);
        f->rax=ok?1:0;
        return f;
    }

    case SYS_NtFlushGraphics: {
        typedef struct { uint32_t x, y, w, h; } GfxUserRect;
        GfxUserRect rect;
        if (!f->rdi ||
            !MmValidateUserRange(MmCurrentProcess, f->rdi, sizeof(rect), 0)) {
            f->rax = 0;
            return f;
        }
        rect = *(GfxUserRect *)f->rdi;
        KPROCESS *caller = MmCurrentProcess;
        if (caller != PspSystemProcess) MmSwitchToProcess(PspSystemProcess);
        int ok = gfx_flush(rect.x, rect.y, rect.w, rect.h);
        if (caller != PspSystemProcess) MmSwitchToProcess(caller);
        f->rax = ok ? 1 : 0;
        return f;
    }

    case SYS_NtInitializeGraphics: {
        GfxBootInfo info = {0};
        if (!f->rdi ||
            !MmValidateUserRange(MmCurrentProcess, f->rdi, sizeof(info), 1)) {
            f->rax = 0;
            return f;
        }
        gfx_boot_initialize(&info, user_fb_w, user_fb_h, user_fb_pitch);
        *(GfxBootInfo *)f->rdi = info;
        f->rax = info.initialized ? 1 : 0;
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
            /* Firmware GOP memory is device memory, not normal WB RAM.
             * Match the kernel's uncached framebuffer mapping; a WB user alias
             * can leave stores invisible to scanout on real hardware/VMs. */
            map_4k_in(cur->Pml4,
                      uva + (i << 12),
                      (user_fb_base + (i << 12)) & ~0xFFFULL,
                      PTE_U | PTE_RW | PTE_PCD | PTE_PWT);
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

    case SYS_NtGetGameMouseDelta: {
        if (!MmCurrentProcess || MmCurrentProcess == PspSystemProcess ||
            wm_get_active_pid() != (int)MmCurrentProcess->Pid) {
            f->rax = 0;
            return f;
        }
        f->rax = ps2_game_mouse_delta();
        return f;
    }

    case SYS_NtGetGameKeyState: {
        /* Only the active user process may poll live game controls. */
        if (!MmCurrentProcess || MmCurrentProcess == PspSystemProcess ||
            wm_get_active_pid() != (int)MmCurrentProcess->Pid) {
            f->rax = 0;
            return f;
        }
        f->rax = ps2_game_key_state();
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
                wm_dbg_input(mepid,out->type,out->x,out->y,out->btn,out->key);
                f->rax = 1;
                return f;
            }

            goto no_input_event;
        }

        ps2_poll();

        if (vi_ok)
            virtio_input_poll();

        /* Tablet owns absolute position when present. Route ordinary pointer
         * motion only to the active window so a background process cannot
         * repaint a game-hidden cursor over the foreground scene. Clicks are
         * still hit-tested and delivered to their actual target below. */
        if (vi_ok && vi_moved && !vi_clicked &&
            wm_get_active_pid() && wm_get_active_pid() != mepid)
            goto no_input_event;

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

                /* Raise/focus the clicked window before routing its button event. */

                wm_activate_pid(target);

                pending_click = *out;
                pending_click_pid = target;
                pending_click_valid = 1;

                if (target != mepid)
                    goto no_input_event;

                pending_click_valid = 0;
            }

            wm_dbg_input(mepid,out->type,out->x,out->y,out->btn,out->key);
            f->rax = 1;
            return f;
        }

        char k = 0;

        /*
         * Keyboard bytes are shared by all user processes. Only the active
         * window may consume them; otherwise Explorer's message pump can eat
         * WASD/Escape before a foreground game ever sees the key.
         */
        int active_input_pid = wm_get_active_pid();
        if ((!active_input_pid || active_input_pid == mepid) && ps2_read_key(&k)) {
            out->type = 1;
            out->key = k;
            out->x = mouse_x;
            out->y = mouse_y;
            out->btn = mouse_btn;
            wm_dbg_input(mepid,out->type,out->x,out->y,out->btn,out->key);
            f->rax = 1;
            return f;
        }

        /* PS/2 relative mouse input, only when no tablet exists. */
        if (!vi_ok) {
            static int lx = -1, ly = -1, lb = -1;

            if (mouse_x != lx || mouse_y != ly || mouse_btn != lb) {
                int pressed = (mouse_btn & 1) && !(lb & 1);
                int active_mouse_pid = wm_get_active_pid();
                if (!pressed && active_mouse_pid && active_mouse_pid != mepid)
                    goto no_input_event;

                out->type = pressed ? 3 : 2;
                out->x = mouse_x;
                out->y = mouse_y;
                out->btn = mouse_btn;

                lx = mouse_x;
                ly = mouse_y;
                lb = mouse_btn;

                wm_dbg_input(mepid,out->type,out->x,out->y,out->btn,out->key);
                if (out->type == 3) {
                    int target = wm_hit_test(out->x, out->y);
                    if (target <= 0) target = mepid;

                    /* Raise/focus the clicked window before routing its button event. */

                    wm_activate_pid(target);

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
        uint64_t dbg_n=++wm_dbg_window_registers;
        DbgPrint("[wm] register #%llu pid=%d title='%s'\n",
                 (unsigned long long)dbg_n,pid,title);

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
        int slot=wm_shared_window_slot(pid);
        uint64_t dbg_n=++wm_dbg_window_closes;
        if(wm_dbg_sample(dbg_n))
            DbgPrint("[wm] unregister #%llu pid=%d slot=%d\n",
                     (unsigned long long)dbg_n,pid,slot);
        int x=0,y=0,w=(int)wm_shared_width,h=(int)wm_shared_height;
        if(slot>=0) { x=wm_tab[slot].x-12; y=wm_tab[slot].y-12; w=wm_tab[slot].w+24; h=wm_tab[slot].h+28; }
        KPROCESS *caller=MmCurrentProcess;
        if(caller!=PspSystemProcess)MmSwitchToProcess(PspSystemProcess);
        if(slot>=0&&wm_shared_windows[slot]) {
            kfree(wm_shared_windows[slot]);wm_shared_windows[slot]=0;
        }
        wm_unregister(pid);
        if(x<0){w+=x;x=0;}if(y<0){h+=y;y=0;}
        if(w>0&&h>0)wm_shared_compose((uint32_t)x,(uint32_t)y,(uint32_t)w,(uint32_t)h);
        if(caller&&caller!=PspSystemProcess)MmSwitchToProcess(caller);
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
        if (!src || !MmValidateUserRange(MmCurrentProcess,(uint64_t)src,sizeof rect,0)) {
            f->rax=0;return f;
        }
        for(int i=0;i<4;i++)rect[i]=src[i];
        int pid=MmCurrentProcess?(int)MmCurrentProcess->Pid:0;
        int slot=wm_shared_window_slot(pid);
        int ox=0,oy=0,ow=0,oh=0;
        if(slot>=0){ox=wm_tab[slot].x-12;oy=wm_tab[slot].y-12;ow=wm_tab[slot].w+24;oh=wm_tab[slot].h+28;}
        KPROCESS *caller=MmCurrentProcess;
        if(caller!=PspSystemProcess)MmSwitchToProcess(PspSystemProcess);
        if(slot>=0&&wm_shared_windows[slot]&&wm_shared_width&&wm_shared_height) {
            int x=wm_tab[slot].x-12,y=wm_tab[slot].y-12;
            int x1=wm_tab[slot].x+wm_tab[slot].w+12,y1=wm_tab[slot].y+wm_tab[slot].h+16;
            if(x<0)x=0;if(y<0)y=0;if(x1>(int)wm_shared_width)x1=(int)wm_shared_width;if(y1>(int)wm_shared_height)y1=(int)wm_shared_height;
            for(int yy=y;yy<y1;yy++)for(int xx=x;xx<x1;xx++)wm_shared_windows[slot][(uint32_t)yy*wm_shared_width+(uint32_t)xx]=0;
        }
        wm_set_rect(pid,rect[0],rect[1],rect[2],rect[3]);
        uint64_t dbg_n=++wm_dbg_window_moves;
        if(wm_dbg_sample(dbg_n))
            DbgPrint("[wm] rect #%llu pid=%d old=(%d,%d %dx%d) new=(%d,%d %dx%d)\n",
                     (unsigned long long)dbg_n,pid,ox+12,oy+12,ow-24,oh-28,
                     rect[0],rect[1],rect[2],rect[3]);
        int nx=rect[0]-12,ny=rect[1]-12,nw=rect[2]+24,nh=rect[3]+28;
        int x0=ox<nx?ox:nx,y0=oy<ny?oy:ny,x1=ox+ow>nx+nw?ox+ow:nx+nw,y1=oy+oh>ny+nh?oy+oh:ny+nh;
        if(x0<0)x0=0;if(y0<0)y0=0;
        if(wm_shared_width&&x1>(int)wm_shared_width)x1=(int)wm_shared_width;
        if(wm_shared_height&&y1>(int)wm_shared_height)y1=(int)wm_shared_height;
        if(x1>x0&&y1>y0)wm_shared_compose((uint32_t)x0,(uint32_t)y0,(uint32_t)(x1-x0),(uint32_t)(y1-y0));
        if(caller&&caller!=PspSystemProcess)MmSwitchToProcess(caller);
        f->rax=0;return f;
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

    case SYS_NtGetTickCount: {
        UserSystemTime now;
        if(!user_boot_rtc_valid||!user_rtc_system_time(&now)){f->rax=(uint64_t)-1LL;return f;}
        uint64_t current=user_rtc_epoch_seconds(&now);
        f->rax=current>=user_boot_rtc_seconds?(current-user_boot_rtc_seconds)*1000u:0u;
        return f;
    }

    case SYS_NtGetSystemTime: {
        UserSystemTime value;
        if(!f->rdi || !MmValidateUserRange(MmCurrentProcess,f->rdi,sizeof(value),1) || !user_rtc_system_time(&value)) {
            f->rax=(uint64_t)-1LL;
            return f;
        }
        *(UserSystemTime *)(uint64_t)f->rdi=value;
        f->rax=0;
        return f;
    }

    case SYS_NtShutdown:
        if (f->rdi == 0)
            HalReturnToFirmware(HalPowerDownRoutine);
        else
            HalReturnToFirmware(HalRebootRoutine);

        f->rax = 0;
        return f;

    case SYS_NtQueryInstallerDisk: {
        if (!f->rdi || !MmValidateUserRange(MmCurrentProcess, f->rdi, sizeof(InstallerDiskInfo), 1)) {
            f->rax = 0;
            return f;
        }
        InstallerDiskInfo info = {0};
        info.sector_size = 512;
        info.sectors = vb_capacity_sectors;
        info.present = (vb_ok && vb_capacity_sectors != 0) ? 1u : 0u;
        *(InstallerDiskInfo *)(uint64_t)f->rdi = info;
        f->rax = info.present ? 1 : 0;
        return f;
    }

    case SYS_NtVerifyInstallerPayload: {
        if (!PspIsSystemProcess(MmCurrentProcess) || f->rdi > 1 || !user_bootinfo) {
            f->rax = (uint64_t)-1LL;
            return f;
        }
        BootModule *package = PspFindModule(user_bootinfo, f->rdi == 0 ? "esp.img" : "system.img");
        BootModule *checksums = PspFindModule(user_bootinfo, "install.sha256");
        if (!package || !checksums || package->size > (1ULL << 30) || checksums->size < 130) {
            f->rax = (uint64_t)-1LL;
            return f;
        }
        const uint8_t *manifest = (const uint8_t *)(HHDM_BASE + checksums->phys);
        const uint8_t *expected = manifest + (f->rdi ? 65 : 0);
        if (manifest[64] != '\n' || manifest[129] != '\n') {
            f->rax = (uint64_t)-1LL;
            return f;
        }
        for (unsigned i = 0; i < 64; i++) {
            uint8_t c = expected[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                f->rax = (uint64_t)-1LL;
                return f;
            }
        }
        uint8_t digest[32];
        const uint8_t *bytes = (const uint8_t *)(HHDM_BASE + package->phys);
        sentinel_sha256(bytes, package->size, digest);
        static const char hex[] = "0123456789abcdef";
        for (unsigned i = 0; i < 32; i++) {
            if (expected[i*2] != (uint8_t)hex[digest[i] >> 4] ||
                expected[i*2+1] != (uint8_t)hex[digest[i] & 15]) {
                f->rax = (uint64_t)-2LL;
                return f;
            }
        }
        f->rax = 0;
        return f;
    }

    case SYS_NtQueryInstallerPayload: {
        if (!PspIsSystemProcess(MmCurrentProcess) || f->rdi > 1 || !f->rsi ||
            !MmValidateUserRange(MmCurrentProcess, f->rsi, sizeof(uint64_t), 1) || !user_bootinfo) {
            f->rax = (uint64_t)-1LL;
            return f;
        }
        BootModule *package = PspFindModule(user_bootinfo, f->rdi == 0 ? "esp.img" : "system.img");
        if (!package) {
            f->rax = (uint64_t)-1LL;
            return f;
        }
        *(uint64_t *)(uint64_t)f->rsi = package->size;
        f->rax = 0;
        return f;
    }

    case SYS_NtReadInstallerPayload: {
        /* Only the trusted setup process can read immutable installer image buffers
           staged by the UEFI loader. Chunks are capped to bound kernel work. */
        if (!PspIsSystemProcess(MmCurrentProcess) || f->rdi > 1 ||
            !user_bootinfo || f->rcx > 65536ULL ||
            (f->rcx && (!f->rdx || !MmValidateUserRange(MmCurrentProcess, f->rdx, f->rcx, 1)))) {
            f->rax = (uint64_t)-1LL;
            return f;
        }
        const char *wanted = f->rdi == 0 ? "esp.img" : "system.img";
        BootModule *package = PspFindModule(user_bootinfo, wanted);
        if (!package || f->rsi > package->size || f->rcx > package->size - f->rsi) {
            f->rax = (uint64_t)-1LL;
            return f;
        }
        if (f->rcx) {
            const uint8_t *src = (const uint8_t *)(HHDM_BASE + package->phys + f->rsi);
            uint8_t *dst = (uint8_t *)(uint64_t)f->rdx;
            for (uint64_t i = 0; i < f->rcx; i++) dst[i] = src[i];
        }
        f->rax = f->rcx;
        return f;
    }

    case SYS_NtInstallerInstallImage: {
        /* Deploy bounded chunks so setup can repaint its progress screen between
           calls instead of blocking the UI for the entire 640 MiB image. */
        if (!PspIsSystemProcess(MmCurrentProcess) || !user_bootinfo || f->rdi > 1 || !vb_ok) {
            f->rax = (uint64_t)-1LL; return f;
        }
        BootModule *image = PspFindModule(user_bootinfo, f->rdi == 0 ? "esp.img" : "system.img");
        uint64_t dest = f->rdi == 0 ? 2048ULL : 264192ULL;
        uint64_t off = f->rsi, bytes = f->rdx;
        if (!image || !image->phys || !image->size || (image->size & 511ULL) ||
            image->size > (1ULL << 30) || dest >= vb_capacity_sectors ||
            image->size / 512ULL > vb_capacity_sectors - dest ||
            (off & 511ULL) || (bytes & 511ULL) || !bytes ||
            bytes > (4ULL * 1024 * 1024) || off > image->size || bytes > image->size - off) {
            f->rax = (uint64_t)-1LL; return f;
        }
        const uint8_t *src = (const uint8_t *)(HHDM_BASE + image->phys);
        uint8_t sector[512];
        if ((off % (32ULL * 1024ULL * 1024ULL)) == 0) {
            DbgPrint("installer: image %llu write progress %llu/%llu MiB\n",
                     (unsigned long long)f->rdi,
                     (unsigned long long)(off / (1024ULL * 1024ULL)),
                     (unsigned long long)(image->size / (1024ULL * 1024ULL)));
        }
        for (uint64_t pos = off; pos < off + bytes; pos += 512ULL) {
            for (unsigned j = 0; j < 512; j++) sector[j] = src[pos + j];
            if (virtio_blk_write_sector(dest + pos / 512ULL, sector) != 0) {
                DbgPrint("installer: image %llu write failed at LBA %llu\n",
                         (unsigned long long)f->rdi,
                         (unsigned long long)(dest + pos / 512ULL));
                f->rax = (uint64_t)-2LL; return f;
            }
        }
        if (off + bytes == image->size) {
            /* Verify first and last sectors only after the final chunk. */
            if (virtio_blk_read_sector(dest, sector) != 0) { f->rax=(uint64_t)-3LL; return f; }
            for (unsigned j=0;j<512;j++) if (sector[j] != src[j]) { f->rax=(uint64_t)-4LL; return f; }
            if (virtio_blk_read_sector(dest + image->size/512ULL - 1, sector) != 0) { f->rax=(uint64_t)-5LL; return f; }
            for (unsigned j=0;j<512;j++) if (sector[j] != src[image->size - 512ULL + j]) { f->rax=(uint64_t)-6LL; return f; }
            DbgPrint("installer: image %llu write progress %llu/%llu MiB (verified)\n",
                     (unsigned long long)f->rdi,
                     (unsigned long long)(image->size / (1024ULL * 1024)),
                     (unsigned long long)(image->size / (1024ULL * 1024)));
        }
        f->rax = 0; return f;
    }

    case SYS_NtInstallerDiskIo: {
        /* Raw disk writes are deliberately restricted to the trusted setup app.
           The caller may access one sector at a time; bounds are enforced here. */
        if (!PspIsSystemProcess(MmCurrentProcess) || !vb_ok ||
            f->rsi >= vb_capacity_sectors || !f->rdx ||
            !MmValidateUserRange(MmCurrentProcess, f->rdx, 512, f->rdi == 0)) {
            f->rax = (uint64_t)-1LL;
            return f;
        }
        if (f->rdi == 0)
            f->rax = virtio_blk_read_sector(f->rsi, (void *)(uint64_t)f->rdx) == 0 ? 0 : (uint64_t)-1LL;
        else if (f->rdi == 1)
            f->rax = virtio_blk_write_sector(f->rsi, (const void *)(uint64_t)f->rdx) == 0 ? 0 : (uint64_t)-1LL;
        else
            f->rax = (uint64_t)-1LL;
        return f;
    }

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
    UserSystemTime boot_time;
    user_boot_rtc_valid=user_rtc_system_time(&boot_time);
    if(user_boot_rtc_valid)user_boot_rtc_seconds=user_rtc_epoch_seconds(&boot_time);
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
    up_tab[0].system_app = 1; /* smss is the trusted system-process root */
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