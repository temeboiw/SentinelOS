#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "ntdef.h"
#include "ke.h"
#include "spinlock.h"

typedef struct _KMUTEX {
    KSPIN_LOCK lock;
    volatile int32_t count;
    KTHREAD *waiting_head;
    KTHREAD *waiting_tail;
} KMUTEX;

#define KMUTEX_INIT { KSPIN_LOCK_INIT, 0, NULL, NULL }

static inline void KeAcquireMutex(KMUTEX *mutex) {
    for (;;) {
        /* A thread can be rescheduled by the clock interrupt. Keep local
         * interrupts disabled while manipulating the spin lock so that an
         * interrupted lock holder cannot be preempted by a waiter which then
         * spins forever on the same CPU. */
        uint64_t irq_state = KeDisableInterrupts();
        KeAcquireSpinLock(&mutex->lock);
        if (mutex->count >= 0) {
            // Mutex was available or we are the first to lock it.
            mutex->count--;
            if (ki_current) ki_current->wake_tick = 0;
            KeReleaseSpinLock(&mutex->lock);
            KeRestoreInterrupts(irq_state);
            return;
        }

        // The clock handler treats ThreadWaiting as a timed sleep. Use an
        // infinite deadline for mutex waits so a clock tick cannot wake this
        // thread while it is still linked on the mutex wait queue. Publish
        // the state and queue entry under the scheduler lock as one change.
        KTHREAD *cur = ki_current;
        KiSchedulerLock();
        cur->wake_tick = UINT64_MAX;
        cur->state = ThreadWaiting;
        cur->next = NULL;
        if (mutex->waiting_tail) mutex->waiting_tail->next = cur;
        else mutex->waiting_head = cur;
        mutex->waiting_tail = cur;
        KiSchedulerUnlock();
        KeReleaseSpinLock(&mutex->lock);
        KeRestoreInterrupts(irq_state);
        // The awakened thread competes for ownership, so it retries.
        __asm__ volatile("int $0x2F" ::: "memory");
    }
}

static inline void KeReleaseMutex(KMUTEX *mutex) {
    uint64_t irq_state = KeDisableInterrupts();
    KeAcquireSpinLock(&mutex->lock);
    mutex->count++;
    KTHREAD *t = NULL;
    if (mutex->waiting_head) {
        t = mutex->waiting_head;
        mutex->waiting_head = t->next;
        if (!mutex->waiting_head) {
            mutex->waiting_tail = NULL;
        }
    }
    KeReleaseSpinLock(&mutex->lock);
    if (t) {
        KiSchedulerLock();
        t->wake_tick = 0;
        rq_push_front(t);
        KiSchedulerUnlock();
    }
    KeRestoreInterrupts(irq_state);
}
