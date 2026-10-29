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
INSTALLER_ESP="$STAGE/installer-esp"
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

# Refuse overlapping source/destination trees before clearing or populating
# staging. Resolving absolute paths here avoids relying on the caller's cwd.
python3 - "$B/rootfs" "$B/esp" "$STAGE" <<'PY'
from pathlib import Path
import sys
sources = [Path(arg).resolve(strict=True) for arg in sys.argv[1:3]]
stage = Path(sys.argv[3]).resolve(strict=False)
for source in sources:
    if source == stage or source in stage.parents or stage in source.parents:
        raise SystemExit(f"error: source and installer staging trees overlap: {source} / {stage}")
PY

# Stage the already-built OS as the deployable target. Installer startup
# binaries live in a separate ESP tree used only by the ISO boot environment.
rm -rf "$STAGE"
mkdir -p "$STAGE/esp" "$STAGE/system" "$INSTALLER_ESP" "$OUT" "$OBJ"
cp -a "$B/esp/." "$STAGE/esp/"
cp -a "$B/rootfs/." "$STAGE/system/"
cp -a "$B/esp/." "$INSTALLER_ESP/"
SYS32="$STAGE/system/magic/System32"
if [[ "$(realpath -e "$STAGE/system")" == "$(realpath -e "$INSTALLER_ESP")" || "$(realpath -e "$STAGE/system")" == "$(realpath -e "$STAGE/esp")" ]]; then
  echo "error: deployable OS and installer staging paths must be separate" >&2
  exit 4
fi

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
  -I"$HERE/source/user"
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
lld -flavor link /subsystem:native /entry:instlr_main /nodefaultlib /fixed:no /base:0x40700000 "/out:$OBJ/instlr.exe" "$OBJ/instlr.o"

clang "${USER_CFLAGS[@]}" -I"$HERE/user" -c "$HERE/user/screen.c" -o "$OBJ/screen.o"
lld -flavor link /subsystem:native /entry:screen_main /nodefaultlib /fixed:no /base:0x40800000 "/out:$OBJ/screen.exe" "$OBJ/screen.o"

clang "${USER_CFLAGS[@]}" -I"$HERE/user" -c "$HERE/user/smss_installer.c" -o "$OBJ/smss_installer.o"
lld -flavor link /subsystem:native /entry:smss_main /nodefaultlib /fixed:no /base:0x40000000 "/out:$OBJ/smss.exe" "$OBJ/smss_installer.o"

for f in "$OBJ/instlr.exe" "$OBJ/screen.exe" "$OBJ/smss.exe"; do
  [[ -s "$f" ]] || { echo "error: installer image missing: $f" >&2; exit 3; }
done

# The installer ISO boots directly from removable media. Only its dedicated
# ESP gets the installer startup programs; STAGE/system stays the main OS.
mkdir -p "$INSTALLER_ESP/magic"
cp -a "$B/rootfs/magic/System32" "$INSTALLER_ESP/magic/"
cp -f "$OBJ/instlr.exe" "$INSTALLER_ESP/magic/System32/instlr.exe"
cp -f "$OBJ/screen.exe" "$INSTALLER_ESP/magic/System32/screen.exe"
cp -f "$OBJ/smss.exe" "$INSTALLER_ESP/magic/System32/smss.exe"
for f in hal.dll krnl.exe smss.exe instlr.exe; do
  [[ -s "$INSTALLER_ESP/magic/System32/$f" ]] || {
    echo "error: installer ESP is missing boot image $f" >&2
    exit 4
  }
done
if cmp -s "$INSTALLER_ESP/magic/System32/smss.exe" "$SYS32/smss.exe"; then
  echo "error: installer ESP startup still points to the main OS session manager" >&2
  exit 4
fi

# Preserve the full main OS payload, including its applications and tests.
[[ -s "$STAGE/esp/EFI/BOOT/BOOTX64.EFI" ]] || {
  echo "error: BOOTX64.EFI missing" >&2
  exit 4
}
for f in krnl.exe hal.dll smss.exe bootvid.exe sentlogon.exe; do
  [[ -s "$SYS32/$f" ]] || {
    echo "error: required main OS payload file missing: $SYS32/$f" >&2
    exit 4
  }
done
if [[ -e "$SYS32/instlr.exe" || -e "$SYS32/screen.exe" ]] || ! cmp -s "$SYS32/smss.exe" "$B/rootfs/magic/System32/smss.exe"; then
  echo "error: installer-only startup content found in the main OS payload" >&2
  exit 4
fi
# Record the exact, verified build output tree used as the install payload.
find "$STAGE/system" -type f -printf '%P\n' | LC_ALL=C sort > "$STAGE/main-os-files.manifest"
[[ -s "$STAGE/main-os-files.manifest" ]] || { echo "error: main OS source manifest is empty" >&2; exit 4; }
if grep -Eiq '(^|/)(instlr|screen)\.exe$' "$STAGE/main-os-files.manifest"; then
  echo "error: installer-only program listed in main OS source manifest" >&2
  exit 4
fi

if [[ "${SECURE_BOOT:-0}" == "1" ]]; then
  : "${SECURE_BOOT_CERT:?set SECURE_BOOT_CERT to the enrolled signing certificate when packaging Secure Boot media}"
  command -v sbverify >/dev/null 2>&1 || { echo "error: Secure Boot packaging requires sbverify (sbsigntool)" >&2; exit 2; }
  for f in "$STAGE/esp/EFI/BOOT/BOOTX64.EFI" \
           "$STAGE/esp/EFI/magic/bootuni.efi" \
           "$STAGE/esp/EFI/magic/loadufi.efi"; do
    sbverify --cert "$SECURE_BOOT_CERT" "$f" >/dev/null || { echo "error: unsigned or invalid EFI stage: $f" >&2; exit 4; }
  done
fi

python3 "$HERE/make_spf.py" build --esp-root "$INSTALLER_ESP" --system-root "$STAGE/system" --output-dir "$OUT"
python3 "$HERE/make_spf.py" verify "$OUT/esp.spf" --kind esp --require-boot-layout
python3 "$HERE/make_spf.py" verify "$OUT/system.spf" --kind system --require-boot-layout
echo "Installer payloads built from the main OS artifacts."
