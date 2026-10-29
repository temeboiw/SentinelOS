#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

typedef struct _KPROCESS {
    uint64_t *Pml4;
    uint64_t Pid;
} KPROCESS;
typedef struct _KTHREAD { KPROCESS *Process; } KTHREAD;
typedef struct {
    uint64_t rip, rsp, rflags, rcx, rax;
    uint64_t cs, ss;
} Frame;

static KPROCESS test_system_process, test_owner, test_other;
static KPROCESS *test_current_process;
static KTHREAD test_current_thread;
static KTHREAD *test_current_thread_ptr = &test_current_thread;
#define MmCurrentProcess (test_current_process)
#define PspSystemProcess (&test_system_process)
#define ki_current (test_current_thread_ptr)
#define USER_TEB_BASE 0x70000000ULL
#define USER_THREAD_TEB_BASE 0x70002000ULL
#define USER_PEB_BASE 0x70000000ULL
#define USER_CS 0x23
#define USER_DS 0x1B
#define PTE_U 0x4ULL
#define PTE_RW 0x2ULL
#define PTE_OWNED 0x200ULL
#define PHYS_TO_VIRT(pa) ((uint64_t *)(uintptr_t)(pa))

static uint64_t test_next_pa = 0x1000;
static uint64_t pmm_alloc_zeroed(void) { test_next_pa += 0x1000; return test_next_pa; }
#define TEST_MAPS 64
static struct { KPROCESS *proc; uint64_t va, pa, flags; int live; } test_maps[TEST_MAPS];
static void map_4k_in(uint64_t *root, uint64_t va, uint64_t pa, uint64_t flags) {
    (void)root;
    for (int i = 0; i < TEST_MAPS; i++) {
        if (!test_maps[i].live) {
            test_maps[i].proc = test_current_process;
            test_maps[i].va = va;
            test_maps[i].pa = pa;
            test_maps[i].flags = flags;
            test_maps[i].live = 1;
            return;
        }
    }
}
static int MmUnmapAllocatedUserPage(KPROCESS *proc, uint64_t va) {
    for (int i = 0; i < TEST_MAPS; i++) {
        if (test_maps[i].live && test_maps[i].proc == proc &&
            test_maps[i].va == va && (test_maps[i].flags & PTE_OWNED)) {
            test_maps[i].live = 0;
            return 1;
        }
    }
    return 0;
}
static int test_live_maps(KPROCESS *proc) {
    int live = 0;
    for (int i = 0; i < TEST_MAPS; i++)
        if (test_maps[i].live && test_maps[i].proc == proc) live++;
    return live;
}
static void MmSwitchToProcess(KPROCESS *proc) { test_current_process = proc; }
static void cpu_set_user_gs_base(uint64_t base) { (void)base; }
static void DbgPrint(const char *format, ...) { (void)format; }

#include "../kernel/uthread.h"

static int check(int condition, const char *name) {
    if (!condition) fprintf(stderr, "FAIL: %s\n", name);
    else printf("PASS: %s\n", name);
    return !condition;
}

int main(void) {
    int failures = 0;
    test_current_process = &test_owner;
    test_current_thread.Process = &test_owner;

    int first = ut_make_handle(0);
    ut_tab[0].proc = &test_owner;
    ut_tab[0].handle = first;
    ut_tab[0].alive = 1;
    ut_tab[0].finished = 0;
    ut_tab[0].stack_base = 0x400000;
    ut_tab[0].teb_base = 0x600000;
    for (uint64_t off = 0; off < UT_STACK_SIZE; off += 0x1000)
        map_4k_in(test_owner.Pml4, ut_tab[0].stack_base + off,
                  0x100000 + off, PTE_U | PTE_RW | PTE_OWNED);
    map_4k_in(test_owner.Pml4, ut_tab[0].teb_base, 0x300000,
              PTE_U | PTE_RW | PTE_OWNED);
    ut_cur = 0;

    failures += check(first > 0 && ut_find_handle(first) == 0,
                      "live handle resolves for its owning process");
    failures += check(ut_wait(&(Frame){0}, first, 0) == 0x102,
                      "zero-time wait reports timeout for a live thread");

    ut_exit_current();
    failures += check(!ut_tab[0].alive && ut_tab[0].finished,
                      "thread exit records a persistent signaled state");
    failures += check(ut_find_handle(first) == 0 &&
                      ut_wait(&(Frame){0}, first, 0) == 0,
                      "wait observes completion after the thread exits");
    failures += check(test_live_maps(&test_owner) == 0 &&
                      ut_tab[0].stack_base == 0 && ut_tab[0].teb_base == 0,
                      "thread exit releases all owned stack and TEB mappings");

    test_current_process = &test_other;
    failures += check(ut_find_handle(first) < 0 &&
                      ut_wait(&(Frame){0}, first, 0) == 0xFFFFFFFFULL,
                      "thread handles cannot be used by another process");

    test_current_process = &test_owner;
    for (int i = 1; i < UT_MAX; i++) ut_tab[i].alive = 1;
    int reusable_slot = 0;
    failures += check(ut_find_slot() == reusable_slot,
                      "completed thread slot is reusable after cleanup");
    int second = ut_make_handle(reusable_slot);
    ut_tab[reusable_slot].handle = second;
    ut_tab[reusable_slot].alive = 1;
    ut_tab[reusable_slot].finished = 0;
    failures += check(second != first && ut_find_handle(first) < 0 &&
                      ut_find_handle(second) == reusable_slot &&
                      test_live_maps(&test_owner) == 0,
                      "reused slots do not revive handles or retain old mappings");

    return failures ? 1 : 0;
}
