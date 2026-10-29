#!/usr/bin/env bash
# Boot the installer ISO against the disposable test.img disk.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO="${SENTINELOS_ISO:-$ROOT/Sentinel-v5-x86-64.iso}"
DISK="${SENTINELOS_TEST_DISK:-$ROOT/test.img}"

[[ -s "$ISO" ]] || { echo "error: installer ISO is missing or empty: $ISO" >&2; exit 1; }
[[ -s "$DISK" ]] || { echo "error: test disk is missing or empty: $DISK" >&2; exit 1; }

QEMU="${QEMU:-$(command -v qemu-system-x86_64 || true)}"
[[ -n "$QEMU" ]] || { echo "error: qemu-system-x86_64 was not found." >&2; exit 1; }

FIRMWARE="${SENTINELOS_FIRMWARE:-}"
if [[ -z "$FIRMWARE" ]]; then
  for candidate in \
    /usr/share/OVMF/OVMF_CODE.fd \
    /usr/share/ovmf/OVMF.fd \
    /usr/share/edk2/x64/OVMF_CODE.fd \
    /usr/share/qemu/OVMF.fd \
    /usr/share/qemu/edk2-x86_64-code.fd; do
    if [[ -s "$candidate" ]]; then FIRMWARE="$candidate"; break; fi
  done
fi
[[ -n "$FIRMWARE" && -s "$FIRMWARE" ]] || {
  echo "error: UEFI firmware (OVMF) was not found; set SENTINELOS_FIRMWARE to its .fd file." >&2
  exit 1
}

RAM="${SENTINELOS_RAM_MB:-2048}"
[[ "$RAM" =~ ^[1-9][0-9]*$ ]] || { echo "error: SENTINELOS_RAM_MB must be a positive integer (MiB)." >&2; exit 1; }

echo "Booting installer ISO: $ISO"
echo "Writable test disk: $DISK"
echo "UEFI firmware: $FIRMWARE"
echo "The installer may erase the selected disk; use only this disposable test image."

exec "$QEMU" \
  -machine q35,accel=tcg -cpu max -m "${RAM}M" \
  -display vnc=:0 \
  -drive "if=pflash,format=raw,readonly=on,file=$FIRMWARE" \
  -cdrom "$ISO" -boot order=d \
  -drive "if=none,id=test-disk,format=raw,file=$DISK" \
  -device virtio-blk-pci,drive=test-disk \
  -serial stdio -monitor none -no-reboot -no-shutdown
