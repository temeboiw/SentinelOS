# SentinelOS V10 ETERNITY — Agent Coordination Protocol

This document establishes the mandatory coordination framework for the six
specialized agents delivering SentinelOS V10 ETERNITY: a complete, stable,
usable x86-64 operating system with Windows compatibility, modern graphics,
desktop shell, storage, networking, universal installer, and QA.

All work is performed directly on the `main` working tree. No Git branch is
created. No push to GitHub occurs unless explicitly instructed.

## 1. Agents and Domains

Six agents run concurrently under this protocol. Each agent owns a disjoint
domain but shares a common syscall ABI, PE image format, and build pipeline.

| Agent | Domain | Key deliverables |
|-------|--------|------------------|
| **Agent 1** — Kernel/HAL/Core | x86-64 kernel, HAL, paging, scheduler, syscall dispatch, interrupt/IDT, SMP, power | `kernel/*`, `hal/*`, `boot/*` |
| **Agent 2** — Win32 Compatibility | PE32+ loader, import resolution, API-set forwarding, kernel32/user32/gdi32/msvcrt/ucrt/vcruntime, syscall shim layer | `user/kernel32.c`, `user/user32.c`, `user/gdi32.c`, `user/msvcp140.c`, `user/ucrt_runtime.c`, `user/vcruntime140_1.c` |
| **Agent 3** — Graphics/Vulkan/DXVK/Gaming | Vulkan CPU software runtime, soft3d rasterizer, framebuffer, VirGL, OpenGL ES compat, game test apps | `user/vulkan.c`, `user/vk_softpipe.c`, `user/soft3d.c`, `user/opengl32.c`, `UserTestApps/*` |
| **Agent 4** — Desktop/Explorer/System Apps | Desktop shell, file explorer, taskbar, settings, task manager, cmd, window manager | `user/explorer.c`, `user/fileexplorer.c`, `user/settings.c`, `user/taskmgr.c`, `user/cmd.c`, `user/taskbar.c` |
| **Agent 5** — Storage/Hardware/Networking/Services | ATA/NVMe block, NTFS/FAT/ext4 VFS, HDA audio, e1000 networking, ACPI, PCI, driver manager | `drivers/*`, `kernel/pci_network.h`, `kernel/netstack.h`, `kernel/e1000.h` |
| **Agent 6** — Installer/Settings/Updates/Security/QA | Universal installer, settings app, update mechanism, security hardening, test suite, QA gates | `installer/*`, `tests/*`, `scripts/*` (QA-related) |

## 2. Mandatory Communication Protocol

### 2.1 Shared contract
All agents must comply with `docs/V10/INTERFACE_CONTRACTS.md`, which defines the
syscall ABI, syscall number registry, PE image requirements, and kernel/user
boundary. No agent may add, remove, or renumber a syscall without updating
`INTERFACE_CONTRACTS.md` and notifying all other agents.

### 2.2 Change announcement
Before modifying any file outside their own domain, an agent must announce the
intent (which files, what change, why) to all other agents. Cross-domain
modifications require explicit acknowledgement from the owning agent.

### 2.3 Build gate
Every change must keep `scripts/build.sh --no-disk-image` passing. A change that
breaks compilation or any existing test is rolled back before the next change
from that agent. No agent may land a change while the tree is red.

### 2.4 No fake implementations
No agent may create fake implementations, placeholder functions, mock success
responses, or misleading compatibility claims. Every exported function must
either implement its documented behavior or return an honest error code.
Unimplemented syscalls return `STATUS_NOT_IMPLEMENTED`, never `STATUS_SUCCESS`
with no work done.

### 2.5 Test everything
Every agent must add or extend tests for their domain under `tests/`. Tests run
in QEMU (TCG, x86_64, OVMF UEFI, GPT disk image) where applicable. Host-side
PE validation tests run on every build.

### 2.6 Conflict resolution
When two agents' changes conflict (same file, overlapping syscall numbers,
incompatible ABI), the agents negotiate a resolution. If unresolved within one
build cycle, the change that preserves the existing ABI and passes all existing
tests takes precedence.

## 3. Phase Execution Plan

### Phase 1 — Foundation stabilization
Stabilize kernel/HAL, process/thread management, finalize ABI, verify UEFI/GPT boot paths. Fix the `uproc.h`/`user.h` scheduler duplication
(see Phase 1 findings below). Ensure `rax` is preserved across context switches.

**Exit criteria:**
- `scripts/build.sh --no-disk-image` compiles all kernel, HAL, and user images
- `pe_image_validation_test.py` passes on all System32 PE images
- `syscall_abi_contract_test.sh` passes
- `vmm_address_space_teardown_test.sh` and `vmm_user_range_permissions_test.sh` pass
- Kernel boots to usermode demo in QEMU (serial output verified)

### Phase 2 — Win32 compatibility
Complete PE32+ loader, import resolution, API-set forwarding, kernel32/user32/
gdi32/msvcrt/ucrt/vcruntime exports. Validate all System32 DLLs are PE32+
x86-64. Run `win32compat.exe` smoke test in QEMU.

### Phase 3 — Graphics
Vulkan CPU software runtime (`vulkan.dll`), soft3d rasterizer, framebuffer
mapping/flush, VirGL optional acceleration. Run `vk_softpipe_test.c` and
`vulkan_test.c`. Game (`maze.exe`) renders a spinning triangle.

### Phase 4 — Desktop shell
Explorer, file explorer, taskbar, settings, task manager, cmd. Desktop shell
integration test passes (`integration_desktop_shell_test.sh`).

### Phase 5 — Storage, networking, installer, QA
Universal installer, settings app, updates, security hardening, full test
suite, release QA. `installer/` builds a bootable GPT disk image.

## 4. Phase 1 Immediate Findings

The following issues were identified during V10 intake and must be addressed in
Phase 1:

1. **`kernel/uproc.h` is dead code.** It defines `up_tab`, `up_save`, `up_load`,
   `up_find_proc`, `up_pick_next` but nothing includes it. `kernel/user.h`
   defines its own copy of the same scheduler state. `kernel/kernel.c` includes
   `user.h`, not `uproc.h`. Decision needed: either make `uproc.h` the single
   authoritative scheduler header (include it from `kernel.c`, remove the
   duplicate definitions from `user.h`), or delete `uproc.h` and keep
   `user.h` authoritative (matching its original comment).

2. **`rax` is not preserved across context switches.** `up_save` in `user.h`
   (line 262) saves `rip`/`rsp`/`rflags` and GPRs via `up_snap` but does NOT
   save `rax` (that line was removed). `up_save_full` (line 271) does save
   `rax`, but neither `up_save` nor `up_save_full` is called anywhere in the
   codebase. The active scheduler is `uthread.h` (`ut_save`/`ut_load`), whose
   `ut_save` also does not save `rax`, and `UThread` has no `rax` field. This
   means `rax` can be clobbered across a context switch, breaking any user code
   that relies on a return value surviving a yield/wait.

3. **`up_save`/`up_save_full` are never called.** The process scheduler state
   in `user.h`/`uproc.h` is defined but unused. Actual context switching goes
   through `uthread.h`. This duplication must be reconciled in Phase 1.

4. **Build environment.** On Windows hosts, `scripts/build.sh` requires
   `sfdisk`, `mkfs.vfat`, `mkntfs`, `ntfscp`, `ntfscat`, `gcc`, `mmd`, `mcopy`,
   `dd` — none of which are present. Only `clang`, `lld-link`, and `qemu-img`
   are available. `build.sh --no-disk-image` may still work for compilation but
   the disk-image stage cannot complete without the missing tools.

## 5. Branch and Release Policy

- **Work on `main` only.** Do not create another Git branch.
- **No GitHub push.** Do not push to GitHub unless explicitly instructed.
- **Never format, repartition, or overwrite a real user disk.** All disk-image
  operations target `build/magic-disk.img` only.
- **Commit often.** Each agent commits their domain changes with clear messages.
- **End commit messages with:** `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`
