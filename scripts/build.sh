#!/usr/bin/env bash
# SentinelOS build script
#   ./scripts/build.sh
#   ./scripts/build.sh --run
#   ./scripts/build.sh --keep-fallback
#   ./scripts/build.sh --reset-nvram
#   ./scripts/build.sh --clean

set -euo pipefail

# Use every online CPU by default. Override with JOBS=2 for low-memory hosts.
if [[ -n "${JOBS:-}" ]]; then
  BUILD_JOBS="$JOBS"
else
  BUILD_JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc 2>/dev/null || echo 1)"
fi
[[ "$BUILD_JOBS" =~ ^[1-9][0-9]*$ ]] || { echo "error: JOBS must be a positive integer" >&2; exit 1; }
echo "==> Build parallelism: $BUILD_JOBS job(s)"

# Run independent commands in parallel, then fail if any command failed.
run_parallel() {
  local -a pids=()
  local cmd pid failed=0
  for cmd in "$@"; do
    eval "$cmd" &
    pids+=("$!")
    if (( ${#pids[@]} >= BUILD_JOBS )); then
      for pid in "${pids[@]}"; do wait "$pid" || failed=1; done
      pids=()
      (( failed == 0 )) || return 1
    fi
  done
  for pid in "${pids[@]}"; do wait "$pid" || failed=1; done
  return "$failed"
}

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

# Resolve real LLVM compiler binaries. Some Android/proot environments expose
# a wrapper named "clang" that forwards Clang's private -cc1 argument to proot,
# which fails with: "proot error: unknown option '-cc1'".
find_llvm_tool() {
  local tool="$1" candidate resolved description path_name
  local -a candidates=()
  if [[ -n "${!tool:-}" ]]; then candidates+=("${!tool}"); fi
  case "$tool" in
    CLANG_BIN) path_name=clang; candidates+=(/usr/local/swift/usr/bin/clang /usr/lib/llvm-*/bin/clang /usr/bin/clang-[0-9]* /usr/bin/clang) ;;
    CLANGXX_BIN) path_name=clang++; candidates+=(/usr/local/swift/usr/bin/clang++ /usr/lib/llvm-*/bin/clang++ /usr/bin/clang++-[0-9]* /usr/bin/clang++) ;;
    LLD_BIN) path_name=lld; candidates+=(/usr/local/swift/usr/bin/lld /usr/lib/llvm-*/bin/lld /usr/bin/lld-[0-9]* /usr/bin/lld) ;;
  esac
  candidates+=("$(command -v "$path_name" 2>/dev/null || true)")
  for candidate in "${candidates[@]}"; do
    [[ -n "$candidate" && -x "$candidate" ]] || continue
    resolved="$(readlink -f "$candidate" 2>/dev/null || printf '%s' "$candidate")"
    description="$(file -b "$resolved" 2>/dev/null || true)"
    # Never run a shell wrapper or proot binary as if it were the compiler.
    [[ "$description" == *"script"* || "$description" == *"text executable"* ]] && continue
    [[ "$(basename "$resolved")" == "proot" ]] && continue
    printf '%s\n' "$resolved"
    return 0
  done
  echo "error: could not find a real LLVM executable for $tool; set the corresponding *_BIN variable" >&2
  return 1
}

CLANG_BIN="$(find_llvm_tool CLANG_BIN)"
CLANGXX_BIN="$(find_llvm_tool CLANGXX_BIN)"
LLD_BIN="$(find_llvm_tool LLD_BIN)"
# Termux/proot workaround: ask the real Clang driver for its job plan,
# then execute only the cc1 compile command. Do not use eval on arbitrary
# driver output and never run the host linker from this workaround.
run_cc() {
  local drv="$1" arg compile_only=0
  shift

  for arg in "$@"; do
    case "$arg" in
      -c|-S|-E) compile_only=1 ;;
    esac
  done

  if (( compile_only == 0 )); then
    echo "error: internal PE Clang wrapper received a link action; use the host compiler for native executables" >&2
    return 2
  fi

  # Termux exports this marker inside proot. Clang then mistakenly selects
  # the proot executable as its cc1 frontend and fails with "unknown option -cc1".
  # Remove it for the compiler process and its children so Clang launches its
  # actual frontend instead. Keep the user's shell environment unchanged.
  env -u TERMUX_EXEC__PROC_SELF_EXE "$drv" "$@"
}

clang()   { run_cc "$CLANG_BIN" "$@"; }
clang++() { run_cc "$CLANGXX_BIN" "$@"; }
lld()     { "$LLD_BIN" "$@"; }

# Catch broken compiler wrappers before starting parallel EFI builds.
PROBE_SRC="$(mktemp --suffix=.c)"
printf 'int sentinel_compiler_probe;\n' > "$PROBE_SRC"
clang --target=x86_64-unknown-windows -ffreestanding -c -o /dev/null "$PROBE_SRC"
rm -f "$PROBE_SRC"
echo "  -> clang: $CLANG_BIN"
echo "  -> clang++: $CLANGXX_BIN"
echo "  -> lld: $LLD_BIN"
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
  kernel/pci_network.h \
  kernel/e1000.h \
  kernel/netstack.h \
  kernel/smp_trampoline.S \
  kernel/gfx.h \
  user/smss.c \
  user/bootvid.c \
  user/sentlogon.c \
  user/taskbar.c \
  user/kernel32.c \
  user/ws2_32.c \
  user/ucrt_runtime.c \
  user/user32.c \
  user/soft3d.c \
  user/font_ttf.h \
  user/explorer.c \
  user/fileexplorer.c \
  user/cmd.c \
  user/taskmgr.c \
  user/msiexec.c \
  UserTestApps/game.cpp \
  UserTestApps/maze.cpp
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

# Windows-like system tree: keep the volume root clean and put OS files in /magic.
# Recreate staging so legacy root-level DLL copies cannot survive between builds.
rm -rf "$B/rootfs"
mkdir -p \
  "$B/obj" \
  "$B/esp/EFI/BOOT" \
  "$B/esp/EFI/magic" \
  "$SYS32/drivers" \
  "$B/rootfs/magic/Program Files" \
  "$B/rootfs/magic/Users/Public"

# Lato is the preferred humanist sans for the Vista/Frutiger Aero era.
# If it is not installed, use a neutral sans-serif TrueType substitute.
# A vendored assets/fonts/Lato-Regular.ttf always takes priority.
FONT_SRC="$ROOT/assets/fonts/Lato-Regular.ttf"
if [[ ! -s "$FONT_SRC" ]]; then
  for candidate in \
    /usr/share/fonts/truetype/lato/Lato-Regular.ttf \
    /usr/share/fonts/truetype/Lato-Regular.ttf \
    /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
    /usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf \
    /usr/share/fonts/truetype/noto/NotoSans-Regular.ttf
  do
    if [[ -s "$candidate" ]]; then FONT_SRC="$candidate"; break; fi
  done
fi
if [[ -s "$FONT_SRC" ]]; then
  cp "$FONT_SRC" "$SYS32/Lato-Regular.ttf"
  echo "  -> Lato TrueType font ($(wc -c < "$FONT_SRC") bytes)"
else
  echo "warning: Lato-Regular.ttf not found; USER32 will use its fallback glyphs"
fi

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

EXTRA=()
if [[ $KEEP_FALLBACK -eq 1 ]]; then
  EXTRA=(-DKEEP_FALLBACK)
fi

# These EFI images are independent, so compile/link them concurrently.
run_parallel \
  'build_efi bootx64 "$B/esp/EFI/BOOT/BOOTX64.EFI"' \
  'build_efi bootuni "$B/esp/EFI/magic/bootuni.efi" "${EXTRA[@]}"' \
  'build_efi loadufi "$B/esp/EFI/magic/loadufi.efi"'

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

clang "${COMMON[@]}" \
  -c "$ROOT/kernel/smp_trampoline.S" \
  -o "$B/obj/smp_trampoline.o"

lld -flavor link \
  /subsystem:native /entry:kmain /nodefaultlib /dynamicbase \
  /base:0xFFFFFFFF80200000 /fixed:no \
  "/out:$SYS32/krnl.exe" \
  "$B/obj/kernel.o" \
  "$B/obj/smp_trampoline.o" \
  "$B/obj/hal.lib"

echo "==> User-mode PE images"

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

echo "  -> vulkan.dll (CPU Vulkan core)"

clang "${USER_CFLAGS[@]}" \
  -c "$ROOT/user/vulkan.c" \
  -o "$B/obj/vulkan.o"

echo "  -> CPU software rasterizer primitives"
# The kernel/user runtime is built with general registers only, but this
# freestanding rasterizer intentionally uses x86-64 SSE2 floating point.
SOFTPIPE_CFLAGS=()
for flag in "${USER_CFLAGS[@]}"; do
  [[ "$flag" == "-mgeneral-regs-only" ]] || SOFTPIPE_CFLAGS+=("$flag")
done
clang "${SOFTPIPE_CFLAGS[@]}" \
  -c "$ROOT/user/vk_softpipe.c" \
  -o "$B/obj/vk_softpipe.o"

lld -flavor link \
  /dll /noentry /subsystem:native /nodefaultlib \
  /fixed:no /dynamicbase /base:0x60000000 \
  /export:vkEnumerateInstanceVersion \
  /export:vkEnumerateInstanceExtensionProperties \
  /export:vkEnumerateInstanceLayerProperties \
  /export:vkCreateInstance \
  /export:vkDestroyInstance \
  /export:vkEnumeratePhysicalDevices \
  /export:vkGetPhysicalDeviceProperties \
  /export:vkGetPhysicalDeviceFeatures \
  /export:vkGetPhysicalDeviceFormatProperties \
  /export:vkGetPhysicalDeviceImageFormatProperties \
  /export:vkGetPhysicalDeviceQueueFamilyProperties \
  /export:vkGetPhysicalDeviceMemoryProperties \
  /export:vkCreateDevice \
  /export:vkDestroyDevice \
  /export:vkGetDeviceQueue \
  /export:vkDeviceWaitIdle \
  /export:vkQueueWaitIdle \
  /export:vkCreateBuffer \
  /export:vkCreateImage \
  /export:vkDestroyImage \
  /export:vkGetImageMemoryRequirements \
  /export:vkBindImageMemory \
  /export:vkCmdClearColorImage \
  /export:vkCmdPipelineBarrier \
  /export:vkDestroyBuffer \
  /export:vkGetBufferMemoryRequirements \
  /export:vkAllocateMemory \
  /export:vkFreeMemory \
  /export:vkBindBufferMemory \
  /export:vkMapMemory \
  /export:vkUnmapMemory \
  /export:vkFlushMappedMemoryRanges \
  /export:vkInvalidateMappedMemoryRanges \
  /export:vkCreateCommandPool \
  /export:vkDestroyCommandPool \
  /export:vkAllocateCommandBuffers \
  /export:vkFreeCommandBuffers \
  /export:vkBeginCommandBuffer \
  /export:vkEndCommandBuffer \
  /export:vkResetCommandBuffer \
  /export:vkCmdFillBuffer \
  /export:vkCmdCopyBuffer \
  /export:vkCmdCopyBufferToImage \
  /export:vkCmdCopyImageToBuffer \
  /export:vkQueueSubmit \
  /export:vkCreateFence \
  /export:vkDestroyFence \
  /export:vkGetFenceStatus \
  /export:vkWaitForFences \
  /export:vkResetFences \
  /export:vkCreateSemaphore \
  /export:vkDestroySemaphore \
  /export:vkCreateDescriptorSetLayout \
  /export:vkDestroyDescriptorSetLayout \
  /export:vkCreatePipelineLayout \
  /export:vkDestroyPipelineLayout \
  /export:vkCreateDescriptorPool \
  /export:vkDestroyDescriptorPool \
  /export:vkResetDescriptorPool \
  /export:vkAllocateDescriptorSets \
  /export:vkFreeDescriptorSets \
  /export:vkUpdateDescriptorSets \
  /export:vkCmdBindDescriptorSets \
  /export:vkCreateShaderModule \
  /export:vkDestroyShaderModule \
  /export:vkGetDeviceProcAddr \
  /export:vkGetInstanceProcAddr \
  /export:SentinelVulkanLoaderStatus \
  /export:SentinelVulkanGpuStatus \
  /export:SentinelVulkanSubmitVirGLStream \
  /export:SentinelVulkanUploadVirGLBuffer \
  /export:SentinelVulkanPresentVirGL \
  /export:SentinelVulkanCmdDrawTriangle \
  "/out:$SYS32/vulkan.dll" \
  "$B/obj/vulkan.o" \
  "$B/obj/vk_softpipe.o"
cp "$SYS32/vulkan.dll" "$SYS32/vulkan-1.dll"

echo "  -> Vulkan CPU runtime regression tests"
# Compile the host-side test against the actual implementation sources. This
# exercises the software runtime independently of the freestanding PE build.
# This is a native host test, not a PE object. Use the host C compiler
# directly; the Clang wrapper above intentionally executes compile-only cc1
# jobs and must not reinterpret GNU ld flags as compiler arguments.
HOST_CC="${HOST_CC:-cc}"
need "$HOST_CC"
# Termux proot exports TERMUX_EXEC__PROC_SELF_EXE, which can make Clang
# mistake the proot executable for its internal -cc1 frontend. The host test
# compiler may itself be a Clang wrapper, so remove the marker here too.
env -u TERMUX_EXEC__PROC_SELF_EXE "$HOST_CC" -O2 -Wall \
  "$ROOT/tests/vulkan_test.c" \
  -o "$B/vulkan_test"
"$B/vulkan_test"


echo "  -> bootvid.exe"

clang "${USER_CFLAGS[@]}" \
  -c "$ROOT/user/bootvid.c" \
  -o "$B/obj/bootvid.o"

lld -flavor link \
  /subsystem:native /entry:bootvid_main /nodefaultlib \
  /fixed:no /base:0x40600000 \
  "/out:$SYS32/bootvid.exe" \
  "$B/obj/bootvid.o"

echo "  -> sentlogon.exe"

clang "${USER_CFLAGS[@]}" \
  -c "$ROOT/user/sentlogon.c" \
  -o "$B/obj/sentlogon.o"

lld -flavor link \
  /subsystem:native /entry:sentlogon_main /nodefaultlib \
  /fixed:no /base:0x40500000 \
  "/out:$SYS32/sentlogon.exe" \
  "$B/obj/sentlogon.o"

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
  "/implib:$B/obj/kernel32.lib" \
  /export:ExitProcess \
  /export:GetStdHandle \
  /export:SetStdHandle \
  /export:LoadLibraryExW \
  /export:FindFirstFileExW \
  /export:WriteFile \
  /export:VirtualAlloc \
  /export:CreateFileA \
  /export:ReadFile \
  /export:CloseHandle \
  /export:QueryDirectoryA \
  /export:FindFirstFileA \
  /export:FindNextFileA \
  /export:FindClose \
  /export:FindFirstFileW \
  /export:FindNextFileW \
  /export:SetCurrentDirectoryA \
  /export:GetLastError \
  /export:SetLastError \
  /export:GetTickCount64 \
  /export:Sleep \
  /export:PeekInput \
  /export:ExitWindowsEx \
  /export:CreateThread \
  /export:WaitForSingleObject \
  /export:GetCurrentProcessId \
  /export:GetCurrentThreadId \
  /export:GetCurrentProcess \
  /export:GetCurrentThread \
  /export:GetModuleHandleA \
  /export:GetCommandLineA \
  /export:GetModuleFileNameA \
  /export:GetProcAddress \
  /export:LoadLibraryA \
  /export:FreeLibrary \
  /export:VirtualFree \
  /export:VirtualProtect \
  /export:VirtualQuery \
  /export:GetCurrentDirectoryA \
  /export:CreateDirectoryA \
  /export:RemoveDirectoryA \
  /export:DeleteFileA \
  /export:MoveFileA \
  /export:CopyFileA \
  /export:GetFileAttributesA \
  /export:SetFileAttributesA \
  /export:GetFileSize \
  /export:SetFilePointer \
  /export:FlushFileBuffers \
  /export:GetTempPathA \
  /export:GetFullPathNameA \
  /export:GetEnvironmentVariableA \
  /export:SetEnvironmentVariableA \
  /export:GetComputerNameA \
  /export:GetUserProfileDirectoryA \
  /export:GetUserProfileDirectoryW \
  /export:GetSystemInfo \
  /export:GetVersion \
  /export:QueryPerformanceCounter \
  /export:QueryPerformanceFrequency \
  /export:GetTickCount \
  /export:GetExitCodeProcess \
  /export:TerminateProcess \
  /export:GetProcessHeap \
  /export:HeapAlloc \
  /export:HeapFree \
  /export:HeapReAlloc \
  /export:HeapSize \
  /export:LocalAlloc \
  /export:LocalFree \
  /export:GlobalAlloc \
  /export:GlobalFree \
  /export:GetConsoleMode \
  /export:SetConsoleMode \
  /export:ReadConsoleA \
  /export:WriteConsoleA \
  /export:GetSystemTime \
  /export:GetLocalTime \
  /export:CreateEventA \
  /export:SetEvent \
  /export:ResetEvent \
  /export:CreateMutexA \
  /export:ReleaseMutex \
  /export:CreateSemaphoreA \
  /export:ReleaseSemaphore \
  /export:WaitForMultipleObjects \
  /export:GetFileType \
  /export:GetCommandLineW \
  /export:GetModuleHandleW \
  /export:GetModuleFileNameW \
  /export:CreateFileW \
  /export:DeleteFileW \
  /export:MoveFileW \
  /export:CopyFileW \
  /export:CreateDirectoryW \
  /export:RemoveDirectoryW \
  /export:GetCurrentDirectoryW \
  /export:SetCurrentDirectoryW \
  /export:GetFullPathNameW \
  /export:GetTempPathW \
  /export:GetEnvironmentVariableW \
  /export:SetEnvironmentVariableW \
  /export:GetFileAttributesW \
  /export:SetFileAttributesW \
  /export:GetTempFileNameA \
  /export:GetTempFileNameW \
  /export:GetShortPathNameA \
  /export:GetLongPathNameA \
  /export:GetDiskFreeSpaceA \
  /export:GetDiskFreeSpaceExA \
  /export:GetDriveTypeA \
  /export:GetLogicalDrives \
  /export:GetLogicalDriveStringsA \
  /export:GetVolumeInformationA \
  /export:GetFileTime \
  /export:SetFileTime \
  /export:GetSystemTimeAsFileTime \
  /export:SystemTimeToFileTime \
  /export:FileTimeToSystemTime \
  /export:CompareFileTime \
  /export:GetFileAttributesExA \
  /export:GetFileInformationByHandle \
  /export:SetEndOfFile \
  /export:GetOverlappedResult \
  /export:CancelIo \
  /export:CreatePipe \
  /export:PeekNamedPipe \
  /export:SetHandleInformation \
  /export:GetHandleInformation \
  /export:DuplicateHandle \
  /export:CreateProcessA \
  /export:GetStartupInfoA \
  /export:GetEnvironmentStringsA \
  /export:FreeEnvironmentStringsA \
  /export:GetProcessTimes \
  /export:GetThreadTimes \
  /export:GetPriorityClass \
  /export:SetPriorityClass \
  /export:GetThreadPriority \
  /export:SetThreadPriority \
  /export:GetCurrentProcessorNumber \
  /export:GetNativeSystemInfo \
  /export:IsProcessorFeaturePresent \
  /export:GetSystemDirectoryA \
  /export:GetWindowsDirectoryA \
  /export:GetUserDefaultLCID \
  /export:GetACP \
  /export:GetOEMCP \
  /export:GetConsoleCP \
  /export:GetConsoleOutputCP \
  /export:SetConsoleCP \
  /export:SetConsoleOutputCP \
  /export:GetFileSizeEx \
  /export:CreateFileMappingA \
  /export:CreateFileMappingW \
  /export:MapViewOfFile \
  /export:UnmapViewOfFile \
  /export:FlushViewOfFile \
  /export:GetFileInformationByHandleEx \
  /export:GetFinalPathNameByHandleA \
  /export:GetFinalPathNameByHandleW \
  /export:GetVolumePathNameA \
  /export:GetVolumePathNameW \
  /export:GetVolumePathNamesForVolumeNameA \
  /export:GetVolumePathNamesForVolumeNameW \
  /export:GetDriveTypeW \
  /export:GetLogicalDriveStringsW \
  /export:GetVolumeInformationW \
  /export:GetDiskFreeSpaceW \
  /export:GetDiskFreeSpaceExW \
  /export:GetFileAttributesExW \
  /export:GetTempPath2A \
  /export:GetTempPath2W \
  /export:GetShortPathNameW \
  /export:GetLongPathNameW \
  /export:GetSystemDirectoryW \
  /export:GetWindowsDirectoryW \
  /export:GetComputerNameW \
  /export:GetUserNameA \
  /export:GetUserNameW \
  /export:GetComputerNameExA \
  /export:GetComputerNameExW \
  /export:GetEnvironmentStringsW \
  /export:FreeEnvironmentStringsW \
  /export:ExpandEnvironmentStringsA \
  /export:ExpandEnvironmentStringsW \
  /export:GetModuleHandleExA \
  /export:GetModuleHandleExW \
  /export:GetStartupInfoW \
  /export:GetProcessId \
  /export:GetThreadId \
  /export:GetProcessIdOfThread \
  /export:OpenProcess \
  /export:OpenThread \
  /export:GetCurrentProcessToken \
  /export:GetCurrentThreadToken \
  /export:GetProcessVersion \
  /export:GetProcessShutdownParameters \
  /export:SetProcessShutdownParameters \
  /export:SetErrorMode \
  /export:GetErrorMode \
  /export:GetSystemTimeAdjustment \
  /export:GetSystemTimePreciseAsFileTime \
  /export:GetThreadLocale \
  /export:SetThreadLocale \
  /export:GetUserDefaultLangID \
  /export:GetThreadUILanguage \
  /export:GetUserDefaultUILanguage \
  /export:GetSystemDefaultUILanguage \
  /export:GetSystemDefaultLangID \
  /export:GetSystemDefaultLCID \
  /export:GetConsoleTitleA \
  /export:GetConsoleTitleW \
  /export:SetConsoleTitleW \
  /export:GetConsoleScreenBufferInfo \
  /export:GetConsoleScreenBufferInfoEx \
  /export:SetConsoleScreenBufferInfoEx \
  /export:GetConsoleCursorInfo \
  /export:SetConsoleCursorInfo \
  /export:GetConsoleWindow \
  /export:GetConsoleProcessList \
  /export:GetNumberOfConsoleInputEvents \
  /export:GetNumberOfConsoleMouseButtons \
  /export:GetLargestConsoleWindowSize \
  /export:GetConsoleFontSize \
  /export:GetCurrentConsoleFont \
  /export:GetCurrentConsoleFontEx \
  /export:SetCurrentConsoleFontEx \
  /export:GetConsoleHistoryInfo \
  /export:SetConsoleHistoryInfo \
  /export:GetConsoleCommandHistoryA \
  /export:GetConsoleCommandHistoryW \
  /export:GetConsoleAliasesA \
  /export:GetConsoleAliasesW \
  /export:GetConsoleAliasesLengthA \
  /export:GetConsoleAliasesLengthW \
  /export:GetConsoleAliasA \
  /export:GetConsoleAliasW \
  /export:GetConsoleAliasExesA \
  /export:GetConsoleAliasExesW \
  /export:GetConsoleAliasExesLengthA \
  /export:GetConsoleAliasExesLengthW \
  /export:AddConsoleAliasA /export:AddConsoleAliasW /export:GetConsoleInputExeNameA \
  /export:GetConsoleInputExeNameW \
  /export:GetConsoleOutputExeNameA \
  /export:GetConsoleOutputExeNameW \
  /export:CreateHardLinkA \
  /export:CreateHardLinkW \
  /export:CreateSymbolicLinkA \
  /export:CreateSymbolicLinkW \
  /export:SetFileValidData \
  /export:LockFile \
  /export:UnlockFile \
  /export:LockFileEx \
  /export:UnlockFileEx \
  /export:GetCompressedFileSizeA \
  /export:GetCompressedFileSizeW \
  /export:SearchPathA \
  /export:SearchPathW \
  /export:GetModuleFileNameExA \
  /export:GetModuleFileNameExW \
  /export:HeapValidate \
  /export:HeapCompact \
  /export:GetProcessHeaps \
  /export:LocalReAlloc \
  /export:LocalSize \
  /export:GlobalReAlloc \
  /export:GlobalSize \
  /export:GlobalLock \
  /export:GlobalUnlock \
  /export:GlobalHandle \
  /export:GetBinaryTypeA \
  /export:GetBinaryTypeW \
  /export:lstrlenA \
  /export:lstrlenW \
  /export:lstrcmpA \
  /export:lstrcmpiA \
  /export:lstrcmpW \
  /export:lstrcmpiW \
  /export:lstrcpyA \
  /export:lstrcpynA \
  /export:lstrcatA \
  /export:CharLowerA \
  /export:CharUpperA \
  /export:CharLowerBuffA \
  /export:CharUpperBuffA \
  /export:CharLowerW \
  /export:CharUpperW \
  /export:CharLowerBuffW \
  /export:CharUpperBuffW \
  /export:IsCharAlphaA \
  /export:IsCharAlphaNumericA \
  /export:IsCharUpperA \
  /export:IsCharLowerA \
  /export:IsCharAlphaW \
  /export:IsCharAlphaNumericW \
  /export:IsCharUpperW \
  /export:IsCharLowerW \
  /export:CharNextA \
  /export:CharPrevA \
  /export:CharNextW \
  /export:CharPrevW \
  /export:EncodePointer \
  /export:DecodePointer \
  /export:IsBadReadPtr \
  /export:IsBadWritePtr \
  /export:IsBadCodePtr \
  /export:IsBadStringPtrA \
  /export:IsBadStringPtrW \
  /export:GetStringTypeA \
  /export:GetStringTypeW \
  /export:CompareStringA \
  /export:CompareStringW \
  /export:lstrcpyW \
  /export:lstrcpynW \
  /export:lstrcatW \
  /export:CharToOemA \
  /export:OemToCharA \
  /export:CharToOemW \
  /export:OemToCharW \
  /export:CharToOemBuffA \
  /export:OemToCharBuffA \
  /export:CharToOemBuffW \
  /export:OemToCharBuffW \
  /export:MulDiv \
  /export:GetCurrentTime \
  /export:GetCPInfo \
  /export:GetCPInfoExA \
  /export:GetCPInfoExW \
  /export:GetProcessAffinityMask \
  /export:SetProcessAffinityMask \
  /export:GetThreadAffinityMask \
  /export:SetThreadAffinityMask \
  /export:GetProcessPriorityBoost \
  /export:SetProcessPriorityBoost \
  /export:GetThreadPriorityBoost \
  /export:SetThreadPriorityBoost \
  /export:GetCurrentProcessorNumberEx \
  /export:SetThreadIdealProcessor \
  /export:GetThreadIdealProcessorEx \
  /export:SetThreadIdealProcessorEx \
  /export:IsWow64Process \
  /export:IsWow64Process2 \
  /export:GetProcessHandleCount \
  /export:GetThreadIOPendingFlag \
  /export:GetProcessIoCounters \
  /export:GetThreadDescription \
  /export:SetThreadDescription \
  /export:GetCurrentPackageId \
  /export:GetCurrentPackageFullName \
  /export:GetCurrentPackageFamilyName \
  /export:GetCurrentApplicationUserModelId \
  /export:GetSystemTimeAdjustmentPrecise \
  /export:GetThreadInformation \
  /export:SetThreadInformation \
  /export:GetProcessWorkingSetSize \
  /export:SetProcessWorkingSetSize \
  /export:EmptyWorkingSet \
  /export:GetProcessMitigationPolicy \
  /export:SetProcessMitigationPolicy \
  /export:GetThreadLocaleName \
  /export:GetUserDefaultGeoName \
  /export:GetGeoInfoA \
  /export:GetGeoInfoW \
  /export:GetTimeZoneInformation \
  /export:GetDynamicTimeZoneInformation \
  /export:SystemTimeToTzSpecificLocalTime \
  /export:TzSpecificLocalTimeToSystemTime \
  /export:MultiByteToWideChar \
  /export:WideCharToMultiByte \
  /export:LCMapStringA \
  /export:LCMapStringW \
  /export:LCMapStringEx \
  /export:GetLocaleInfoA \
  /export:GetLocaleInfoW \
  /export:GetLocaleInfoEx \
  /export:GetDateFormatA \
  /export:GetDateFormatW \
  /export:GetTimeFormatA \
  /export:GetTimeFormatW \
  /export:GetNumberFormatA \
  /export:GetNumberFormatW \
  /export:GetCurrencyFormatA \
  /export:GetCurrencyFormatW \
  /export:GetCalendarInfoA \
  /export:GetCalendarInfoW \
  /export:GetActiveProcessorCount \
  /export:GetMaximumProcessorCount \
  /export:GetActiveProcessorGroupCount \
  /export:GetNumaHighestNodeNumber \
  /export:GetNumaProcessorNode \
  /export:GetNumaAvailableMemoryNode \
  /export:GetNumaAvailableMemoryNodeEx \
  /export:GetNumaNodeProcessorMask \
  /export:GetNumaProximityNode \
  /export:GetNumaProximityNodeEx \
  /export:CreateFile2 \
  /export:MoveFileExA \
  /export:MoveFileExW \
  /export:CopyFileExA \
  /export:CopyFileExW \
  /export:ReplaceFileA \
  /export:ReplaceFileW \
  /export:CreateDirectoryExA \
  /export:CreateDirectoryExW \
  /export:CancelIoEx \
  /export:GetOverlappedResultEx \
  /export:WaitForSingleObjectEx \
  /export:WaitForMultipleObjectsEx \
  /export:SleepEx \
  /export:SignalObjectAndWait \
  /export:CreateEventW \
  /export:CreateMutexW \
  /export:CreateSemaphoreW \
  /export:OpenEventA \
  /export:OpenEventW \
  /export:OpenMutexA \
  /export:OpenMutexW \
  /export:OpenSemaphoreA \
  /export:OpenSemaphoreW \
  /export:PulseEvent \
  /export:CreateWaitableTimerA \
  /export:CreateWaitableTimerW \
  /export:SetWaitableTimer \
  /export:CancelWaitableTimer \
  /export:LoadLibraryW \
  /export:FreeLibraryAndExitThread \
  /export:GetDllDirectoryA \
  /export:GetDllDirectoryW \
  /export:SetDllDirectoryA \
  /export:SetDllDirectoryW \
  /export:GetSystemWindowsDirectoryA \
  /export:GetSystemWindowsDirectoryW \
  /export:GetSystemWow64DirectoryA \
  /export:GetSystemWow64DirectoryW \
  /export:GetSystemWow64Directory2A \
  /export:GetSystemWow64Directory2W \
  /export:CreateProcessW \
  /export:WinExec \
  /export:GetVersionExA \
  /export:GetVersionExW \
  /export:VerifyVersionInfoA \
  /export:VerifyVersionInfoW \
  /export:GetSystemTimes \
  /export:GlobalFlags \
  /export:LocalFlags \
  /export:HeapCreate \
  /export:HeapDestroy \
  /export:HeapLock \
  /export:HeapUnlock \
  /export:HeapSetInformation \
  /export:HeapQueryInformation \
  /export:GlobalMemoryStatusEx \
  /export:GlobalMemoryStatus \
  /export:GetPhysicallyInstalledSystemMemory \
  /export:GetLogicalProcessorInformation \
  /export:GetLogicalProcessorInformationEx \
  /export:GetProcessDefaultCpuSets \
  /export:SetProcessDefaultCpuSets \
  /export:GetThreadSelectedCpuSets \
  /export:SetThreadSelectedCpuSets \
  /export:GetProcessGroupAffinity \
  /export:GetThreadGroupAffinity \
  /export:SetThreadGroupAffinity \
  /export:GetNumaNodeProcessorMaskEx \
  /export:GetProcessVersionEx \
  /export:GetSystemDefaultLCIDEx \
  /export:GetUserDefaultLCIDEx \
  /export:GetSystemDirectory \
  /export:GetWindowsDirectory \
  /export:GetModuleFileName \
  /export:GetModuleHandle \
  /export:GetCommandLine \
  /export:GetTempPath \
  /export:GetCurrentDirectory \
  /export:SetCurrentDirectory \
  /export:GetEnvironmentVariable \
  /export:SetEnvironmentVariable \
  /export:GetFileAttributes \
  /export:SetFileAttributes \
  /export:GetLargePageMinimum \
  /export:GetSystemRegistryQuota \
  /export:GetVolumeNameForVolumeMountPointA \
  /export:GetVolumeNameForVolumeMountPointW \
  /export:SetVolumeMountPointA \
  /export:SetVolumeMountPointW \
  /export:DeleteVolumeMountPointA \
  /export:DeleteVolumeMountPointW \
  /export:FindFirstVolumeA \
  /export:FindFirstVolumeW \
  /export:FindNextVolumeA \
  /export:FindNextVolumeW \
  /export:FindVolumeClose \
  /export:FindFirstVolumeMountPointA \
  /export:FindFirstVolumeMountPointW \
  /export:FindNextVolumeMountPointA \
  /export:FindNextVolumeMountPointW \
  /export:FindVolumeMountPointClose \
  /export:SetVolumeLabelA \
  /export:SetVolumeLabelW \
  /export:GetVolumeInformationByHandleW \
  /export:GetFirmwareType \
  /export:GetFirmwareEnvironmentVariableA \
  /export:GetFirmwareEnvironmentVariableW \
  /export:SetFirmwareEnvironmentVariableA \
  /export:SetFirmwareEnvironmentVariableW \
  /export:GetSystemPreferredUILanguages \
  /export:GetUserPreferredUILanguages \
  /export:GetThreadPreferredUILanguages \
  /export:GetProcessPreferredUILanguages \
  /export:SetThreadPreferredUILanguages \
  /export:SetProcessPreferredUILanguages \
  /export:GetProcessMemoryInfo \
  /export:GetProcessWorkingSetSizeEx \
  /export:SetProcessWorkingSetSizeEx \
  /export:GetProcessMemoryInfoEx \
  /export:GetSystemWow64Directory \
  /export:GetSystemWindowsDirectory \
  /export:GetDllDirectory \
  /export:SetDllDirectory \
  /export:CreateFileTransactedA \
  /export:CreateFileTransactedW \
  /export:DeleteFileTransactedA \
  /export:DeleteFileTransactedW \
  /export:MoveFileTransactedA \
  /export:MoveFileTransactedW \
  /export:CopyFileTransactedA \
  /export:CopyFileTransactedW \
  /export:CreateDirectoryTransactedA \
  /export:CreateDirectoryTransactedW \
  /export:RemoveDirectoryTransactedA \
  /export:RemoveDirectoryTransactedW \
  /export:IsDebuggerPresent \
  /export:CheckRemoteDebuggerPresent \
  /export:OutputDebugStringA \
  /export:OutputDebugStringW \
  /export:DebugBreak \
  /export:GetThreadErrorMode \
  /export:SetThreadErrorMode \
  /export:SetThreadStackGuarantee \
  /export:GetThreadStackGuarantee \
  /export:GetProcessDefaultLayout \
  /export:SetProcessDefaultLayout \
  /export:GetSystemPowerStatus \
  /export:SetSystemPowerState \
  /export:GetSystemDefaultLocaleName \
  /export:GetUserDefaultLocaleName \
  /export:GetLocaleInfoByName \
  /export:GetCalendarInfoEx \
  /export:GetDateFormatEx \
  /export:GetTimeFormatEx \
  /export:GetNumberFormatEx \
  /export:GetCurrencyFormatEx \
  /export:GetDurationFormat \
  /export:GetDurationFormatEx \
  /export:FindNLSString \
  /export:FindNLSStringEx \
  /export:CompareStringEx \
  /export:SetFileInformationByHandle \
  /export:GetFileAttributesTransactedA \
  /export:GetFileAttributesTransactedW \
  /export:SetFileAttributesTransactedA \
  /export:SetFileAttributesTransactedW \
  "/out:$SYS32/kernel32.dll" \
  "$B/obj/kernel32.o"

echo "  -> winmm.dll"

clang "${USER_CFLAGS[@]}" -fno-builtin \
  -c "$ROOT/user/winmm.c" \
  -o "$B/obj/winmm.o"

lld -flavor link \
  /dll /noentry /subsystem:native /nodefaultlib \
  /fixed:no /base:0x43200000 \
  /export:timeGetTime \
  "/out:$SYS32/winmm.dll" \
  "$B/obj/winmm.o"

echo "  -> ws2_32.dll (Winsock 2 API and address utilities)"
clang "${USER_CFLAGS[@]}" -fno-builtin \
  -c "$ROOT/user/ws2_32.c" \
  -o "$B/obj/ws2_32.o"

lld -flavor link \
  /dll /noentry /subsystem:native /nodefaultlib \
  /fixed:no /dynamicbase /base:0x43400000 \
  /export:WSAStartup /export:WSACleanup /export:WSAGetLastError /export:WSASetLastError \
  /export:WSAIsBlocking /export:htons /export:ntohs /export:htonl /export:ntohl \
  /export:inet_addr /export:inet_ntoa /export:inet_pton /export:inet_ntop \
  /export:gethostname /export:getaddrinfo /export:freeaddrinfo /export:getnameinfo \
  /export:socket /export:closesocket /export:connect /export:bind /export:listen /export:accept \
  /export:send /export:recv /export:sendto /export:recvfrom /export:shutdown /export:select \
  /export:ioctlsocket /export:setsockopt /export:getsockopt /export:getsockname /export:getpeername \
  /export:WSAIoctl /export:WSAEventSelect /export:WSACreateEvent /export:WSACloseEvent \
  /export:WSAWaitForMultipleEvents /export:WSAEnumNetworkEvents /export:WSAAsyncSelect \
  "/out:$SYS32/ws2_32.dll" \
  "$B/obj/ws2_32.o"

echo "  -> opengl32.dll (bounded framebuffer software OpenGL runtime)"
OGL_CFLAGS=()
for flag in "${USER_CFLAGS[@]}"; do
  [[ "$flag" == "-mgeneral-regs-only" ]] || OGL_CFLAGS+=("$flag")
done
clang "${OGL_CFLAGS[@]}" -fno-builtin \
  -c "$ROOT/user/opengl32.c" \
  -o "$B/obj/opengl32.o"

lld -flavor link \
  /dll /noentry /subsystem:native /nodefaultlib \
  /fixed:no /dynamicbase /base:0x62000000 \
  /export:glClearColor /export:glClear /export:glViewport /export:glScissor \
  /export:glEnable /export:glDisable /export:glIsEnabled /export:glGetError \
  /export:glGetString /export:glDrawArrays /export:glDrawElements \
  /export:glVertexPointer /export:glColorPointer /export:glTexCoordPointer \
  /export:glEnableClientState /export:glDisableClientState /export:glLineWidth \
  /export:glBlendFunc /export:glPixelStorei /export:glClearStencil /export:glClearDepth \
  /export:glDepthMask /export:glDepthFunc /export:glStencilMask /export:glStencilFunc \
  /export:glStencilOp /export:glTexParameteri /export:glTexImage2D \
  /export:glBindTexture /export:glGenTextures /export:glDeleteTextures \
  /export:glGetIntegerv /export:glGetFloatv /export:glGetBooleanv /export:glReadPixels \
  /export:glGetDoublev /export:glFinish /export:glFlush /export:wglGetProcAddress \
  /export:wglMakeCurrent /export:wglDeleteContext /export:wglCreateContext \
  /export:wglGetCurrentDC /export:wglGetCurrentContext \
  /export:wglShareLists /export:SwapBuffers /export:glDrawBuffer /export:glReadBuffer \
  /export:glGetTexParameteriv /export:glGetTexLevelParameteriv /export:glTexSubImage2D \
  /export:glColor4f /export:glColor4ub /export:glClearAccum \
  "/out:$SYS32/opengl32.dll" \
  "$B/obj/opengl32.o"


echo "  -> Universal CRT runtime (real initializer-table execution)"

clang "${USER_CFLAGS[@]}" -fno-builtin \
  -c "$ROOT/user/ucrt_runtime.c" \
  -o "$B/obj/ucrt_runtime.o"

lld -flavor link \
  /dll /noentry /subsystem:native /nodefaultlib \
  /fixed:no /base:0x43000000 \
  /export:_initterm_e \
  /export:_initterm \
  /export:_errno \
  /export:malloc \
  /export:calloc \
  /export:realloc \
  /export:free \
  /export:_msize \
  /export:memcpy \
  /export:memmove \
  /export:memset \
  /export:memcmp \
  /export:memchr \
  /export:strlen \
  /export:strcmp \
  /export:strncmp \
  /export:strchr \
  /export:strstr \
  /export:strrchr \
  /export:atoi \
  /export:strtol \
  /export:strtoul \
  /export:strtoll \
  /export:strtoull \
  /export:wcstombs \
  /export:mbstowcs \
  /export:isprint \
  /export:strspn \
  /export:strcspn \
  /export:bsearch \
  /export:qsort \
  /export:rand \
  /export:srand \
  /export:_initialize_onexit_table \
  /export:_execute_onexit_table \
  /export:_cexit \
  /export:_get_errno \
  /export:_set_errno \
  /export:_errno \
  /export:_initterm \
  /export:_initterm_e \
  /export:_beginthreadex \
  /export:terminate \
  /export:__std_terminate \
  /export:_purecall \
  /export:abort \
  /export:_invoke_watson \
  /export:strerror \
  /export:_configure_narrow_argv \
  /export:_initialize_narrow_environment \
  /export:_seh_filter_dll \
  /export:__std_type_info_destroy_list \
  /export:__C_specific_handler \
  "/out:$SYS32/api-ms-win-crt-runtime-l1-1-0.dll" \
  "$B/obj/ucrt_runtime.o"

# The API-set contract name is the import name used by Geometry Dash.
# Keep the conventional UCRT name pointing at the same implementation too.
cp "$SYS32/api-ms-win-crt-runtime-l1-1-0.dll" "$SYS32/ucrtbase.dll"
cp "$SYS32/api-ms-win-crt-runtime-l1-1-0.dll" "$SYS32/ucrtbase.dll"


# These API-set names are separate import contracts on Windows. SentinelOS
# currently provides one shared implementation for the overlapping CRT
# memory, string, allocator, and initializer entry points.
for contract in \
  api-ms-win-crt-heap-l1-1-0.dll \
  api-ms-win-crt-string-l1-1-0.dll \
  api-ms-win-crt-stdio-l1-1-0.dll \
  api-ms-win-crt-math-l1-1-0.dll \
  api-ms-win-crt-convert-l1-1-0.dll \
  api-ms-win-crt-environment-l1-1-0.dll
do
  cp "$SYS32/api-ms-win-crt-runtime-l1-1-0.dll" "$SYS32/$contract"
  cp "$SYS32/api-ms-win-crt-runtime-l1-1-0.dll" "$SYS32/$contract"
done

# VCRUNTIME140 imports these standard memory/string primitives. Publish a
# separate module under that contract name rather than assuming API-set
# forwarding exists in the PE loader.
lld -flavor link \
  /dll /noentry /subsystem:native /nodefaultlib \
  /fixed:no /base:0x43100000 \
  /export:memcpy \
  /export:memmove \
  /export:memset \
  /export:memcmp \
  /export:memchr \
  /export:strlen \
  /export:strcmp \
  /export:strncmp \
  /export:strchr \
  /export:strstr \
  /export:strrchr \
  /export:malloc \
  /export:calloc \
  /export:realloc \
  /export:free \
  /export:_msize \
  /export:_purecall \
  /export:__std_terminate \
  /export:_invoke_watson \
  "/out:$SYS32/vcruntime140.dll" \
  "$B/obj/ucrt_runtime.o"

echo "  -> user32.dll"

clang "${USER_CFLAGS[@]}" \
  -c "$ROOT/user/user32.c" \
  -o "$B/obj/user32.o"

echo "  -> soft3d.o"
clang "${USER_CFLAGS[@]}" \
  -c "$ROOT/user/soft3d.c" \
  -o "$B/obj/soft3d.o"

lld -flavor link \
  /dll /noentry /subsystem:native /nodefaultlib \
  /fixed:no /base:0x41100000 \
  "/implib:$B/obj/user32.lib" \
  /export:CreateWindowExA \
  /export:ShowWindow \
  /export:UpdateWindow \
  /export:TextOutA \
  /export:InvalidateRect \
  /export:GetMessageA \
  /export:TranslateMessage \
  /export:DispatchMessageA \
  /export:MessageBoxA \
  /export:DestroyWindow \
  /export:PostQuitMessage \
  /export:ExitWindowsEx \
  /export:DrawTaskbar \
  /export:S3DGetSurface \
  /export:S3DInitializeSurface \
  /export:S3DInitialize \
  /export:S3DClear \
  /export:S3DDrawTriangle \
  /export:S3DPresent \
  /export:S3DSubmitVirGL \
  /export:S3DPresentVirGL \
  /export:S3DClearVirGL \
  /export:S3DDrawTriangleVirGL \
  /export:S3DUploadVirGLBuffer \
  /export:BeginRedrawBatch \
  /export:EndRedrawBatch \
  /export:GetClientRect \
  /export:MoveWindow \
  /export:SetWindowPos \
  /export:GetWindowRect \
  /export:IsWindow \
  /export:IsWindowVisible \
  /export:EnableWindow \
  /export:IsIconic \
  /export:GetForegroundWindow \
  /export:SetForegroundWindow \
  /export:GetActiveWindow \
  /export:SetActiveWindow \
  /export:GetWindowTextA \
  /export:SetWindowTextA \
  /export:GetClassNameA \
  /export:RegisterClassA \
  /export:UnregisterClassA \
  /export:DefWindowProcA \
  /export:SendMessageA \
  /export:PostMessageA \
  /export:PeekMessageA \
  /export:GetKeyState \
  /export:GetAsyncKeyState \
  /export:GetCursorPos \
  /export:SetCursorPos \
  /export:ShowCursor \
  /export:SetCapture \
  /export:ReleaseCapture \
  /export:GetCapture \
  /export:GetDesktopWindow \
  /export:GetDC \
  /export:ReleaseDC \
  /export:BeginPaint \
  /export:EndPaint \
  /export:GetWindowLongA \
  /export:SetWindowLongA \
  /export:GetSystemMetrics \
  /export:SetTimer \
  /export:KillTimer \
  /export:GetDlgItem \
  /export:SetDlgItemTextA \
  /export:SetDlgItemTextW \
  /export:GetDlgItemTextA \
  /export:GetDlgItemTextW \
  /export:CreateMenu \
  /export:CreatePopupMenu \
  /export:AppendMenuA \
  /export:DestroyMenu \
  /export:TrackPopupMenu \
  /export:LoadCursorA \
  /export:LoadIconA \
  /export:LoadImageA \
  /export:DrawMenuBar \
  /export:GetMessagePos \
  /export:GetMessageTime \
  /export:GetDoubleClickTime \
  /export:MessageBeep \
  /export:RedrawWindow \
  /export:ScrollWindow \
  /export:GetUpdateRect \
  /export:SetFocus \
  /export:GetFocus \
  /export:MessageBoxW \
  /export:CreateWindowExW \
  /export:RegisterClassW \
  /export:DefWindowProcW \
  /export:SendMessageW \
  /export:PostMessageW \
  /export:PeekMessageW \
  /export:GetMessageW \
  /export:CreateDialogParamA \
  /export:CreateDialogParamW \
  /export:DialogBoxParamA \
  /export:DialogBoxParamW \
  /export:EndDialog \
  /export:IsDialogMessageA \
  /export:IsDialogMessageW \
  /export:GetWindowTextW \
  /export:SetWindowTextW \
  /export:GetClassNameW \
  /export:LoadCursorW \
  /export:LoadIconW \
  /export:LoadImageW \
  /export:FindWindowA \
  /export:FindWindowW \
  /export:FindWindowExA \
  /export:FindWindowExW \
  /export:GetParent \
  /export:SetParent \
  /export:GetWindow \
  /export:GetWindowThreadProcessId \
  /export:GetAncestor \
  /export:EnumWindows \
  /export:EnumChildWindows \
  /export:EnumThreadWindows \
  /export:GetTopWindow \
  /export:BringWindowToTop \
  /export:SetWindowPlacement \
  /export:GetWindowPlacement \
  /export:GetWindowInfo \
  /export:GetWindowModuleFileNameA \
  /export:GetWindowContextHelpId \
  /export:SetWindowContextHelpId \
  /export:GetMenu \
  /export:SetMenu \
  /export:GetSubMenu \
  /export:GetMenuItemCount \
  /export:GetMenuItemID \
  /export:GetMenuState \
  /export:CheckMenuItem \
  /export:EnableMenuItem \
  /export:DeleteMenu \
  /export:RemoveMenu \
  /export:InsertMenuA \
  /export:ModifyMenuA \
  /export:SetMenuItemInfoA \
  /export:GetMenuItemInfoA \
  /export:GetMenuStringA \
  /export:SetMenuDefaultItem \
  /export:GetMenuDefaultItem \
  /export:GetSystemMenu \
  /export:GetScrollInfo \
  /export:SetScrollInfo \
  /export:ShowScrollBar \
  /export:SetScrollRange \
  /export:GetScrollRange \
  /export:GetScrollPos \
  /export:SetScrollPos \
  /export:SetWindowRgn \
  /export:GetWindowRgn \
  /export:MapWindowPoints \
  /export:ClientToScreen \
  /export:ScreenToClient \
  /export:WindowFromPoint \
  /export:ChildWindowFromPoint \
  /export:SetDoubleClickTime \
  /export:GetLastActivePopup \
  /export:GetWindowTextLengthA \
  /export:GetWindowTextLengthW \
  /export:GetWindowLongPtrA \
  /export:SetWindowLongPtrA \
  /export:GetWindowLongPtrW \
  /export:SetWindowLongPtrW \
  /export:GetClassLongA \
  /export:SetClassLongA \
  /export:GetClassLongPtrA \
  /export:SetClassLongPtrA \
  /export:GetClassLongW \
  /export:SetClassLongW \
  /export:GetClassLongPtrW \
  /export:SetClassLongPtrW \
  /export:GetWindowModuleFileNameW \
  /export:GetWindowRgnBox \
  /export:GetWindowDisplayAffinity \
  /export:SetWindowDisplayAffinity \
  /export:IsChild \
  /export:IsWindowUnicode \
  /export:IsWindowEnabled \
  /export:GetWindowOwner \
  /export:GetWindowWord \
  /export:SetWindowWord \
  /export:GetWindowLongW \
  /export:SetWindowLongW \
  /export:GetDlgCtrlID \
  /export:GetDlgItemInt \
  /export:SetDlgItemInt \
  /export:CheckDlgButton \
  /export:IsDlgButtonChecked \
  /export:CheckRadioButton \
  /export:GetNextDlgTabItem \
  /export:GetNextDlgGroupItem \
  /export:GetDialogBaseUnits \
  /export:MapDialogRect \
  /export:AdjustWindowRect \
  /export:AdjustWindowRectEx \
  /export:AdjustWindowRectExForDpi \
  /export:GetDpiForWindow \
  /export:GetDpiForSystem \
  /export:GetSystemMetricsForDpi \
  /export:GetShellWindow \
  /export:ClipCursor \
  /export:GetClipCursor \
  /export:GetTitleBarInfo \
  /export:GetMenuBarInfo \
  /export:GetGUIThreadInfo \
  /export:GetLastInputInfo \
  /export:GetCursor \
  /export:GetCursorInfo \
  /export:SetCursor \
  /export:GetMouseMovePointsEx \
  /export:GetKeyboardState \
  /export:SetKeyboardState \
  /export:GetKeyboardLayout \
  /export:GetKeyboardLayoutList \
  /export:GetKeyboardLayoutNameA \
  /export:GetKeyboardLayoutNameW \
  /export:ActivateKeyboardLayout \
  /export:LoadKeyboardLayoutA \
  /export:LoadKeyboardLayoutW \
  /export:UnloadKeyboardLayout \
  /export:GetKeyNameTextA \
  /export:GetKeyNameTextW \
  /export:MapVirtualKeyA \
  /export:MapVirtualKeyW \
  /export:MapVirtualKeyExA \
  /export:MapVirtualKeyExW \
  /export:ToUnicode \
  /export:ToUnicodeEx \
  /export:VkKeyScanA \
  /export:VkKeyScanW \
  /export:VkKeyScanExA \
  /export:VkKeyScanExW \
  /export:GetKeyboardType \
  /export:GetCaretBlinkTime \
  /export:SetCaretBlinkTime \
  /export:GetCaretPos \
  /export:SetCaretPos \
  /export:CreateCaret \
  /export:DestroyCaret \
  /export:HideCaret \
  /export:ShowCaret \
  /export:GetMenuStringW \
  /export:SetMenuItemBitmaps \
  /export:GetMenuCheckMarkDimensions \
  /export:HiliteMenuItem \
  /export:InsertMenuW \
  /export:AppendMenuW \
  /export:ModifyMenuW \
  /export:SetMenuItemInfoW \
  /export:GetMenuItemInfoW \
  /export:TrackPopupMenuEx \
  /export:EndMenu \
  /export:GetMenuContextHelpId \
  /export:SetMenuContextHelpId \
  /export:GetMenuInfo \
  /export:SetMenuInfo \
  /export:GetMenuItemRect \
  /export:SetMenuItemCount \
  /export:IsZoomed \
  /export:GetWindowPos \
  /export:UnregisterClassW \
  /export:ValidateRect \
  /export:GetMessageExtraInfo \
  /export:SetMessageExtraInfo \
  /export:GetQueueStatus \
  /export:GetInputState \
  /export:WaitMessage \
  /export:ReplyMessage \
  /export:InSendMessage \
  /export:InSendMessageEx \
  /export:GetWindowDC \
  /export:GetDCEx \
  /export:GetSysColor \
  /export:GetSysColorBrush \
  /export:SetSysColors \
  /export:FillRect \
  /export:FrameRect \
  /export:InvertRect \
  /export:DrawFocusRect \
  /export:OpenIcon \
  /export:CloseWindow \
  /export:ShowOwnedPopups \
  /export:FlashWindow \
  /export:AnimateWindow \
  /export:GetDesktopWindowRect \
  /export:GetWindowFromDC \
  /export:GetUpdateRgn \
  /export:ExcludeUpdateRgn \
  /export:GetScrollPos32 \
  /export:SetScrollPos32 \
  /export:GetWindowThreadProcessIdEx \
  /export:IsHungAppWindow \
  /export:GetActiveWindowThread \
  /export:GetWindowLongOffset \
  /export:SetWindowLongOffset \
  /export:GetCursorPosEx \
  /export:SetCursorPosEx \
  /export:GetCaretState \
  /export:GetDialogControlCount \
  /export:GetMenuItemFlags \
  /export:GetMenuItemTextA \
  /export:SetMenuItemTextA \
  /export:GetWindowStyle \
  /export:SetWindowStyle \
  /export:GetWindowExStyle \
  /export:SetWindowExStyle \
  /export:GetWindowClientOrigin \
  /export:SetWindowClientOrigin \
  /export:DrawEdge \
  /export:DrawFrameControl \
  /export:DrawCaption \
  /export:DrawIcon \
  /export:DrawIconEx \
  /export:DrawTextA \
  /export:DrawTextW \
  /export:GetTextExtentPoint32A \
  /export:GetTextExtentPoint32W \
  /export:GetTextColor \
  /export:SetTextColor \
  /export:GetBkColor \
  /export:SetBkColor \
  /export:SetBkMode \
  /export:SetTextAlign \
  /export:GetTextAlign \
  /export:GetDeviceCaps \
  /export:GetWindowOrgEx \
  /export:SetWindowOrgEx \
  /export:GetViewportOrgEx \
  /export:SetViewportOrgEx \
  /export:LPtoDP \
  /export:DPtoLP \
  /export:SaveDC \
  /export:RestoreDC \
  /export:GetDCOrgEx \
  /export:GetBrushOrgEx \
  /export:SetBrushOrgEx \
  /export:OpenClipboard \
  /export:CloseClipboard \
  /export:EmptyClipboard \
  /export:SetClipboardData \
  /export:GetClipboardData \
  /export:IsClipboardFormatAvailable \
  /export:RegisterClipboardFormatA \
  /export:RegisterClipboardFormatW \
  /export:DestroyIcon \
  /export:DestroyCursor \
  /export:CopyIcon \
  /export:CopyCursor \
  /export:GetIconInfo \
  /export:LoadBitmapA \
  /export:LoadBitmapW \
  /export:LoadMenuA \
  /export:LoadMenuW \
  /export:TranslateAcceleratorA \
  /export:TranslateAcceleratorW \
  /export:GetClassInfoA \
  /export:GetClassInfoW \
  /export:GetClassInfoExA \
  /export:GetClassInfoExW \
  /export:SystemParametersInfoA \
  /export:SystemParametersInfoW \
  /export:GetWindowTextLength \
  /export:GetClassName \
  /export:GetWindowText \
  /export:SetWindowText \
  /export:GetScrollBarInfo \
  /export:GetComboBoxInfo \
  /export:GetListBoxInfo \
  /export:GetWindowModuleFileName \
  /export:GetWindowDpiAwarenessContext \
  /export:AreDpiAwarenessContextsEqual \
  /export:GetAwarenessFromDpiAwarenessContext \
  /export:GetProcessDpiAwareness \
  /export:SetPropA \
  /export:GetPropA \
  /export:RemovePropA \
  /export:SetPropW \
  /export:GetPropW \
  /export:RemovePropW \
  /export:SetLayeredWindowAttributes \
  /export:GetLayeredWindowAttributes \
  /export:UpdateLayeredWindow \
  /export:RegisterHotKey \
  /export:UnregisterHotKey \
  /export:SetWindowsHookExA \
  /export:SetWindowsHookExW \
  /export:UnhookWindowsHookEx \
  /export:CallNextHookEx \
  /export:GetWindowSubclass \
  /export:SetWindowSubclass \
  /export:RemoveWindowSubclass \
  /export:DefSubclassProc \
  /export:GetTextMetricsA \
  /export:GetTextMetricsW \
  /export:GetCharWidth32A \
  /export:GetCharWidth32W \
  /export:GetTextExtentExPointA \
  /export:GetTextExtentExPointW \
  /export:TabbedTextOutA \
  /export:TabbedTextOutW \
  /export:GetClipboardOwner \
  /export:GetOpenClipboardWindow \
  /export:GetClipboardSequenceNumber \
  /export:GetClipboardFormatNameA \
  /export:GetClipboardFormatNameW \
  /export:GetFocusThread \
  /export:GetWindowDisplayAffinityEx \
  /export:GetWindowDpiAwarenessContextEx \
  /export:GetDpiForMonitor \
  /export:EnableNonClientDpiScaling \
  /export:SetThreadDpiAwarenessContext \
  /export:GetThreadDpiAwarenessContext \
  /export:GetDpiFromDpiAwarenessContext \
  /export:SetProcessDpiAwarenessContext \
  /export:GetProcessWindowStation \
  /export:GetThreadDesktop \
  /export:GetUserObjectInformationA \
  /export:GetUserObjectInformationW \
  /export:GetUserObjectSecurity \
  /export:SetUserObjectInformationA \
  /export:SetUserObjectInformationW \
  /export:EnumDisplayMonitors \
  /export:MonitorFromWindow \
  /export:MonitorFromPoint \
  /export:MonitorFromRect \
  /export:GetMonitorInfoA \
  /export:GetMonitorInfoW \
  /export:EnumDisplaySettingsA \
  /export:EnumDisplaySettingsW \
  /export:ChangeDisplaySettingsA \
  /export:ChangeDisplaySettingsW \
  /export:ChangeDisplaySettingsExA \
  /export:ChangeDisplaySettingsExW \
  /export:LoadAcceleratorsA \
  /export:LoadAcceleratorsW \
  /export:CreateAcceleratorTableA \
  /export:CreateAcceleratorTableW \
  /export:DestroyAcceleratorTable \
  /export:CopyAcceleratorTableA \
  /export:CopyAcceleratorTableW \
  /export:LoadStringA \
  /export:LoadStringW \
  /export:LoadMenuIndirectA \
  /export:LoadMenuIndirectW \
  /export:RegisterDeviceNotificationA \
  /export:RegisterDeviceNotificationW \
  /export:UnregisterDeviceNotification \
  /export:SetProcessDPIAware \
  /export:IsProcessDPIAware \
  /export:SetThreadDesktop \
  /export:OpenWindowStationA \
  /export:OpenWindowStationW \
  /export:CloseWindowStation \
  /export:SetProcessWindowStation \
  /export:EnumWindowStationsA \
  /export:EnumWindowStationsW \
  /export:EnumDesktopsA \
  /export:EnumDesktopsW \
  /export:OpenDesktopA \
  /export:OpenDesktopW \
  /export:CloseDesktop \
  /export:SwitchDesktop \
  /export:GetUserObjectInformation \
  /export:SetUserObjectInformation \
  /export:GetMonitorDpiType \
  /export:GetSystemMetricsForMonitor \
  /export:GetWindowDpi \
  /export:GetDpiForWindowEx \
  /export:GetPhysicalCursorPos \
  /export:SetPhysicalCursorPos \
  /export:GetCursorInfoEx \
  /export:SetWindowFeedbackSetting \
  /export:GetWindowFeedbackSetting \
  /export:GetWindowCompositionAttribute \
  /export:SetWindowCompositionAttribute \
  /export:GetMenuInfoEx \
  /export:SetMenuInfoEx \
  /export:GetMenuItemIDEx \
  /export:GetSubMenuEx \
  /export:SetWindowRedraw \
  /export:GetWindowRedraw \
  /export:GetWindowArrange \
  /export:ArrangeIconicWindows \
  /export:CascadeWindows \
  /export:TileWindows \
  /export:GetWindowPlacementEx \
  /export:GetWindowRectEx \
  /export:SetWindowPosEx \
  /export:MoveWindowEx \
  /export:SetForegroundWindowEx \
  /export:GetForegroundWindowEx \
  /export:GetPixel \
  /export:SetPixel \
  /export:SetPixelV \
  /export:MoveToEx \
  /export:LineTo \
  /export:Rectangle \
  /export:Ellipse \
  /export:RoundRect \
  /export:Polyline \
  /export:Polygon \
  /export:SetROP2 \
  /export:GetROP2 \
  /export:SetStretchBltMode \
  /export:GetStretchBltMode \
  /export:SetMapMode \
  /export:GetMapMode \
  /export:GetWindowExtEx \
  /export:SetWindowExtEx \
  /export:GetViewportExtEx \
  /export:SetViewportExtEx \
  /export:SetArcDirection \
  /export:GetArcDirection \
  /export:GetWindowRgnEx \
  /export:GetClassInfoEx \
  /export:GetWindowTextExA \
  /export:SetWindowTextExA \
  /export:GetMenuDefaultItemEx \
  /export:GetSystemMenuEx \
  /export:SetMenuEx \
  /export:GetMenuEx \
  /export:GetWindowParent \
  /export:GetWindowAncestor \
  /export:GetWindowChild \
  /export:GetWindowTop \
  /export:GetWindowOwnerEx \
  /export:GetWindowEnabled \
  /export:SetWindowEnabled \
  /export:GetWindowVisible \
  /export:SetWindowVisible \
  /export:GetWindowMinimized \
  /export:GetWindowZoomed \
  /export:GetWindowClassNameA \
  /export:GetWindowClassNameW \
  /export:GetMenuItemState \
  /export:SetMenuItemState \
  /export:GetWindowStyleEx \
  /export:SetWindowStyleEx \
  /export:GetWindowUserData \
  /export:SetWindowUserData \
  /export:GetWindowProcedure \
  /export:SetWindowProcedure \
  /export:GetWindowIdentifier \
  /export:SetWindowIdentifier \
  /export:GetWindowContextHelpIdEx \
  /export:SetWindowContextHelpIdEx \
  /export:GetMenuCount \
  /export:GetMenuItemId \
  /export:GetMenuString \
  /export:AppendMenu \
  /export:InsertMenu \
  /export:ModifyMenu \
  /export:DeleteMenuItem \
  /export:RemoveMenuItem \
  /export:CheckMenuItemState \
  /export:EnableMenuItemState \
  /export:RegisterClassExA \
  /export:RegisterClassExW \
  /export:CreateWindowA \
  /export:CreateWindowW \
  /export:DispatchMessageW \
  /export:CreateDialogIndirectParamA \
  /export:CreateDialogIndirectParamW \
  /export:DialogBoxIndirectParamA \
  /export:DialogBoxIndirectParamW \
  /export:GetRawInputData \
  /export:GetRawInputBuffer \
  /export:GetRawInputDeviceInfoA \
  /export:GetRawInputDeviceInfoW \
  /export:GetRawInputDeviceList \
  /export:RegisterRawInputDevices \
  /export:DefRawInputProc \
  /export:GetPointerType \
  /export:GetPointerInfo \
  /export:GetPointerInfoHistory \
  /export:GetPointerPenInfo \
  /export:GetPointerTouchInfo \
  /export:GetPointerFrameInfo \
  /export:GetPointerDevice \
  /export:GetPointerDeviceRects \
  /export:GetPointerDevices \
  /export:GetGestureInfo \
  /export:CloseGestureInfoHandle \
  /export:GetGestureExtraArgs \
  /export:SetGestureConfig \
  /export:GetTouchInputInfo \
  "/out:$SYS32/user32.dll" \
  "$B/obj/user32.o" \
  "$B/obj/soft3d.o"

echo "  -> explorer.exe"

clang "${USER_CFLAGS[@]}" \
  -c "$ROOT/user/explorer.c" \
  -o "$B/obj/explorer.o"

lld -flavor link \
  /subsystem:windows /entry:explorer_main /nodefaultlib \
  /fixed:no /base:0x40000000 \
  "/out:$SYS32/explorer.exe" \
  "$B/obj/explorer.o"

echo "  -> fileexplorer.exe"

clang "${USER_CFLAGS[@]}" \
  -c "$ROOT/user/fileexplorer.c" \
  -o "$B/obj/fileexplorer.o"

lld -flavor link \
  /subsystem:windows /entry:mainCRTStartup /nodefaultlib \
  /fixed:no /base:0x40100000 \
  "/out:$SYS32/fileexplorer.exe" \
  "$B/obj/fileexplorer.o"

echo "  -> cmd.exe"

clang "${USER_CFLAGS[@]}" \
  -c "$ROOT/user/cmd.c" \
  -o "$B/obj/cmd.o"

lld -flavor link \
  /subsystem:windows /entry:mainCRTStartup /nodefaultlib \
  /fixed:no /base:0x40300000 \
  "/out:$SYS32/cmd.exe" \
  "$B/obj/cmd.o"

echo "  -> taskmgr.exe"

clang "${USER_CFLAGS[@]}" \
  -c "$ROOT/user/taskmgr.c" \
  -o "$B/obj/taskmgr.o"

lld -flavor link \
  /subsystem:windows /entry:mainCRTStartup /nodefaultlib \
  /fixed:no /base:0x40500000 \
  "/out:$SYS32/taskmgr.exe" \
  "$B/obj/taskmgr.o"

echo "  -> msiexec.exe"

clang "${USER_CFLAGS[@]}" \
  -c "$ROOT/user/msiexec.c" \
  -o "$B/obj/msiexec.o"

lld -flavor link \
  /subsystem:windows /entry:mainCRTStartup /nodefaultlib \
  /fixed:no /base:0x40400000 \
  "/out:$SYS32/msiexec.exe" \
  "$B/obj/msiexec.o"

echo "  -> game.exe (UserTestApps C++ game, installed at disk root)"

USER_CXXFLAGS=(
  -target x86_64-windows-gnu
  -ffreestanding
  -fno-exceptions
  -fno-rtti
  -fno-threadsafe-statics
  -fno-use-cxa-atexit
  -fno-builtin
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

clang++ "${USER_CXXFLAGS[@]}" \
  -c "$ROOT/UserTestApps/game.cpp" \
  -o "$B/obj/game.o"

lld -flavor link \
  /subsystem:windows /entry:game_mainCRTStartup /nodefaultlib \
  /fixed:no /base:0x42000000 \
  "/out:$B/rootfs/magic/Program Files/game.exe" \
  "$B/obj/game.o"

if [[ ! -s "$B/rootfs/magic/Program Files/game.exe" ]]; then
  echo "FATAL: game.exe was not produced"
  exit 1
fi

ls -lh "$B/rootfs/magic/Program Files/game.exe"

echo "  -> maze.exe (VirGL shader-driven spinning 3D triangle; GPU-only)"

# Maze uses floating-point raycasting and inline x87 trigonometry. The shared
# user flags prohibit all FP/SIMD registers, which forces software helper calls.
# Clang's Windows x64 target does not support -mfpmath=387, so enable the
# target's normal hardware floating-point code generation for this app only.
# Other OS/user images retain the conservative general-register-only flags.
MAZE_CXXFLAGS=()
for flag in "${USER_CXXFLAGS[@]}"; do
  [[ "$flag" == "-mgeneral-regs-only" ]] || MAZE_CXXFLAGS+=("$flag")
done

clang++ "${MAZE_CXXFLAGS[@]}" \
  -c "$ROOT/UserTestApps/maze.cpp" \
  -o "$B/obj/maze.o"

lld -flavor link \
  /machine:x64 /subsystem:windows /entry:maze_mainCRTStartup /nodefaultlib \
  /fixed:no /base:0x42100000 \
  "/out:$B/rootfs/magic/Program Files/maze.exe" \
  "$B/obj/maze.o" \
  "$B/obj/soft3d.o"

if [[ ! -s "$B/rootfs/magic/Program Files/maze.exe" ]]; then
  echo "FATAL: maze.exe was not produced"
  exit 1
fi

# Fail the build if the game is not an x86-64 PE32+ image. The .exe suffix
# alone is not a format check; SentinelOS user executables must be PE32+.
MAZE_FORMAT="$(file -b "$B/rootfs/magic/Program Files/maze.exe")"
echo "  -> maze.exe format: $MAZE_FORMAT"
if [[ "$MAZE_FORMAT" != *"PE32+"* || "$MAZE_FORMAT" != *"x86-64"* ]]; then
  echo "FATAL: maze.exe is not a PE32+ x86-64 executable"
  exit 1
fi
ls -lh "$B/rootfs/magic/Program Files/maze.exe"

echo "==> Checking required images"

for image in \
  hal.dll \
  krnl.exe \
  smss.exe \
  bootvid.exe \
  sentlogon.exe \
  explorer.exe \
  fileexplorer.exe \
  taskbar.dll \
  kernel32.dll \
  winmm.dll \
  opengl32.dll \
  api-ms-win-crt-runtime-l1-1-0.dll \
  ucrtbase.dll \
  user32.dll \
  vulkan.dll \
  cmd.exe \
  taskmgr.exe \
  msiexec.exe
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
  "$SYS32/bootvid.exe" \
  "$SYS32/sentlogon.exe" \
  "$SYS32/explorer.exe" \
  "$SYS32/fileexplorer.exe" \
  "$SYS32/taskbar.dll" \
  "$SYS32/kernel32.dll" \
  "$SYS32/winmm.dll" \
  "$SYS32/opengl32.dll" \
  "$SYS32/api-ms-win-crt-runtime-l1-1-0.dll" \
  "$SYS32/ucrtbase.dll" \
  "$SYS32/user32.dll" \
  "$SYS32/vulkan.dll" \
  "$SYS32/cmd.exe" \
  "$SYS32/taskmgr.exe" \
  "$SYS32/msiexec.exe"

# Reject accidental files at the filesystem root; only /magic is permitted.
if [[ ! -d "$B/rootfs/magic/System32/drivers" ]]; then
  echo "FATAL: missing magic/System32/drivers"
  exit 1
fi
while IFS= read -r top_entry; do
  if [[ "$(basename "$top_entry")" != "magic" ]]; then
    echo "FATAL: unexpected rootfs entry: $top_entry (only /magic is allowed)"
    exit 1
  fi
done < <(find "$B/rootfs" -mindepth 1 -maxdepth 1 -print)

echo "==> GPT disk image (content-sized FAT32 ESP + ext4, with 100 MiB extra)"

# Size the ESP from the EFI files (minimum 64 MiB for a practical FAT32 ESP),
# and size ext4 from the rootfs contents plus at least 100 MiB of spare space.
# Additional allowance covers ext4 metadata and avoids packing files too tightly.
MIB=$((1024 * 1024))
SECTOR_SIZE=512
SECTORS_PER_MIB=$((MIB / SECTOR_SIZE))
ESP_BYTES=$(du -sb "$B/esp" | awk '{print $1}')
ROOTFS_BYTES=$(du -sb "$B/rootfs" | awk '{print $1}')

ESP_MIB=$(((ESP_BYTES + 8 * MIB + MIB - 1) / MIB))
(( ESP_MIB < 64 )) && ESP_MIB=64
ESP_SECTORS=$((ESP_MIB * SECTORS_PER_MIB))
EXT_START=$((2048 + ESP_SECTORS))

# Keep the ext4 partition MiB-aligned. The 10% + 16 MiB allowance is for
# filesystem structures; the requested 100 MiB remains available beyond files.
EXT_BYTES=$((ROOTFS_BYTES + ROOTFS_BYTES / 10 + 16 * MIB + 100 * MIB))
EXT_BLOCKS=$(((EXT_BYTES + 4095) / 4096))
EXT_SECTORS=$((EXT_BLOCKS * 8))
EXT_OFF=$((EXT_START * SECTOR_SIZE))
EXT_SZ=$((EXT_SECTORS * SECTOR_SIZE))

# Round the whole disk up to a MiB and leave room for the backup GPT header.
DISK_SECTORS=$((((EXT_START + EXT_SECTORS + 34 + SECTORS_PER_MIB - 1) / SECTORS_PER_MIB) * SECTORS_PER_MIB))
DISK_MIB=$((DISK_SECTORS / SECTORS_PER_MIB))

echo "  -> ESP: ${ESP_MIB} MiB; rootfs data: $((ROOTFS_BYTES / MIB)) MiB; disk: ${DISK_MIB} MiB"

rm -f "$DISKIMG"
qemu-img create -f raw "$DISKIMG" "${DISK_MIB}M" >/dev/null

sfdisk "$DISKIMG" <<SFDISK
label: gpt
unit: sectors
first-lba: 2048

start=2048, size=$ESP_SECTORS, type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B, name="MAGICESP"
start=$EXT_START, size=$EXT_SECTORS, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4, name="magic"
SFDISK

dd if=/dev/zero \
  of="$B/esp.fat" \
  bs=1M count="$ESP_MIB" status=none

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

FEATS="^has_journal,^metadata_csum,^64bit,^resize_inode,^dir_index"

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

# Normal build only creates the bootable disk image. Never invoke installer
# scripts or generate an installer ISO from this entry point.

if [[ $DO_RUN -eq 1 ]]; then
  exec "$ROOT/scripts/run.sh"
fi