# BIOS/MBR Boot Chain (SentinelOS V10 ETERNITY)

This document describes the BIOS + MBR boot path, complementing the UEFI/GPT
path described in CLAUDE.md. It is the reference for the `run-bios.sh` QEMU
launcher and the `bios_boot_contract_test.sh` source-contract test.

## Boot chain overview

```
SeaBIOS (INT 13h)
   |
   v
MBR (LBA 0) -> stage-1 boot sector (boot/stage1.S)
   |  loads stage-2 from LBA 1..62 via INT 13h AH=02h
   v
stage-2 loader (boot/biosload.c) at physical 0x100000
   |  real mode -> protected mode -> long mode
   |  reads MBR, parses active NTFS partition (LBA 64)
   |  loads krnl.exe + hal.dll from magic/System32 (NTFS)
   |  PE32+ parse + relocation
   |  collects E820 memory map (INT 15h AX=E820h)
   |  detects VESA framebuffer (INT 10h AX=4F01h)
   |  builds BootInfo at physical 0x6000, boot_mode = 1 (BIOS)
   |  far jump to code64 -> kmain(RDI = BootInfo*)
   v
kernel kmain (kernel/kernel.c): branches on bi->boot_mode,
   initializes CPU/memory/heap, paints the framebuffer, starts usermode.
```

## LBA assignments (MBR disk image, `build.sh --mbr`)

| LBA(s) | Contents |
|--------|----------|
| 0 | MBR: 446-byte bootstrap + 4 partition entries x 16 bytes + `0x55 0xAA` signature (offset 510-511). Entry 1 active/bootable, type 0x07 (NTFS), starting at LBA 64. |
| 1-63 | Reserved for the BIOS stage-2 loader (loaded by stage-1, 62 sectors). |
| 64 | NTFS system volume (`magic/`, `magic/System32/`), 1 MiB aligned. |

Stage-1 (`boot/stage1.S`) loads 62 sectors from LBA 1 into RAM at `0x100000`
(segment:offset `0x1000:0x0000`) using `INT 13h AH=02h` with LBA-to-CHS
conversion, then jumps to `0x100000` with `DL` = boot drive.

Stage-2 (`boot/biosload.c`) runs in real mode, detects `INT 13h` extensions,
parses the MBR to find the active NTFS partition, loads `hal.dll` and
`krnl.exe`, parses/relocates the PE32+ images, collects the E820 memory map,
detects the VESA framebuffer, builds a `BootInfo` at physical address `0x6000`
with `boot_mode = 1` (BIOS), then transitions to long mode (GDT, CR4 PAE/PGE/
PSE, EFER LME, CR3 page tables, CR0 paging+protected mode, far jump to code64)
and jumps to `kmain`.

## BootInfo fields (BIOS)

Defined in `shared/bootinfo.h` and populated by `boot/biosload.c`:

| Field | Meaning (BIOS) |
|-------|----------------|
| `boot_mode` | Set to `1` (BIOS); matched by `BOOT_MODE_BIOS` define |
| `BOOT_MODE_BIOS` | Define for the BIOS boot-mode value |
| `fb_base`, `width`, `height`, `pitch` | VESA framebuffer (from stage-2) |
| `mmap`, `mmap_size`, `desc_size` | E820 memory map (from stage-2) |
| `e820_map`, `e820_count`, `e820_entry_size` | E820 map array, count, entry size |
| `module_count`, `modules[]` | `hal.dll` and `krnl.exe` module descriptors |
| `ap_trampoline_phys` | AP trampoline physical address (0 for BIOS) |

`kernel/kernel.c` branches on `bi->boot_mode` (or an equivalent boot-mode
discriminator) at `kmain` entry.

## QEMU test command

Build the MBR disk image:

```bash
./scripts/build.sh --mbr
```

Launch with SeaBIOS:

```bash
./scripts/run-bios.sh
```

This is equivalent to:

```bash
qemu-system-x86_64 -machine q35 -accel tcg -cpu max \
  -smp 1 -m 2048M \
  -bios /usr/share/seabios/bios-256k.bin \
  -drive file=build/magic-disk.img,format=raw,if=none,id=d0 \
  -device virtio-blk-pci,drive=d0 \
  -netdev user,id=net0 -device e1000,netdev=net0 \
  -display sdl -serial stdio -no-reboot
```

Under proot/Termux, `-display sdl` is replaced by `-display none -vnc 127.0.0.1:0`
(VNC on `127.0.0.1:5900`), mirroring `scripts/run.sh`.

RAM/CPUS can be overridden with `SENTINELOS_RAM_MB` / `SENTINELOS_CPUS`; the
SeaBIOS image can be overridden with `SENTINELOS_SEABIOS`.

## Source-contract test

Run from the repo root (no QEMU needed):

```bash
bash tests/bios_boot_contract_test.sh .
```

Checks performed:
- `boot/stage1.S` exists and carries the `0xAA55` boot signature (`0x55 0xAA`).
- `boot/stage1.S` uses `INT 13h` (`AH=02h`) to read sectors.
- `boot/biosload.c` exists and performs VESA detection (`0x4F00`).
- `boot/biosload.c` collects the E820 memory map (`0xE820`).
- `boot/biosload.c` switches page tables via CR3 (`mov cr3` / `cr3`).
- `shared/bootinfo.h` declares `boot_mode` and defines `BOOT_MODE_BIOS`.
- `shared/bootinfo.h` declares `e820_map`, `e820_count`, `e820_entry_size`.
- `kernel/kernel.c` branches on `bi->boot_mode`.
- `scripts/build.sh` handles `--mbr` / partition selection.

## SeaBIOS locations (this host)

`run-bios.sh` searches, in order:
- `$SENTINELOS_SEABIOS` (if set)
- `/usr/share/seabios/bios-256k.bin`
- `/usr/share/seabios/bios.bin`
- `/usr/share/seabios/seabios.bin`
- `/usr/share/qemu/bios-256k.bin`
- `/usr/share/qemu/bios.bin`
- `/usr/share/qemu/seabios.bin`

On this host none of these are present, so `run-bios.sh` exits with an error
message instructing the operator to install SeaBIOS (e.g. `apt install seabios`)
or set `SENTINELOS_SEABIOS`.

## Relationship to UEFI/GPT path

This BIOS/MBR path is tested alongside the existing OVMF + GPT path
(`scripts/run.sh`) in Phase 1's integration gate: QEMU must boot SentinelOS via
SeaBIOS + MBR **and** via OVMF + GPT, both reaching `kmain` and painting the
framebuffer. The MBR disk image (`build/magic-disk.img`) is a separate build
artifact produced by `build.sh --mbr`; it is not the UEFI/GPT image.
