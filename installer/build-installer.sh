#!/usr/bin/env bash
# Build an isolated SentinelOS installer edition and SPF payloads.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/source"; B="$SRC/build"; SYS32="$B/rootfs/magic/System32"
STAGE="$HERE/staging"; OUT="$HERE/artifacts"
if [[ ! -f "$SRC/scripts/build.sh" ]]; then echo "error: isolated source snapshot missing" >&2; exit 2; fi
# Patch a disposable sibling script so the committed isolated snapshot stays pristine.
BUILD_SCRIPT="$SRC/scripts/.build-installer.generated.sh"
trap 'rm -f "$BUILD_SCRIPT"' EXIT
cp "$SRC/scripts/build.sh" "$BUILD_SCRIPT"
python3 - "$BUILD_SCRIPT" <<'PY'
import pathlib,sys
p=pathlib.Path(sys.argv[1]);s=p.read_text()
s=s.replace('  UserTestApps/game.cpp \\\n  UserTestApps/maze.cpp \\\n','')
start=s.find('echo "  -> game.exe')
end=s.find('echo "==> Checking required images',start)
if start>=0 and end>start:s=s[:start]+s[end:]
p.write_text(s)
PY
bash "$BUILD_SCRIPT" "$@"
# Build installer-only native applications using the same PE ABI and user headers.
USER_CFLAGS=(-target x86_64-windows-gnu -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone -mno-stack-arg-probe -mgeneral-regs-only -O2 -Wall -I"$SRC/user" -I"$SRC/shared")
CLANG_BIN="${CLANG_BIN:-$(command -v clang)}"
CLANGXX_BIN="${CLANGXX_BIN:-$(command -v clang++)}"
LLD_BIN="${LLD_BIN:-$(command -v lld)}"
[[ -x "$CLANG_BIN" && -x "$LLD_BIN" ]] || { echo "error: clang and lld are required to link installer session apps" >&2; exit 2; }
"$CLANG_BIN" "${USER_CFLAGS[@]}" -c "$HERE/user/instlr.c" -o "$B/obj/instlr.o"
"$LLD_BIN" -flavor link /subsystem:native /entry:instlr_main /nodefaultlib /fixed:no /base:0x40700000 "/out:$SYS32/instlr.exe" "$B/obj/instlr.o"
"$CLANG_BIN" "${USER_CFLAGS[@]}" -c "$HERE/user/screen.c" -o "$B/obj/screen.o"
"$LLD_BIN" -flavor link /subsystem:native /entry:screen_main /nodefaultlib /fixed:no /base:0x40800000 "/out:$SYS32/screen.exe" "$B/obj/screen.o"
"$CLANG_BIN" "${USER_CFLAGS[@]}" -c "$HERE/user/smss_installer.c" -o "$B/obj/smss_installer.o"
"$LLD_BIN" -flavor link /subsystem:native /entry:smss_main /nodefaultlib /fixed:no /base:0x40000000 "/out:$SYS32/smss.exe" "$B/obj/smss_installer.o"
for f in "$SYS32/instlr.exe" "$SYS32/screen.exe" "$SYS32/smss.exe"; do [[ -s "$f" ]] || { echo "error: installer image missing: $f" >&2; exit 3; }; done
rm -rf "$STAGE"; mkdir -p "$STAGE/esp" "$STAGE/system" "$OUT"
cp -a "$B/esp/." "$STAGE/esp/"; cp -a "$B/rootfs/." "$STAGE/system/"
# The installed-system loader reads modules from ext4, but an installer ISO has
# no installed ext4 volume yet. Mirror the built System32 tree onto the ESP so
# loadufi can boot directly from removable media using its SimpleFileSystem path.
mkdir -p "$STAGE/esp/magic"
cp -a "$STAGE/system/magic/System32" "$STAGE/esp/magic/"
for f in hal.dll krnl.exe smss.exe instlr.exe; do
  [[ -s "$STAGE/esp/magic/System32/$f" ]] || { echo "error: installer ESP is missing boot image $f" >&2; exit 4; }
done
rm -f "$STAGE/system/game.exe" "$STAGE/system/maze.exe"
rm -rf "$STAGE/system/UserTestApps" "$STAGE/system/games" "$STAGE/system/tests"
[[ -s "$STAGE/esp/EFI/BOOT/BOOTX64.EFI" ]] || { echo "error: BOOTX64.EFI missing" >&2; exit 4; }
[[ -s "$STAGE/system/magic/System32/krnl.exe" ]] || { echo "error: krnl.exe missing" >&2; exit 4; }
python3 "$HERE/make_spf.py" build --esp-root "$STAGE/esp" --system-root "$STAGE/system" --output-dir "$OUT"
python3 "$HERE/make_spf.py" verify "$OUT/esp.spf"
python3 "$HERE/make_spf.py" verify "$OUT/system.spf"
echo "Installer binaries and SPF payloads built. Run make-iso.sh to master the ISO."
