#!/usr/bin/env bash
# One-command build, master, and validate the SentinelOS installer ISO.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
need() { command -v "$1" >/dev/null 2>&1 || { echo "error: required tool not found: $1" >&2; exit 2; }; }
for tool in python3 xorriso mkfs.vfat mcopy mmd sha256sum; do need "$tool"; done

echo "==> Build installer binaries and SPF packages"
bash "$HERE/build-installer.sh"

echo "==> Master UEFI ISO"
bash "$HERE/make-iso.sh"

echo "==> Validate finished ISO"
bash "$HERE/check-iso.sh"
echo "ISO build pipeline completed."
