#pragma once
#include <stdint.h>
#include "serial.h"
#include "cpu.h"
#include "vmm.h"
#include "pmm.h"
#include "heap.h"
#include "bootinfo.h"
#include "ps2.h"

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

static uint8_t user_kstack[16 * 1024] __attribute__((aligned(16)));
static uint64_t user_saved_ksp, user_saved_rip;
static BootInfo *user_bootinfo;
static KPROCESS *user_pending;
static uint64_t  user_pending_entry;
static uint64_t user_fb_base;
static uint32_t user_fb_w, user_fb_h, user_fb_pitch;
static int mod_relocated[BOOT_MAX_MODULES];

static void pe_apply_relocs(uint8_t *phys, int64_t delta) {
    if (!delta) return;
    if (phys[0] != 'M' || phys[1] != 'Z') return;
    int32_t lfanew = *(int32_t *)(phys + 0x3C);
    uint8_t *nth = phys + lfanew;
    if (*(uint32_t *)nth != 0x4550) return;
    uint8_t *opt = nth + 4 + 20;
    if (*(uint16_t *)opt != 0x20B) return;
    uint32_t rel_rva = *(uint32_t *)(opt + 112 + 5 * 8);
    uint32_t rel_sz  = *(uint32_t *)(opt + 112 + 5 * 8 + 4);
    if (!rel_rva || !rel_sz) return;
    uint8_t *p = phys + rel_rva, *end = p + rel_sz;
    while (p + 8 <= end) {
        uint32_t va = *(uint32_t *)p, sz = *(uint32_t *)(p + 4);
        if (sz < 8) break;
        uint16_t *e = (uint16_t *)(p + 8);
        uint32_t n = (sz - 8) / 2;
        for (uint32_t i = 0; i < n; i++)
            if ((e[i] >> 12) == 10)
                *(uint64_t *)(phys + va + (e[i] & 0xFFF)) += (uint64_t)delta;
        p += sz;
    }
}

static BootModule *PspFindModule(BootInfo *bi, const char *name) {
    for (uint32_t i = 0; i < bi->module_count; i++) {
        int match = 1;
        for (int k = 0; name[k]; k++) {
            char a = bi->modules[i].name[k], b = name[k];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { match = 0; break; }
        }
        if (match) return &bi->modules[i];
    }
    return NULL;
}

static int PspMapUserImage(KPROCESS *proc, BootModule *mod) {
    uint64_t prefer = mod->base, phys = mod->phys, size = mod->size;
    if (!prefer || !phys || !size || !user_bootinfo) return -1;
    int idx = (int)(mod - user_bootinfo->modules);
    if (idx >= 0 && idx < (int)BOOT_MAX_MODULES && !mod_relocated[idx]) {
        pe_apply_relocs((uint8_t *)(HHDM_BASE + phys), (int64_t)(prefer - phys));
        mod_relocated[idx] = 1;
    }
    for (uint64_t off = 0; off < size; off += 0x1000)
        map_4k_in(proc->Pml4, prefer + off, phys + off, PTE_U | PTE_RW);
    /* map user stack only if missing – remapping zeroes it and breaks GetProcAddress */
    {
        uint64_t va = USER_STACK_TOP - 0x1000;
        int need = 1;
        uint64_t *pml4 = proc->Pml4;
        int i4=(va>>39)&511, i3=(va>>30)&511, i2=(va>>21)&511, i1=(va>>12)&511;
        if ((pml4[i4] & 1) && !(pml4[i4] & 0x80)) {
            uint64_t *pdpt = (uint64_t *)(pml4[i4] & ~0xFFFULL);
            if ((pdpt[i3] & 1) && !(pdpt[i3] & 0x80)) {
                uint64_t *pd = (uint64_t *)(pdpt[i3] & ~0xFFFULL);
                if ((pd[i2] & 1) && !(pd[i2] & 0x80)) {
                    uint64_t *pt = (uint64_t *)(pd[i2] & ~0xFFFULL);
                    if (pt[i1] & 1) need = 0;
                }
            }
        }
        if (need) MmMapUserPage(proc, va, 1);
    }
    return 0;
}

static int copy_user_str(uint64_t uaddr, char *out, int max) {
    if (uaddr < 0x10000 || uaddr >= 0x0000800000000000ULL) return -1;
    for (int i = 0; i < max - 1; i++) {
        char c = *(volatile char *)(uaddr + (uint64_t)i);
        out[i] = c;
        if (!c) return 0;
    }
    out[max - 1] = 0;
    return 0;
}

static uint64_t pe_get_export(BootModule *mod, const char *func) {
    uint8_t *phys = (uint8_t *)(HHDM_BASE + mod->phys);
    if (phys[0] != 'M' || phys[1] != 'Z') return 0;
    int32_t lfanew = *(int32_t *)(phys + 0x3C);
    uint8_t *opt = phys + lfanew + 4 + 20;
    if (*(uint16_t *)opt != 0x20B) return 0;
    uint32_t exp_rva = *(uint32_t *)(opt + 112);
    uint32_t exp_sz  = *(uint32_t *)(opt + 116);
    if (!exp_rva || !exp_sz) return 0;
    uint8_t *exp = phys + exp_rva;
    uint32_t nnames = *(uint32_t *)(exp + 24);
    uint32_t names_rva = *(uint32_t *)(exp + 32);
    uint32_t ords_rva  = *(uint32_t *)(exp + 36);
    uint32_t funcs_rva = *(uint32_t *)(exp + 28);
    for (uint32_t i = 0; i < nnames; i++) {
        uint32_t nrva = *(uint32_t *)(phys + names_rva + i * 4);
        const char *n = (const char *)(phys + nrva);
        int match = 1;
        for (int k = 0; func[k] || n[k]; k++)
            if (func[k] != n[k]) { match = 0; break; }
        if (!match) continue;
        uint16_t ord = *(uint16_t *)(phys + ords_rva + i * 2);
        uint32_t frva = *(uint32_t *)(phys + funcs_rva + ord * 4);
        return mod->base + frva;
    }
    return 0;
}

static Frame *syscall_handler(Frame *f) {
    switch (f->rax) {
    case SYS_NtTerminateProcess: {
        DbgPrint("[user] exit(%llu)\n", f->rdi);
        if (user_pending) {
            KPROCESS *next = user_pending;
            uint64_t entry = user_pending_entry;
            user_pending = 0; user_pending_entry = 0;
            if (ki_current) ki_current->Process = next;
            MmSwitchToProcess(next);
            DbgPrint("user: run pending pid=%lu entry=%p\n",
                     (unsigned long)next->Pid, (void *)entry);
            f->rip = entry; f->cs = USER_CS; f->ss = USER_DS;
            f->rsp = USER_STACK_TOP; f->rflags = 0x202; f->rax = 0;
            return f;
        }
        MmSwitchToProcess(PspSystemProcess);
        if (ki_current) ki_current->Process = PspSystemProcess;
        f->rip = user_saved_rip; f->cs = KERNEL_CS; f->ss = KERNEL_DS;
        f->rsp = user_saved_ksp; f->rflags = 0x202;
        return f;
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
        if (!size || size > 4ull * 1024 * 1024) { f->rax = 0; return f; }
        KPROCESS *proc = MmCurrentProcess;
        if (!proc || proc == PspSystemProcess) { f->rax = 0; return f; }
        if (!prefer) { static uint64_t bump = 0x50000000ULL; prefer = bump; bump += size; }
        MmSwitchToProcess(PspSystemProcess);
        int ok = 1;
        for (uint64_t off = 0; off < size; off += 0x1000) {
            uint64_t pa = pmm_alloc_zeroed();
            if (!pa) { ok = 0; break; }
            map_4k_in(proc->Pml4, prefer + off, pa, PTE_U | PTE_RW);
        }
        MmSwitchToProcess(proc);
        if (!ok) { f->rax = 0; return f; }
        f->rax = prefer;
        return f;
    }
    case SYS_NtCreateProcess: {
        char name[32];
        if (copy_user_str(f->rdi, name, sizeof name) != 0) { f->rax = (uint64_t)-1LL; return f; }
        BootModule *mod = PspFindModule(user_bootinfo, name);
        if (!mod) { DbgPrint("user: NtCreateProcess '%s' not found\n", name); f->rax = (uint64_t)-1LL; return f; }
        KPROCESS *caller = MmCurrentProcess;
        MmSwitchToProcess(PspSystemProcess);
        KPROCESS *proc = MmCreateAddressSpace(name);
        int ok = proc && (PspMapUserImage(proc, mod) == 0);
        uint64_t entry = mod->entry;
        MmSwitchToProcess(caller);
        if (!ok) { f->rax = (uint64_t)-1LL; return f; }
        user_pending = proc; user_pending_entry = entry;
        DbgPrint("user: NtCreateProcess '%s' pid=%lu pending\n", name, (unsigned long)proc->Pid);
        f->rax = 0;
        return f;
    }
    case SYS_NtOpenFile: {
        char name[32];
        if (copy_user_str(f->rdi, name, sizeof name) != 0) { f->rax = (uint64_t)-1LL; return f; }
        BootModule *mod = PspFindModule(user_bootinfo, name);
        if (!mod) { f->rax = (uint64_t)-1LL; return f; }
        for (uint32_t i = 0; i < user_bootinfo->module_count; i++)
            if (&user_bootinfo->modules[i] == mod) { f->rax = i + 1; return f; }
        f->rax = (uint64_t)-1LL;
        return f;
    }
    case SYS_NtReadFile: {
        uint64_t h = f->rdi, uaddr = f->rsi, len = f->rdx;
        if (!h || h > user_bootinfo->module_count) { f->rax = (uint64_t)-1LL; return f; }
        BootModule *mod = &user_bootinfo->modules[h - 1];
        if (len > mod->size) len = mod->size;
        uint8_t *src = (uint8_t *)(HHDM_BASE + mod->phys);
        for (uint64_t i = 0; i < len; i++) ((uint8_t *)uaddr)[i] = src[i];
        f->rax = len;
        return f;
    }
    case SYS_NtQueryDirectoryFile: {
        uint64_t index = f->rdi, uname = f->rsi;
        if (index >= user_bootinfo->module_count) { f->rax = 1; return f; }
        char *dst = (char *)uname;
        const char *src = user_bootinfo->modules[index].name;
        for (int i = 0; i < 31; i++) { dst[i] = src[i]; if (!src[i]) break; }
        dst[31] = 0;
        f->rax = 0;
        return f;
    }
    case SYS_NtMapFramebuffer: {
        if (!user_fb_base || !f->rdi) { f->rax = 0; return f; }
        KPROCESS *proc = MmCurrentProcess;
        if (!proc || proc == PspSystemProcess) { f->rax = 0; return f; }
        uint64_t uva = 0x60000000ULL;
        uint64_t size = (uint64_t)user_fb_pitch * user_fb_h;
        if (size > 32ull * 1024 * 1024) size = 32ull * 1024 * 1024;
        MmSwitchToProcess(PspSystemProcess);
        for (uint64_t off = 0; off < size; off += 0x1000) {
            uint64_t pa = (user_fb_base + off) & ~0xFFFULL;
            map_4k_in(proc->Pml4, uva + off, pa, PTE_U | PTE_RW);
        }
        {
            uint32_t *fb = (uint32_t *)(void *)user_fb_base;
            uint32_t pitch = user_fb_pitch / 4;
            uint32_t w = user_fb_w, h = user_fb_h;
            for (uint32_t y = 0; y < h; y++)
                for (uint32_t x = 0; x < w; x++)
                    fb[y * pitch + x] = 0xFF1A1A2E;
            uint32_t y0 = h > 56 ? h - 56 : 0;
            for (uint32_t y = y0; y < h; y++)
                for (uint32_t x = 0; x < w; x++)
                    fb[y * pitch + x] = 0xFF16213E;
            for (uint32_t x = 0; x < w; x++)
                fb[y0 * pitch + x] = 0xFFE94560;
            DbgPrint("user: FB mapped %ux%u @%p\n", w, h, (void *)user_fb_base);
        }
        MmSwitchToProcess(proc);
        *(uint64_t *)f->rdi = uva;
        *(uint32_t *)(f->rdi + 8) = user_fb_w;
        *(uint32_t *)(f->rdi + 12) = user_fb_h;
        *(uint32_t *)(f->rdi + 16) = user_fb_pitch;
        f->rax = uva;
        return f;
    }
    case SYS_NtGetProcAddress: {
        char dll[32], fn[64];
        if (copy_user_str(f->rdi, dll, sizeof dll) != 0 ||
            copy_user_str(f->rsi, fn, sizeof fn) != 0) { f->rax = 0; return f; }
        BootModule *mod = PspFindModule(user_bootinfo, dll);
        if (!mod) { DbgPrint("user: GetProcAddress: no %s\n", dll); f->rax = 0; return f; }
        KPROCESS *caller = MmCurrentProcess;
        MmSwitchToProcess(PspSystemProcess);
        PspMapUserImage(caller, mod);
        MmSwitchToProcess(caller);
        uint64_t addr = pe_get_export(mod, fn);
        DbgPrint("user: GetProcAddress %s!%s = %p\n", dll, fn, (void *)addr);
        f->rax = addr;
        return f;
    }

    case SYS_NtGetInput: {
        for (int i = 0; i < 64; i++) {
            if (!(ps2_inb(0x64) & 1)) break;
            ps2_handle_byte(ps2_inb(0x60));
        }
        int *out = (int *)f->rdi;
        if (!out) { f->rax = 0; return f; }
        while (kbd_r != kbd_w) {
            uint8_t sc = kbd_buf[kbd_r++ % 64];
            if (sc & 0x80) continue;
            int step = 24;
            if (sc == 0x48 || sc == 0x11) { mouse_y -= step; if (mouse_y < 0) mouse_y = 0; }
            else if (sc == 0x50 || sc == 0x1F) { mouse_y += step; if (mouse_y > 799) mouse_y = 799; }
            else if (sc == 0x4B || sc == 0x1E) { mouse_x -= step; if (mouse_x < 0) mouse_x = 0; }
            else if (sc == 0x4D || sc == 0x20) { mouse_x += step; if (mouse_x > 1279) mouse_x = 1279; }
            else if (sc == 0x1C || sc == 0x39) {
                out[0] = 3; out[1] = mouse_x; out[2] = mouse_y; out[3] = 1;
                f->rax = 1; return f;
            } else {
                char key = ps2_scancode_to_ascii(sc);
                if (key) {
                    out[0] = 1; out[1] = mouse_x; out[2] = mouse_y; out[3] = mouse_btn;
                    ((char *)out)[16] = key;
                    f->rax = 1; return f;
                }
            }
        }
        static int last_mx = -1, last_my = -1, last_btn = -1;
        if (mouse_x != last_mx || mouse_y != last_my || mouse_btn != last_btn) {
            int down = (mouse_btn & 1) && !(last_btn & 1);
            out[0] = down ? 3 : 2;
            out[1] = mouse_x; out[2] = mouse_y; out[3] = mouse_btn;
            last_mx = mouse_x; last_my = mouse_y; last_btn = mouse_btn;
            f->rax = 1; return f;
        }
        f->rax = 0;
        return f;
    }
    default:
        DbgPrint("[user] unknown syscall %llu\n", f->rax);
        f->rax = (uint64_t)-1LL;
        return f;
    }
}

static void user_enter(BootInfo *bi) {
    ps2_init();
    DbgPrint("\n-- usermode (smss) --\n");
    user_bootinfo = (BootInfo *)(HHDM_BASE + (uint64_t)bi);
    user_fb_base = bi->fb_base;
    user_fb_w = bi->width;
    user_fb_h = bi->height;
    user_fb_pitch = bi->pitch;
    user_pending = 0;
    DbgPrint("fs: C: mounted -> System32 (%u files)\n", user_bootinfo->module_count);

    DbgPrint("user: BootInfo modules (%u)\n", user_bootinfo->module_count);
    for (uint32_t i = 0; i < user_bootinfo->module_count; i++)
        DbgPrint("  [%u] '%s'\n", i, user_bootinfo->modules[i].name);

    BootModule *smss = PspFindModule(user_bootinfo, "smss.exe");
    if (!smss) { DbgPrint("user: smss.exe not loaded\n"); return; }

    KPROCESS *proc = MmCreateAddressSpace("smss");
    if (!proc || PspMapUserImage(proc, smss) != 0) {
        DbgPrint("user: map smss failed\n"); return;
    }

    uint64_t ktop = ((uint64_t)user_kstack + sizeof user_kstack) & ~15ULL;
    user_set_kernel_stack(ktop);
    if (ki_current) ki_current->Process = proc;

    uint64_t entry = smss->entry;
    DbgPrint("user: pid=%lu enter smss %p\n", (unsigned long)proc->Pid, (void *)entry);

    uint64_t *s = (uint64_t *)ktop;
    *--s = USER_DS; *--s = USER_STACK_TOP; *--s = 0x202; *--s = USER_CS; *--s = entry;
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
        : "rcx", "memory");

    MmSwitchToProcess(PspSystemProcess);
    if (ki_current) ki_current->Process = PspSystemProcess;
    DbgPrint("user: all user processes exited, back in kernel\n");
}
