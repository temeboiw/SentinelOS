#!/usr/bin/env bash
# Protect the mutex wait-state contract shared with the timer scheduler.
set -euo pipefail

ROOT="${1:-.}"
SYNC="$ROOT/kernel/sync.h"
fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

acquire_body="$(sed -n '/static inline void KeAcquireMutex(/,/^}/p' "$SYNC")"
release_body="$(sed -n '/static inline void KeReleaseMutex(/,/^}/p' "$SYNC")"

grep -Fq 'uint64_t irq_state = KeDisableInterrupts();' <<<"$acquire_body" || fail "mutex acquisition must disable local interrupts before locking"
grep -Fq 'cur->wake_tick = UINT64_MAX;' <<<"$acquire_body" || fail "mutex wait must not look like an expired timer wait"
grep -Fq 'KiSchedulerLock();' <<<"$acquire_body" || fail "wait-state publication must synchronize with timer scheduling"
grep -Fq 'cur->state = ThreadWaiting;' <<<"$acquire_body" || fail "contended mutex must put its thread into waiting state"
grep -Fq 'mutex->waiting_tail = cur;' <<<"$acquire_body" || fail "contended thread must be linked to the mutex wait queue"
grep -Fq 'KeRestoreInterrupts(irq_state);' <<<"$acquire_body" || fail "acquisition must restore the caller interrupt state before yielding"
pass "contended mutex wait is published safely and cannot be timer-woken"

grep -Fq 't->wake_tick = 0;' <<<"$release_body" || fail "mutex release must clear the sleep deadline before wakeup"
grep -Fq 'rq_push_front(t);' <<<"$release_body" || fail "mutex release must enqueue the selected waiter"
grep -Fq 'KiSchedulerLock();' <<<"$release_body" || fail "waiter wakeup must synchronize with timer scheduling"
grep -Fq 'KeRestoreInterrupts(irq_state);' <<<"$release_body" || fail "release must restore the caller interrupt state"
pass "mutex release wakes one waiter through the scheduler queue"
