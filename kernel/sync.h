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
        KeAcquireSpinLock(&mutex->lock);
        if (mutex->count >= 0) {
            // Mutex was available or we are the first to lock it.
            mutex->count--;
            KeReleaseSpinLock(&mutex->lock);
            return;
        }

        // Queue the current thread and yield. The awakened thread competes
        // for ownership, so it must retry after returning from the scheduler.
        KTHREAD *cur = ki_current;
        cur->state = ThreadWaiting;
        cur->next = NULL;
        if (mutex->waiting_tail) mutex->waiting_tail->next = cur;
        else mutex->waiting_head = cur;
        mutex->waiting_tail = cur;
        KeReleaseSpinLock(&mutex->lock);
        __asm__ volatile("int $0x2F" ::: "memory");
    }
}

static inline void KeReleaseMutex(KMUTEX *mutex) {
    KeAcquireSpinLock(&mutex->lock);
    mutex->count++;
    if (mutex->waiting_head) {
        KTHREAD *t = mutex->waiting_head;
        mutex->waiting_head = t->next;
        if (!mutex->waiting_head) {
            mutex->waiting_tail = NULL;
        }
        KeReleaseSpinLock(&mutex->lock);

        KiSchedulerLock();
        rq_push_front(t);
        KiSchedulerUnlock();
    } else {
        KeReleaseSpinLock(&mutex->lock);
    }
}
