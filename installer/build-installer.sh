#!/usr/bin/env bash
# Build the installer payload from the SAME artifacts produced by scripts/build.sh.
# This script does not maintain or compile a second SentinelOS source snapshot.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
B="$ROOT/build"
STAGE="$HERE/staging"
OUT="$HERE/artifacts"
SYS32="$STAGE/system/magic/System32"
OBJ="$B/installer-obj"

for f in "$B/esp/EFI/BOOT/BOOTX64.EFI" "$B/esp/EFI/magic/bootuni.efi" \
         "$B/esp/EFI/magic/loadufi.efi" "$B/rootfs/magic/System32/krnl.exe" \
         "$B/rootfs/magic/System32/hal.dll" "$B/rootfs/magic/System32/kernel32.dll" \
         "$B/rootfs/magic/System32/user32.dll"; do
  [[ -s "$f" ]] || {
    echo "error: required OS build artifact missing: $f" >&2
    echo "Run bash scripts/build.sh to build SentinelOS and its installer ISO." >&2
    exit 2
  }
done

# Stage the already-built OS. Installer-only binaries are linked into this
# staging copy, never into build/rootfs, so the installed OS keeps its own SMSS.
rm -rf "$STAGE"
mkdir -p "$STAGE/esp" "$STAGE/system" "$OUT" "$OBJ"
cp -a "$B/esp/." "$STAGE/esp/"
cp -a "$B/rootfs/." "$STAGE/system/"
SYS32="$STAGE/system/magic/System32"

# Installer apps use SentinelOS's existing PE ABI and shared user headers.
USER_CFLAGS=(
  -target x86_64-windows-gnu
  -ffreestanding
  -fno-stack-protector
  -fno-pic
  -mno-red-zone
  -mno-stack-arg-probe
  -mgeneral-regs-only
  -O2
  -Wall
  -I"$ROOT/user"
  -I"$ROOT/shared"
)

CLANG_BIN="${CLANG_BIN:-$(command -v clang)}"
LLD_BIN="${LLD_BIN:-$(command -v lld)}"
[[ -x "$CLANG_BIN" && -x "$LLD_BIN" ]] || {
  echo "error: clang and lld are required to link installer applications" >&2
  exit 2
}

echo "==> Link installer session apps against the main SentinelOS build"
clang "${USER_CFLAGS[@]}" -I"$HERE/user" -c "$HERE/user/instlr.c" -o "$OBJ/instlr.o"
lld -flavor link /subsystem:native /entry:instlr_main /nodefaultlib /fixed:no /base:0x40700000 "/out:$SYS32/instlr.exe" "$OBJ/instlr.o"

clang "${USER_CFLAGS[@]}" -I"$HERE/user" -c "$HERE/user/screen.c" -o "$OBJ/screen.o"
lld -flavor link /subsystem:native /entry:screen_main /nodefaultlib /fixed:no /base:0x40800000 "/out:$SYS32/screen.exe" "$OBJ/screen.o"

clang "${USER_CFLAGS[@]}" -I"$HERE/user" -c "$HERE/user/smss_installer.c" -o "$OBJ/smss_installer.o"
lld -flavor link /subsystem:native /entry:smss_main /nodefaultlib /fixed:no /base:0x40000000 "/out:$SYS32/smss.exe" "$OBJ/smss_installer.o"

for f in "$SYS32/instlr.exe" "$SYS32/screen.exe" "$SYS32/smss.exe"; do
  [[ -s "$f" ]] || { echo "error: installer image missing: $f" >&2; exit 3; }
done

# The installer ISO boots directly from removable media, so mirror the staged
# System32 boot modules onto the ESP. Do not alter the normal OS ESP/rootfs.
mkdir -p "$STAGE/esp/magic"
cp -a "$SYS32" "$STAGE/esp/magic/"
for f in hal.dll krnl.exe smss.exe instlr.exe; do
  [[ -s "$STAGE/esp/magic/System32/$f" ]] || {
    echo "error: installer ESP is missing boot image $f" >&2
    exit 4
  }
done

# Keep test applications out of installation media; they remain in the normal
# build and installed OS where applicable.
rm -f "$STAGE/system/game.exe" "$STAGE/system/maze.exe"
rm -rf "$STAGE/system/UserTestApps" "$STAGE/system/games" "$STAGE/system/tests"
[[ -s "$STAGE/esp/EFI/BOOT/BOOTX64.EFI" ]] || {
  echo "error: BOOTX64.EFI missing" >&2
  exit 4
}
[[ -s "$STAGE/system/magic/System32/krnl.exe" ]] || {
  echo "error: krnl.exe missing" >&2
  exit 4
}

python3 "$HERE/make_spf.py" build --esp-root "$STAGE/esp" --system-root "$STAGE/system" --output-dir "$OUT"
python3 "$HERE/make_spf.py" verify "$OUT/esp.spf" --kind esp --require-boot-layout
python3 "$HERE/make_spf.py" verify "$OUT/system.spf" --kind system --require-boot-layout
echo "Installer payloads built from the main OS artifacts."
