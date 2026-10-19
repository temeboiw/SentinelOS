
#!/usr/bin/env bash
# SentinelOS build script
#   ./scripts/build.sh
#   ./scripts/build.sh --run
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
    *)
      echo "Unknown option: $arg"
      exit 1
      ;;
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

need() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "error: missing tool '$1'"
    exit 1
  }
}

need clang
need lld
need qemu-img
need sfdisk
need mkfs.vfat
need mke2fs
need mmd
need mcopy
need dd

for src in \
  boot/bootx64.c \
  boot/bootuni.c \
  boot/loadufi.c \
  hal/hal.c \
  kernel/kernel.c \
  user/smss.c \
  user/taskbar.c \
  user/kernel32.c \
  user/user32.c \
  user/explorer.c \
  user/fileexplorer.c \
  user/cmd.c
do
  if [[ ! -f "$ROOT/$src" ]]; then
    echo "FATAL: required source file missing: $src"
    exit 1
  fi
done

if [[ -d "${EDK2}/MdePkg/Include" ]]; then
  EDK_INC=(
    "-isystem${EDK2}/MdePkg/Include"
    "-isystem${EDK2}/MdePkg/Include/X64"
  )
elif [[ -d /usr/include/efi ]]; then
  EDK_INC=(
    -isystem/usr/include/efi
    -isystem/usr/include/efi/x86_64
  )
else
  echo "warning: EDK2 headers not found; set EDK2=/path/to/edk2"
  EDK_INC=()
fi

mkdir -p \
  "$B/obj" \
  "$B/esp/EFI/BOOT" \
  "$B/esp/EFI/magic" \
  "$SYS32" \
  "$B/rootfs/Program Files" \
  "$B/rootfs/Program Files (x86)" \
  "$B/rootfs/Program Files/Common Files" \
  "$B/rootfs/Program Files (x86)/Common Files" \
  "$B/rootfs/Users/Public/Downloads" \
  "$B/rootfs/Program Files/Google/Chrome/Application" \
  "$B/rootfs/Program Files (x86)/Google/Chrome/Application" \
  "$B/rootfs/Windows/System32"

COMMON=(
  --target=x86_64-unknown-windows
  -ffreestanding
  -mno-red-zone
  -fno-stack-protector
  -mno-stack-arg-probe
  -mgeneral-regs-only
  -O2
  -Wall
  -Wno-unused-function
  -I"$ROOT/shared"
  -I"$ROOT/boot"
)

build_efi() {
  local name="$1"
  local out="$2"
  shift 2

  clang "${COMMON[@]}" "$@" \
    -fshort-wchar "${EDK_INC[@]}" \
    -c "$ROOT/boot/${name}.c" \
    -o "$B/obj/${name}.o"

  lld -flavor link \
    /subsystem:efi_application \
    /entry:efi_main \
    /nodefaultlib \
    "/out:$out" \
    "$B/obj/${name}.o"
}

echo "==> UEFI stages"

build_efi bootx64 "$B/esp/EFI/BOOT/BOOTX64.EFI"

EXTRA=()
if [[ $KEEP_FALLBACK -eq 1 ]]; then
  EXTRA=(-DKEEP_FALLBACK)
fi

build_efi bootuni "$B/esp/EFI/magic/bootuni.efi" "${EXTRA[@]}"
build_efi loadufi "$B/esp/EFI/magic/loadufi.efi"

echo "==> HAL + kernel"

clang "${COMMON[@]}" \
  -DHAL_BUILD \
  -c "$ROOT/hal/hal.c" \
  -o "$B/obj/hal.o"

lld -flavor link \
  /dll /noentry /subsystem:native /nodefaultlib \
  /base:0xFFFFFFFF81000000 /fixed:no \
  "/implib:$B/obj/hal.lib" \
  "/out:$SYS32/hal.dll" \
  "$B/obj/hal.o"

clang "${COMMON[@]}" \
  -c "$ROOT/kernel/kernel.c" \
  -o "$B/obj/kernel.o"

lld -flavor link \
  /subsystem:native /entry:kmain /nodefaultlib /dynamicbase \
  /base:0xFFFFFFFF80200000 /fixed:no \
  "/out:$SYS32/krnl.exe" \
  "$B/obj/kernel.o" \
  "$B/obj/hal.lib"

echo "==> User-mode PE images"

USER_CFLAGS=(
  -target x86_64-windows-gnu
  -ffreestanding
  -fno-stack-protector
  -fno-pic
  -mno-red-zone
  -mgeneral-regs-only
  -O2
  -Wall
  -I"$ROOT/user"
  -I"$ROOT/shared"
)

echo "  -> smss.exe"

clang "${USER_CFLAGS[@]}" \
  -c "$ROOT/user/smss.c" \
  -o "$B/obj/smss.o"

lld -flavor link \
  /subsystem:native /entry:smss_main /nodefaultlib \
  /fixed:no /base:0x40000000 \
  "/out:$SYS32/smss.exe" \
  "$B/obj/smss.o"

echo "  -> taskbar.dll"

clang "${USER_CFLAGS[@]}" \
  -c "$ROOT/user/taskbar.c" \
  -o "$B/obj/taskbar.o"

lld -flavor link \
  /dll /noentry /subsystem:native /nodefaultlib \
  /fixed:no /base:0x40200000 \
  /export:TaskbarDraw \
  "/out:$SYS32/taskbar.dll" \
  "$B/obj/taskbar.o"

echo "  -> kernel32.dll"

clang "${USER_CFLAGS[@]}" \
  -c "$ROOT/user/kernel32.c" \
  -o "$B/obj/kernel32.o"

lld -flavor link \
  /dll /noentry /subsystem:native /nodefaultlib \
  /fixed:no /base:0x41000000 \
  /export:ExitProcess \
  /export:GetStdHandle \
  /export:WriteFile \
  /export:VirtualAlloc \
  /export:CreateFileA \
  /export:ReadFile \
  /export:CloseHandle \
  /export:QueryDirectoryA \
  /export:SetCurrentDirectoryA \
  /export:GetLastError \
  /export:SetLastError \
  /export:PeekInput \
  /export:GetTickCount64 \
  /export:Sleep \
  /export:ExitWindowsEx \
  /export:CreateThread \
  /export:WaitForSingleObject \
  "/implib:$B/obj/kernel32.lib" \
  "/out:$SYS32/kernel32.dll" \
  "$B/obj/kernel32.o"

echo "  -> user32.dll"

clang "${USER_CFLAGS[@]}" \
  -c "$ROOT/user/user32.c" \
  -o "$B/obj/user32.o"

lld -flavor link \
  /dll /noentry /subsystem:native /nodefaultlib \
  /fixed:no /base:0x41100000 \
  /export:MessageBoxA \
  /export:CreateWindowExA \
  /export:ShowWindow \
  /export:UpdateWindow \
  /export:TextOutA \
  /export:InvalidateRect \
  /export:GetMessageA \
  /export:TranslateMessage \
  /export:DispatchMessageA \
  /export:DestroyWindow \
  /export:PostQuitMessage \
  /export:ExitWindowsEx \
  /export:DrawTaskbar \
  "/implib:$B/obj/user32.lib" \
  "/out:$SYS32/user32.dll" \
  "$B/obj/user32.o"

echo "  -> explorer.exe"

clang "${USER_CFLAGS[@]}" \
  -c "$ROOT/user/explorer.c" \
  -o "$B/obj/explorer.o"

lld -flavor link \
  /subsystem:native /entry:explorer_main /nodefaultlib \
  /fixed:no /base:0x40000000 \
  "/out:$SYS32/explorer.exe" \
  "$B/obj/explorer.o"

echo "  -> fileexplorer.exe"

clang "${USER_CFLAGS[@]}" \
  -c "$ROOT/user/fileexplorer.c" \
  -o "$B/obj/fileexplorer.o"

lld -flavor link \
  /subsystem:native /entry:mainCRTStartup /nodefaultlib \
  /fixed:no /base:0x40100000 \
  "/out:$SYS32/fileexplorer.exe" \
  "$B/obj/fileexplorer.o"

echo "  -> cmd.exe"

clang "${USER_CFLAGS[@]}" \
  -c "$ROOT/user/cmd.c" \
  -o "$B/obj/cmd.o"

lld -flavor link \
  /subsystem:native /entry:mainCRTStartup /nodefaultlib \
  /fixed:no /base:0x40300000 \
  "/out:$SYS32/cmd.exe" \
  "$B/obj/cmd.o"

echo "==> Checking required images"

for image in \
  hal.dll \
  krnl.exe \
  smss.exe \
  explorer.exe \
  fileexplorer.exe \
  taskbar.dll \
  kernel32.dll \
  user32.dll \
  cmd.exe
do
  if [[ ! -s "$SYS32/$image" ]]; then
    echo "FATAL: missing or empty image: $SYS32/$image"
    exit 1
  fi
done

ls -lh \
  "$SYS32/hal.dll" \
  "$SYS32/krnl.exe" \
  "$SYS32/smss.exe" \
  "$SYS32/explorer.exe" \
  "$SYS32/fileexplorer.exe" \
  "$SYS32/taskbar.dll" \
  "$SYS32/kernel32.dll" \
  "$SYS32/user32.dll" \
  "$SYS32/cmd.exe"

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

dd if=/dev/zero \
  of="$B/esp.fat" \
  bs=1M count=128 status=none

mkfs.vfat -F 32 -n MAGICESP "$B/esp.fat" >/dev/null

export MTOOLS_SKIP_CHECK=1

mmd -i "$B/esp.fat" ::/EFI
mmd -i "$B/esp.fat" ::/EFI/BOOT
mmd -i "$B/esp.fat" ::/EFI/magic

mcopy -i "$B/esp.fat" -s \
  "$B/esp/EFI/BOOT/"* ::/EFI/BOOT/

mcopy -i "$B/esp.fat" -s \
  "$B/esp/EFI/magic/"* ::/EFI/magic/

dd if="$B/esp.fat" \
  of="$DISKIMG" \
  bs=512 seek=2048 conv=notrunc status=none

rm -f "$B/esp.fat"

FEATS="^has_journal,^metadata_csum,^64bit,^resize_inode"

if ! mke2fs -t ext4 -b 4096 -L magic -O "$FEATS" \
  -d "$B/rootfs" \
  -E "offset=$EXT_OFF" \
  "$DISKIMG" "$EXT_BLOCKS" >/dev/null 2>&1
then
  echo "  (using temporary image for ext4)"

  qemu-img create -f raw "$B/ext4.tmp" "$EXT_SZ" >/dev/null

  mke2fs -t ext4 -b 4096 -L magic -O "$FEATS" \
    -d "$B/rootfs" \
    "$B/ext4.tmp" "$EXT_BLOCKS" >/dev/null

  dd if="$B/ext4.tmp" \
    of="$DISKIMG" \
    bs=512 seek=264192 conv=notrunc status=none

  rm -f "$B/ext4.tmp"
fi

echo "build ok -> $DISKIMG"

if [[ $DO_RUN -eq 1 ]]; then
  exec "$ROOT/scripts/run.sh"
fi
