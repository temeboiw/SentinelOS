# SentinelOS V10 ETERNITY — Interface Contracts

This document defines the immutable interfaces between kernel and user mode,
between agents, and between SentinelOS and Windows-compatible user-mode
binaries. All six agents must comply. Changes require updating this document
and notifying all agents.

## 1. System Call ABI

Syscalls are invoked via `int 0x80` from ring 3.

| Register | Purpose |
|----------|---------|
| `rax` | Syscall number (input); return value / NTSTATUS (output) |
| `rdi` | Argument 1 |
| `rsi` | Argument 2 |
| `rdx` | Argument 3 |
| `rcx` | Argument 4 |
| `r8` | Argument 5 |
| `r9 | Argument 6 |

The syscall handler runs on the kernel stack with the user frame pointer
available as `Frame *f`. All general-purpose registers except `rax` are
callee-saved across a context switch — `rax` is the return-value register and
is NOT preserved by `up_save`/`ut_save` (see Phase 1 finding #2 in
`AGENT_COORDINATION.md`). User code must not assume `rax` survives a syscall
that yields or waits.

### Frame layout (kernel/cpu.h)

```c
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error;      // pushed by per-vector stub
    uint64_t rip, cs, rflags, rsp, ss;   // pushed by CPU on privilege change
} Frame;
```

User CS = `0x1B`, User SS = `0x23` (ring 3 selectors).

## 2. Syscall Number Registry

| Number | Name | Agent | Status |
|--------|------|-------|--------|
| 0 | `SYS_NtTerminateProcess` | 1 | defined |
| 1 | `SYS_NtWriteFile` | 1/5 | defined |
| 2 | `SYS_NtGetCurrentProcessId` | 1 | defined |
| 3 | `SYS_NtAllocateVirtualMemory` | 1 | defined |
| 4 | `SYS_NtCreateProcess` | 1 | defined |
| 5 | `SYS_NtOpenFile` | 1/5 | defined |
| 6 | `SYS_NtReadFile` | 1/5 | defined |
| 7 | `SYS_NtQueryDirectoryFile` | 1/5 | defined |
| 8 | `SYS_NtMapFramebuffer` | 3 | defined |
| 9 | `SYS_NtGetProcAddress` | 2 | defined |
| 10 | `SYS_NtGetInput` | 4 | defined |
| 11 | `SYS_NtShutdown` | 1 | defined |
| 12 | `SYS_NtSetCurrentDirectory` | 4 | defined |
| 13 | `SYS_NtCreateThread` | 1 | defined |
| 14 | `SYS_NtWaitForSingleObject` | 1 | defined |
| 15 | `SYS_WmRegister` | 4 | defined |
| 16 | `SYS_WmSetMin` | 4 | defined |
| 17 | `SYS_WmUnregister` | 4 | defined |
| 18 | `SYS_WmQuery` | 4 | defined |
| 19 | `SYS_WmRestore` | 4 | defined |
| 20 | `SYS_WmSetRect` | 4 | defined |
| 21 | `SYS_NtYield` | 1 | defined |
| 22 | `SYS_NtCloseFile` | 1/5 | defined |
| 23 | `SYS_NtGetSystemTime` | 1 | defined |
| 24 | `SYS_NtGetTickCount` | 1 | defined |
| 25 | `SYS_NtQueryFileSize` | 1/5 | defined |
| 26 | `SYS_NtSetFilePosition` | 1/5 | defined |
| 27 | `SYS_NtFreeVirtualMemory` | 1 | defined |
| 28 | `SYS_NtProtectVirtualMemory` | 1 | defined |
| 29 | `SYS_NtQueryVirtualMemory` | 1 | defined |
| 30 | `SYS_NtQueryFileMetadata` | 1/5 | defined |
| 31 | `SYS_NtQueryVolumeMetadata` | 1/5 | defined |
| 32 | `SYS_NtInitializeGraphics` | 3 | defined |
| 33 | `SYS_NtFlushGraphics` | 3 | defined |
| 34 | `SYS_NtSubmit3D` | 3 | defined |
| 35 | `SYS_NtPresent3D` | 3 | defined |
| 36 | `SYS_NtUploadVirGLBuffer` | 3 | defined |
| 37 | `SYS_NtPresentLayer` | 3/4 | defined |
| 38 | `SYS_NtGetGameKeyState` | 3 | defined |
| 39 | `SYS_NtGetGameMouseDelta` | 3 | defined |
| 40 | `SYS_NtPresentWindowSurface` | 3/4 | defined |
| 41 | `SYS_NtQueryProcessList` | 1 | defined |
| 42 | `SYS_NtGetApplicationError` | 1 | defined |
| 43 | `SYS_NtLoadLibrary` | 2 | defined |
| 44 | `SYS_NtSocketCreate` | 5 | defined |
| 45 | `SYS_NtSocketClose` | 5 | defined |
| 46 | `SYS_NtSocketBind` | 5 | defined |
| 47 | `SYS_NtSocketSendTo` | 5 | defined |
| 48 | `SYS_NtSocketRecvFrom` | 5 | defined |
| 49 | `SYS_NtNetworkInfo` | 5 | defined |
| 50 | `SYS_NtPing` | 5 | defined |
| 51 | `SYS_NtCreateOutputFile` | 1/5 | defined |
| 52 | `SYS_NtWriteOutputFile` | 1/5 | defined |
| 53 | `SYS_WmGetDirty` | 4 | defined |
| 56 | `SYS_NtSocketConnect` | 5 | defined |
| 57 | `SYS_NtSocketSend` | 5 | defined |
| 58 | `SYS_NtSocketRecv` | 5 | defined |
| 60 | `SYS_NtQueryInstallerDisk` | 6 | defined |
| 61 | `SYS_NtInstallerDiskIo` | 6 | defined |
| 62 | `SYS_NtReadInstallerPayload` | 6 | defined |
| 63 | `SYS_NtQueryInstallerPayload` | 6 | defined |
| 64 | `SYS_NtVerifyInstallerPayload` | 6 | defined |
| 65 | `SYS_NtInstallerInstallImage` | 6 | defined |

Syscall numbers 0–65 are allocated. Numbers 22, 25, 26 are also defined at
lines 1097–1101 of `kernel/user.h` (duplicate definitions — must be
de-duplicated in Phase 1). Numbers 54, 55, 59, 66+ are unallocated and
reserved for future use.

Return convention: `rax` holds NTSTATUS-like result. Zero or positive =
success; negative = failure. User-mode libraries translate to Win32
`GetLastError` values.

## 3. PE Image Contract

All user-mode executables and DLLs must be **PE32+ x86-64** images:
- DOS header with `MZ` signature
- `PE\0\0` signature at `e_lfanew`
- COFF machine = `0x8664` (x86-64)
- Optional header magic = `0x20b` (PE32+)

Validated by `tests/pe_image_validation_test.py` on every file in
`build/rootfs/magic/System32/` with `.dll` or `.exe` extension.

User images are loaded as private physical copies with relocations applied
(IMAGE_REL_BASED_DIR64, type 10). Import resolution supports API-set contract
forwarding (e.g. `api-ms-win-crt-*` → shared CRT implementation) and export
forwarding chains. Each process/module pair has its own physical image
allocation; `BootModule` metadata is shared and must never be modified.

User mode address space layout:
- `USER_TEB_BASE` = `0x000000007FFF0000`
- `USER_PEB_BASE` = `0x000000007FFF1000`
- `USER_THREAD_TEB_BASE` = `0x000000007F000000`
- `USER_STACK_TOP` — process stack top (exact value arch-defined)

## 4. Graphics / Framebuffer Contract

| Syscall | Name | Purpose |
|---------|------|---------|
| 32 | `SYS_NtInitializeGraphics` | Discover and initialize graphics backend; map surface into calling process |
| 8 | `SYS_NtMapFramebuffer` | Map framebuffer pages into user address space |
| 33 | `SYS_NtFlushGraphics` | Transfer a damaged rectangle to scanout |
| 34 | `SYS_NtSubmit3D` | Submit bounded VirGL command stream |
| 35 | `SYS_NtPresent3D` | Transfer VirGL render target to backing memory, present via 2D scanout |
| 36 | `SYS_NtUploadVirGLBuffer` | Upload shared vertex/index buffer contents before drawing |
| 37 | `SYS_NtPresentLayer` | Present a composed layer |
| 40 | `SYS_NtPresentWindowSurface` | Present a window surface |

The kernel owns PCI configuration, BAR mappings, the VirtIO-GPU control
virtqueue, and command submission. User-mode code never accesses PCI
configuration ports or GPU MMIO directly.

When VirGL is unavailable, the software 3D renderer (`soft3d.c`) provides a
fixed-point reference rasterizer with depth buffer.

## 5. Filesystem / VFS Contract

| Syscall | Name |
|---------|------|
| 5 | `SYS_NtOpenFile` |
| 6 | `SYS_NtReadFile` |
| 7 | `SYS_NtQueryDirectoryFile` |
| 22 | `SYS_NtCloseFile` |
| 25 | `SYS_NtQueryFileSize` |
| 26 | `SYS_NtSetFilePosition` |
| 30 | `SYS_NtQueryFileMetadata` |
| 31 | `SYS_NtQueryVolumeMetadata` |

The system volume is NTFS at a GPT partition. The EFI System Partition is FAT32.
The root of the OS filesystem tree is `/magic`; all OS binaries live under
`/magic/System32/`. No PE images may appear at the filesystem root.

User page tracking: `PTE_OWNED` bit marks user-allocated pages so cleanup
cannot free borrowed pages.

## 6. Process / Thread Contract

| Syscall | Name |
|---------|------|
| 0 | `SYS_NtTerminateProcess` |
| 4 | `SYS_NtCreateProcess` |
| 13 | `SYS_NtCreateThread` |
| 14 | `SYS_NtWaitForSingleObject` |
| 21 | `SYS_NtYield` |
| 41 | `SYS_NtQueryProcessList` |
| 42 | `SYS_NtGetApplicationError` |

Two scheduler subsystems exist in the source tree:
- `kernel/uthread.h` — active user-thread scheduler (`ut_tab`, `ut_save`,
  `ut_load`, `ut_create`, `ut_exit_current`, `ut_wait`). Used by user32/syscall
  path. `ut_save` does NOT preserve `rax`.
- `kernel/user.h` — process scheduler (`up_tab`, `up_save`, `up_save_full`,
  `up_load`). Defined but never called.
- `kernel/uproc.h` — duplicate of process scheduler. Not included by any TU.

Phase 1 must reconcile these into a single scheduler and ensure `rax` is
preserved across context switches.

## 7. Memory Management Contract

| Syscall | Name |
|---------|------|
| 3 | `SYS_NtAllocateVirtualMemory` |
| 27 | `SYS_NtFreeVirtualMemory` |
| 28 | `SYS_NtProtectVirtualMemory` |
| 29 | `SYS_NtQueryVirtualMemory` |

4-level paging (PML4/PDPT/PD/PT), 2 MiB and 4 KiB pages. Higher-half kernel
at `HHDM_BASE = 0xFFFF800000000000`. Kernel loads at `0xFFFFFFFF80200000`.
HAL at `0xFFFFFFFF81000000`.

`map_4k_in` maps a single 4 KiB page with `PTE_U | PTE_RW | PTE_OWNED` for
user pages. `map_2m` maps 2 MiB pages. `invlpg` was removed from `map_4k_in`
and `map_2m` in a recent change — TLB invalidation behavior must be verified
in Phase 1.

## 8. Networking Contract

| Syscall | Name |
|---------|------|
| 44 | `SYS_NtSocketCreate` |
| 45 | `SYS_NtSocketClose` |
| 46 | `SYS_NtSocketBind` |
| 47 | `SYS_NtSocketSendTo` |
| 48 | `SYS_NtSocketRecvFrom` |
| 49 | `SYS_NtNetworkInfo` |
| 50 | `SYS_NtPing` |
| 56 | `SYS_NtSocketConnect` |
| 57 | `SYS_NtSocketSend` |
| 58 | `SYS_NtSocketRecv` |

e1000 Ethernet driver. IPv4, TCP, UDP. `SYS_NtNetworkInfo` returns IP, mask,
gateway, DNS, DHCP state, link state, MAC address.

## 9. Installer Contract

| Syscall | Name |
|---------|------|
| 60 | `SYS_NtQueryInstallerDisk` |
| 61 | `SYS_NtInstallerDiskIo` |
| 62 | `SYS_NtReadInstallerPayload` |
| 63 | `SYS_NtQueryInstallerPayload` |
| 64 | `SYS_NtVerifyInstallerPayload` |
| 65 | `SYS_NtInstallerInstallImage` |

The universal installer produces a GPT disk image (FAT32 ESP + NTFS system
volume) at `build/magic-disk.img`. It never touches real user disks.

## 10. Build Contract

- `scripts/build.sh --no-disk-image` must compile all kernel, HAL, and user
  images and produce a complete `build/rootfs/magic/System32/` tree.
- All System32 `.dll`/`.exe` files must be valid PE32+ x86-64 (validated by
  `tests/pe_image_validation_test.py`).
- `settings.exe` must be produced and must be PE32+ x86-64 (mandatory native
  Settings app per V6 ASCENSION prompt, carried into V10).
- Host-side regression tests (`vulkan_test.c`, etc.) run when a host C
  compiler is available.

## 11. No-Fake-Implementation Rule

Every exported function and syscall handler must either implement its documented
behavior or return an honest error code (`STATUS_NOT_IMPLEMENTED`,
`STATUS_NOT_SUPPORTED`, etc.). No function may return `STATUS_SUCCESS` without
performing the work. No placeholder, stub, or mock success response may be
committed to `main`.
