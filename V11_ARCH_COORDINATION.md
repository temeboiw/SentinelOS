# SentinelOS V11 — Architecture Coordination (Step B)

**Source:** Consolidated findings from all six Step A agents (Kernel & NT Runtime, Win32 Compatibility, UI & Shell, Security & System Services, Vulkan & Hardware, DirectX/DXVK/QA) plus the kernel architecture audit (`KERNEL_AUDIT_REPORT.md`).  
**Status:** Read-only inspection complete. No files modified. This document establishes shared interfaces, file ownership, dependencies, and the integrated implementation plan for Step C.

---

## 1. Conflicts to Resolve (P0 — block all downstream work)

### C1: Dual / triple schedulers with no unification

**Finding:** Three independent scheduler subsystems coexist:
- `up_tab[8]` / UserProc (process-level) — `kernel/user.h:104-143`, `up_save`/`up_load` at 263-310
- `ut_tab[16]` / UThread (thread-level) — `kernel/uthread.h:23-164`, `ut_save`/`ut_load` at 56-100
- `ki_threads[32]` / KTHREAD (clock-driven) — `kernel/ke.h:21-273`, `KiSchedule` at 74-109

Each has its own save/load/dispatch. `ut_create` threads are not KTHREADs; clock preemption doesn't see `ut_tab` threads; `isr_handler` may call `KiSchedule` while `ut_wait` manually does `ut_save`/`ut_load`.

**Decision:** Consolidate on **`ut_tab`/`UThread`** as the single user-mode thread scheduler (most complete: save/load including `rax`, handle generation, stack+TEB lifecycle, GS/TEB base per thread). Deprecate `up_tab`/`up_save`/`up_save_full`/`up_load`/`up_pick_next` and `proc.h` dead code. `ke.h` KTHREAD remains for kernel-only threads if needed, but does not own user context.

**Fix route:** Unify context-switch through one `SchedulerSwitch(Frame*, old_idx, new_idx)` that saves all GPRs + callee-saved + GS/TEB + CR3 into old, loads from new. Both `isr_common` (interrupt/syscall entry) and explicit yield (`KeYield`) call this. `syscall_handler` checks quantum exhaustion before `iretq` and reschedules via the unified switch.

**File owner:** Agent 1 (Kernel & NT Runtime)  
**Blocks:** All threading/syscall/user-mode work (Agents 2, 3)

---

### C2: rax / callee-saved register preservation

**Finding (audit P0-2/P0-3):** `ut_save` saves `rip/rsp/rflags/rax` but drops `rbx/rbp/rsi/rdi/r12-r15`. `up_save`/`up_save_full` also thin. Callee-saved registers clobbered across context switch = silent nondeterministic corruption.

**Decision:** Extend `UThread` (and any retained `up_tab` entry) with `rbx, rbp, rsi, rdi, r12, r13, r14, r15`. `ut_save`/`ut_load` save/restore all callee-saved + `rax` exactly. Unify on `ut_save`/`ut_load` (remove `up_save_full`).

**File owner:** Agent 1 (Kernel & NT Runtime)  
**Blocks:** All C code that runs after context switch (everything)

---

### C3: GS base / TEB per-thread correctness

**Finding (audit P0-4):** `up_load` pins GS to `USER_TEB_BASE` always; `ut_load` correctly uses per-thread `teb_base`. Mixed scheduler use means some threads get wrong TEB.

**Decision:** Remove `up_load`; route all user-mode switches through `ut_load` which already does per-thread GS correctly.

**File owner:** Agent 1 (Kernel & NT Runtime)

---

### C4: SMP AP scheduler uses globals, not per-CPU state

**Finding (audit P0-5):** `KiInitializeApScheduler` writes global `ki_idle`/`ki_current`, not per-CPU `cpu_locals[cpu_index]`.

**Decision:** Make `ki_current`, `ki_idle`, runqueue, scheduler lock per-CPU via `cpu_locals[cpu_index]`. `KiInitializeApScheduler(cpu_index)` initializes only that CPU's fields.

**File owner:** Agent 1 (Kernel & NT Runtime)

---

### C5: `KeAcquireMutex` deadlock window

**Finding (audit P0-6):** Interrupts restored between releasing mutex spinlock and rescheduling; clock interrupt can re-enter wait queue.

**Decision:** Do not restore interrupts between releasing mutex spinlock and rescheduling. Clock handler skips threads with `wake_tick == UINT64_MAX` (infinite mutex wait).

**File owner:** Agent 1 (Kernel & NT Runtime)  
**Blocks:** All synchronization/mutex work (Agents 2, 4)

---

## 2. Missing / Stubbed APIs by Domain

### Kernel32 (`user/kernel32.c`) — ~300 exports implemented

**Implemented:** Process/thread/memory basics (ExitProcess, CreateThread, WaitForSingleObject, VirtualAlloc/Free/Protect/Query, GetProcAddress, LoadLibraryA), file I/O (CreateFileA/ReadFile/WriteFile/CloseHandle, FindFirstFile/Next/Close), time, console, environment, interlocked atomics (~40), TLS, critical sections/SRW locks/condition variables, string/memory runtime (~100).

**Stubbed / missing:** CreateDirectory/RemoveDirectory/DeleteFile/MoveFile/CopyFile/SetFileAttributes/FlushFileBuffers (return error), CreateProcessA/W, CreateMutex/Event/Semaphore, WaitForMultipleObjects, Resume/SuspendThread, GetThreadContext, GetExitCodeProcess (stubbed), GetCommandLine/GetModuleHandle/GetModuleFileName, SetLastError/GetLastError.

**Owner:** Agent 2 (Win32 Compatibility)

### USER32 (`user/user32.c`) — substantial implementation

**Implemented:** CreateWindowExA/ShowWindow/UpdateWindow/DestroyWindow/MoveWindow/SetWindowPos/GetClientRect/GetWindowRect, message queue (GetMessage/TranslateMessage/DispatchMessage/PostMessage/PeekMessage/PostQuitMessage), drawing (TextOutA/InvalidateRect/BeginPaint/EndPaint/GetDC/ReleaseDC), input (GetKeyState/GetAsyncKeyState/GetCursorPos/SetCursorPos/ShowCursor), timers (SetTimer/KillTimer), dialogs/controls (GetDlgItem/SetDlgItemText), menus (CreateMenu/AppendMenu/DestroyMenu/TrackPopupMenu), resources (LoadCursor/LoadIcon/LoadImage), system metrics, desktop shell (CreateWindowExA/ShowWindow/GetMessageA/TextOutA/DrawTaskbar/SetTimer/UpdateWindow/BeginRedrawBatch/EndRedrawBatch via NtGetProcAddress).

**Missing:** CreateWindowExW, RegisterClassW, DialogBox/CreateDialog/EndDialog/IsDialogMessage, TranslateAccelerator, GetSystemMenu/GetMenu/SetMenu/DrawText/FillRect/FrameRect/InvalidateRect/ValidateRect, ClientToScreen/ScreenToClient/MapWindowPoints, BringWindowToTop/IsZoomed/IsWindowEnabled/IsWindowUnicode, clipboard APIs, hook APIs, and many more.

**Owner:** Agent 3 (UI & Shell)

### GDI32 — NOT IMPLEMENTED (only TextOutA exists in user32)

**Missing entirely:** BitBlt, CreateDC/CreateCompatibleDC/DeleteDC, SelectObject/DeleteObject, Rectangle/Ellipse/LineTo/MoveToEx/SetPixel/GetPixel, CreateFontA/ExtTextOutA, DIB APIs, palette, metafile, etc.

**Owner:** Agent 3 (UI & Shell) — new file `user/gdi32.c` required

### WS2_32 (`user/ws2_32.c`) — partial

**Implemented:** WSAStartup/WSACleanup/socket/closesocket/connect/bind/send/sendto/recv/recvfrom/getaddrinfo/gethostname.

**Stubbed / missing:** listen/accept/shutdown/select/ioctlsocket/setsockopt/getsockopt/getsockname/getpeername/WSAIoctl/WSAGetLastError/inet_addr/inet_ntoa/gethostbyname/WSASocket.

**Owner:** Agent 2 (Win32 Compatibility)

### ADVAPI32 (`user/advapi32.c`) — stubbed

**Implemented:** GetUserNameW (hardcoded "Sentinel"), SystemFunction036 (RDRAND).

**Stubbed / missing:** All Reg* APIs (return ERROR_FILE_NOT_FOUND), SetEntriesInAclA/GetSecurityInfo/SetSecurityInfo (return NOT_SUPPORTED/CALL_NOT_IMPLEMENTED). No tokens, SIDs, ACLs, security descriptors, cryptography beyond RDRAND.

**Owner:** Agent 4 (Security & System Services)

### Shell32 / COMCTL32 / COMDLG32 / MSVCRT / ntdll user-mode / OLE32 — NOT PRESENT

**Owner:** Agent 3 (UI & Shell) for shell/comctl/comdlg; Agent 4 (Security) for ole32/com

---

## 3. Graphics / Hardware Status

### Implemented
- HAL: port I/O, COM1, TSC calibration, local APIC + clock interrupt, firmware re-entry (`hal/hal.c`)
- VirtIO-GPU 2D: PCI discovery, virtqueue, resource/scanout, transfer, flush (`kernel/gfx.h`)
- VirGL command submission: context, render target, shared buffer, submit, upload, present (`kernel/gfx.h`) — **no Vulkan→VirGL translation**
- Graphics syscalls: 8 (MapFramebuffer), 32-37, 40, 54 (WmCursor) — consistent cross-agent (`kernel/user.h`, `user32.c`, `gdi32.c`, `ntdll_user.h`, `vulkan.c`)
- Userspace Vulkan: complete API layer but CPU software rasterizer only (soft3d) — **not a real GPU driver/ICD**

### Missing
- Vulkan→VirGL bridge / translator
- GPU memory management (no VkDeviceMemory GPU heap)
- GPU fences/semaphores/timeline sync
- Intel/AMD/NVIDIA kernel drivers (only detected in PCI inventory)
- Swapchain / present engine
- D3D 8/9/10/11, DXGI, D3D12, DXVK (DXVK = uninitialized git submodule)

**Owner:** Agent 5 (Vulkan & Hardware) for kernel graphics; Agent 6 (DirectX/DXVK/QA) for DXVK/D3D strategy

---

## 4. Security Status

### Implemented
- Object Manager: handle-based access control, granted_access validation, per-owner handle isolation, type checking, reference counting, spinlock sync (`kernel/obmgr.h`)
- Driver registry (32 slots) + driver loader with PE validation/relocation (`kernel/driver.h`, `kernel/driver_loader.h`)
- SHA-256 exists (`kernel/sha256.h`) but **not used by driver loader** (no signature verification)

### Missing
- No access tokens, SIDs, ACLs, security descriptors
- No DACLs/SACLs, no privilege system, no impersonation
- No UAC/elevation, no consent UI
- No auditing, no security event log, no system policies
- No driver signature verification
- No Service Control Manager (SCM)
- `ObTypeKey` defined but unused (no registry hive)

**Owner:** Agent 4 (Security & System Services)

---

## 5. Shared Interfaces (established, verified cross-agent)

| Interface | Location | Status |
|-----------|----------|--------|
| Syscall ABI | `int 0x80`, rax=number, rdi/rsi/rdx args, result in rax | Verified (`syscall-abi-numbers.md`, `boot/bootinfo.h:46-59`) |
| Syscall numbers | 0-65 allocated (`kernel/user.h:19-65`) | Verified |
| Object Manager | `kernel/obmgr.h` — OB_MAX_OBJECTS=64, OB_MAX_HANDLES=256 | Implemented |
| Driver ABI | `kernel/driver.h` — SENTINEL_DRIVER_SERVICES v1 | Implemented |
| Graphics syscalls | 8, 32-37, 40, 54 | Verified cross-agent (`tests/integration_graphics_syscall_test.sh`) |
| NTSTATUS | `shared/ntabi.h` — redefined vs `shared/ntdef.h` | Needs dedup |

---

## 6. File Ownership for Step C (Implementation)

| Domain | Owner Agent | Primary Files |
|--------|-------------|---------------|
| Kernel & NT Runtime | Agent 1 | `kernel/user.h`, `kernel/uthread.h`, `kernel/ke.h`, `kernel/sync.h`, `kernel/cpu.h`, `kernel/vmm.h`, `kernel/pmm.h`, `kernel/uproc.h` (deprecate) |
| Win32 Compatibility | Agent 2 | `user/kernel32.c`, `user/ws2_32.c` |
| UI & Shell | Agent 3 | `user/user32.c`, `user/gdi32.c` (new), `user/explorer.c`, `kernel/wm.h` |
| Security & System Services | Agent 4 | `kernel/obmgr.h`, `kernel/driver.h`, `kernel/driver_loader.h`, `user/advapi32.c` |
| Vulkan & Hardware | Agent 5 | `kernel/gfx.h`, `hal/hal.c`, `drivers/ata/ata.c`, `drivers/audio/hda.c` |
| DirectX / DXVK / QA | Agent 6 | `tests/`, `scripts/build.ps1`, `docs/VULKAN_DXVK_STATUS.md`, DXVK submodule |

**Cross-cutting (orchestrator owns):** `kernel/kernel.c` (boot sequence), `C:\SentinelOS\V11_ARCH_COORDINATION.md` (this plan), `KERNEL_AUDIT_REPORT.md` (audit findings)

**Rule:** No agent may modify files owned by another agent without orchestrator coordination. Shared kernel interfaces (syscall numbers, Object Manager, driver ABI, graphics syscall contract) cannot be changed by any agent without orchestrator approval.

---

## 7. Dependencies (execution order)

```
Phase 1 (foundation, no external deps):
  P0 scheduler unification (Agent 1) → unblocks threading
  P0 callee-saved register preservation (Agent 1) → unblocks all C code after switch
  P0 GS/TEB per-thread (Agent 1) → unblocks user-mode threads
  P0 SMP per-CPU scheduler (Agent 1) → unblocks SMP
  P0 KeAcquireMutex deadlock fix (Agent 1) → unblocks synchronization

Phase 2 (kernel APIs):
  Missing syscalls: NtReadFile, NtOpenFile, NtCreateFile, NtAllocateVirtualMemory, NtFreeVirtualMemory, NtQueryInformationProcess (Agent 1)
  Driver import resolver / kernel export table (Agent 4) → unblocks real drivers
  Driver signature verification using SHA-256 (Agent 4)

Phase 3 (Win32 APIs):
  kernel32: CreateProcess, mutexes, events, semaphores, WaitForMultipleObjects (Agent 2)
  ws2_32: listen/accept/shutdown/select/etc. (Agent 2)
  advapi32: registry APIs, security APIs (Agent 4)

Phase 4 (UI/GDI):
  GDI32 implementation: BitBlt, CreateDC, SelectObject, Rectangle, etc. (Agent 3)
  USER32 missing: dialogs, clipboard, wide-char APIs (Agent 3)

Phase 5 (Graphics):
  Vulkan→VirGL bridge (Agent 5)
  GPU memory management, GPU fences/semaphores (Agent 5)
  DXVK integration + D3D12 strategy (Agent 6)

Phase 6 (Security):
  Tokens/SIDs/DACLs, UAC, auditing, SCM (Agent 4)

Phase 7 (Testing & integration):
  Runtime app tests (Chrome, Steam, Geometry Dash) (Agent 6)
  Build integration & performance validation (Agent 6)
  Regression testing across all domains (all agents)
```

---

## 8. P0 Fixes from Kernel Audit (must apply before V11 features)

| # | Finding | Fix | Owner |
|---|---------|-----|-------|
| P0-1 | Dual schedulers conflict | Consolidate on `ut_tab`/`UThread`; remove `up_tab`, `up_save`, `up_save_full`, `up_load`, `up_pick_next` | Agent 1 |
| P0-2 | Callee-saved registers not preserved | Add `rbx/rbp/rsi/rdi/r12-r15` to `UThread`; save/restore in `ut_save`/`ut_load` | Agent 1 |
| P0-3 | rax not preserved across switch | Unify on `ut_save`/`ut_load` preserving `rax` exactly | Agent 1 |
| P0-4 | `up_load` pins GS to `USER_TEB_BASE` | Remove `up_load`; route through `ut_load` | Agent 1 |
| P0-5 | SMP AP scheduler uses globals | Per-CPU `ki_current`/`ki_idle`/runqueue via `cpu_locals[cpu_index]` | Agent 1 |
| P0-6 | `KeAcquireMutex` deadlock window | Don't restore interrupts between spinlock release and reschedule; clock skips `wake_tick==UINT64_MAX` | Agent 1 |

---

## 9. Implementation Status Tracking

Use these statuses per component:
- NOT IMPLEMENTED
- STUBBED
- PARTIALLY IMPLEMENTED
- IMPLEMENTED
- TESTED
- VERIFIED

All components start at their current status per the Step A reports. Phase 1 P0 fixes move components from STUBBED/PARTIAL toward IMPLEMENTED.

---

## 10. Next Step (Step C — Implementation)

Begin with Phase 1 P0 fixes. Agent 1 starts scheduler unification in `kernel/uthread.h` and `kernel/ke.h`. No other agent touches scheduler files during Phase 1.

Files **not** to be modified during Phase 1 (orchestrator lock): `kernel/uthread.h`, `kernel/ke.h`, `kernel/sync.h`, `kernel/cpu.h` (scheduler-related sections).

After Phase 1 completes and is verified, agents proceed to their Phase 2+ domains concurrently where independent.

---

**End of Step B.** Ready for Step C implementation.