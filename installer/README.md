# SentinelOS V.4 [Beta] installer

All installer-specific code and generated output stays under `installer/`; the original root-level source files are not edited.

## Build the ISO

Use a **native x86-64 Ubuntu/Debian Linux environment** (not Android/Termux Clang). Install the build tools:

```sh
apt update
apt install clang lld llvm python3 xorriso dosfstools mtools qemu-utils fdisk e2fsprogs
```

Set `EDK2` to an edk2 checkout with `MdePkg/Include`, then from the repository root run:

```sh
export EDK2=/path/to/edk2
bash installer/build-iso.sh
```

This runs the isolated source build, links installer-specific `instlr.exe`, `screen.exe`, and installer `smss.exe`, creates and verifies `artifacts/esp.spf` and `artifacts/system.spf`, masters a UEFI El Torito/hybrid ISO, then smoke-checks the final ISO and package payloads. The intended output is `installer/artifacts/SentinelOS-V4-BETA.iso` with a SHA-256 sidecar.

To run individual stages:

```sh
bash installer/build-installer.sh
bash installer/make-iso.sh
bash installer/check-iso.sh
```

ISO mastering requires `xorriso`, `dosfstools` (`mkfs.vfat`), and `mtools` (`mcopy`, `mmd`). The default EFI image is 128 MiB; set `ESP_SIZE_MIB=256` before running `make-iso.sh` to enlarge it (allowed range 32–1024 MiB). Set `JOBS=2` to limit build parallelism on low-memory hosts.

## Official pre-release test procedure

Run the repeatable validation script from the repository root:

```sh
bash installer/test-official.sh unit
bash installer/test-official.sh iso
```

For an automated UEFI firmware launch smoke test, install `qemu-system-x86`, `ovmf`, and `coreutils` (for `timeout`), then run:

```sh
bash installer/test-official.sh vm
```

Or run all stages in order:

```sh
bash installer/test-official.sh all
```

The VM stage runs QEMU/OVMF for 45 seconds and saves `installer/artifacts/qemu-uefi-smoke.log`. It is a launch smoke test, **not** proof that the UI completed boot; use a graphical VM session to confirm the setup screen. Run this on a native Linux x86-64 host. Android/Termux's Clang targets Android ARM64 and must not be used as the host compiler for this build.

## Included media

- Builds from the isolated `installer/source/` snapshot and leaves root-level project sources untouched.
- Includes an installer session manager, framebuffer terms/target UI, and progress-style screen binary.
- Packages staged EFI and system filesystem trees into separate SPF archives. SPF is a custom zlib-compressed tar format, not WIM.
- Creates a FAT32 EFI boot image, copies the full staged EFI directory tree plus any other ESP-root files into it, and adds both SPF packages to the ISO.
- Emits `SHA256SUMS` for the embedded boot image, packages, and media README.
- Validates the ISO checksum, El Torito UEFI entry, fallback `EFI/BOOT/BOOTX64.EFI`, and both embedded SPF packages.

## What is not yet a complete disk installer

**This is not yet a full disk installer.** The setup UI cannot enumerate physical disks, partition or format them, or install onto a selected drive. The user syscall interface in the isolated source snapshot exposes file and directory access but no disk enumeration, raw block read/write, partition-table, or filesystem-format service for user-mode setup. Pretending this is implemented would risk data loss. The current UI therefore disables disk installation rather than issuing unverified writes.

The host-side `installer/install_from_spf.py` tool can extract verified packages into two existing, non-overlapping mounted directories. It defaults to dry-run and requires `--apply --confirm "INSTALL SENTINELOS"` to write files; it does not create partitions, format disks, configure boot entries, or install onto raw devices. Extraction is not transactional.

The ISO and VM checks have not been run by this repository-editing session. A QEMU launch smoke test does not replace a visual UEFI boot test or an end-to-end installation test onto a disposable virtual disk.

## Regression tests

```sh
python3 -m unittest discover -s installer/tests -v
```
