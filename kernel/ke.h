#pragma once
// Kernel threads + round-robin scheduler driven by the clock tick (single CPU).
// A thread's saved context is the interrupt frame on its own stack: switching threads means
// isr_handler returns a different thread's frame, and isr_common's iretq resumes it.
// Kernel code and HAL are built with -mgeneral-regs-only, so no FPU/SSE state needs saving yet.
#include <stdint.h>
#include "ntdef.h"
#include "hal.h"
#include "serial.h"
#include "cpu.h"
#include "heap.h"

#define MAX_THREADS    32
#define THREAD_STACK   (4 * 4096 - 16)              // exactly 4 pages with the 16-byte heap header
#define QUANTUM_TICKS  2                            // 20 ms at 100 Hz
#define CLOCK_VECTOR   0xD1                         // the same vector the Windows x64 clock uses

typedef enum { ThreadReady, ThreadRunning, ThreadWaiting, ThreadTerminated } KTHREAD_STATE;

typedef struct _KTHREAD {
    Frame *frame;
    uint8_t *stack;
    struct _KTHREAD *next;
    struct _KPROCESS *Process;
    ULONGLONG wake_tick;
    ULONGLONG runs, ticks;
    ULONG id;
    KTHREAD_STATE state;
    char name[16];
} KTHREAD;

static KTHREAD *ki_threads[MAX_THREADS];
static KTHREAD *ki_current, *ki_idle, *rq_head, *rq_tail;
static volatile ULONGLONG KeTickCount;
static ULONGLONG ki_switches;
static ULONG ki_next_id;
static int ki_quantum_left;

static void rq_push(KTHREAD *t) {
    t->next = NULL; t->state = ThreadReady;
    if (rq_tail) rq_tail->next = t; else rq_head = t;
    rq_tail = t;
}
static void rq_push_front(KTHREAD *t) {                        // wake-up boost: a thread leaving a wait runs next
    t->state = ThreadReady; t->next = rq_head; rq_head = t;
    if (!rq_tail) rq_tail = t;
}
static KTHREAD *rq_pop(void) {
    KTHREAD *t = rq_head;
    if (t) { rq_head = t->next; if (!rq_head) rq_tail = NULL; t->next = NULL; }
    return t;
}

// pick the next thread; called from isr_handler with the interrupted thread's frame
static Frame *KiSchedule(Frame *f) {
    KTHREAD *cur = ki_current;
    cur->frame = f;
    if (cur->state == ThreadRunning) {
        if (cur != ki_idle) rq_push(cur); else cur->state = ThreadReady;     // the idle thread is never queued
    }
    KTHREAD *next = rq_pop();
    if (!next) next = ki_idle;
    next->state = ThreadRunning;
    next->runs++;
    if (next != cur) ki_switches++;
    if (next->Process && next->Process != cur->Process)
        MmSwitchToProcess(next->Process);
    ki_current = next;
    ki_quantum_left = QUANTUM_TICKS;
    return next->frame;
}

static void KiClockInterrupt(Frame *f) {
    (void)f;
    KeTickCount++;
    ki_current->ticks++;
    int woke = 0;
    for (int i = 0; i < MAX_THREADS; i++) {                                  // wake sleepers whose time has come
        KTHREAD *t = ki_threads[i];
        if (t && t->state == ThreadWaiting && t->wake_tick <= KeTickCount) { rq_push_front(t); woke = 1; }
    }
    if (woke) ki_need_resched = 1;                                           // and let them preempt the running thread
    if (ki_current == ki_idle) { if (rq_head) ki_need_resched = 1; }
    else if (--ki_quantum_left <= 0) {
        if (rq_head) ki_need_resched = 1; else ki_quantum_left = QUANTUM_TICKS;
    }
}

static void KiDispatchInterrupt(Frame *f) { (void)f; ki_need_resched = 1; }

static void KeYield(void) {
    ki_need_resched = 1;
    __asm__ volatile("int $0x2F" ::: "memory");
}

static NTSTATUS KeDelayExecutionThread(CHAR WaitMode, BOOLEAN Alertable, PLARGE_INTEGER Interval) {
    (void)WaitMode; (void)Alertable;
    ULONGLONG ms = Interval->QuadPart < 0 ? (ULONGLONG)(-Interval->QuadPart) / 10000 : 0;   // negative = relative, 100 ns units
    ULONGLONG ticks = (ms + 9) / 10;
    if (!ticks) ticks = 1;
    uint64_t fl = KeDisableInterrupts();
    ki_current->wake_tick = KeTickCount + ticks;
    ki_current->state = ThreadWaiting;
    ki_need_resched = 1;
    KeRestoreInterrupts(fl);
    __asm__ volatile("int $0x2F" ::: "memory");                              // switch away until the clock wakes us
    return STATUS_SUCCESS;
}

static void KeSleep(ULONG ms) {
    LARGE_INTEGER i;
    i.QuadPart = -(LONGLONG)ms * 10000;
    KeDelayExecutionThread(0, FALSE, &i);
}

static void PsTerminateSystemThread(NTSTATUS Status) {
    (void)Status;
    KeDisableInterrupts();
    ki_current->state = ThreadTerminated;
    ki_need_resched = 1;
    __asm__ volatile("int $0x2F" ::: "memory");                              // never returns: nothing re-queues a terminated thread
    for (;;) __asm__ volatile("hlt");
}

// returning from a thread's entry function lands here
static void KiThreadExit(void) { PsTerminateSystemThread(STATUS_SUCCESS); }

static KTHREAD *ki_make_thread(const char *name, void (*entry)(PVOID), PVOID arg) {
    KTHREAD *t = kzalloc(sizeof *t);
    uint8_t *stack = kmalloc(THREAD_STACK);
    if (!t || !stack) { kfree(t); kfree(stack); return NULL; }

    uint64_t top  = ((uint64_t)stack + THREAD_STACK) & ~15ULL;
    uint64_t rsp0 = top - 40;                                                // return address + 32 bytes shadow space, rsp = 8 mod 16
    *(uint64_t *)rsp0 = (uint64_t)KiThreadExit;
    Frame *f = (Frame *)(rsp0 - sizeof(Frame));
    memset(f, 0, sizeof *f);
    f->rip = (uint64_t)entry; f->rcx = (uint64_t)arg;                        // Win64 ABI: first argument in rcx
    f->cs = 0x08; f->ss = 0x10; f->rflags = 0x202; f->rsp = rsp0;           // kernel segments, interrupts enabled

    t->frame = f; t->stack = stack; t->id = ki_next_id++;
    t->Process = PspSystemProcess;
    for (int i = 0; i < 15 && name[i]; i++) t->name[i] = name[i];
    return t;
}

static int ki_register(KTHREAD *t) {
    for (int i = 0; i < MAX_THREADS; i++) if (!ki_threads[i]) { ki_threads[i] = t; return 1; }
    return 0;
}

static KTHREAD *KeCreateThread(const char *name, void (*entry)(PVOID), PVOID arg) {
    KTHREAD *t = ki_make_thread(name, entry, arg);
    if (!t) return NULL;
    uint64_t fl = KeDisableInterrupts();
    if (!ki_register(t)) { KeRestoreInterrupts(fl); kfree(t->stack); kfree(t); return NULL; }
    rq_push(t);
    KeRestoreInterrupts(fl);
    return t;
}

// free the stacks and control blocks of terminated threads (never the running one)
static ULONG KeReapThreads(void) {
    ULONG n = 0;
    for (int i = 0; i < MAX_THREADS; i++) {
        uint64_t fl = KeDisableInterrupts();
        KTHREAD *t = ki_threads[i];
        if (t && t->state == ThreadTerminated && t != ki_current) { ki_threads[i] = NULL; KeRestoreInterrupts(fl); kfree(t->stack); kfree(t); n++; }
        else KeRestoreInterrupts(fl);
    }
    return n;
}

static void KiIdleThread(PVOID arg) { (void)arg; for (;;) __asm__ volatile("sti; hlt"); }

// the code running now (kmain2 on the boot stack) becomes thread "Main"; a separate idle thread runs when nothing is ready
static void KiInitializeScheduler(void) {
    KTHREAD *boot = kzalloc(sizeof *boot);
    boot->id = ki_next_id++; boot->state = ThreadRunning;
    boot->Process = PspSystemProcess;
    boot->name[0] = 'M'; boot->name[1] = 'a'; boot->name[2] = 'i'; boot->name[3] = 'n';
    ki_threads[0] = boot; ki_current = boot;

    ki_idle = ki_make_thread("Idle", KiIdleThread, NULL);
    ki_idle->state = ThreadReady;
    ki_threads[1] = ki_idle;

    ki_quantum_left = QUANTUM_TICKS;
    KeConnectInterrupt(DISPATCH_VECTOR, KiDispatchInterrupt);
    KeConnectInterrupt(CLOCK_VECTOR, KiClockInterrupt);
    KeConnectInterrupt(0x21, irq_stub_eoi);
    KeConnectInterrupt(0x2C, irq_stub_eoi);
}
