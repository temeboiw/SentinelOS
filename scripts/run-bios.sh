#!/usr/bin/env bash
# Run the SentinelOS MBR disk image under SeaBIOS (BIOS/MBR boot path).
# Mirrors scripts/run.sh (OVMF/GPT) but uses SeaBIOS instead of OVMF and the
# MBR disk image produced by `./scripts/build.sh --mbr`.
#
# QEMU command line:
#   qemu-system-x86_64 -machine q35 -accel tcg -cpu max \
#     -bios <seabios> -smp <CPUS> -m <RAM>M \
#     -drive file=build/magic-disk.img,format=raw,if=none,id=d0 \
#     -device virtio-blk-pci,drive=d0 \
#     -netdev user,id=net0 -device e1000,netdev=net0 \
#     [-vnc 127.0.0.1:0 | -display sdl] -serial stdio -no-reboot
#
# Exits with QEMU's exit code.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
DISK="$BUILD/magic-disk.img"

if [[ ! -f "$DISK" ]]; then
  echo "error: SentinelOS MBR disk image is missing: $DISK" >&2
  echo "Run ./scripts/build.sh --mbr first." >&2
  exit 1
fi

QEMU="${QEMU:-$(command -v qemu-system-x86_64 || true)}"
if [[ -z "$QEMU" ]]; then
  echo "error: qemu-system-x86_64 was not found. Install QEMU for x86_64 guests." >&2
  exit 1
fi

# Resolve SeaBIOS. Prefer an explicitly staged image, then common locations.
SEABIOS="${SENTINELOS_SEABIOS:-}"
if [[ -z "$SEABIOS" || ! -f "$SEABIOS" ]]; then
  SEABIOS=""
  for candidate in \
    /usr/share/seabios/bios-256k.bin \
    /usr/share/seabios/bios.bin \
    /usr/share/seabios/seabios.bin \
    /usr/share/qemu/bios-256k.bin \
    /usr/share/qemu/bios.bin \
    /usr/share/qemu/seabios.bin; do
    if [[ -f "$candidate" ]]; then SEABIOS="$candidate"; break; fi
  done
fi
if [[ -z "$SEABIOS" ]]; then
  echo "error: SeaBIOS firmware was not found." >&2
  echo "Install the distro's SeaBIOS package (for example: apt install seabios), or set SENTINELOS_SEABIOS to a SeaBIOS .bin file." >&2
  echo "Searched: /usr/share/seabios/{bios-256k.bin,bios.bin,seabios.bin}, /usr/share/qemu/{bios-256k.bin,bios.bin,seabios.bin}" >&2
  exit 1
fi

RAM="${SENTINELOS_RAM_MB:-2048}"
CPUS="${SENTINELOS_CPUS:-1}"
[[ "$RAM" =~ ^[1-9][0-9]*$ ]] || { echo "error: SENTINELOS_RAM_MB must be a positive integer (MiB)." >&2; exit 1; }
[[ "$CPUS" =~ ^[1-9][0-9]*$ ]] || { echo "error: SENTINELOS_CPUS must be a positive integer." >&2; exit 1; }

# proot usually has no usable native SDL window. Detect common proot markers
# and expose the guest display through QEMU's VNC server on TCP port 5900.
IS_PROOT=0
if [[ -n "${PROOT_TMP_DIR:-}" || -n "${PROOT_LOADER:-}" || -n "${PROOT_ROOTFS:-}" ]] ||
   grep -qiE 'proot|termux' /proc/self/mountinfo 2>/dev/null; then
  IS_PROOT=1
fi

QEMU_DISPLAY_ARGS=(-vga std)
if [[ "$IS_PROOT" == 1 ]]; then
  QEMU_DISPLAY_ARGS+=(-display none -vnc 127.0.0.1:0)
else
  QEMU_DISPLAY_ARGS+=(-display sdl)
fi

echo "SentinelOS BIOS/MBR runner"
echo "  Disk: $DISK (raw MBR image, built with build.sh --mbr)"
echo "  Firmware: $SEABIOS (SeaBIOS)"
echo "  RAM: $RAM MiB; CPUs: $CPUS"
if [[ "$IS_PROOT" == 1 ]]; then
  echo "  Environment: proot detected; VNC listening on 127.0.0.1:5900"
  echo "  Connect with a VNC viewer to localhost:5900."
else
  echo "  Display: SDL"
fi
echo "  Note: on ARM64 proot, QEMU will emulate x86_64 with TCG."

exec "$QEMU" \
  -machine q35 \
  -accel tcg \
  -cpu max \
  -smp "$CPUS" \
  -m "${RAM}M" \
  -bios "$SEABIOS" \
  -drive "file=$DISK,format=raw,if=none,id=d0" \
  -device virtio-blk-pci,drive=d0 \
  -netdev user,id=net0 \
  -device e1000,netdev=net0 \
  "${QEMU_DISPLAY_ARGS[@]}" \
  -serial stdio \
  -no-reboot
