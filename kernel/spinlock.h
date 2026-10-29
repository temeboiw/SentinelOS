#pragma once
#include <stdint.h>

typedef struct _KSPIN_LOCK {
    volatile uint32_t lock;
} KSPIN_LOCK;

#define KSPIN_LOCK_INIT { 0 }

static inline void KeAcquireSpinLock(KSPIN_LOCK *lock) {
    while (__atomic_exchange_n(&lock->lock, 1, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&lock->lock, __ATOMIC_RELAXED))
            __asm__ volatile("pause");
    }
}

static inline void KeReleaseSpinLock(KSPIN_LOCK *lock) {
    __atomic_store_n(&lock->lock, 0, __ATOMIC_RELEASE);
}
