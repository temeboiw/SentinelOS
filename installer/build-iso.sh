#!/usr/bin/env bash
# One-command build, master, and validate the SentinelOS installer ISO.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
need() { command -v "$1" >/dev/null 2>&1 || { echo "error: required tool not found: $1" >&2; exit 2; }; }
# Check the full native-host toolchain up front instead of failing halfway
# through the nested OS image build.
for tool in clang clang++ lld python3 xorriso qemu-img sfdisk mkfs.vfat mke2fs mcopy mmd dd sha256sum; do need "$tool"; done

echo "==> Build installer binaries and SPF packages"
bash "$HERE/build-installer.sh"

echo "==> Master UEFI ISO"
bash "$HERE/make-iso.sh"

echo "==> Validate finished ISO"
bash "$HERE/check-iso.sh"
echo "ISO build pipeline completed."
