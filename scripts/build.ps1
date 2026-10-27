# SentinelOS Windows-host build entry point.
# The complete PE/user-mode build lives in build.sh. Run that canonical build
# in WSL, then stage its ESP and full rootfs into the native Windows VHDX.
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

# Keep one authoritative build definition: build.sh builds every real PE image,
# DLL export table, driver, resource, font and application. WSL runs that Linux
# toolchain, then this script stages its complete output into a native VHDX.
if (-not (Get-Command "wsl.exe" -ErrorAction SilentlyContinue)) {
    throw "WSL is required for the complete build.sh pipeline. Install/configure WSL with SentinelOS build dependencies; no partial VHDX was created."
}
Write-Stage "Build every SentinelOS component via scripts/build.sh in WSL"
$wslRootResult = & wsl.exe --exec wslpath -u $root 2>&1
if ($LASTEXITCODE -ne 0) { throw "Could not translate repository path into WSL: $($wslRootResult -join [Environment]::NewLine)" }
$wslRoot = ($wslRootResult -join [Environment]::NewLine).Trim()
if ([string]::IsNullOrWhiteSpace($wslRoot)) { throw "wslpath returned an empty repository path." }
$quotedRoot = "'" + ($wslRoot -replace "'", "'\''") + "'"
$buildArgs = @()
if ($KeepFallback) { $buildArgs += "--keep-fallback" }
if ($ResetNvram) { $buildArgs += "--reset-nvram" }
$argText = ($buildArgs | ForEach-Object { "'" + $_ + "'" }) -join " "
$bashCommand = "cd $quotedRoot && chmod +x scripts/build.sh && ./scripts/build.sh $argText"
Write-Host "[WSL] bash -lc $bashCommand" -ForegroundColor Cyan
& wsl.exe --exec bash -lc $bashCommand
if ($LASTEXITCODE -ne 0) { throw "scripts/build.sh failed inside WSL (exit $LASTEXITCODE). VHDX staging was not started." }

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
