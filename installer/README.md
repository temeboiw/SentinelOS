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

Create a disposable sparse 1 GiB raw disk for the installer VM (this creates a local file; it does not touch physical disks):

```sh
bash installer/create-test-disk.sh
```

This runs the isolated source build, links installer-specific `instlr.exe`, `screen.exe`, and installer `smss.exe`, creates and verifies `artifacts/esp.spf` and `artifacts/system.spf`, masters a UEFI El Torito/hybrid ISO, then smoke-checks the final ISO and package payloads. The intended output is `installer/artifacts/SentinelOS-V4-BETA.iso` with a SHA-256 sidecar.

To run individual stages:

```sh
bash installer/build-installer.sh
bash installer/make-iso.sh
bash installer/check-iso.sh
```

ISO mastering requires `xorriso`, `dosfstools` (`mkfs.vfat`), and `mtools` (`mcopy`, `mmd`). The EFI image auto-sizes to the staged ESP files plus both embedded SPF packages, with additional free-space headroom (supported range 128–1024 MiB by default). Set `ESP_SIZE_MIB=256` before running `make-iso.sh` to override the automatic size; values must be 32–1024 MiB. Set `JOBS=2` to limit build parallelism on low-memory hosts.

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

For the interactive VNC test, `vm-vnc` automatically creates and attaches `installer/artifacts/sentinel-test-disk.img` as a VirtIO block device if the image does not already exist:

```sh
bash installer/test-official.sh vm-vnc
```

Connect to `127.0.0.1:5900`. The VNC server has no authentication; keep it bound to localhost.

Or run all stages in order:

```sh
bash installer/test-official.sh all
```

The VM stage runs QEMU/OVMF for 45 seconds and saves `installer/artifacts/qemu-uefi-smoke.log`. It is a launch smoke test, **not** proof that the UI completed boot; use a graphical VM session to confirm the setup screen. Run this on a native Linux x86-64 host. Android/Termux's Clang targets Android ARM64 and must not be used as the host compiler for this build.

## Included media

- Builds from the isolated `installer/source/` snapshot and leaves root-level project sources untouched.
- Includes an installer session manager, framebuffer terms/target UI, and progress-style screen binary.
- Packages staged EFI and system filesystem trees into separate SPF archives. SPF is a custom zlib-compressed tar format, not WIM.
- Creates a FAT32 EFI boot image and copies the staged EFI tree plus `magic/System32` boot modules onto the ESP. This lets `loadufi.efi` load the HAL, kernel, and setup session from the ISO itself; installed-system boots still fall back to the ext4 reader when those files are not on the ESP.
- Adds both SPF packages to the ISO.
- Emits `SHA256SUMS` for the embedded boot image, packages, and media README.
- Validates the ISO checksum, El Torito UEFI entry, fallback `EFI/BOOT/BOOTX64.EFI`, the embedded filesystem images, their SHA-256 manifest, and the SPF build-validation packages.

## Installation status

The graphical installer is designed to install without a shell. The ISO builder creates a 128 MiB FAT32 EFI image and a pre-populated ext4 system image (512 MiB by default), hashes both images, and embeds them on the boot ESP. UEFI stages esp.img, system.img, and install.sha256 as immutable boot modules. Before any disk writes, the trusted kernel checks both SHA-256 digests.

When the user clicks **Install OS**, the in-OS setup app writes GPT metadata and asks the trusted kernel to copy the verified images to the fixed partition offsets. The kernel bounds-checks image sizes against the VirtIO disk capacity and reads back the first and last sectors after copying. The target disk must be at least about 640 MiB. The installed ext4 filesystem initially retains the image size even if its partition is larger.

The host-side SPF extractor and .spf packages remain build/validation utilities; they are not the installation path. **The code path has been connected, but this session has not run the installer build, ISO checks, or a real UEFI install-and-reboot test. Do not treat the installer as release-ready until those checks pass.** Installing erases the selected disk. Use only a disposable virtual disk during testing.

## Regression tests

The Python suite covers SPF package staging, byte-for-byte extraction, manifest/file metadata, payload-digest mismatch rejection, invalid headers, and path-traversal rejection. `test-official.sh unit` runs this suite alongside the SHA-256 known-answer test and shell syntax checks.

```sh
python3 -m unittest discover -s installer/tests -v
```
