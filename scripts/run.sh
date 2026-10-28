#!/usr/bin/env bash
# Run the Linux/proot raw SentinelOS disk image with QEMU.
# This is intentionally separate from run.ps1, which boots the Windows VHDX.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
DISK="$BUILD/magic-disk.img"

if [[ ! -f "$DISK" ]]; then
  echo "error: SentinelOS disk image is missing: $DISK" >&2
  echo "Run ./scripts/build.sh first." >&2
  exit 1
fi

QEMU="${QEMU:-$(command -v qemu-system-x86_64 || true)}"
if [[ -z "$QEMU" ]]; then
  echo "error: qemu-system-x86_64 was not found. Install QEMU for x86_64 guests." >&2
  exit 1
fi

# Prefer an explicitly staged firmware image, then common Linux OVMF locations.
FIRMWARE="${SENTINELOS_FIRMWARE:-$BUILD/ovmf.fd}"
if [[ ! -f "$FIRMWARE" ]]; then
  FIRMWARE=""
  for candidate in \
    /usr/share/OVMF/OVMF_CODE.fd \
    /usr/share/ovmf/OVMF.fd \
    /usr/share/edk2/x64/OVMF_CODE.fd \
    /usr/share/qemu/OVMF.fd \
    /usr/share/qemu/edk2-x86_64-code.fd; do
    if [[ -f "$candidate" ]]; then FIRMWARE="$candidate"; break; fi
  done
fi
if [[ -z "$FIRMWARE" ]]; then
  echo "error: OVMF UEFI firmware was not found." >&2
  echo "Install the distro's OVMF package (for example: apt install ovmf), or set SENTINELOS_FIRMWARE to a firmware .fd file." >&2
  exit 1
fi

RAM="${SENTINELOS_RAM_MB:-2048}"
CPUS="${SENTINELOS_CPUS:-1}"
[[ "$RAM" =~ ^[1-9][0-9]*$ ]] || { echo "error: SENTINELOS_RAM_MB must be a positive integer (MiB)." >&2; exit 1; }
[[ "$CPUS" =~ ^[1-9][0-9]*$ ]] || { echo "error: SENTINELOS_CPUS must be a positive integer." >&2; exit 1; }

echo "SentinelOS Linux runner"
echo "  Disk: $DISK (raw GPT image)"
echo "  Firmware: $FIRMWARE"
echo "  RAM: $RAM MiB; CPUs: $CPUS"
echo "  Note: on ARM64 proot, QEMU will emulate x86_64 with TCG."

exec "$QEMU" \
  -machine q35 \
  -accel tcg \
  -cpu max \
  -smp "$CPUS" \
  -m "${RAM}M" \
  -bios "$FIRMWARE" \
  -drive "file=$DISK,format=raw,if=none,id=d0" \
  -device virtio-blk-pci,drive=d0 \
  -netdev user,id=net0 \
  -device e1000,netdev=net0 \
  -vga std \
  -display sdl \
  -serial stdio \
  -no-reboot
