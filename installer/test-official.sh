#!/usr/bin/env bash
# Repeatable pre-release validation for SentinelOS installer media.
# Run on a native Ubuntu/Debian x86-64 host, not Android/Termux.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
MODE="${1:-all}"
case "$MODE" in
  all|unit|iso|vm|vm-vnc) ;;
  *) echo "usage: $0 [all|unit|iso|vm|vm-vnc]" >&2; exit 2 ;;
esac
need() { command -v "$1" >/dev/null 2>&1 || { echo "ERROR: required command not found: $1" >&2; exit 2; }; }
run_unit() {
  need python3
  echo "==> Python installer regression tests"
  (cd "$ROOT" && python3 -m unittest discover -s installer/tests -v)
  echo "==> Shell syntax checks"
  bash -n "$HERE/build-installer.sh" "$HERE/build-iso.sh" "$HERE/make-iso.sh" "$HERE/check-iso.sh" "$HERE/test-official.sh"
  echo "PASS: unit tests and shell syntax"
}
run_iso() {
  echo "==> Native host tool preflight"
  for tool in clang clang++ lld python3 xorriso qemu-img sfdisk mkfs.vfat mke2fs mmd mcopy dd sha256sum; do need "$tool"; done
  if [[ -z "${EDK2:-}" || ! -d "${EDK2}/MdePkg/Include" ]]; then
    echo "ERROR: set EDK2 to an edk2 checkout containing MdePkg/Include" >&2
    exit 2
  fi
  echo "==> Build and validate ISO"
  (cd "$ROOT" && bash installer/build-iso.sh)
  echo "PASS: ISO mastering and structural validation"
}
run_vm() {
  need qemu-system-x86_64
  ISO="$HERE/artifacts/SentinelOS-V4-BETA.iso"
  [[ -s "$ISO" ]] || { echo "ERROR: build ISO first: $ISO" >&2; exit 3; }
  OVMF=""
  for candidate in \
    /usr/share/OVMF/OVMF_CODE.fd \
    /usr/share/ovmf/OVMF.fd \
    /usr/share/edk2/x64/OVMF_CODE.4m.fd \
    /usr/share/qemu/OVMF_CODE.fd
  do
    if [[ -s "$candidate" ]]; then OVMF="$candidate"; break; fi
  done
  if [[ -z "$OVMF" ]]; then
    echo "ERROR: UEFI firmware not found; install ovmf (Debian/Ubuntu)." >&2
    exit 2
  fi
  if [[ "$MODE" == "vm-vnc" ]]; then
    LOG="$HERE/artifacts/qemu-uefi-vnc.log"
    DISK="$HERE/artifacts/sentinel-test-disk.img"
    if [[ ! -s "$DISK" ]]; then bash "$HERE/create-test-disk.sh" "$DISK"; fi
    VNC_ADDR="${VNC_ADDR:-127.0.0.1}"
    VNC_DISPLAY="${VNC_DISPLAY:-0}"
    echo "==> Launch interactive UEFI VM with VNC at ${VNC_ADDR}:$((5900 + VNC_DISPLAY))"
    echo "Disposable test disk: $DISK"
    echo "Connect with a VNC viewer. Stop QEMU with Ctrl+C."
    echo "VNC is unauthenticated; keep it on 127.0.0.1 unless you deliberately choose a trusted network."
    qemu-system-x86_64 \
      -machine q35,accel=tcg -cpu max -m 2048 \
      -drive if=pflash,format=raw,readonly=on,file="$OVMF" \
      -cdrom "$ISO" -boot order=d -display none \
      -drive "if=none,id=installer-disk,format=raw,file=$DISK" \
      -device virtio-blk-pci,drive=installer-disk \
      -vnc "${VNC_ADDR}:${VNC_DISPLAY}" -serial "file:$LOG" \
      -monitor none -no-reboot -no-shutdown
    return
  fi
  LOG="$HERE/artifacts/qemu-uefi-smoke.log"
  echo "==> Launch UEFI VM for 45 seconds; serial output -> $LOG"
  echo "This checks that QEMU/OVMF can start the ISO; it does not prove the installer UI or disk installation works."
  set +e
  timeout --signal=TERM 45s qemu-system-x86_64 \
    -machine q35,accel=tcg -cpu max -m 2048 \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF" \
    -cdrom "$ISO" -boot order=d -display none -serial "file:$LOG" \
    -monitor none -no-reboot -no-shutdown
  rc=$?
  set -e
  if [[ "$rc" -ne 124 && "$rc" -ne 0 && "$rc" -ne 143 ]]; then
    echo "ERROR: QEMU exited unexpectedly with status $rc" >&2
    [[ -f "$LOG" ]] && cat "$LOG" >&2
    exit "$rc"
  fi
  echo "QEMU ran until the smoke-test timeout. Inspect the display manually with a graphical VM run before calling this a successful boot test."
  echo "Serial log: $LOG"
}
case "$MODE" in
  unit) run_unit ;;
  iso) run_unit; run_iso ;;
  vm|vm-vnc) run_vm ;;
  all) run_unit; run_iso; run_vm ;;
esac
echo "Requested validation stages completed."
