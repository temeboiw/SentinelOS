#!/usr/bin/env bash
# magic OS QEMU launcher (Linux)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
B="$ROOT/build"
DISKIMG="$B/magic-disk.img"
VARS="$B/vars.fd"

if [[ ! -f "$DISKIMG" ]]; then
  echo "error: $DISKIMG missing – run ./scripts/build.sh first"
  exit 1
fi

OVMF_CODE=""
OVMF_VARS_SRC=""
for candidate in \
  /usr/share/OVMF/OVMF_CODE.fd \
  /usr/share/OVMF/OVMF_CODE_4M.fd \
  /usr/share/edk2/ovmf/OVMF_CODE.fd \
  /usr/share/edk2-ovmf/x64/OVMF_CODE.fd \
  /usr/share/qemu/OVMF.fd \
  /usr/share/ovmf/OVMF.fd
do
  [[ -f "$candidate" ]] && { OVMF_CODE="$candidate"; break; }
done

for candidate in \
  /usr/share/OVMF/OVMF_VARS.fd \
  /usr/share/OVMF/OVMF_VARS_4M.fd \
  /usr/share/edk2/ovmf/OVMF_VARS.fd \
  /usr/share/edk2-ovmf/x64/OVMF_VARS.fd
do
  [[ -f "$candidate" ]] && { OVMF_VARS_SRC="$candidate"; break; }
done

if [[ -z "$OVMF_CODE" ]]; then
  echo "error: OVMF firmware not found. Install ovmf / edk2-ovmf package."
  exit 1
fi

if [[ ! -f "$VARS" ]]; then
  if [[ -n "$OVMF_VARS_SRC" ]]; then
    cp "$OVMF_VARS_SRC" "$VARS"
  else
    dd if=/dev/zero of="$VARS" bs=1M count=2 status=none 2>/dev/null || truncate -s 2M "$VARS"
  fi
fi

echo "QEMU: $DISKIMG  (serial=stdio, VNC=:0)"
exec qemu-system-x86_64 \
  -machine q35,accel=kvm:tcg \
  -cpu max \
  -m 512 \
  -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
  -drive if=pflash,format=raw,file="$VARS" \
  -drive file="$DISKIMG",format=raw,if=virtio \
  -serial stdio \
  -vnc :0 \
  -no-reboot \
  "$@"
