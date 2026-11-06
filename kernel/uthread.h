#pragma once
#include <stdint.h>

#define UT_MAX 16
#define UT_STACK_SIZE 0x10000ULL
#define UT_HANDLE_SLOT_BITS 5u
#define UT_HANDLE_SLOT_MASK ((1u << UT_HANDLE_SLOT_BITS) - 1u)
#define UT_HANDLE_GENERATION_MASK 0x03FFFFFFu

typedef struct {
    KPROCESS *proc;
    uint64_t entry;
    uint64_t param;
    uint64_t rip, rsp, rflags, rax;
    /* Callee-saved GPRs (x86-64 ABI): preserved across context switches. */
    uint64_t rbx, rcx, rdx, rsi, rdi, r8, r9, r10, r11, r12, r13, r14, r15, rbp;
    uint64_t stack_base;
    uint64_t teb_base;
    int alive;
    int fresh;
    int finished;
    int handle;
    /* >= 0: handle of the thread this thread is waiting on (blocked in ut_wait).
     * -1: not waiting on any thread. */
    int waiting_on;
} UThread;

static UThread ut_tab[UT_MAX];
static int ut_n;
static int ut_cur = -1;
static volatile uint32_t ut_next_generation;

/* Handles carry a generation so reusing a table slot cannot revive a stale ID. */
static int ut_make_handle(int slot) {
    if (slot < 0 || slot >= UT_MAX) return 0;
    uint32_t generation;
    do {
        generation = __atomic_add_fetch(&ut_next_generation, 1u,
                                        __ATOMIC_RELAXED) &
                     UT_HANDLE_GENERATION_MASK;
    } while (!generation);
    return (int)((generation << UT_HANDLE_SLOT_BITS) | (uint32_t)(slot + 1));
}

static int ut_find_handle(int h) {
    if (h <= 0) return -1;
    uint32_t value = (uint32_t)h;
    uint32_t encoded_slot = value & UT_HANDLE_SLOT_MASK;
    if (!encoded_slot || encoded_slot > UT_MAX) return -1;
    int slot = (int)encoded_slot - 1;
    UThread *thread = &ut_tab[slot];
    if (thread->handle != h || (!thread->alive && !thread->finished) ||
        !thread->proc || thread->proc != MmCurrentProcess) return -1;
    return slot;
}
static int ut_find_slot(void) {
    for (int i = 0; i < UT_MAX; i++)
        if (!ut_tab[i].alive) return i;
    return -1;
}
static void ut_save(Frame *f, int idx) {
    if (idx < 0 || idx >= UT_MAX || !ut_tab[idx].alive) return;
    ut_tab[idx].rip = f->rip;
    ut_tab[idx].rsp = f->rsp;
    ut_tab[idx].rflags = f->rflags;
    ut_tab[idx].rax = f->rax;
    /* Callee-saved GPRs (x86-64 ABI): rbx, rbp, rsi, rdi, r12-r15 plus
     * caller-saved that a thread may expect preserved across a yield. */
    ut_tab[idx].rbx = f->rbx;
    ut_tab[idx].rcx = f->rcx;
    ut_tab[idx].rdx = f->rdx;
    ut_tab[idx].rsi = f->rsi;
    ut_tab[idx].rdi = f->rdi;
    ut_tab[idx].r8 = f->r8;
    ut_tab[idx].r9 = f->r9;
    ut_tab[idx].r10 = f->r10;
    ut_tab[idx].r11 = f->r11;
    ut_tab[idx].r12 = f->r12;
    ut_tab[idx].r13 = f->r13;
    ut_tab[idx].r14 = f->r14;
    ut_tab[idx].r15 = f->r15;
    ut_tab[idx].rbp = f->rbp;
    ut_tab[idx].fresh = 0;
}
/* A completed thread keeps its handle state for waits, but no longer needs
 * its private stack or TEB mappings. The unmap helper validates PTE_OWNED
 * before releasing each frame, so cleanup cannot free borrowed pages. */
static void ut_release_resources(UThread *thread) {
    if (!thread || !thread->proc) return;
    if (thread->stack_base) {
        for (uint64_t off = 0; off < UT_STACK_SIZE; off += 0x1000)
            MmUnmapAllocatedUserPage(thread->proc, thread->stack_base + off);
        thread->stack_base = 0;
    }
    if (thread->teb_base) {
        MmUnmapAllocatedUserPage(thread->proc, thread->teb_base);
        thread->teb_base = 0;
    }
}
static void ut_load(Frame *f, int idx) {
    UThread *t = &ut_tab[idx];
    if (ki_current) ki_current->Process = t->proc;
    MmSwitchToProcess(t->proc);
    cpu_set_user_gs_base(t->teb_base ? t->teb_base : USER_TEB_BASE);
    f->cs = USER_CS;
    f->ss = USER_DS;
    if (t->fresh) {
        f->rip = t->entry;
        f->rsp = t->stack_base + UT_STACK_SIZE - 0x28;
        f->rflags = 0x202;
        f->rcx = t->param;
        f->rax = 0;
        /* Fresh thread: all other GPRs start clean (zeroed). */
        f->rbx = 0;
        f->rdx = 0;
        f->rsi = 0;
        f->rdi = 0;
        f->r8 = 0;
        f->r9 = 0;
        f->r10 = 0;
        f->r11 = 0;
        f->r12 = 0;
        f->r13 = 0;
        f->r14 = 0;
        f->r15 = 0;
        f->rbp = 0;
        t->fresh = 0;
    } else {
        f->rip = t->rip;
        f->rsp = t->rsp;
        f->rflags = t->rflags ? t->rflags : 0x202;
        f->rax = t->rax;
        /* Restore callee-saved GPRs so the thread resumes with its pre-switch state. */
        f->rbx = t->rbx;
        f->rcx = t->rcx;
        f->rdx = t->rdx;
        f->rsi = t->rsi;
        f->rdi = t->rdi;
        f->r8 = t->r8;
        f->r9 = t->r9;
        f->r10 = t->r10;
        f->r11 = t->r11;
        f->r12 = t->r12;
        f->r13 = t->r13;
        f->r14 = t->r14;
        f->r15 = t->r15;
        f->rbp = t->rbp;
    }
    ut_cur = idx;
}
static int ut_pick_next(void) {
    int start = ut_cur < 0 ? 0 : ut_cur;
    for (int k = 1; k <= UT_MAX; k++) {
        int i = (start + k) % UT_MAX;
        if (ut_tab[i].alive && ut_tab[i].waiting_on < 0) return i;
    }
    return -1;
}
static uint64_t ut_create(uint64_t start, uint64_t param) {
    KPROCESS *proc = MmCurrentProcess;
    if (!proc || proc == PspSystemProcess || !start) return (uint64_t)-1LL;
    int slot = ut_find_slot();
    if (slot < 0) return (uint64_t)-1LL;
    MmSwitchToProcess(PspSystemProcess);
    uint64_t stack_va = 0x7FFE0000ULL - (uint64_t)(slot + 1) * UT_STACK_SIZE;
    uint64_t mapped = 0;
    for (; mapped < UT_STACK_SIZE; mapped += 0x1000) {
        uint64_t pa = pmm_alloc_zeroed();
        if (!pa) break;
        map_4k_in(proc->Pml4, stack_va + mapped, pa,
                  PTE_U | PTE_RW | PTE_OWNED);
    }
    if (mapped != UT_STACK_SIZE) {
        while (mapped) {
            mapped -= 0x1000;
            MmUnmapAllocatedUserPage(proc, stack_va + mapped);
        }
        MmSwitchToProcess(proc);
        return (uint64_t)-1LL;
    }
    uint64_t teb_va = USER_THREAD_TEB_BASE + (uint64_t)slot * 0x2000ULL;
    uint64_t teb_pa = pmm_alloc_zeroed();
    if (!teb_pa) {
        while (mapped) {
            mapped -= 0x1000;
            MmUnmapAllocatedUserPage(proc, stack_va + mapped);
        }
        MmSwitchToProcess(proc);
        return (uint64_t)-1LL;
    }
    map_4k_in(proc->Pml4, teb_va, teb_pa, PTE_U | PTE_RW | PTE_OWNED);
    uint64_t *teb = (uint64_t *)(uintptr_t)PHYS_TO_VIRT(teb_pa);
    teb[1] = stack_va + UT_STACK_SIZE;
    teb[2] = stack_va;
    teb[6] = teb_va;
    teb[12] = USER_PEB_BASE;
    MmSwitchToProcess(proc);
    UThread *t = &ut_tab[slot];
    t->proc = proc;
    t->entry = start;
    t->param = param;
    t->stack_base = stack_va;
    t->teb_base = teb_va;
    t->rip = start;
    t->rsp = stack_va + UT_STACK_SIZE - 0x28;
    t->rflags = 0x202;
    t->alive = 1;
    t->fresh = 1;
    t->finished = 0;
    t->waiting_on = -1;
    t->handle = ut_make_handle(slot);
    if (slot >= ut_n) ut_n = slot + 1;
    DbgPrint("uthread: created");
    return (uint64_t)t->handle;
}
static void ut_exit_current(void) {
    if (ut_cur >= 0 && ut_cur < UT_MAX && ut_tab[ut_cur].alive) {
        UThread *thread = &ut_tab[ut_cur];
        thread->finished = 1;
        /* Wake any threads blocked waiting on this thread's handle so
         * ut_pick_next can schedule them again. */
        uint64_t my_handle = thread->handle;
        for (int i = 0; i < UT_MAX; i++) {
            if (ut_tab[i].alive && ut_tab[i].waiting_on == (int)my_handle) {
                ut_tab[i].waiting_on = -1;
            }
        }
        thread->alive = 0;
        ut_release_resources(thread);
    }
}
static uint64_t ut_wait(Frame *f, int handle, uint32_t ms) {
    int idx = ut_find_handle(handle);
    if (idx < 0) return 0xFFFFFFFFULL; /* WAIT_FAILED */
    if (ut_tab[idx].finished) return 0; /* WAIT_OBJECT_0 */
    if (!ut_tab[idx].alive) return 0xFFFFFFFFULL;

    /* ms == 0: non-blocking poll only. */
    if (ms == 0) return 0x102ULL; /* STATUS_TIMEOUT */

    int me = ut_cur;
    if (me >= 0) {
        /* Mark this thread as waiting on the target and save its context. */
        ut_tab[me].waiting_on = handle;
        ut_save(f, me);
    }

    /* Re-check: target may have finished between our first check and the save. */
    if (ut_tab[idx].finished) {
        if (me >= 0) ut_tab[me].waiting_on = -1;
        return 0;
    }
    if (!ut_tab[idx].alive) {
        if (me >= 0) ut_tab[me].waiting_on = -1;
        return 0xFFFFFFFFULL;
    }

    int nxt = ut_pick_next();
    if (nxt >= 0 && nxt != me) {
        ut_load(f, nxt);
        return 0x103;
    }
    return 0xFFFFFFFFULL;
}
