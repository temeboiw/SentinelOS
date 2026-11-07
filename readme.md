# SentinelOS V11 — SOVEREIGN

SentinelOS is an independent **x86-64 operating system** project focused on a native Windows-style desktop, a real kernel and user-mode runtime, and broad Win32 application compatibility. It is built from native components rather than being a Windows installation or a Linux distribution with a themed desktop.

V11 is the current development line. The project is actively evolving: features described as implemented below may still need end-to-end boot and runtime verification. This README distinguishes current implementation from the compatibility and product goals still being developed.

## Project direction

The goal is a cohesive desktop operating system with familiar Windows workflows and a native implementation:

- UEFI-first x86-64 boot and a GPT system disk.
- A native kernel, hardware abstraction layer, user-mode process/thread support, and PE32+ executable loading.
- A Windows-like filesystem layout and native system applications.
- Win32-compatible APIs and runtime services so existing Windows software can be brought up without bundling copied Windows system binaries.
- GPU-backed desktop and application rendering, with Vulkan as the graphics foundation and DXVK as a route toward Direct3D compatibility.
- User accounts, access control, elevation, system settings, and familiar administration tools.

## Current implementation snapshot

The repository contains active V11 implementation work in these areas:

### Boot, kernel, and hardware

- x86-64 UEFI boot chain and GPT disk-image build.
- FAT32 EFI System Partition and NTFS system partition in the Windows-host VHDX build.
- PE32+ loading infrastructure for boot and user-mode components.
- Kernel CPU, memory-management, executive, and scheduling subsystems.
- Hardware abstraction and device/graphics work, including VirtIO-GPU support.

### Win32 and user mode

- Native PE32+ user applications and DLLs.
- Expanding implementations of core Win32 libraries, including `kernel32`, `user32`, `advapi32`, and `ws2_32`.
- Window creation, message handling, text rendering, input, menus, and desktop-shell work.
- Ongoing API coverage and application-compatibility testing.

API coverage is not equivalent to complete Windows compatibility. Individual exports and behaviors still require implementation and testing against real applications.

### Registry and system tools

- Persistent registry-hive work in `advapi32`.
- Native `regedit.exe` work for browsing and editing registry keys and values.
- Basic `.reg` import/export code is present. The current implementation has format and buffer limitations, and end-to-end execution inside a booted SentinelOS environment has not yet been verified.
- User-system and UAC/elevation work are part of the V11 system-services implementation.

### Graphics

- VirtIO-GPU and userspace graphics work are present.
- The longer-term graphics direction is real GPU-backed Vulkan rendering, followed by a Direct3D compatibility path using DXVK.
- Do not interpret API presence or a software fallback as proof of a complete hardware Vulkan driver. Intel, AMD, and NVIDIA support and real application workloads require separate validation.

## V11 roadmap

The following are product and compatibility goals, not claims that every item is already finished.

### Shell and desktop

- **One unified `explorer.exe`:** desktop, taskbar, and file-browsing experiences should be parts of the same shell process. The intended architecture uses modular components such as DesktopView, FileBrowser, Taskbar, and shell services; a separate `fileexplorer.exe` should not be the final design.
- **System-wide right-click context menus** for files, folders, desktop items, and supported shell objects.
- Consistent window management, drag/resize behavior, icons, cursors, and desktop interactions.

### Registry and configuration

- Robust native `.reg` file association, import, and export.
- Compatibility with the Windows Registry Editor Version 5.00 format, including correct escaping, Unicode, supported registry value types, deletion entries, and multiline data.
- Validation and safe failure handling for imports, with clear error reporting.

### Command-line environments

- A full native **CMD** environment, including command execution, batch files, environment variables, pipes/redirection, and common built-in commands.
- A full native **PowerShell** environment with a compatible command/runtime model and scripting support.
- These are substantial compatibility projects; a basic command prompt or a limited command subset is not the finished target.

### Application compatibility

- Continue expanding Win32 API coverage based on real application requirements.
- Improve process, thread, memory, file, registry, networking, windowing, GDI, COM, and graphics compatibility.
- Validate against real applications and document known blockers instead of treating export counts as a compatibility percentage.

### Security and system services

- Complete user and permission infrastructure, elevation/UAC behavior, and system-wide security policy.
- Continue native service, driver, and security infrastructure work.
- Sentinel Defender is a future planned subsystem; it is not represented as implemented.

## Build and run

The current checked-in Windows build entry point is `scripts/build.ps1`. Run it from an **Administrator PowerShell** on a Windows host with the required native LLVM, EDK II, and QEMU tooling available.

Build:

```powershell
.\scripts\build.ps1
```

Build and launch in QEMU:

```powershell
.\scripts\build.ps1 -Run
```

Launch an existing build:

```powershell
.\scripts\run.ps1
```

Available build switches:

- `-Run` — launch QEMU after a successful build.
- `-Clean` — remove generated build output and detach generated disk images.
- `-KeepFallback` — retain the fallback UEFI bootloader.
- `-ResetNvram` — reset saved QEMU UEFI variables before rebuilding.

The build creates an expandable GPT VHDX with a FAT32 EFI System Partition and an NTFS system partition. Build artifacts are placed under `build\`.

> Build success, PE validation, and source-level tests do not by themselves prove that the complete OS boots or that an application works correctly. Runtime claims should be backed by an actual SentinelOS/QEMU or hardware test.

## Repository guide

- `boot/` — UEFI boot stages, image loading, and executable-loading code.
- `kernel/` — kernel, memory, process/thread, executive, and graphics subsystems.
- `hal/` — hardware abstraction layer.
- `drivers/` — device-driver code.
- `user/` — native user-mode libraries, shell, and system applications.
- `shared/` — shared ABI and system definitions.
- `scripts/` — Windows build and QEMU launch scripts.
- `tests/` — source-contract and integration tests.
- `docs/` and root-level status reports — architecture notes, audits, and implementation tracking.

## Development status

SentinelOS V11 is under active development and is not yet a finished, general-purpose Windows replacement. Compatibility is incremental: each subsystem needs implementation, integration, and runtime testing. Status reports such as [V11 architecture coordination](V11_ARCH_COORDINATION.md), [kernel audit](KERNEL_AUDIT_REPORT.md), and [Registry Editor status](REGEDIT_SECTION13_STATUS.md) provide deeper technical detail and should be read with their stated scope and verification limits in mind.

## Project history

SentinelOS V10 was the first kernel-era milestone. V11 SOVEREIGN continues from that foundation with a larger focus on native user mode, desktop integration, system services, graphics, and application compatibility.
