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
    for (int i = 0; i < UP_MAX; i++)
        if (up_tab[i].alive && up_tab[i].proc == p) return i;
    return -1;
}
static int up_pick_next(void) {
    int start = (up_cur < 0) ? 0 : up_cur;
    for (int k = 1; k <= UP_MAX; k++) {
        int i = (start + k) % UP_MAX;
        if (up_tab[i].alive && up_tab[i].proc) return i;
    }
    return -1;
}

static uint64_t user_fb_base;
static uint32_t user_fb_w, user_fb_h, user_fb_pitch;
static int mod_relocated[BOOT_MAX_MODULES];
static int pe_resolving;
static int fb_map_logged;

static void pe_apply_relocs(uint8_t *phys, int64_t delta) {
    if (phys[0] != 'M' || phys[1] != 'Z') return;
    int32_t lfanew = *(int32_t *)(phys + 0x3C);
    uint8_t *opt = phys + lfanew + 4 + 20;
    if (*(uint16_t *)opt != 0x20B) return;
    uint32_t reloc_rva = *(uint32_t *)(opt + 112 + 5 * 8);
    uint32_t reloc_sz  = *(uint32_t *)(opt + 112 + 5 * 8 + 4);
    if (!reloc_rva || !reloc_sz) return;
    uint8_t *rel = phys + reloc_rva;
    uint8_t *end = rel + reloc_sz;
    while (rel + 8 <= end) {
        uint32_t page_rva = *(uint32_t *)rel;
        uint32_t block_sz = *(uint32_t *)(rel + 4);
        if (block_sz < 8) break;
        uint32_t count = (block_sz - 8) / 2;
        uint16_t *entry = (uint16_t *)(rel + 8);
        for (uint32_t i = 0; i < count; i++) {
            uint16_t e = entry[i];
            uint16_t type = e >> 12;
            uint16_t off = e & 0xFFF;
            if (type == 10) {
                uint64_t *p = (uint64_t *)(phys + page_rva + off);
                *p += (uint64_t)delta;
            } else if (type == 3) {
                uint32_t *p = (uint32_t *)(phys + page_rva + off);
                *p += (uint32_t)delta;
            }
        }
        rel += block_sz;
    }
}

static BootModule *PspFindModule(BootInfo *bi, const char *name) {
    for (uint32_t i = 0; i < bi->module_count; i++) {
        const char *n = bi->modules[i].name;
        int m = 1;
        for (int k = 0; name[k] || n[k]; k++) {
            char a = name[k], b = n[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) { m = 0; break; }
        }
        if (m) return &bi->modules[i];
    }
    return 0;
}

static int PspMapUserImage(KPROCESS *proc, BootModule *mod) {
    if (!proc || !mod || !mod->phys || !mod->size) return -1;
    uint8_t *phys = (uint8_t *)(HHDM_BASE + mod->phys);
    uint64_t base = mod->base ? mod->base : 0x40000000ULL;
    uint64_t size = (mod->size + 0xFFF) & ~0xFFFULL;
    for (uint64_t off = 0; off < size; off += 0x1000) {
        uint64_t pa = mod->phys + off;
        map_4k_in(proc->Pml4, base + off, pa, PTE_U | PTE_RW);
    }
    if (phys[0] == 'M' && phys[1] == 'Z') {
        int32_t lfanew = *(int32_t *)(phys + 0x3C);
        uint8_t *opt = phys + lfanew + 4 + 20;
        if (*(uint16_t *)opt == 0x20B) {
            uint64_t img_base = *(uint64_t *)(opt + 24);
            int64_t delta = (int64_t)base - (int64_t)img_base;
            if (delta) pe_apply_relocs(phys, delta);
            uint32_t ep = *(uint32_t *)(opt + 16);
            mod->entry = base + ep;
            mod->base = base;
        }
    } else {
        mod->base = base;
        if (!mod->entry) mod->entry = base;
    }
    return 0;
}

static uint64_t pe_get_export(BootModule *mod, const char *func) {
    if (!mod || !mod->phys) return 0;
    uint8_t *phys = (uint8_t *)(HHDM_BASE + mod->phys);
    if (phys[0] != 'M' || phys[1] != 'Z') return 0;
    int32_t lfanew = *(int32_t *)(phys + 0x3C);
    uint8_t *opt = phys + lfanew + 4 + 20;
    if (*(uint16_t *)opt != 0x20B) return 0;
    uint32_t exp_rva = *(uint32_t *)(opt + 112);
    if (!exp_rva) return 0;
    uint8_t *exp = phys + exp_rva;
    uint32_t nnames = *(uint32_t *)(exp + 24);
    uint32_t names_rva = *(uint32_t *)(exp + 32);
    uint32_t ords_rva  = *(uint32_t *)(exp + 36);
    uint32_t funcs_rva = *(uint32_t *)(exp + 28);
    for (uint32_t i = 0; i < nnames; i++) {
        uint32_t nrva = *(uint32_t *)(phys + names_rva + i * 4);
        const char *n = (const char *)(phys + nrva);
        int match = 1;
        for (int k = 0; func[k] || n[k]; k++) {
            if (func[k] != n[k]) { match = 0; break; }
        }
        if (!match) continue;
        uint16_t ord = *(uint16_t *)(phys + ords_rva + i * 2);
        uint32_t frva = *(uint32_t *)(phys + funcs_rva + ord * 4);
        return mod->base + frva;
    }
    return 0;
}

static void pe_resolve_imports(KPROCESS *proc, BootModule *mod) {
    uint8_t *phys = (uint8_t *)(HHDM_BASE + mod->phys);
    if (phys[0] != 'M' || phys[1] != 'Z') return;
    int32_t lfanew = *(int32_t *)(phys + 0x3C);
    uint8_t *opt = phys + lfanew + 4 + 20;
    if (*(uint16_t *)opt != 0x20B) return;
    uint32_t imp_rva = *(uint32_t *)(opt + 112 + 1 * 8);
    if (!imp_rva) return;
    for (uint32_t off = 0; ; off += 20) {
        uint8_t *desc = phys + imp_rva + off;
        uint32_t name_rva = *(uint32_t *)(desc + 12);
        uint32_t ft_rva   = *(uint32_t *)(desc + 16);
        uint32_t oft_rva  = *(uint32_t *)(desc + 0);
        if (!name_rva || !ft_rva) break;
        const char *dll = (const char *)(phys + name_rva);
        BootModule *dep = PspFindModule(user_bootinfo, dll);
        if (!dep) continue;
        PspMapUserImage(proc, dep);
        uint32_t look_rva = oft_rva ? oft_rva : ft_rva;
        for (uint32_t i = 0; ; i++) {
            uint64_t look = *(uint64_t *)(phys + look_rva + i * 8);
            if (!look) break;
            uint64_t *iat = (uint64_t *)(phys + ft_rva + i * 8);
            uint64_t addr = 0;
            if (!(look & 0x8000000000000000ULL)) {
                const char *fn = (const char *)(phys + (uint32_t)look + 2);
                addr = pe_get_export(dep, fn);
            }
            *iat = addr;
        }
    }
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
        ut_exit_current();
        if (dying && dying != PspSystemProcess) {
            wm_unregister((int)dying->Pid);
            for (int i = 0; i < UP_MAX; i++)
                if (up_tab[i].proc == dying) up_tab[i].alive = 0;
        }
        int nxt = up_pick_next();
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
        if (!proc || PspMapUserImage(proc, mod) != 0) {
            MmSwitchToProcess(caller);
            f->rax = (uint64_t)-1LL;
            return f;
        }
        pe_resolve_imports(proc, mod);
        uint64_t entry = mod->entry;

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
            PspMapUserImage(cur, mod);
            pe_resolve_imports(cur, mod);
            addr = pe_get_export(mod, func);
            MmSwitchToProcess(cur);
            DbgPrint("user: GetProcAddress %s!%s = %p", dll, func, (void *)addr);
        }
        f->rax = addr;
        return f;
    }
    case SYS_NtGetInput: {
        typedef struct { int type, x, y, btn; char key; } UIn;
        UIn *out = (UIn *)f->rdi;
        if (!out) { f->rax = 0; return f; }

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
                f->rax = 1;
                return f;
            }
        }

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
    case SYS_WmRestore: {
        wm_restore_pid((int)f->rdi);
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
