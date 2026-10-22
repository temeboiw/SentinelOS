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

## Deploy to a dedicated disk (host-side fallback)

The in-OS setup application currently partitions and formats the disk but does
not yet write the package file tree. For an actual deployment test in the
meantime, `install-disk.sh` performs the full host-side disk pipeline on a
dedicated Linux block device: validates both SPF packages, creates GPT with a
128 MiB FAT32 EFI partition and an ext4 system partition, extracts the verified
packages, then checks the UEFI fallback loader and required system files.

**This destroys all data on the target disk. Use a disposable virtual disk only.**
Build packages first, identify the virtual disk carefully, then run:

```sh
sudo bash installer/install-disk.sh --target /dev/vdX --confirm "ERASE /dev/vdX"
```

Replace `/dev/vdX` with the exact whole-disk device shown by `lsblk`. The
script refuses mounted devices and requires the confirmation string to match
the target. It requires `sgdisk`, `partprobe`, `udevadm`, `mkfs.fat`,
`mkfs.ext4`, and standard mount utilities. It does not run automatically from
the in-OS setup UI and is not a substitute for testing that UI's deployment
engine. Successful file checks are not proof of a successful UEFI boot.

## Included media

- Builds from the isolated `installer/source/` snapshot and leaves root-level project sources untouched.
- Includes an installer session manager, framebuffer terms/target UI, and progress-style screen binary.
- Packages staged EFI and system filesystem trees into separate SPF archives. SPF is a custom zlib-compressed tar format, not WIM.
- Creates a FAT32 EFI boot image and copies the staged EFI tree plus `magic/System32` boot modules onto the ESP. This lets `loadufi.efi` load the HAL, kernel, and setup session from the ISO itself; installed-system boots still fall back to the ext4 reader when those files are not on the ESP.
- Adds both SPF packages to the ISO.
- Emits `SHA256SUMS` for the embedded boot image, packages, and media README.
- Validates the ISO checksum, El Torito UEFI entry, fallback `EFI/BOOT/BOOTX64.EFI`, both embedded SPF packages, and a SHA-256 manifest that the trusted kernel checks before allowing disk writes.

## Installation status (work in progress)

The setup UI detects the first supported VirtIO block disk, writes GPT primary/backup metadata, formats the 128 MiB EFI partition as FAT32, and formats the system partition with a minimal ext4 layout (superblock, group descriptors, bitmaps, inode tables, and root directory). Both formatters perform basic read-back checks. The kernel exposes bounded one-sector read/write operations only to the trusted setup process; requests are range-checked against the reported device capacity. The UEFI loader now also stages `esp.spf`, `system.spf`, and `packages.sha256` from the ESP as raw boot modules. The trusted kernel exposes bounded 64 KiB package reads and verifies each complete package against the embedded SHA-256 manifest before permitting destructive disk operations. `make-iso.sh` embeds and byte-compares both package files inside the ESP as well as keeping the host-side ISO copies. The ISO builder estimates the required FAT ESP capacity from the staged tree and package sizes instead of assuming 128 MiB will always be enough.

**This is still not a complete OS installation.** The system filesystem currently contains only an empty root directory; SentinelOS boot/system files are not copied to the target. Installed boot configuration and an end-to-end installation/reboot test remain unfinished. The installer UI stops after filesystem formatting. Do not select a disk containing data you need: partitioning overwrites its partition table and formatting destroys data in both target partitions. Use only the disposable `sentinel-test-disk.img` during development. Do not run the VM test again until payload deployment and the remaining install stages are implemented and reviewed.

The host-side `installer/install_from_spf.py` tool can extract verified packages into two existing, non-overlapping mounted directories. It defaults to dry-run and requires `--apply --confirm "INSTALL SENTINELOS"` to write files; it does not create partitions, format disks, configure boot entries, or install onto raw devices. The standalone SPF extractor now verifies the full archive and every per-file digest before creating destination directories or writing files. Extraction is not transactional.

The ISO and VM checks have not been run by this repository-editing session. A QEMU launch smoke test does not replace a visual UEFI boot test or an end-to-end installation test onto a disposable virtual disk.

## Regression tests

The Python suite covers SPF package staging, byte-for-byte extraction, manifest/file metadata, payload-digest mismatch rejection, invalid headers, and path-traversal rejection. `test-official.sh unit` runs this suite alongside the SHA-256 known-answer test and shell syntax checks.

```sh
python3 -m unittest discover -s installer/tests -v
```
