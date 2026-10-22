#!/usr/bin/env bash
# Create a disposable, sparse 1 GiB raw disk image for SentinelOS installer testing.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-$HERE/artifacts/sentinel-test-disk.img}"
SIZE="${SIZE:-1G}"
need() { command -v "$1" >/dev/null 2>&1 || { echo "ERROR: required command not found: $1" >&2; exit 2; }; }
need qemu-img
mkdir -p "$(dirname "$OUT")"
if [[ -e "$OUT" ]]; then
  echo "ERROR: refusing to overwrite existing disk image: $OUT" >&2
  echo "Move it aside or choose another output path explicitly." >&2
  exit 3
fi
qemu-img create -f raw "$OUT" "$SIZE"
qemu-img info "$OUT"
echo "Created disposable test disk: $OUT"
echo "It is a raw 1 GiB virtual disk; sparse allocation means it initially uses little host storage."
