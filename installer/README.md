# SentinelOS V.4 [Beta] installer

The installer is part of the SentinelOS repository and uses the **same build artifacts** as the OS. Installer-specific setup applications and ISO packaging scripts remain in `installer/`, but there is no separate OS source snapshot build.

## Build the OS and installer ISO together

Use a native x86-64 Linux environment with the required build tools installed. For example:

```sh
apt update
apt install clang lld llvm python3 xorriso dosfstools mtools qemu-utils fdisk e2fsprogs
```

From the repository root, run the single build command:

```sh
bash scripts/build.sh
```

The build compiles SentinelOS, stages the installer payload from that build, creates the UEFI El Torito/hybrid ISO, and validates the final media. It does not launch QEMU. The ISO and SHA-256 sidecar are written to `installer/artifacts/SentinelOS-V4-BETA.iso` and `installer/artifacts/SentinelOS-V4-BETA.iso.sha256`.

For packaging-only iteration after a successful OS build, `bash installer/build-iso.sh` remains available as a developer subcommand; it does not build a second OS source snapshot. QEMU-based boot smoke tests are optional and separate from building the ISO.

To run individual stages:

```sh
bash installer/build-installer.sh
bash installer/make-iso.sh
bash installer/check-iso.sh
```

ISO mastering requires `xorriso`, `dosfstools` (`mkfs.vfat`), and `mtools` (`mcopy`, `mmd`). The EFI image auto-sizes to the staged ESP files and zero-block packed installer images, with additional free-space headroom (minimum 128 MiB). Set `ESP_SIZE_MIB=256` before running `make-iso.sh` to override the automatic size; values must be 32–1024 MiB. Set `JOBS=2` to limit build parallelism on low-memory hosts.

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
- Creates a FAT32 EFI boot image and copies the staged EFI tree plus `magic/System32` boot modules onto the ESP. The installer partition images use a zero-block encoding on the ESP; `loadufi.efi` expands them into their normal raw bytes while booting, so the virtual DVD does not have to read hundreds of MiB of empty filesystem space.
- Adds both SPF packages to the ISO.
- Emits `SHA256SUMS` for the embedded boot image, packages, and media README.
- Validates the ISO checksum, El Torito UEFI entry, fallback `EFI/BOOT/BOOTX64.EFI`, the embedded filesystem images, their SHA-256 manifest, and the SPF build-validation packages.

## Installation status

The graphical installer is designed to install without a shell. The ISO builder creates a 128 MiB FAT32 EFI image and a pre-populated ext4 system image (512 MiB by default), hashes both raw images, and embeds zero-block packed versions on the boot ESP. UEFI expands them into immutable `esp.img` and `system.img` boot modules and stages `install.sha256`. Before any disk writes, the trusted kernel checks both SHA-256 digests.

When the user clicks **Install OS**, the in-OS setup app writes GPT metadata and asks the trusted kernel to copy the verified images to the fixed partition offsets. The kernel bounds-checks image sizes against the VirtIO disk capacity and reads back the first and last sectors after copying. The target disk must be at least the combined partition-image size plus GPT metadata (about 640 MiB at defaults). The installed ext4 filesystem initially retains the image size even if its partition is larger.

The only supported installation path is the native in-OS graphical installer. Host-side install/extraction entry points are intentionally not provided. **The code path has been connected, but this session has not run the installer build, ISO checks, or a real UEFI install-and-reboot test. Do not treat the installer as release-ready until those checks pass.** Installing erases the selected disk. Use only a disposable virtual disk during testing.

## Regression tests

The Python suite covers SPF package staging, byte-for-byte extraction, manifest/file metadata, payload-digest mismatch rejection, invalid headers, and path-traversal rejection. `test-official.sh unit` runs this suite alongside the SHA-256 known-answer test and shell syntax checks.

```sh
python3 -m unittest discover -s installer/tests -v
```
