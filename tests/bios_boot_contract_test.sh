#!/usr/bin/env bash
# Source-contract checks for SentinelOS BIOS/MBR boot (host, no QEMU required).
# Mirrors tests/syscall_abi_contract_test.sh: grep-based assertions over the
# source files that implement the BIOS + MBR boot chain.
#
# Usage: bash tests/bios_boot_contract_test.sh [repo-root]   (default: .)
#
set -euo pipefail

ROOT="${1:-.}"

require() {
  local file="$1" pattern="$2" label="$3"
  grep -Eq -- "$pattern" "$file" || {
    echo "FAIL: $label ($file does not match $pattern)" >&2
    exit 1
  }
  echo "PASS: $label"
}

require_file() {
  local file="$1" label="$2"
  [[ -f "$file" ]] || {
    echo "FAIL: $label ($file missing)" >&2
    exit 1
  }
  echo "PASS: $label"
}

# --- stage-1 (MBR boot sector) ----------------------------------------------

STAGE1="$ROOT/boot/stage1.S"
require_file "$STAGE1" "stage-1 MBR boot sector exists (boot/stage1.S)"
# 0xAA55 signature bytes (0x55 then 0xAA) at the end of the boot sector.
require "$STAGE1" '0xAA55|0x55|0xAA' "stage-1 carries the 0xAA55 boot signature"
# INT 13h disk read (AH=02h) to load stage-2 from LBA 1.
require "$STAGE1" 'int 0x13|0x13' "stage-1 uses INT 13h disk read (AH=02h)"

# --- stage-2 (BIOS loader) --------------------------------------------------

BIOSLOAD="$ROOT/boot/biosload.c"
require_file "$BIOSLOAD" "stage-2 BIOS loader exists (boot/biosload.c)"
# VESA detection (INT 10h AX=4F00h controller info / 4F00h).
require "$BIOSLOAD" '0x4F00|4F00h' "stage-2 performs VESA detection (0x4F00)"
# E820 memory map (INT 15h AX=E820h).
require "$BIOSLOAD" '0xE820|e820' "stage-2 collects the E820 memory map"
# Page-table / CR3 switch on the real-mode to long-mode transition.
require "$BIOSLOAD" 'mov cr3|cr3' "stage-2 switches page tables via CR3"

# --- BootInfo ABI -----------------------------------------------------------

BOOTINFO="$ROOT/shared/bootinfo.h"
require_file "$BOOTINFO" "shared/bootinfo.h exists"
require "$BOOTINFO" 'boot_mode' "bootinfo.h declares the boot_mode field"
require "$BOOTINFO" 'BOOT_MODE_BIOS' "bootinfo.h defines BOOT_MODE_BIOS"
require "$BOOTINFO" 'e820_map' "bootinfo.h declares e820_map"
require "$BOOTINFO" 'e820_count' "bootinfo.h declares e820_count"
require "$BOOTINFO" 'e820_entry_size' "bootinfo.h declares e820_entry_size"

# --- kernel boot-mode branch ------------------------------------------------

KERNEL="$ROOT/kernel/kernel.c"
require_file "$KERNEL" "kernel/kernel.c exists"
require "$KERNEL" 'bi->boot_mode|boot_mode' "kernel.kernel.c branches on bi->boot_mode"

# --- build.sh MBR support ---------------------------------------------------

BUILD="$ROOT/scripts/build.sh"
require_file "$BUILD" "scripts/build.sh exists"
require "$BUILD" '--mbr|partition' "scripts/build.sh handles --mbr / partition selection"

echo "SentinelOS BIOS/MBR boot contract checks passed."
