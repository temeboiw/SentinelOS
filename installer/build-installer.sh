#!/usr/bin/env bash
# Build the isolated SentinelOS installer payload without touching root sources.
# Usage: bash installer/build-installer.sh [normal build.sh options]
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/source"
BUILD="$SRC/build"
STAGE="$HERE/staging"
OUT="$HERE/artifacts"

if [[ ! -f "$SRC/scripts/build.sh" || ! -f "$SRC/boot/bootx64.c" ]]; then
  echo "error: isolated source snapshot is incomplete: $SRC" >&2
  exit 2
fi
command -v python3 >/dev/null 2>&1 || { echo "error: python3 is required" >&2; exit 2; }

echo "==> Building the isolated SentinelOS source tree"
echo "    Source: $SRC"
echo "    Output: $BUILD"
echo "    Root-level source files are not used or modified."
bash "$SRC/scripts/build.sh" "$@"

for path in "$BUILD/esp/EFI/BOOT" "$BUILD/esp/EFI/magic" "$BUILD/rootfs/magic/System32"; do
  [[ -d "$path" ]] || { echo "error: expected build output missing: $path" >&2; exit 3; }
done

rm -rf "$STAGE"
mkdir -p "$STAGE/esp" "$STAGE/system" "$OUT"
cp -a "$BUILD/esp/." "$STAGE/esp/"
cp -a "$BUILD/rootfs/." "$STAGE/system/"

# Game/test binaries are deliberately excluded from the installer package.
rm -f "$STAGE/system/game.exe" "$STAGE/system/maze.exe"
rm -rf "$STAGE/system/UserTestApps" "$STAGE/system/games" "$STAGE/system/tests"

# Do not silently create a package from an incomplete boot tree.
[[ -s "$STAGE/esp/EFI/BOOT/BOOTX64.EFI" || -s "$STAGE/esp/EFI/BOOT/bootx64.efi" ]] || {
  echo "error: ESP is missing BOOTX64.EFI; refusing to create esp.spf" >&2
  exit 4
}
[[ -s "$STAGE/system/magic/System32/krnl.exe" ]] || {
  echo "error: system package is missing magic/System32/krnl.exe" >&2
  exit 4
}

echo "==> Creating Sentinel Package Files"
python3 "$HERE/make_spf.py" build --esp-root "$STAGE/esp" --system-root "$STAGE/system" --output-dir "$OUT"
python3 "$HERE/make_spf.py" verify "$OUT/esp.spf"
python3 "$HERE/make_spf.py" verify "$OUT/system.spf"
python3 "$HERE/make_spf.py" info "$OUT/esp.spf"
python3 "$HERE/make_spf.py" info "$OUT/system.spf"

echo
echo "Payload packages verified:"
echo "  $OUT/esp.spf"
echo "  $OUT/system.spf"
echo "NOTE: this does not yet produce SentinelOS-V4-BETA.iso. Installer boot/UI,"
echo "      disk partitioning/formatting, SPF application, and ISO mastering remain."
