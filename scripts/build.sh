
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
  "$B/rootfs/Users/Public" \
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
  "/implib:$B/obj/kernel32.lib" \
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
  /export:GetConsoleInputExeNameA \
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
    /export:GetBinaryTypeW \
  /export:GetBinaryTypeA \
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
  /export:GetDlgItemTextA \
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
  "/implib:$B/obj/user32.lib" \
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
