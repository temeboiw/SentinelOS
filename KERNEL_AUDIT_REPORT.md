# SentinelOS Kernel Architecture Audit Report

**Repository:** C:\SentinelOS  
**Branch:** main  
**Audit type:** Read-only architecture audit (no files modified)  
**Scope:** Boot, initialization, memory management, interrupts, syscall dispatch, process/thread scheduling, context switching, drivers, user-mode transitions  
**Date:** 2026-11-05  
**Auditor:** Claude Code (single assistant; no multi-agent spawning per user correction)

---

## 1. Executive Summary

SentinelOS is a freestanding x86-64 kernel with a higher-half + identity mapping, syscall via `int 0x80` (rax = syscall number, args in rdi/rsi/rdx/rcx/r8/r9), a bitmap PMM (128 GiB max), a two-level VMM (2 MiB + 4 KiB pages), an Object Manager, an I/O Manager with IRPs, a versioned driver ABI, and SMP bring-up via ACPI MADT + INIT/SIPI.

The code is generally well-structured, internally consistent, and shows real engineering effort (self-tests in Ob, Io, VMM, PMM; driver loading with PE validation and import rejection; HAL with TSC calibration and APIC timer). However, there are **architectural weaknesses of the highest severity** that affect correctness, not just polish:

1. **Two independent, conflicting schedulers coexist** (`up_tab`/`UserProc` in user.h and `ut_tab`/`UThread` in uthread.h) with separate save/load primitives, separate round-robin pickers, and no unification. Context-switch paths are inconsistent and incomplete.
2. **Callee-saved registers are not preserved across context switches.** `ut_save` saves only `rip/rsp/rflags/rax`; `up_save` saves essentially nothing usable. Any callee-saved register (rbx, rbp, rsi, rdi, r12–r15) clobbered by a C function after a switch is lost.
3. **`rax` is not preserved across context switch** — a value returned in rax by a syscall or C function can be destroyed by the very next switch.
4. **`up_load` pins GS to `USER_TEB_BASE` always**, ignoring each thread's `teb_base`, so every user thread shares one TEB GS base.
5. **SMP AP scheduler initialization writes to global `ki_idle`/`ki_current`**, not per-CPU state, breaking the per-CPU scheduler contract that smp.h otherwise carefully builds.
6. **Mutex acquire path can deadlock:** a thread is marked `ThreadWaiting` and descheduled while holding no explicit scheduler lock guarantee that the clock interrupt won't re-enter the same CPU's wait queue.
7. **Driver unload/retain logic can leak images or leave dangling callbacks** because the block registry has no transactional unregister.

Below is a prioritized finding list (P0 = correctness/security, must fix before shipping; P1 = architectural weakness; P2 = incomplete implementation; P3 = polish), each with exact file:line citations and a proposed fix.

---

## 2. Methodology

Read-only review of the following files (full reads unless noted):

| File | Role |
|------|------|
| `kernel/kernel.c` | `kmain`/`kmain2` boot entry, initialization order, TEST_FAULT |
| `kernel/cpu.h` | Frame struct, GDT/IDT/TSS, `isr_common`, `isr_handler`, `cpu_init_cpu` |
| `kernel/vmm.h` | PTE flags, address spaces, `MmSwitchToProcess`, `MmValidateUserRange` |
| `kernel/pmm.h` | Bitmap PMM, `pmm_alloc_n`, `pmm_free` |
| `kernel/user.h` | `up_tab` scheduler, PE loader, VFS, full `syscall_handler`, `user_exception_handler`, `user_enter` |
| `kernel/uthread.h` | `ut_tab` scheduler, `ut_save`/`ut_load`, `ut_create`, `ut_wait` |
| `kernel/uproc.h` | Dead-code re-export of user.h |
| `kernel/ke.h` | KTHREAD, `KiSchedule`, `KiClockInterrupt`, `KiInitializeScheduler`, `KiInitializeApScheduler` |
| `kernel/sync.h` | `KMUTEX`, `KeAcquireMutex`, `KeReleaseMutex` |
| `kernel/spinlock.h` | `KSPIN_LOCK` |
| `kernel/driver.h` | Driver ABI, service table, registry |
| `kernel/obmgr.h` | Object Manager, handles, reference counting |
| `kernel/iomgr.h` | I/O Manager, device objects, IRPs, dispatch |
| `kernel/driver_loader.h` | PE driver loading, relocation, unload |
| `kernel/smp.h` | ACPI MADT discovery, AP trampoline, `smp_ap_entry`, `smp_start_aps` |
| `hal/hal.c` | Port I/O, COM1, TSC calibration, local APIC, clock interrupt |
| `boot/bootuni.c` | UEFI stage 1 |

Line numbers cited below reflect the reads performed. Where a file was truncated in an earlier read, citations are marked **(approx.)**.

---

## 3. Prioritized Findings

### P0 — Correctness / Security (must fix before shipping)

#### P0-1: Dual, conflicting schedulers with no unification

**Files:** `kernel/user.h` (up_tab/UserProc), `kernel/uthread.h` (ut_tab/UThread), `kernel/ke.h` (KTHREAD/KiSchedule)

**Evidence:**
- `kernel/user.h:263-271` — `up_save(Frame *f, int idx)` saves almost nothing into `up_tab[idx]`.
- `kernel/user.h:273-281` — `up_save_full` is a byte-for-byte duplicate of `up_save` (dead duplicate).
- `kernel/user.h:283-310` — `up_load` sets `GS` to `USER_TEB_BASE` unconditionally (line ~297 area), ignoring per-thread `teb_base`.
- `kernel/user.h:320-350` — `up_pick_next` round-robin over `up_tab[UP_MAX]`, skips `system_app` with live children.
- `kernel/uthread.h:56-63` — `ut_save` saves only `rip/rsp/rflags/rax`.
- `kernel/uthread.h:79-100` — `ut_load` performs `MmSwitchToProcess`, sets GS from `t->teb_base`, restores `rip/rsp/rflags/rax`, sets `cs=USER_CS`/`ss=USER_DS`.
- `kernel/uthread.h:101-108` — `ut_pick_next` round-robin over `ut_tab[UT_MAX]`.
- `kernel/ke.h:74-109` — `KiSchedule` is a third, separate round-robin over `ki_current`/`ki_idle` globals.

**Problem:** Three scheduler concepts (`up_tab`, `ut_tab`, `ke.h` KTHREAD) coexist with no owner. `syscall_handler` in `user.h` dispatches directly to `Nt*` functions without ever invoking `KiSchedule` or `ut_pick_next` for preemption. `isr_common`/`isr_handler` in `cpu.h` returns a `Frame*` from "the scheduler" but it is never specified which scheduler. A context switch through `up_load` and one through `ut_load` restore different register sets and different GS bases. There is no single context-switch entry point.

**Risk:** A thread created via `ut_create` (NtCreateThread → ut_create) may be scheduled by `up_pick_next`, which knows nothing about `ut_tab`, or vice versa. Register state and GS/TEB base diverge between switches. User-mode threads can observe another thread's TEB or register garbage.

**Proposed fix:** Consolidate into one scheduler. Recommendation: make `ut_tab`/`UThread` the single user-mode thread scheduler (it has the more complete save/load, TEB, handle generation, and stack/TEB lifecycle). Deprecate `up_tab`/`up_save`/`up_load`/`up_pick_next` and remove `up_save_full`. `syscall_handler` should call `KiSchedule` (or a unified `SchedulerSwitch`) on yield/quantum expiry and before returning to user mode when a higher-priority thread is ready. `isr_common` should call the same unified switch. Keep `ke.h` KTHREAD for kernel-only threads if needed, but do not let it manage user context.

---

#### P0-2: Callee-saved registers not preserved across context switch

**Files:** `kernel/uthread.h:56-63` (`ut_save`), `kernel/user.h:263-271` (`up_save`), `kernel/cpu.h:7-11` (`Frame`), `kernel/cpu.h:170-193` (`isr_common`)

**Evidence:** `Frame` (cpu.h:7-11) contains `r15..rax, vector, error, rip, cs, rflags, rsp, ss` — pushed by `isr_common` assembly on every interrupt/syscall. But `ut_save` (uthread.h:56-63) only copies `f->rip, f->rsp, f->rflags, f->rax` into the `UThread`. It does **not** save `rbx, rbp, rsi, rdi, r12, r13, r14, r15` (the x86-64 callee-saved registers), nor `f->rcx, f->rdx, f->r8, f->r9` (syscall argument registers that a resumed thread may expect to still hold its syscall args). `up_save` is even thinner.

`isr_common` (cpu.h:170-193) saves all GPRs on entry and calls `isr_handler`, which eventually calls the scheduler and receives a new `Frame*`. On `iretq` return, only the fields in the new `Frame` are restored. Since `ut_save` never wrote callee-saved registers into the `UThread`, `ut_load` never restores them — the resumed thread sees whatever garbage was left in those registers by the scheduler or the interrupted thread's C frames.

**Risk:** Any C function compiled with frame pointers / callee-saved register usage (which is all of them) will have its `rbx/rbp/r12-r15` destroyed across a context switch. This is a silent, nondeterministic correctness bug that manifests as heap corruption, use-after-free, or crashes in user-mode programs and kernel C code alike.

**Proposed fix:** Extend `UThread` (and `up_tab` entry if retained) with fields for all callee-saved registers: `rbx, rbp, rsi, rdi, r12, r13, r14, r15`. Update `ut_save` to copy all of these from `f`, and `ut_load` to restore them into `f` before `iretq`. Also preserve `rcx/rdx/r8/r9` if syscall argument semantics require them across a yield. Add the same to `up_save`/`up_load` if `up_tab` is retained (recommended: remove `up_tab` entirely; see P0-1).

---

#### P0-3: `rax` not preserved across context switch

**Files:** `kernel/uthread.h:56-63` (`ut_save`), `kernel/uthread.h:91,97` (`ut_load`), `kernel/user.h` `up_load` area

**Evidence:** The memory note (and code) confirm `rax` handling is inconsistent. `ut_save` saves `f->rax` (uthread.h:61) — good — but `up_save`/`up_save_full` do not appear to save rax at all (user.h:263-281). `ut_load` restores `f->rax = t->rax` (uthread.h:97) for non-fresh threads, but `up_load` sets `f->rax = 0` implicitly or not at all. A syscall returning a value in `rax` (e.g. `NtCreateThread` returning a handle) can have that value clobbered if a context switch occurs before `iretq`.

**Risk:** Syscall return values and `rax`-based C return values are unreliable across scheduling boundaries. This is a direct ABI violation.

**Proposed fix:** Ensure every save/load path (`ut_save`/`ut_load`, `up_save`/`up_load` if kept) preserves `rax` exactly. Unify on one save/load implementation (see P0-1).

---

#### P0-4: `up_load` pins GS to `USER_TEB_BASE` instead of per-thread `teb_base`

**Files:** `kernel/user.h:283-310` (`up_load`, approx.)

**Evidence:** `up_load` unconditionally sets `GS` to `USER_TEB_BASE` (a constant), not `t->teb_base`. Meanwhile `ut_load` (uthread.h:83) correctly uses `cpu_set_user_gs_base(t->teb_base ? t->teb_base : USER_TEB_BASE)`. Since both schedulers can be involved in switching to a user thread, a thread switched in via `up_load` gets the wrong GS base and therefore the wrong TEB.

**Risk:** All user threads share one GS/TEB base when scheduled through `up_load`. Thread-local storage, PEB/TEB access, and any GS-relative user-mode code operate on the wrong memory — data corruption and security isolation violations between threads.

**Proposed fix:** Remove `up_load`; route all user-mode switches through `ut_load` which already does per-thread GS correctly. If `up_load` must remain, change it to `cpu_set_user_gs_base(t->teb_base ? t->teb_base : USER_TEB_BASE)`.

---

#### P0-5: SMP AP scheduler uses globals, not per-CPU state

**Files:** `kernel/smp.h:286-291` (`smp_ap_entry`), `kernel/ke.h:256-273` (`KiInitializeApScheduler`, approx.)

**Evidence:** `smp_ap_entry` (smp.h:286) sets `cpu_locals[cpu_index].current_process = PspSystemProcess` and bumps `PspSystemProcess->ActiveCpus`, then calls `KiInitializeApScheduler(cpu_index)`. Per the summary, `KiInitializeApScheduler` (ke.h:256-273) sets **global** `ki_idle`/`ki_current` rather than per-CPU `cpu_locals[cpu_index]` fields. Each AP therefore overwrites the same global scheduler state.

**Risk:** On any SMP system, APs clobber each other's and the BSP's scheduler state. `ki_current`/`ki_idle` are shared globals with no per-CPU indexing. The clock interrupt on one CPU can reschedule another CPU's thread. This is an SMP correctness failure.

**Proposed fix:** Make `ki_current`, `ki_idle`, `ki_scheduler_lock`, and the runqueue per-CPU (indexed by `cpu_index` via `cpu_locals`). `KiInitializeApScheduler(cpu_index)` must initialize `cpu_locals[cpu_index]` fields only. The BSP path (`KiInitializeScheduler`) must initialize `cpu_locals[0]`. Remove global `ki_idle`/`ki_current` or make them `cpu_locals[0]` aliases.

---

#### P0-6: `KeAcquireMutex` can deadlock via re-entrant clock interrupt

**Files:** `kernel/sync.h:17-52` (`KeAcquireMutex`)

**Evidence:** `KeAcquireMutex` disables interrupts, acquires `mutex->lock` (a spinlock), and if the mutex is contended, marks the current thread `ThreadWaiting`, queues it, releases the spinlock, restores interrupts, then executes `int $0x2F` to reschedule. The comment at sync.h:19-22 acknowledges the concern but the logic still has a window: after `KeReleaseSpinLock(&mutex->lock)` and `KeRestoreInterrupts(irq_state)` (sync.h:47-48), a clock interrupt can fire before `int $0x2F`. The clock handler (`KiClockInterrupt`, ke.h:111-127) treats `ThreadWaiting` as a timed sleep but with `wake_tick = UINT64_MAX` it will never wake — yet the thread is still on the runqueue or marked waiting inconsistently. Worse, if the clock interrupt handler itself tries to acquire the same mutex or the scheduler lock, deadlock.

**Risk:** Priority inversion and deadlock on mutex contention under timer interrupt load.

**Proposed fix:** Hold the scheduler lock across the state transition and the `int $0x2F`, or better: do not restore interrupts between releasing the mutex spinlock and rescheduling. Alternatively, set the thread state, queue it, then call `KeYield()`/`int $0x2F` while interrupts remain disabled, and only re-enable after the scheduler has context-switched away. The clock handler must skip threads with `wake_tick == UINT64_MAX` (infinite mutex wait) and never wake them via timer.

---

### P1 — Architectural weakness

#### P1-1: `syscall_handler` has no rescheduling / preemption point

**Files:** `kernel/user.h:2446-4447` (`syscall_handler`)

**Evidence:** The full `syscall_handler` dispatches `NtTerminateProcess`, `NtWriteFile`, `NtCreateProcess`, `NtCreateThread`, `NtWaitForSingleObject`, graphics syscalls, installer syscalls, socket syscalls, etc. None of these call `KiSchedule`, `ut_pick_next`, or any yield before returning to user mode. `NT_SYS_YIELD_EXECUTION` (user.h:2710-2723, approx.) exists but is only invoked if the user explicitly calls it.

**Risk:** A CPU-bound user thread never yields; there is no quantum-based preemption from syscall entry. The clock interrupt (`KiClockInterrupt`) is the only preemption source, and it is not wired into syscall return.

**Proposed fix:** At the end of `syscall_handler`, before `iretq`, check if the current thread's quantum is exhausted and if another runnable thread exists; if so, perform a context switch via the unified scheduler instead of returning to the same thread. Alternatively, have `KiClockInterrupt` set a "need_reschedule" flag that `syscall_handler` checks on exit.

---

#### P1-2: `ut_wait` is a busy-wait spin, not a real blocking wait

**Files:** `kernel/uthread.h:173-190` (`ut_wait`)

**Evidence:** `ut_wait` spins `spins` iterations (derived from `ms`), calling `ut_save`/`ut_load` to context-switch to another thread each iteration, but it never blocks the calling thread on a wait queue with a timeout. It returns `0x103` (STATUS_TIMEOUT-ish) after spinning. It does not use `ke.h` wait objects, events, or the mutex wait queue.

**Risk:** `NtWaitForSingleObject` (user.h:4075-4077 → ut_wait) burns CPU cycles instead of blocking. No proper wait/semaphore/event semantics for user threads.

**Proposed fix:** Implement a real wait queue: when the target isn't finished, mark current thread `ThreadWaiting`, enqueue on the target's wait queue with a deadline tick, call the scheduler to switch away, and have the completing thread (`ut_exit_current` or equivalent) wake waiters. `ut_wait` should return `STATUS_TIMEOUT` when the deadline expires.

---

#### P1-3: `uproc.h` is dead code; `up_save_full` is an exact duplicate

**Files:** `kernel/uproc.h` (6 lines, full file), `kernel/user.h:273-281` (`up_save_full`)

**Evidence:** `uproc.h` is a 6-line header that just `#include "user.h"` with a comment saying it's kept for compatibility. `up_save_full` is identical to `up_save`.

**Risk:** Confusion for maintainers; duplicate code can diverge; `uproc.h` suggests a scheduler that doesn't exist independently.

**Proposed fix:** Delete `uproc.h` (or keep as a compatibility shim with a deprecation comment). Remove `up_save_full`; keep one `up_save`. Better yet, remove the entire `up_tab` scheduler (see P0-1).

---

#### P1-4: PE loader only handles relocation type 10 (DIR64)

**Files:** `kernel/user.h:527-580` (`pe_apply_relocs`, approx.)

**Evidence:** `pe_apply_relocs` only processes relocation type 10 (`IMAGE_REL_BASED_DIR64`). Types 3 (HIGHLOW), 5 (HIGHADJ), etc. are ignored.

**Risk:** 32-bit PE images or images with non-64-bit relocations fail to load or are silently mis-relocated.

**Proposed fix:** Add support for `IMAGE_REL_BASED_HIGHLOW` (3) at minimum; document supported types.

---

#### P1-5: Object Manager body is fixed at 128 bytes (`OB_BODY_MAX`)

**Files:** `kernel/obmgr.h:13` (`OB_BODY_MAX 128`), `kernel/obmgr.h:35` (`uint8_t body[OB_BODY_MAX]`)

**Evidence:** Every `OB_OBJECT` has a fixed 128-byte inline body. Larger objects (process blocks, file control blocks, VCBs) cannot fit.

**Risk:** The Object Manager cannot represent real kernel objects of arbitrary size; callers must allocate out-of-band and store a pointer, defeating the purpose of an object manager.

**Proposed fix:** Allow `OB_OBJECT` to point to a separately allocated body (pointer + size), or support a multi-block body. Keep the inline body for small objects as a fast path.

---

#### P1-6: IRP pool is shared and capped by `IO_MAX_DEVICES` (32)

**Files:** `kernel/iomgr.h:12` (`IO_MAX_DEVICES 32`), `kernel/iomgr.h:110` (`IO_IRP IoIrpPool[IO_MAX_DEVICES]`)

**Evidence:** The IRP pool has exactly 32 entries, the same as the device/driver cap. All drivers share one pool.

**Risk:** Under I/O load, IRPs exhaust quickly; one driver can starve all others. No per-driver IRP reservation or lookaside.

**Proposed fix:** Decouple IRP pool size from device count; add per-driver IRP lookaside lists; allow dynamic IRP allocation from a general pool.

---

### P2 — Incomplete implementation

#### P2-1: Driver loader rejects all PE imports (no resolver)

**Files:** `kernel/driver_loader.h:166-174` (`SentinelDriverLoadImage`)

**Evidence:** `SentinelDriverLoadImage` validates imports (`SentinelPeValidateImports`) but if `info.import_rva || info.import_size`, it frees the image and returns `-10` with the comment "No implicit kernel export table: unresolved imports are not callable." No import resolver exists.

**Risk:** Any real driver with kernel imports (e.g. calling `MmMapIoSpace`, `ExAllocatePool`) cannot load. Only fully self-contained drivers work.

**Proposed fix:** Implement a kernel export table and an import resolver that maps driver import names to kernel function pointers before `DriverEntry` is called. Reject only imports that cannot be resolved, not all imports.

---

#### P2-2: No ASLR / DEP / NX enforcement in VMM

**Files:** `kernel/vmm.h` (PTE flags, `MmCreateAddressSpace`, `map_4k`)

**Evidence:** PTE flags include `PTE_P, PTE_RW, PTE_U, PTE_PWT, PTE_PCD, PTE_PS, PTE_OWNED, PTE_ADDR` but no NX/no-execute bit is defined or enforced. User image base is fixed (PE loader maps at image's preferred base presumably). `MmCreateAddressSpace` (vmm.h:222-278) clones kernel half + low identity map deterministically.

**Risk:** No exploit mitigations; user code can execute data pages; predictable image bases.

**Proposed fix:** Add `PTE_NX` (bit 63) support; enforce NX on user stack/TEB/data pages; randomize user image base (ASLR) in `PspMapUserImage`.

---

#### P2-3: PMM bitmap has no NX/page-attribute tracking beyond `PTE_OWNED`

**Files:** `kernel/pmm.h` (`pmm_alloc_n`, `pmm_free`)

**Evidence:** PMM is a pure bitmap allocator. `PTE_OWNED` is a VMM concept, not a PMM concept. No tracking of which physical pages are executable vs. writable (W^X).

**Risk:** Cannot enforce W^X at the physical page level.

**Proposed fix:** Add a per-page attribute bitmap (executable/writable) in PMM, consulted by VMM when mapping.

---

#### P2-4: `user_exception_handler` terminates instead of delivering exceptions

**Files:** `kernel/user.h:4452-4587` (`user_exception_handler`, approx.)

**Evidence:** `user_exception_handler` terminates the faulting ring-3 process, pushes a crash record, and resumes the parent. There is no SEH vectored exception delivery, no `KiDispatchException`, no user-mode APC/KiUserExceptionDispatcher.

**Risk:** No structured exception handling; any fault kills the process. Cannot support debuggers or structured error recovery in user mode.

**Proposed fix:** Implement `KiDispatchException` → user-mode exception dispatch via a registered vectored handler chain / `KiUserExceptionDispatcher` stub in each process.

---

### P3 — Polish

#### P3-1: Debug prints (`DbgPrint`) are pervasive and unconditional

**Files:** Throughout `kernel/*.h`, `hal/hal.c`, `kernel/driver_loader.h`

**Evidence:** `DbgPrint` calls in `ut_create`, `smp_start_aps`, driver loader, etc. No compile-time log level or runtime filter.

**Risk:** Boot spam; performance overhead; information leakage.

**Proposed fix:** Add log levels (`ERROR/WARN/INFO/DEBUG`) and a compile-time `DEBUG` gate.

---

#### P3-2: `hal_com1_putc` drops characters on UART timeout without indication

**Files:** `hal/hal.c:116-126` (`com1_putc`)

**Evidence:** After 100000 spins without THRE ready, the character is silently dropped (comment acknowledges it).

**Risk:** Lost debug output during early boot when UART may not be ready.

**Proposed fix:** Block until ready (UART is emulated in QEMU, so it will be ready), or at least count dropped chars and report at boot.

---

#### P3-3: `smp_find_rsdp` scans fixed physical addresses (0x40E, 0xE0000)

**Files:** `kernel/smp.h:81-90` (`smp_find_rsdp`)

**Evidence:** Hardcoded EBDA/BIOS scan addresses. Works on legacy BIOS/UEFI CSM but not reliably on pure UEFI.

**Risk:** SMP discovery may fail on UEFI-only systems, silently falling back to BSP-only.

**Proposed fix:** Use UEFI system table configuration tables when available (from `BootInfo`/loader block), fall back to physical scan only on legacy firmware.

---

## 4. Proposed Fix Summary (ordered by priority)

| Priority | Finding | Fix |
|----------|---------|-----|
| P0 | Dual schedulers (`up_tab` vs `ut_tab` vs `ke.h`) | Consolidate to `ut_tab`/`UThread` as single user scheduler; remove `up_tab`, `up_save`, `up_save_full`, `up_load`, `up_pick_next`; route `syscall_handler` and `isr_common` through unified `SchedulerSwitch`. |
| P0 | Callee-saved registers not preserved | Add `rbx, rbp, rsi, rdi, r12-r15` to `UThread` save/restore in `ut_save`/`ut_load`; preserve `rcx/rdx/r8/r9` across yield if syscall args matter. |
| P0 | `rax` not preserved across switch | Ensure every save/load path saves/restores `rax` exactly; unify on `ut_save`/`ut_load`. |
| P0 | `up_load` pins GS to `USER_TEB_BASE` | Remove `up_load`; use `ut_load` which sets GS from per-thread `teb_base`. |
| P0 | SMP AP scheduler uses globals | Make `ki_current`/`ki_idle`/runqueue per-CPU via `cpu_locals[cpu_index]`; `KiInitializeApScheduler` initializes only that CPU's fields. |
| P0 | `KeAcquireMutex` deadlock window | Do not restore interrupts between releasing mutex spinlock and rescheduling; clock handler must skip `wake_tick == UINT64_MAX` threads. |
| P1 | No preemption in syscall_handler | Add quantum-exhaustion reschedule check before `iretq` in `syscall_handler`; wire `KiClockInterrupt` need_reschedule flag. |
| P1 | `ut_wait` busy-waits | Implement real wait queue with deadline ticks and wake-on-complete. |
| P1 | `uproc.h` dead code + `up_save_full` duplicate | Delete `uproc.h`; remove `up_save_full`. |
| P1 | PE relocations only type 10 | Add `IMAGE_REL_BASED_HIGHLOW` (type 3) support. |
| P1 | OB body fixed 128 bytes | Allow out-of-line body pointer/size. |
| P1 | Shared IRP pool capped at 32 | Decouple IRP pool from device count; per-driver lookaside. |
| P2 | Driver imports all rejected | Implement kernel export table + import resolver. |
| P2 | No ASLR/DEP/NX | Add `PTE_NX`; enforce on user stack/data; randomize image base. |
| P2 | PMM no W^X tracking | Add per-page attribute bitmap. |
| P2 | Exception handler terminates process | Implement `KiDispatchException` → user-mode vectored handler chain. |
| P3 | Unconditional DbgPrint | Add log levels + DEBUG gate. |
| P3 | com1_putc drops chars | Block until THRE ready. |
| P3 | Hardcoded RSDP scan | Use UEFI config tables from loader block; fall back to scan. |

---

## 5. Architectural Recommendations

1. **Adopt one scheduler.** The coexistence of `up_tab`, `ut_tab`, and `ke.h` KTHREAD is the single biggest correctness risk. Pick `ut_tab`/`UThread` (most complete) and delete the rest.

2. **Define a single context-switch ABI.** One function: `void SchedulerSwitch(Frame *old_frame, int old_idx, int new_idx)` that saves all GPRs + callee-saved + GS/TEB + CR3 into `old`, loads all from `new`, and returns. Both interrupt/syscall entry (`isr_common`) and explicit yield (`KeYield`) call this.

3. **Per-CPU scheduler state.** `cpu_locals[n]` should own `ki_current[n]`, `ki_idle[n]`, runqueue, and scheduler lock. No global scheduler mutable state.

4. **Wait queue + dispatcher object.** Implement `KEVENT`, `KMUTEX` wait queues properly with `ThreadWaiting` states and wake-by-signal, not busy-spin.

5. **Driver import resolver.** Before any real driver can load, the kernel needs an export table and import resolution. This gates the driver ecosystem.

6. **NX + ASLR.** Minimum exploit mitigations for a modern OS targeting Win32 compatibility.

---

## 6. Files Not Yet Examined (recommended follow-up)

These were not read in this audit and should be checked before applying fixes:

- `kernel/ntdef.h` (referenced by sync.h; not found at that path — may not exist)
- `kernel/hal.h`, `kernel/bootinfo.h`, `kernel/driver_pe.h`, `kernel/heap.h`, `kernel/serial.h`, `kernel/ext4k.h`, `kernel/ntfsk.h`
- `boot/bootx64.c`, `boot/loadufi.c`
- `user/explorer.c`, `user/user32.c`
- `tests/` — especially `uthread_lifecycle_test.c`, `obmgr_lifetime_test.c`, `vmm_user_range_permissions_test.c`, `syscall_abi_contract_test.sh`

---

**End of report.** No files were modified. Awaiting your direction on which P0 fixes to apply first.
