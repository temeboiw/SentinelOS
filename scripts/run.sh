#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
B="$ROOT/build"
DISKIMG="$B/magic-disk.img"
VARS="$B/vars.fd"
OVMF_CODE=""
for c in /usr/share/OVMF/OVMF_CODE.fd /usr/share/OVMF/OVMF_CODE_4M.fd \
         /usr/share/edk2/ovmf/OVMF_CODE.fd /usr/share/edk2-ovmf/x64/OVMF_CODE.fd
do [[ -f "$c" ]] && { OVMF_CODE="$c"; break; }; done
[[ -n "$OVMF_CODE" ]] || { echo "no OVMF"; exit 1; }
[[ -f "$VARS" ]] || dd if=/dev/zero of="$VARS" bs=1M count=2 status=none
echo "QEMU: tablet + virtio-blk + VNC :0"
qemu-system-x86_64 \
  -machine q35 -m 1024 \
  -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
  -drive if=pflash,format=raw,file="$VARS" \
  -drive file="$DISKIMG",format=raw,if=none,id=d0 \
  -device virtio-blk-pci,drive=d0 \
  -device virtio-tablet-pci \
  -vnc :0 \
  -serial mon:stdio \
  -no-reboot
