# SentinelOS native Windows-host build entry point.
# Uses Windows LLVM + Git for Windows Bash (MSYS), never WSL. The canonical
# build script compiles all images and stages rootfs/ESP; PowerShell creates VHDX.
# Run from an Administrator PowerShell because DiskPart creates the VHDX.
param([switch]$Clean, [switch]$Run, [switch]$KeepFallback, [switch]$ResetNvram)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$b = Join-Path $root "build"
$vhd = Join-Path $b "magic-disk.vhdx"

function Write-Stage([string]$name) {
    Write-Host ""
    Write-Host ("========== {0} ==========" -f $name) -ForegroundColor Yellow
}
function Run-Diskpart([string]$text, [switch]$IgnoreErrors) {
    $f = Join-Path $b "dp.txt"
    Set-Content -LiteralPath $f -Value $text -Encoding ASCII
    $out = (& diskpart /s $f) -join [Environment]::NewLine
    if ($LASTEXITCODE -ne 0 -and -not $IgnoreErrors) { throw "diskpart failed:" + [Environment]::NewLine + $out }
    return $out
}
function Detach-Vhd {
    if (Test-Path -LiteralPath $vhd) {
        $cmd = "select vdisk file=" + [char]34 + $vhd + [char]34 + [Environment]::NewLine + "detach vdisk"
        [void](Run-Diskpart $cmd -IgnoreErrors)
    }
}

if ($Clean) {
    if (Test-Path -LiteralPath $b) { Detach-Vhd }
    Remove-Item -LiteralPath $b -Recurse -Force -ErrorAction SilentlyContinue
    return
}
if ($ResetNvram) {
    Remove-Item -LiteralPath (Join-Path $b "vars.fd") -Force -ErrorAction SilentlyContinue
    Write-Host "NVRAM reset - rebuilding boot files and disk image."
}
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { throw "Run this from an Administrator PowerShell (DiskPart needs it)." }
New-Item -ItemType Directory -Force -Path $b | Out-Null

# Locate Git for Windows Bash. This is a Windows process; WSL is never invoked.
$bashCandidates = @(
    (Join-Path $env:ProgramFiles "Git\bin\bash.exe"),
    (Join-Path ${env:ProgramFiles(x86)} "Git\bin\bash.exe"),
    (Join-Path $env:LOCALAPPDATA "Programs\Git\bin\bash.exe")
) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) }
$bashCommand = Get-Command bash.exe -ErrorAction SilentlyContinue
if ($bashCommand) { $bashCandidates += $bashCommand.Source }
$bash = $bashCandidates | Select-Object -First 1
if (-not $bash) {
    throw "Git for Windows Bash is required (not WSL). Install Git for Windows and native LLVM for Windows, then rerun."
}
$clang = Get-Command clang.exe -ErrorAction SilentlyContinue
$clangxx = Get-Command clang++.exe -ErrorAction SilentlyContinue
$lld = Get-Command lld.exe -ErrorAction SilentlyContinue
if (-not $clang -or -not $clangxx -or -not $lld) {
    throw "Native LLVM tools are missing from PATH. Install LLVM for Windows (clang.exe, clang++.exe, lld.exe); WSL is not used."
}
Write-Stage "Build all PE/EFI images with native Windows LLVM"
$nativeArgs = @()
if ($KeepFallback) { $nativeArgs += "--keep-fallback" }
if ($ResetNvram) { $nativeArgs += "--reset-nvram" }
$argString = ($nativeArgs -join " ")
$escapedRoot = $root.Replace("\", "/")
$escapedRoot = $escapedRoot -replace "'", "'\''"
$command = "cd '$escapedRoot' && ./scripts/build.sh --no-disk-image $argString"
Write-Host "[Windows/Git Bash] $command" -ForegroundColor Cyan
& $bash --noprofile --norc -lc $command
if ($LASTEXITCODE -ne 0) {
    throw "Native Windows compile/staging failed (exit $LASTEXITCODE). No VHDX was created."
}

$rootfs = Join-Path $b "rootfs"
$sys32 = Join-Path $rootfs "magic\System32"
$esp = Join-Path $b "esp"
$drivers = Join-Path $sys32 "drivers"

Write-Stage "Validate required build outputs before creating the VHDX"
$required = @(
    (Join-Path $esp "EFI\BOOT\BOOTX64.EFI"),
    (Join-Path $esp "EFI\magic\bootuni.efi"),
    (Join-Path $esp "EFI\magic\loadufi.efi"),
    (Join-Path $sys32 "hal.dll"),
    (Join-Path $sys32 "krnl.exe"),
    (Join-Path $sys32 "smss.exe"),
    (Join-Path $sys32 "sentlogon.exe"),
    (Join-Path $sys32 "bootvid.exe"),
    (Join-Path $sys32 "explorer.exe"),
    (Join-Path $sys32 "fileexplorer.exe"),
    (Join-Path $sys32 "kernel32.dll"),
    (Join-Path $sys32 "user32.dll"),
    (Join-Path $sys32 "taskbar.dll"),
    (Join-Path $sys32 "vulkan.dll"),
    (Join-Path $drivers "ata.sys")
)
foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -le 0) {
        throw "Required build output missing or empty: $path. Refusing to create an incomplete system disk."
    }
}
if (-not (Test-Path -LiteralPath $rootfs -PathType Container) -or -not (Test-Path -LiteralPath $drivers -PathType Container)) {
    throw "build.sh did not produce rootfs/magic/System32/drivers."
}
$topLevel = @(Get-ChildItem -LiteralPath $rootfs -Force)
if ($topLevel.Count -ne 1 -or $topLevel[0].Name -ne "magic" -or -not $topLevel[0].PSIsContainer) {
    throw "rootfs must contain exactly one top-level directory: magic"
}
if (-not (Test-Path -LiteralPath (Join-Path $rootfs "magic\logo.bmp") -PathType Leaf)) { throw "SentinelOS logo was not staged in rootfs." }
Write-Host ("Validated {0} files under magic\System32." -f (@(Get-ChildItem -LiteralPath $sys32 -File -Recurse).Count)) -ForegroundColor Green

Write-Stage "Create expandable GPT VHDX with FAT32 ESP and NTFS MAGIC partition"
$usedLetters = [IO.DriveInfo]::GetDrives() | ForEach-Object { $_.Name.Substring(0, 1).ToUpperInvariant() }
$freeLetters = @("Z", "Y", "X", "W", "V", "U", "T", "S", "R", "Q", "P", "O", "N", "M") | Where-Object { $_ -notin $usedLetters }
if (@($freeLetters).Count -lt 2) { throw "Two free drive letters are required to stage the ESP and NTFS system partition." }
$espLetter = $freeLetters[0]
$sysLetter = $freeLetters[1]
$espRoot = "$($espLetter):\"
$sysRoot = "$($sysLetter):\"
Detach-Vhd
Remove-Item -LiteralPath $vhd -Force -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $vhd) { throw "Cannot recreate VHDX because it still exists or is locked: $vhd. Close QEMU and detach it in Disk Management, then retry." }

try {
    [void](Run-Diskpart @"
create vdisk file="$vhd" maximum=131072 type=expandable
select vdisk file="$vhd"
attach vdisk
convert gpt
create partition efi size=128
format fs=fat32 quick label=MAGICESP
assign letter=$espLetter
create partition primary
format fs=ntfs quick label=MAGIC
assign letter=$sysLetter
"@)
    if (-not (Test-Path -LiteralPath $espRoot)) { throw "EFI System Partition was not mounted at $espRoot" }
    Copy-Item -Path (Join-Path $esp "*") -Destination $espRoot -Recurse -Force
    $disk = Get-DiskImage -ImagePath $vhd | Get-Disk
    $partitions = @(Get-Partition -DiskNumber $disk.Number)
    if ($partitions.Count -ne 2) { throw "Expected exactly two GPT partitions, found $($partitions.Count)." }
    $rootPartition = $partitions | Where-Object { $_.PartitionNumber -eq 2 } | Select-Object -First 1
    if (-not $rootPartition) { throw "NTFS system partition was not created." }
    if (-not (Test-Path -LiteralPath $sysRoot)) { throw "NTFS system partition was not mounted at $sysRoot" }

    # Copy the whole rootfs recursively: all built EXEs, DLLs, drivers, fonts,
    # assets, and any future files produced by build.sh are staged automatically.
    Copy-Item -Path (Join-Path $rootfs "*") -Destination $sysRoot -Recurse -Force
    foreach ($relative in @("magic\System32\smss.exe","magic\System32\sentlogon.exe","magic\System32\bootvid.exe","magic\System32\explorer.exe","magic\System32\fileexplorer.exe","magic\System32\krnl.exe","magic\System32\hal.dll")) {
        $copied = Join-Path $sysRoot $relative
        if (-not (Test-Path -LiteralPath $copied -PathType Leaf) -or (Get-Item -LiteralPath $copied).Length -le 0) {
            throw "Staging verification failed; missing copied file: $copied"
        }
    }
} finally {
    Detach-Vhd
}
Write-Host "build ok: complete build.sh output staged to $vhd" -ForegroundColor Green
if ($Run) { & (Join-Path $PSScriptRoot "run.ps1") }
