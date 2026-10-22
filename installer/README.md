# SentinelOS V.4 [Beta] installer

All installer-specific code and generated output stays under `installer/`; the original root-level source files are not edited.

## One-command ISO build

Install the host tools (Debian/Ubuntu):

```sh
apt install clang lld llvm python3 xorriso dosfstools mtools
```

Then from the repository root run:

```sh
bash installer/build-iso.sh
```

This runs the isolated source build, links installer-specific `instlr.exe`, `screen.exe`, and installer `smss.exe`, creates and verifies `artifacts/esp.spf` and `artifacts/system.spf`, masters a UEFI El Torito/hybrid ISO, then smoke-checks the final ISO and package payloads. The final image is `installer/artifacts/SentinelOS-V4-BETA.iso`; its portable checksum is `installer/artifacts/SentinelOS-V4-BETA.iso.sha256`.

To run individual stages:

```sh
bash installer/build-installer.sh
bash installer/make-iso.sh
bash installer/check-iso.sh
```

ISO mastering requires `xorriso`, `dosfstools` (`mkfs.vfat`), and `mtools` (`mcopy`, `mmd`). The default EFI image is 128 MiB; set `ESP_SIZE_MIB=256` before running `make-iso.sh` to enlarge it (allowed range 32–1024 MiB). Set `JOBS=2` to limit build parallelism on low-memory hosts.

## What the installer currently does

- Builds from the isolated `installer/source/` snapshot and leaves root-level project sources untouched.
- Includes an installer session manager, a framebuffer-based terms/target-selection UI, and a progress-style screen binary.
- Packages the staged EFI and system filesystem trees into separate SPF archives. SPF is a custom zlib-compressed tar format, not WIM.
- Creates a FAT32 EFI boot image and copies the full staged EFI directory tree plus any other ESP-root files into it.
- Places both SPF packages in the ISO and emits `SHA256SUMS` for the embedded boot image, packages, and media README.
- Validates the ISO checksum, El Torito UEFI entry, fallback `EFI/BOOT/BOOTX64.EFI`, and both embedded SPF packages.

## Important implementation boundary

**This is a bootable-media pipeline, not yet a complete disk installer.** The installer UI reports disk enumeration as unavailable. The current kernel does not expose a verified disk enumeration/partition-format service to the installer, so no real disk is listed, partitioned, formatted, or modified. The progress screen is not yet wired into a real installation flow. ISO mastering and its host-side validation are implemented, but the ISO must still be built on the user's machine and boot-tested in a UEFI VM before it can be described as working.

`installer/make_spf.py extract` writes only into an existing mounted directory; it does not mount, format, or write raw disk devices. The host-side `installer/install_from_spf.py` coordinator defaults to dry-run and requires `--apply --confirm "INSTALL SENTINELOS"` to extract onto two existing, non-overlapping target directories. Extraction is not transactional and the utility is not wired into the booted installer UI.

## Regression tests

Run the standalone Python tests:

```sh
python3 -m unittest discover -s installer/tests -v
```

These exercise host-side SPF packaging and coordinator behavior; they do not test the kernel's disk drivers or replace a UEFI VM boot test.
