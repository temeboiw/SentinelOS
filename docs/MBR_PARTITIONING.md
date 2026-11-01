# MBR Partitioning Support in SentinelOS

SentinelOS historically built a GPT-only disk image (FAT32 ESP + NTFS system
partition). V10 adds BIOS/MBR boot support. This document describes the MBR
layout, the LBA assignments, and how the `--mbr` / `--partition` build flag
works.

## Build flags

| Flag | Meaning |
|------|---------|
| `--partition gpt` (default) | GPT-only disk image, exactly as before. No change to existing behavior. |
| `--partition mbr` | MBR-only disk image: MBR at LBA 0, one NTFS system volume at LBA 64. |
| `--partition hybrid` | GPT disk (ESP + NTFS system volume) **plus** a real MBR overlaid at LBA 0, so the same disk boots under both UEFI (GPT) and BIOS (MBR). |
| `--mbr` | Shorthand for `--partition mbr`. |
| `--partition=<value>` | Equivalent to `--partition <value>`. |

The default is `gpt`, so existing builds are unaffected unless the flag is
passed. `--partition` is validated to be one of `gpt`, `mbr`, or `hybrid`;
any other value (or a missing argument) fails the build before any disk image
is created.

## MBR layout

The MBR lives at LBA 0 (sector 0) and is exactly 512 bytes:

| Offset | Size | Contents |
|--------|------|----------|
| 0x000–0x1BD | 446 bytes | Bootstrap code (placeholder: zeros; the BIOS stage-1 / stage-2 loader is a separate boot/ concern) |
| 0x1BE–0x1CD | 16 bytes | Partition entry 1: active/bootable system volume |
| 0x1CE–0x1DD | 16 bytes | Partition entry 2: unused (MBR) / protective 0xEE (hybrid) |
| 0x1DE–0x1ED | 16 bytes | Partition entry 3: unused |
| 0x1EE–0x1FD | 16 bytes | Partition entry 4: unused |
| 0x1FE–0x1FF | 2 bytes | Signature `0x55 0xAA` |

Each partition entry is the standard 16-byte MBR entry:

| Offset within entry | Size | Field |
|---------------------|------|-------|
| +0 | 1 | Status (`0x80` = active/bootable, `0x00` = not bootable) |
| +1–3 | 3 | CHS of first sector (not used; LBA fields are authoritative) |
| +4 | 1 | Partition type (`0x07` = NTFS, `0x0C` = FAT32 LBA, `0xEE` = protective) |
| +5–7 | 3 | CHS of last sector (not used) |
| +8–11 | 4 | First LBA, little-endian |
| +12–15 | 4 | Sector count, little-endian |

## LBA assignments

| LBA | Purpose |
|-----|---------|
| 0 | MBR (bootstrap + partition table + 0xAA55 signature) |
| 1–63 | Reserved for the BIOS stage-2 loader (62 sectors, ~31 KiB) |
| 64 | System volume (NTFS), 1 MiB aligned |

In `mbr` mode the disk contains only the MBR at LBA 0 and the NTFS system
volume starting at LBA 64. There is no ESP and no GPT.

In `hybrid` mode the disk keeps the GPT layout (ESP at LBA 2048, NTFS system
volume after it) and additionally overlays an MBR at LBA 0 whose entry 1 is an
active `0x07` entry describing LBA 64–2047 (the BIOS-bootable region ahead of
the GPT ESP) and whose entry 2 is a protective `0xEE` entry starting at LBA 1,
so GPT-aware firmware still sees a valid protective MBR. GPT structures at
LBA 1+ are untouched by the MBR overlay.

## Boot drive number

Per the INT 13h convention, the boot drive number is passed in the `DL`
register by the firmware. The MBR bootstrap code (stage-1) loads the stage-2
loader from LBA 1–63, which in turn loads `krnl.exe` from the filesystem.

## Build-time validation

When `mbr` or `hybrid` is selected, `scripts/build.sh` validates the MBR it
writes at LBA 0 by reading it back and checking:

1. **Signature** — bytes at offset 510–511 are `0x55 0xAA`.
2. **Active flag** — partition entry 1 status byte is `0x80` (bootable).
3. **Partition type** — entry 1 type is `0x07` (NTFS) (or `0x0C` for FAT32).
4. **First LBA / sector count** — entry 1 starts at the expected volume LBA
   (64) and its sector count matches the volume size.
5. **Bounds** — entry 1 does not extend past the end of the disk.
6. **Hybrid only** — entry 2 is type `0xEE` (protective) and starts at LBA 1.

Any failure aborts the build with a `FATAL` message.

## Files changed

- `scripts/build.sh` — added `--partition`/`--mbr` flag, MBR layout constants
  (LBA 0 MBR, LBA 1–63 stage-2 reserve, LBA 64 system volume), `build_mbr` /
  `write_mbr_byte` / `write_le32_at` helpers to emit a 512-byte MBR, and
  `validate_mbr` to verify it. The `gpt` code path is byte-for-byte unchanged
  in behavior.
- `docs/MBR_PARTITIONING.md` — this document (new).

## Open questions

1. **What populates LBA 1–63 in `mbr`/`hybrid`?** The MBR bootstrap/stage-1
   and stage-2 loader code live in `boot/` (read-only for this task) and are
   not written by `build.sh`. The reserved region is left zeroed; the actual
   stage-1/bootstrap image must be supplied by the boot-stage workstream and
   copied into LBA 1–63 (or the MBR bootstrap area) before the disk is bootable
   on BIOS.

2. **`hybrid` LBA 64 volume:** the hybrid MBR entry 1 describes LBA 64–2047
   as a BIOS-bootable region, but no filesystem is placed there by
   `build.sh` (the real OS volumes are the GPT ESP + NTFS). Decide whether
   LBA 64 should hold a minimal bootable FAT/NTFS stage-2 volume or whether
   the MBR entry should instead point directly at the GPT NTFS partition.

3. **Filesystem type for `mbr`:** the implementation defaults to NTFS
   (`0x07`) matching the existing GPT system volume. `0x0C` (FAT32 LBA) is
   supported by the entry format but not yet wired to a separate build path.

4. **Backup GPT header in `hybrid`:** the existing GPT path reserves room for
   the backup GPT header at the end of the disk; this is preserved in hybrid.
