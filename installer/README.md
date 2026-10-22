# SentinelOS V.4 [Beta] installer

All installer-specific code and generated output stays under `installer/`; the original root-level source files are not edited.

## Build

```sh
bash installer/build-installer.sh
```

The script builds from the isolated `installer/source/` snapshot, excludes game/test app compilation from the copied build script, then links installer-specific `instlr.exe`, `screen.exe`, and installer `smss.exe`. It stages real outputs and creates/verifies `artifacts/esp.spf` and `artifacts/system.spf`.

## Current implementation boundary

- `instlr.exe` is a framebuffer-based terms/target-selection UI and starts directly from installer SMSS rather than Winlogon/Explorer/taskbar.
- `make_spf.py build` packages ESP/system partition directory trees into `esp.spf` and `system.spf`. `make_spf.py extract` verifies an SPF and extracts files into an already-mounted partition directory; it does not mount, format, or write raw disk devices. The target page intentionally reports disk enumeration as unavailable. Next/format/install remain disabled because the kernel currently exposes no verified disk enumeration/partition-format service to this installer. It does not pretend a disk is detected and does not format anything.
- `screen.exe` is the full-screen progress-style spinner for the later installation phase, but installation orchestration does not yet launch it.
- SPF is a custom zlib/tar format, not WIM-compatible. Example: `python3 installer/make_spf.py extract installer/artifacts/system.spf --target-root /mnt/sentinel-system`. Add `--overwrite` only when deliberately replacing files.
- Boot path selection, real disk enumeration, partition creation, FAT32/ext4 formatting, safe package extraction, and ISO mastering still need implementation and VM testing.
