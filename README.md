# magic OS

magic OS is an experimental x86-64 operating system project. It boots through UEFI, loads its kernel and HAL from an ext4 volume, and runs in QEMU for development and testing.

## Current features

- Three-stage UEFI boot chain with a firmware fallback loader and NVRAM boot entry.
- UEFI loader for x64 PE images, including base relocations and imports between loaded modules.
- Kernel and `hal.dll` loaded from `magic/System32` on ext4.
- Kernel initialization for CPU tables and exceptions, physical and virtual memory, and the kernel heap.
- HAL and kernel-thread scheduler self-tests, with serial diagnostics.
- QEMU run configuration with persistent UEFI variables under `build/`.

This is an early development project, not a general-purpose operating system. The kernel currently runs its tests, paints the framebuffer, and halts.

## Build prerequisites

The build scripts target Windows and expect:

- Administrator PowerShell for `scripts/build.ps1` (`diskpart` creates the EFI disk image).
- `clang`, `lld-link`, `qemu-img`, and `qemu-system-x86_64` available on `PATH`.
- EDK II headers in `C:\edk2`, or set the `EDK2` environment variable to their location.
- `mke2fs.exe` at `C:\msys64\usr\bin\mke2fs.exe`, or WSL with `mke2fs` installed.
- QEMU UEFI firmware under `C:\Program Files\qemu\share` for `scripts/run.ps1`.

## Build and run

From the repository root in an Administrator PowerShell:

```powershell
.\scripts\build.ps1
```

Build and launch the result in QEMU:

```powershell
.\scripts\build.ps1 -Run
```

To launch an already-built image without rebuilding:

```powershell
.\scripts\run.ps1
```

QEMU connects the guest serial port to the PowerShell terminal. Kernel diagnostics and self-test results appear there. On a normal run, the framebuffer is painted and the kernel halts.

## Build options

- `-Run`: launch QEMU after a successful build.
- `-KeepFallback`: do not remove `EFI\BOOT\BOOTX64.EFI` after registering the UEFI boot entry.
- `-ResetNvram`: remove the saved `build\vars.fd` before rebuilding, resetting QEMU's UEFI boot variables.
- `-Clean`: detach the generated virtual disk if needed and remove `build\`.

For example, rebuild with the fallback loader retained:

```powershell
.\scripts\build.ps1 -KeepFallback -Run
```

## Boot flow

1. Firmware starts `EFI\BOOT\BOOTX64.EFI` from the FAT EFI System Partition.
2. `bootx64.efi` starts `EFI\magic\bootuni.efi`.
3. `bootuni.efi` registers the `magic OS` UEFI boot entry and starts `EFI\magic\loadufi.efi`.
4. `loadufi.efi` reads `hal.dll` and `krnl.exe` from `magic/System32` on the ext4 volume, exits UEFI boot services, and transfers control to the kernel.

## Generated files

Build artifacts are written under `build/`:

- `esp.img`: FAT EFI System Partition disk image.
- `magic.img`: ext4 volume containing `magic/System32/hal.dll` and `krnl.exe`.
- `vars.fd`: persistent QEMU UEFI variables.
- `esp/`, `rootfs/`, and `obj/`: staged files and intermediate objects.

## Source layout

- `boot/`: UEFI boot stages, ext4 reader, and PE loader.
- `kernel/`: kernel entry point, CPU setup, memory managers, heap, and executive routines.
- `hal/`: hardware abstraction layer.
- `shared/`: boot information and shared type definitions.
- `scripts/`: Windows build and QEMU launch scripts.
