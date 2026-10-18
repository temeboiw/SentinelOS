#!/usr/bin/env bash
# magic OS Linux build script
#   ./scripts/build.sh              build only
#   ./scripts/build.sh --run        build, then launch QEMU
#   ./scripts/build.sh --keep-fallback
#   ./scripts/build.sh --reset-nvram
#   ./scripts/build.sh --clean
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
B="$ROOT/build"
EDK2="${EDK2:-/usr/include/efi}"
DISKIMG="$B/magic-disk.img"
SYS32="$B/rootfs/magic/System32"

KEEP_FALLBACK=0
RESET_NVRAM=0
DO_RUN=0
DO_CLEAN=0

for arg in "$@"; do
  case "$arg" in
    --run|-r)            DO_RUN=1 ;;
    --keep-fallback|-k)  KEEP_FALLBACK=1 ;;
    --reset-nvram)       RESET_NVRAM=1 ;;
    --clean|-c)          DO_CLEAN=1 ;;
    -h|--help)
      echo "Usage: $0 [--run] [--keep-fallback] [--reset-nvram] [--clean]"
      exit 0
      ;;
    *) echo "Unknown option: $arg"; exit 1 ;;
  esac
done

if [[ $DO_CLEAN -eq 1 ]]; then
  rm -rf "$B"
  echo "cleaned $B"
  exit 0
fi

if [[ $RESET_NVRAM -eq 1 ]]; then
  rm -f "$B/vars.fd"
  echo "NVRAM reset"
fi

need() { command -v "$1" >/dev/null 2>&1 || { echo "error: missing tool '$1'"; exit 1; }; }
need clang
need python3
need lld
need qemu-img
need sfdisk
need mkfs.vfat
need mke2fs
need mmd
need mcopy

if [[ -d "${EDK2}/MdePkg/Include" ]]; then
  EDK_INC=(-isystem"$EDK2/MdePkg/Include" -isystem"$EDK2/MdePkg/Include/X64")
elif [[ -d /usr/include/efi ]]; then
  EDK_INC=(-isystem/usr/include/efi -isystem/usr/include/efi/x86_64)
else
  echo "warning: EDK2 headers not found – set EDK2=/path/to/edk2"
  EDK_INC=()
fi

mkdir -p "$B/obj" "$B/esp/EFI/BOOT" "$B/esp/EFI/magic" "$SYS32" "$B/rootfs/Program Files" "$B/rootfs/Program Files (x86)" "$B/rootfs/Program Files/Common Files" "$B/rootfs/Program Files (x86)/Common Files" "$B/rootfs/Users/Public" "$B/rootfs/Windows/System32"

COMMON=(
  --target=x86_64-unknown-windows
  -ffreestanding -mno-red-zone -fno-stack-protector -mno-stack-arg-probe
  -mgeneral-regs-only -O2 -Wall -Wno-unused-function
  -I"$ROOT/shared" -I"$ROOT/boot" -I"$B"
)

build_efi() {
  local name="$1" out="$2"
  shift 2
  clang "${COMMON[@]}" "$@" -fshort-wchar "${EDK_INC[@]}" \
    -c "$ROOT/boot/${name}.c" -o "$B/obj/${name}.o"
  lld -flavor link /subsystem:efi_application /entry:efi_main /nodefaultlib \
    "/out:$out" "$B/obj/${name}.o"
}

echo "==> UEFI stages"
build_efi bootx64 "$B/esp/EFI/BOOT/BOOTX64.EFI"
EXTRA=()
[[ $KEEP_FALLBACK -eq 1 ]] && EXTRA=(-DKEEP_FALLBACK)
build_efi bootuni "$B/esp/EFI/magic/bootuni.efi" "${EXTRA[@]}"
build_efi loadufi "$B/esp/EFI/magic/loadufi.efi"

echo "==> HAL + kernel"
clang "${COMMON[@]}" -DHAL_BUILD -c "$ROOT/hal/hal.c" -o "$B/obj/hal.o"
lld -flavor link /dll /noentry /subsystem:native /nodefaultlib \
  /base:0xFFFFFFFF81000000 /fixed:no \
  "/implib:$B/obj/hal.lib" "/out:$SYS32/hal.dll" "$B/obj/hal.o"

python3 "$ROOT/scripts/prepare-audio-sample.py" "$B/audio_sample.h"
clang "${COMMON[@]}" -c "$ROOT/kernel/kernel.c" -o "$B/obj/kernel.o"
lld -flavor link /subsystem:native /entry:kmain /nodefaultlib /dynamicbase \
  /base:0xFFFFFFFF80200000 /fixed:no \
  "/out:$SYS32/krnl.exe" "$B/obj/kernel.o" "$B/obj/hal.lib"


echo "==> user PE images"
USER_CFLAGS=(-target x86_64-windows-gnu -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone -mgeneral-regs-only -O2 -Wall -I"$ROOT/user")
set -e
clang "${USER_CFLAGS[@]}" -c "$ROOT/user/smss.c" -o "$B/obj/smss.o"
lld -flavor link /subsystem:native /entry:smss_main /nodefaultlib /fixed:no /base:0x40000000 \
  "/out:$SYS32/smss.exe" "$B/obj/smss.o"

clang "${USER_CFLAGS[@]}" -c "$ROOT/user/taskbar.c" -o "$B/obj/taskbar.o"
lld -flavor link /dll /noentry /subsystem:native /nodefaultlib /fixed:no /base:0x40200000 \
  /export:TaskbarDraw \
  "/out:$SYS32/taskbar.dll" "$B/obj/taskbar.o"

clang "${USER_CFLAGS[@]}" -c "$ROOT/user/kernel32.c" -o "$B/obj/kernel32.o"
lld -flavor link /dll /noentry /subsystem:native /nodefaultlib /fixed:no /base:0x41000000 \
  /export:ExitProcess /export:GetStdHandle /export:WriteFile /export:VirtualAlloc \
  /export:CreateFileA /export:ReadFile /export:CloseHandle /export:QueryDirectoryA /export:SetCurrentDirectoryA \
  /export:GetLastError /export:SetLastError /export:PeekInput /export:GetTickCount64 /export:Sleep /export:ExitWindowsEx \
  /export:CreateThread /export:WaitForSingleObject \
  "/implib:$B/obj/kernel32.lib" "/out:$SYS32/kernel32.dll" "$B/obj/kernel32.o"

clang "${USER_CFLAGS[@]}" -c "$ROOT/user/user32.c" -o "$B/obj/user32.o"
lld -flavor link /dll /noentry /subsystem:native /nodefaultlib /fixed:no /base:0x41100000 \
  /export:MessageBoxA /export:CreateWindowExA /export:ShowWindow /export:UpdateWindow \
  /export:TextOutA /export:InvalidateRect /export:GetMessageA /export:TranslateMessage \
  /export:DispatchMessageA /export:DestroyWindow /export:PostQuitMessage /export:ExitWindowsEx /export:DrawTaskbar \
  "/implib:$B/obj/user32.lib" "/out:$SYS32/user32.dll" "$B/obj/user32.o"
test -f "$SYS32/kernel32.dll" || { echo FATAL: kernel32.dll missing; exit 1; }
test -f "$SYS32/user32.dll" || { echo FATAL: user32.dll missing; exit 1; }
echo "Win32 DLLs:" && ls -la "$SYS32/kernel32.dll" "$SYS32/user32.dll"


clang "${USER_CFLAGS[@]}" -c "$ROOT/user/explorer.c" -o "$B/obj/explorer.o"
lld -flavor link /subsystem:native /entry:explorer_main /nodefaultlib /fixed:no /base:0x40000000 \
  "/out:$SYS32/explorer.exe" "$B/obj/explorer.o"

clang "${USER_CFLAGS[@]}" -c "$ROOT/user/fileexplorer.c" -o "$B/obj/fileexplorer.o"
lld -flavor link /subsystem:native /entry:mainCRTStartup /nodefaultlib /fixed:no /base:0x40100000 \
  "/out:$SYS32/fileexplorer.exe" "$B/obj/fileexplorer.o"

ls -la "$SYS32"/smss.exe "$SYS32"/explorer.exe "$SYS32"/fileexplorer.exe "$SYS32"/taskbar.dll
test -f "$SYS32/explorer.exe" || { echo FATAL: no explorer.exe; exit 1; }
test -f "$SYS32/taskbar.dll"  || { echo FATAL: no taskbar.dll; exit 1; }


echo "==> GPT disk image (FAT32 ESP + ext4)"
rm -f "$DISKIMG"
qemu-img create -f raw "$DISKIMG" 256M >/dev/null

sfdisk "$DISKIMG" <<SFDISK
label: gpt
unit: sectors
first-lba: 2048

start=2048, size=262144, type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B, name="MAGICESP"
start=264192, size=196608, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4, name="magic"
SFDISK

ESP_OFF=$((2048 * 512))
EXT_OFF=$((264192 * 512))
EXT_SZ=$((196608 * 512))
EXT_BLOCKS=$((EXT_SZ / 4096))

dd if=/dev/zero of="$B/esp.fat" bs=1M count=128 status=none
mkfs.vfat -F 32 -n MAGICESP "$B/esp.fat" >/dev/null
export MTOOLS_SKIP_CHECK=1
mmd -i "$B/esp.fat" ::/EFI
mmd -i "$B/esp.fat" ::/EFI/BOOT
mmd -i "$B/esp.fat" ::/EFI/magic
mcopy -i "$B/esp.fat" -s "$B/esp/EFI/BOOT/"* ::/EFI/BOOT/
mcopy -i "$B/esp.fat" -s "$B/esp/EFI/magic/"* ::/EFI/magic/
dd if="$B/esp.fat" of="$DISKIMG" bs=512 seek=2048 conv=notrunc status=none
rm -f "$B/esp.fat"

FEATS="^has_journal,^metadata_csum,^64bit,^resize_inode"
mke2fs -t ext4 -b 4096 -L magic -O "$FEATS" \
  -d "$B/rootfs" -E "offset=$EXT_OFF" "$DISKIMG" "$EXT_BLOCKS" >/dev/null 2>&1 || {
  echo "  (using temp image for ext4)"
  qemu-img create -f raw "$B/ext4.tmp" "$((EXT_SZ))" >/dev/null
  mke2fs -t ext4 -b 4096 -L magic -O "$FEATS" -d "$B/rootfs" "$B/ext4.tmp" "$EXT_BLOCKS" >/dev/null
  dd if="$B/ext4.tmp" of="$DISKIMG" bs=512 seek=264192 conv=notrunc status=none
  rm -f "$B/ext4.tmp"
}

echo "build ok → $DISKIMG"
if [[ $DO_RUN -eq 1 ]]; then
  exec "$ROOT/scripts/run.sh"
fi
