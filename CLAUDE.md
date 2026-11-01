# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

SentinelOS is an experimental x86-64 operating system. It boots through UEFI from a single GPT disk (FAT32 EFI System Partition + NTFS system partition) and runs in QEMU for development and testing. The boot chain is three-stage UEFI: `EFI\BOOT\BOOTX64.EFI` → `EFI\magic\bootuni.efi` → `EFI\magic\loadufi.efi`, which loads `magic/System32/hal.dll` and `krnl.exe` from the system volume and transfers control to the kernel. The kernel currently initializes CPU tables/memory/heap, runs HAL and kernel-thread scheduler self-tests, paints the framebuffer, and starts usermode (`smss.exe`).

## Build and run

The build scripts target Windows; on Linux/proot the PowerShell entry point forwards to `scripts/build.sh`.

### Linux/proot (primary host here)

Required tools: `clang`, `lld`, `qemu-img`, `qemu-system-x86_64`, `sfdisk`, `mkfs.vfat`, `mkntfs`, `ntfscp`, `ntfscat`, `gcc`, `mtools` (`mmd`, `mcopy`), `dd`, EDK II headers (via `EDK2` env var, falling back to `/usr/include/efi`).

```bash
./scripts/build.sh              # build all PE/EFI images and the GPT disk image
./scripts/build.sh --run        # build then launch QEMU
./scripts/run.sh                # launch an already-built image (build/magic-disk.img)
./scripts/build.sh --clean      # remove build/
./scripts/build.sh --reset-nvram # remove build/vars.fd before rebuilding
./scripts/build.sh --keep-fallback # keep EFI\BOOT\BOOTX64.EFI after registering the UEFI entry
```

QEMU connects the guest serial port to the terminal; kernel diagnostics and self-test results appear there. `run.sh` detects proot/Termux and serves the display over VNC on `127.0.0.1:5900`; otherwise it uses SDL.

### Windows (native)

From an Administrator PowerShell (DiskPart creates the VHDX):

```powershell
.\scripts\build.ps1
.\scripts\build.ps1 -Run
.\scripts\run.ps1
```

`build.ps1` delegates compilation/staging to `scripts/build.sh` via Git for Windows Bash (never WSL), then creates the expandable GPT VHDX (`build/magic-disk.vhdx`) with a FAT32 ESP and NTFS `MAGIC` system partition, staging `build/rootfs` into it. `run.ps1` boots the VHDX with WHPX/host CPU by default.

Build options (Linux flags shown; PowerShell equivalents in readme.md): `--run`, `--keep-fallback`, `--reset-nvram`, `--clean`, `--no-disk-image` (compile/stage only, skip disk-image creation), `--secure-boot` (requires `SECURE_BOOT_KEY` and `SECURE_BOOT_CERT` plus `sbsign`/`sbverify`).

## Tests

There is no test framework binary to invoke. Tests are:

**Source-contract shell tests** (`tests/*.sh`) — grep source files for ABI/syscall contracts, run directly from the repo root:

```bash
bash tests/syscall_abi_contract_test.sh .
bash tests/kernel_io_test.sh        # not present; see below
```

**Host-compiled C tests** (`tests/*.c`) — compiled with the host C compiler (`cc`/`gcc`, not the freestanding clang target) and run directly:

```bash
cc tests/kernel_io_test.c -o /tmp/kernel_io_test && /tmp/kernel_io_test
cc tests/soft3d_test.c user/soft3d.c -o /tmp/soft3d_test && /tmp/soft3d_test
cc -std=c11 -O2 -Wall -Wextra -Werror user/vk_softpipe.c tests/vk_softpipe_test.c -o /tmp/vk-softpipe-test && /tmp/vk-softpipe-test
clang -std=c11 -Wall -Wextra -Iuser tests/vulkan_test.c -o /tmp/vulkan-test && /tmp/vulkan-test
```

CI (`.github/workflows/sentinel-build.yml`) runs the file-explorer contract tests, the Vulkan and soft3d host tests, then `bash ./scripts/build.sh`, export/packaging validation, and a 45-second QEMU boot smoke test asserting graphics initialization and `bootvid.exe → sentlogon.exe` appear in the serial log. A separate `vulkan-softpipe.yml` workflow builds/runs the softpipe rasterizer test on changes to `user/vk_softpipe.[ch]` or `tests/vk_softpipe_test.c`.

To run a single test, pick the file and invoke it directly — there is no test runner. Shell tests take the repo root as their first argument (default `.`).

## High-level architecture

### Directory responsibilities

- `boot/` — UEFI boot stages and the ext4 reader / PE loader: `bootx64.c` (firmware fallback loader), `bootuni.c` (registers the `magic OS` NVRAM boot entry, starts `loadufi.efi`), `loadufi.c` (scans block devices for the ext4 partition, reads `hal.dll` and `krnl.exe`, exits boot services, jumps to the kernel), plus `ext4.h`, `ntfs.h`, `nvram.h`, `devpath.h`, `common.h`.
- `kernel/` — kernel entry (`kmain` in `kernel.c`), CPU setup (`cpu.h`, `smp.h`, `smp_trampoline.S`), physical/virtual memory managers (`pmm.h`, `vmm.h`, `heap.h`), kernel-thread scheduler (`ke.h`), object manager (`obmgr.h`), I/O manager + IRP dispatch (`iomgr.h`, `block.h`), driver loader/PE (`driver.h`, `driver_pe.h`, `driver_loader.h`), block/network/audio/graphics drivers (`ahci.h`, `ata.h`, `virtio_blk.h`, `virtio_input.h`, `e1000.h`, `pci_network.h`, `netstack.h`, `hda.h`, `gfx.h`, `wm.h`, `ps2.h`, `serial.h`), sync primitives (`sync.h`, `spinlock.h`), usermode boundary (`user.h`, `uproc.h`), boot-time checks (`bootcheck.h`), `klib.h`, `sha256.h`, `partition.h`.
- `hal/` — hardware abstraction layer (`hal.c`), one file.
- `shared/` — types shared between bootloader, kernel, and usermode: `bootinfo.h` (`BootInfo` passed from loader to `kmain`), `hal.h`, `ntabi.h` (NT-style ABI/syscall numbers), `ntdef.h` (NT-style base types).
- `user/` — Win32-like usermode subsystem implemented as freestanding PE images: `kernel32.dll`, `user32.dll`, `vulkan.dll` (CPU software renderer via `vk_softpipe.c`), `opengl32.dll`, `ucrt_runtime.c`, `api-ms-win-crt-*` API-set shims, `msvcp140.dll`, `vcruntime140*.dll`, `advapi32.dll`, `userenv.dll`, `shell32.dll`, `comdlg32.dll`, `winmm.dll`, `ws2_32.dll`, `imageres.dll`, `taskbar.dll`, `soft3d.c`, plus system executables `smss.exe`, `sentlogon.exe`, `explorer.exe`, `fileexplorer.exe`, `cmd.exe`, `taskmgr.exe`, `settings.exe`, `msiexec.exe`, `bootvid.exe`. Fonts: `font8x8.h`, `font_ttf.h`, `temp_path.h`.
- `UserTestApps/` — user-mode compatibility apps installed at disk root: `game.cpp`, `maze.cpp` (VirGL), `win32_compat.c`, `virgl_protocol.h`.
- `drivers/ata/` — kernel-side ATA driver (`ata.c`) built to `magic/System32/drivers/ata.sys`.
- `scripts/` — `build.sh`/`build.ps1`, `run.sh`/`run.ps1`, `run-iso.sh`, `vhd.ps1`, `scripts/ntfs-mkdir.c` (libntfs-3g helper for populating the NTFS image without FUSE/mount).
- `installer/` — UEFI ISO installer build (`build-iso.sh`, `make-iso.sh`, `check-iso.sh`, `build-installer.sh`, `test-official.sh`, `make_spf.py`, `pack_image.py`); Python unit tests under `installer/tests/`.
- `tests/` — contract tests and host-compiled C tests (see "Tests" above).
- `docs/` — `CPU_COMPATIBILITY.md`, `SECURE_BOOT.md`, `SETTINGS.md`, `STORAGE_COMPATIBILITY.md`, `V5_FILE_EXPLORER_FILESYSTEM.md`, `VULKAN_DXVK.md`, `VULKAN_DXVK_STATUS.md`, `graphics.md`.
- `assets/` — icons and fonts (e.g. `assets/icons-bmp/*.bmp`, `assets/fonts/Lato-Regular.ttf`).
- `resources/logo.bmp` — staged at `/magic/logo.bmp` in the OS image.
- `build/` — generated output (gitignored): `magic-disk.img/.vhdx`, `vars.fd`, `esp/`, `rootfs/`, `obj/`.

### Build toolchain and conventions

- Kernel/HAL/UEFI images: `clang --target=x86_64-unknown-windows -ffreestanding -mgeneral-regs-only -fno-stack-protector -mno-red-zone -O2`, linked with `lld -flavor link`. UEFI apps use `/subsystem:efi_application /entry:efi_main`; kernel uses `/subsystem:native /entry:kmain`; HAL is a DLL (`/dll /noentry`).
- Usermode PE images: `clang -target x86_64-windows-gnu -ffreestanding -fno-pic -mgeneral-regs-only`, linked with `lld -flavor link` with explicit `/base:` addresses and `/export:` lists. DLLs use `/dll /noentry`; EXEs specify an entry point (e.g. `smss_main`, `explorer_main`, `mainCRTStartup`).
- User C++ (`UserTestApps/*.cpp`) is built with `clang++` and extra flags (`-fno-exceptions -fno-rtti -fno-threadsafe-statics ...`).
- All images land in `build/rootfs/magic/System32/` (DLLs, EXEs, drivers) or `build/rootfs/magic/Program Files/` (UserTestApps). The build rejects PE images at `magic/` root.
- The disk image is GPT: FAT32 ESP (EFI\BOOT\BOOTX64.EFI, EFI\magic\bootuni.efi, loadufi.efi) + NTFS system partition populated from `build/rootfs` via libntfs-3g (no FUSE/mount).
- `__chkstk` is a naked `ret` in `kernel.c` because the kernel is freestanding.
- Kernel code and HAL are built with `-mgeneral-regs-only`, so no FPU/SSE state is saved across context switches yet (see `docs/CPU_COMPATIBILITY.md` — AVX/AVX2/XSAVE are not context-switched).

### Boot flow (runtime)

1. Firmware starts `EFI\BOOT\BOOTX64.EFI` from the FAT ESP.
2. `bootx64.efi` starts `EFI\magic\bootuni.efi`.
3. `bootuni.efi` registers the `magic OS` UEFI boot entry (and deletes the fallback unless `-KeepFallback`/`-DropFallback`).
4. `loadufi.efi` scans UEFI block devices for the ext4/NTFS system partition, reads `magic/System32/hal.dll` and `krnl.exe`, exits UEFI boot services, transfers control to `kmain`.
5. `kmain` → `kmain2`: `HalInitSystem`, CPU init, memory map dump, PMM init + selftest, VMM init, network/disk discovery, SMP AP startup, object manager + I/O manager self-tests, driver manager init, module dump + HAL + scheduler self-tests, then `user_enter(bi)` (does not return on success; on exit the kernel halts).

### Important project policies (read before changing kernel code)

- `CONTRIBUTING.md`: before any contribution is merged, the contributor must sign a written copyright assignment agreement approved by maintainer **temeboiw**. A PR/commit alone is not a signed agreement.
- `COPYRIGHT.md`: do not add a separate or replacement kernel and present it as the original SentinelOS kernel; do not rename/repackage/relabel existing kernel files or remove authorship/copyright/provenance notices. Kernel redesigns, replacements, and kernel-file renames require explicit maintainer approval. New work must be clearly distinguished from the existing kernel and accurately attributed.
- Third-party code/assets remain subject to their respective licenses; do not include secrets or incompatible-license code.

## Files not to edit without care

- `scripts/build.sh` — single source of truth for what gets built, where it lands, required tool versions, and disk-image sizing; changes here affect both Linux and Windows builds (the PS1 delegates to it).
- `kernel/kernel.c` — `kmain` entry point and the `TEST_FAULT` fault-injection knob; usermode entry `user_enter(bi)`.
- `shared/bootinfo.h` — `BootInfo` struct is the ABI between `loadufi.efi` and the kernel.
- `kernel/ke.h` — round-robin scheduler and thread primitives (quantum, clock vector 0xD1, yield via `int 0x2F`).
- `kernel/user.h` / `shared/ntabi.h` — syscall ABI contract numbers tested by `tests/syscall_abi_contract_test.sh`.
- `scripts/build.ps1` — Windows-only VHDX staging and DiskPart logic; run only from Administrator PowerShell.
