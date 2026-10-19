
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
  user/cmd.c \
  user/gdi32.c \
  user/advapi32.c \
  user/shell32.c \
  user/ole32.c \
  user/hello.cpp
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
  /export:GetCurrentProcessId /export:GetCurrentThreadId /export:GetCurrentProcess /export:GetCurrentThread \
  /export:IsDebuggerPresent /export:QueryPerformanceCounter /export:QueryPerformanceFrequency \
  /export:GetSystemTimeAsFileTime /export:GetCurrentDirectoryA /export:GetWindowsDirectoryA \
  /export:GetSystemDirectoryA /export:GetTempPathA /export:GetModuleFileNameA /export:GetModuleHandleA \
  /export:GetModuleHandleW /export:GetProcessAffinityMask /export:GetFileAttributesA /export:CreateDirectoryA \
  /export:DeleteFileA /export:MoveFileA /export:GetComputerNameA /export:GetEnvironmentVariableA \
  /export:SetEnvironmentVariableA /export:FlushFileBuffers /export:GetExitCodeProcess /export:TerminateProcess \
  /export:ProcessIdToSessionId \
  /export:GetTickCount /export:lstrlenA /export:lstrcmpA /export:lstrcmpiA \\
  /export:lstrcpyA /export:lstrcpynA /export:GetVersion /export:GetVersionExA \\
  /export:CreateProcessA /export:CreateProcessW /export:GetProcessId \\
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

echo "  -> hello.exe (from hello.cpp)"

clang "${USER_CFLAGS[@]}" -x c++ \
  -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit \
  -c "$ROOT/user/hello.cpp" \
  -o "$B/obj/hello.o"

lld -flavor link \
  /subsystem:native /entry:hello_main /nodefaultlib \
  /fixed:no /base:0x40400000 \
  "/out:$SYS32/hello.exe" \
  "$B/obj/hello.o"

echo "  -> gdi32.dll"
clang "${USER_CFLAGS[@]}" -c "$ROOT/user/gdi32.c" -o "$B/obj/gdi32.o"
lld -flavor link /dll /noentry /subsystem:native /nodefaultlib /fixed:no /base:0x41200000 \
  /export:CreateCompatibleDC /export:DeleteDC /export:SelectObject /export:DeleteObject \
  /export:CreateSolidBrush /export:CreatePen /export:CreateFontA /export:MoveToEx /export:LineTo \
  /export:Rectangle /export:FillRect /export:PatBlt /export:BitBlt /export:TextOutA \
  /export:SetBkMode /export:SetTextColor /export:SetBkColor /export:GetTextExtentPoint32A \
  /export:SaveDC /export:RestoreDC /export:GetDeviceCaps /export:CreateDCA \
  "/out:$SYS32/gdi32.dll" "$B/obj/gdi32.o"

echo "  -> advapi32.dll"
clang "${USER_CFLAGS[@]}" -c "$ROOT/user/advapi32.c" -o "$B/obj/advapi32.o"
lld -flavor link /dll /noentry /subsystem:native /nodefaultlib /fixed:no /base:0x41300000 \
  /export:RegOpenKeyExA /export:RegCreateKeyExA /export:RegQueryValueExA /export:RegSetValueExA \
  /export:RegCloseKey /export:RegDeleteKeyA /export:RegDeleteValueA /export:RegEnumKeyExA \
  /export:RegEnumValueA /export:AdjustTokenPrivileges /export:OpenProcessToken \
  /export:LookupPrivilegeValueA /export:GetUserNameA /export:GetComputerNameA \
  "/out:$SYS32/advapi32.dll" "$B/obj/advapi32.o"

echo "  -> shell32.dll"
clang "${USER_CFLAGS[@]}" -c "$ROOT/user/shell32.c" -o "$B/obj/shell32.o"
lld -flavor link /dll /noentry /subsystem:native /nodefaultlib /fixed:no /base:0x41400000 \
  /export:ShellExecuteA /export:ShellExecuteExA /export:SHGetFolderPathA /export:CommandLineToArgvA \
  "/out:$SYS32/shell32.dll" "$B/obj/shell32.o"

echo "  -> ole32.dll"
clang "${USER_CFLAGS[@]}" -c "$ROOT/user/ole32.c" -o "$B/obj/ole32.o"
lld -flavor link /dll /noentry /subsystem:native /nodefaultlib /fixed:no /base:0x41500000 \
  /export:CoInitialize /export:CoInitializeEx /export:CoUninitialize /export:CoCreateInstance \
  /export:CoGetClassObject /export:CoInitializeSecurity \
  "/out:$SYS32/ole32.dll" "$B/obj/ole32.o"

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
  gdi32.dll \
  advapi32.dll \
  shell32.dll \
  ole32.dll \
  cmd.exe \
  hello.exe
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
  "$SYS32/gdi32.dll" \
  "$SYS32/advapi32.dll" \
  "$SYS32/shell32.dll" \
  "$SYS32/ole32.dll" \
  "$SYS32/cmd.exe" \
  "$SYS32/hello.exe"

# Stage the freshly built user-facing system files into the Windows path too.
# Keep magic/System32 intact: the UEFI loader uses it as its boot-module source.
echo "==> Staging updated system files into C:\\Windows\\System32"
for image in \
  hal.dll krnl.exe smss.exe explorer.exe fileexplorer.exe taskbar.dll \
  kernel32.dll user32.dll gdi32.dll advapi32.dll shell32.dll ole32.dll cmd.exe hello.exe
do
  install -m 0644 "$SYS32/$image" "$B/rootfs/Windows/System32/$image"
done

# Show the exact Explorer and user32 binaries that will be included in the image.
ls -lh \
  "$B/rootfs/Windows/System32/fileexplorer.exe" \
  "$B/rootfs/Windows/System32/user32.dll"

echo "==> GPT disk image (FAT32 ESP + ext4)"

rm -f "$DISKIMG"
qemu-img create -f raw "$DISKIMG" 5G >/dev/null

# The GPT reserves its final 33 sectors for backup metadata. Compute the
# second partition from the actual 5 GiB image size instead of overrunning it.
DISK_BYTES=$((5 * 1024 * 1024 * 1024))
DISK_SECTORS=$((DISK_BYTES / 512))
ESP_START=2048
ESP_SECTORS=262144
EXT_START=$((ESP_START + ESP_SECTORS))
GPT_RESERVED_END=33
EXT_SECTORS=$((DISK_SECTORS - GPT_RESERVED_END - EXT_START))
if (( EXT_SECTORS <= 0 )); then
  echo "FATAL: disk image is too small for the requested partitions"
  exit 1
fi

sfdisk "$DISKIMG" <<SFDISK
label: gpt
unit: sectors
first-lba: 2048

start=$ESP_START, size=$ESP_SECTORS, type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B, name="MAGICESP"
start=$EXT_START, size=$EXT_SECTORS, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4, name="magic"
SFDISK

ESP_OFF=$((ESP_START * 512))
EXT_OFF=$((EXT_START * 512))
EXT_SZ=$((EXT_SECTORS * 512))
EXT_BLOCKS=$((EXT_SZ / 4096))
# Leave a little room for ext4 metadata; disable the default 5% reserved blocks
# because this is a fixed-size OS image, not a multi-user server filesystem.
if (( EXT_BLOCKS < 1024 )); then
  echo "FATAL: ext4 partition is too small"
  exit 1
fi
ROOTFS_BYTES=$(du -sb "$B/rootfs" | awk '{print $1}')
if (( ROOTFS_BYTES > EXT_SZ - 64 * 1024 * 1024 )); then
  echo "FATAL: rootfs contents need ${ROOTFS_BYTES} bytes, but the 5-GiB disk only"
  echo "       has ${EXT_SZ} bytes for ext4. Remove large files or increase disk size."
  exit 1
fi

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

if ! mke2fs -t ext4 -b 4096 -m 0 -L magic -O "$FEATS" \
  -d "$B/rootfs" \
  -E "offset=$EXT_OFF" \
  "$DISKIMG" "$EXT_BLOCKS" >/dev/null 2>&1
then
  echo "  (using temporary image for ext4)"

  qemu-img create -f raw "$B/ext4.tmp" "$EXT_SZ" >/dev/null

  mke2fs -t ext4 -b 4096 -m 0 -L magic -O "$FEATS" \
    -d "$B/rootfs" \
    "$B/ext4.tmp" "$EXT_BLOCKS" >/dev/null

  dd if="$B/ext4.tmp" \
    of="$DISKIMG" \
    bs=512 seek="$EXT_START" conv=notrunc status=none

  rm -f "$B/ext4.tmp"
fi

echo "build ok -> $DISKIMG"

if [[ $DO_RUN -eq 1 ]]; then
  exec "$ROOT/scripts/run.sh"
fi
