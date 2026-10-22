#!/usr/bin/env bash
# Install a verified SentinelOS SPF build onto a dedicated Linux block device.
# Destructive operation: requires an explicit target device and confirmation.
set -Eeuo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$HERE/.." && pwd)"
ESP_PACKAGE="${ESP_PACKAGE:-$HERE/artifacts/esp.spf}"
SYSTEM_PACKAGE="${SYSTEM_PACKAGE:-$HERE/artifacts/system.spf}"
TARGET=""
CONFIRM=""

usage() {
  cat <<'EOF'
Usage:
  sudo bash installer/install-disk.sh --target /dev/sdX --confirm "ERASE /dev/sdX"

Installs the verified ESP and system SPF packages to a dedicated target disk.
The target disk will be repartitioned and both partitions formatted. All data
on that disk will be destroyed. Never target the host's boot/system disk.

Options:
  --target DEVICE       Whole disk (not a partition), e.g. /dev/sdb
  --confirm TEXT        Must exactly equal: ERASE DEVICE
  --help                Show this help

Environment:
  ESP_PACKAGE           Defaults to installer/artifacts/esp.spf
  SYSTEM_PACKAGE        Defaults to installer/artifacts/system.spf
EOF
}

while (($#)); do
  case "$1" in
    --target) (($# >= 2)) || { echo "missing --target value" >&2; exit 2; }; TARGET="$2"; shift 2 ;;
    --confirm) (($# >= 2)) || { echo "missing --confirm value" >&2; exit 2; }; CONFIRM="$2"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

[[ $EUID -eq 0 ]] || { echo "run as root (sudo)" >&2; exit 1; }
[[ -n "$TARGET" && -b "$TARGET" ]] || { echo "--target must name an existing whole block device" >&2; exit 2; }
[[ -e "$ESP_PACKAGE" && -e "$SYSTEM_PACKAGE" ]] || { echo "SPF package missing; build installer packages first" >&2; exit 1; }
for cmd in lsblk sgdisk partprobe udevadm mkfs.fat mkfs.ext4 mount umount python3; do
  command -v "$cmd" >/dev/null || { echo "required command not found: $cmd" >&2; exit 1; }
done

# Refuse partitions, loopback files, mounted disks/children, and any device that
# appears to contain a mounted root/boot filesystem. This is intentionally strict.
[[ "$(lsblk -dnro TYPE "$TARGET")" == "disk" ]] || { echo "target must be a whole disk, not a partition/loop device" >&2; exit 1; }
if lsblk -nrpo MOUNTPOINT "$TARGET" | grep -q '[^[:space:]]'; then
  echo "refusing target with mounted filesystem(s): $TARGET" >&2
  lsblk -o NAME,SIZE,FSTYPE,MOUNTPOINTS "$TARGET" >&2
  exit 1
fi
TARGET_REAL="$(readlink -f -- "$TARGET")"
[[ "$CONFIRM" == "ERASE $TARGET" || "$CONFIRM" == "ERASE $TARGET_REAL" ]] || {
  echo "destructive install requires --confirm 'ERASE $TARGET' (or resolved path)" >&2
  exit 2
}

# Check packages before touching the disk. The coordinator repeats this
# verification immediately before extraction.
python3 "$HERE/make_spf.py" verify "$ESP_PACKAGE" --kind esp --require-boot-layout
python3 "$HERE/make_spf.py" verify "$SYSTEM_PACKAGE" --kind system --require-boot-layout

WORK="$(mktemp -d /tmp/sentinel-install.XXXXXX)"
MOUNTED=0
cleanup() {
  set +e
  if ((MOUNTED)); then
    sync
    umount "$WORK/system" 2>/dev/null
    umount "$WORK/esp" 2>/dev/null
  fi
  rm -rf -- "$WORK"
}
trap cleanup EXIT INT TERM
mkdir -p "$WORK/esp" "$WORK/system"

echo "WARNING: repartitioning and formatting $TARGET; ALL DATA ON IT WILL BE LOST."
echo "Target device:"
lsblk -o NAME,SIZE,MODEL,SERIAL,TYPE,MOUNTPOINTS "$TARGET"
echo "Installing SentinelOS to $TARGET"

# GPT: 128 MiB EFI System Partition, remainder Linux filesystem.
sgdisk --zap-all "$TARGET"
sgdisk --clear   --new=1:2048:+128M --typecode=1:EF00 --change-name=1:"Sentinel ESP"   --new=2:0:0 --typecode=2:8300 --change-name=2:"SentinelOS" "$TARGET"
partprobe "$TARGET"
udevadm settle
ESP_PART="${TARGET}1"
SYS_PART="${TARGET}2"
# NVMe, MMC, and loop-style device names use a p suffix.
if [[ "$TARGET" =~ (nvme[0-9]+n[0-9]+|mmcblk[0-9]+|loop[0-9]+)$ ]]; then
  ESP_PART="${TARGET}p1"; SYS_PART="${TARGET}p2"
fi
[[ -b "$ESP_PART" && -b "$SYS_PART" ]] || {
  echo "partition nodes did not appear: $ESP_PART / $SYS_PART" >&2
  exit 1
}
mkfs.fat -F 32 -n SENTINEL "$ESP_PART"
mkfs.ext4 -F -L SENTINELOS "$SYS_PART"
mount "$ESP_PART" "$WORK/esp"
MOUNTED=1
mount "$SYS_PART" "$WORK/system"

python3 "$HERE/install_from_spf.py" \
  --esp-package "$ESP_PACKAGE" --system-package "$SYSTEM_PACKAGE" \
  --esp-target "$WORK/esp" --system-target "$WORK/system" \
  --apply --confirm "INSTALL SENTINELOS"

# Verify the files actually landed where the UEFI fallback loader and kernel
# expect them. The loader is installed at the standard removable-media path.
test -s "$WORK/esp/EFI/BOOT/BOOTX64.EFI"
test -s "$WORK/esp/magic/System32/hal.dll"
test -s "$WORK/esp/magic/System32/krnl.exe"
test -s "$WORK/system/magic/System32/hal.dll"
test -s "$WORK/system/magic/System32/krnl.exe"
test -s "$WORK/system/magic/System32/smss.exe"
sync
echo "SentinelOS files deployed and boot-critical paths verified."
echo "UEFI fallback loader: EFI/BOOT/BOOTX64.EFI"
echo "System payload: magic/System32/{hal.dll,krnl.exe,smss.exe}"
echo "The disk write completed; a real UEFI reboot test is still required to prove boot."
