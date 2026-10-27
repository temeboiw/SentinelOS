# magic OS build script   (run from an ADMIN PowerShell - diskpart builds the system disk)
#   .\scripts\build.ps1                 build only
#   .\scripts\build.ps1 -Run            build, then launch QEMU
#   .\scripts\build.ps1 -KeepFallback   bootuni will NOT delete EFI\BOOT\BOOTX64.EFI
#   .\scripts\build.ps1 -ResetNvram     forget the saved UEFI variables, then rebuild
#   .\scripts\build.ps1 -Clean          delete build\
param([switch]$Clean, [switch]$Run, [switch]$KeepFallback, [switch]$ResetNvram)
$ErrorActionPreference = "Stop"

$root = Split-Path $PSScriptRoot -Parent
$b    = "$root\build"
$edk2 = if ($env:EDK2) { $env:EDK2 } else { "C:\edk2" }
$mke2fs = if ($env:MKE2FS) { $env:MKE2FS } else { "$root\tools\mke2fs.exe" }
$vhd    = "$b\magic-disk.vhdx"
$rawimg = "$b\magic-disk.raw.tmp"

function Run-Diskpart($text, [switch]$IgnoreErrors) {
    $f = "$b\dp.txt"
    Set-Content $f $text -Encoding ASCII
    $out = (diskpart /s $f) -join "`n"
    if ($LASTEXITCODE -ne 0 -and -not $IgnoreErrors) { throw "diskpart failed:`n$out" }
    return $out
}
function Detach-Vhd {
    if (Test-Path $vhd) { [void](Run-Diskpart "select vdisk file=`"$vhd`"`ndetach vdisk" -IgnoreErrors) }
}

if ($Clean) {
    if (Test-Path $b) { Detach-Vhd }
    Remove-Item $b -Recurse -Force -ErrorAction SilentlyContinue
    return
}
if ($ResetNvram) {
    Remove-Item "$b\vars.fd" -ErrorAction SilentlyContinue
    Write-Host "NVRAM reset - rebuilding so BOOTX64.EFI is back on the ESP"
}

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
           ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { throw "Run this from an Administrator PowerShell (diskpart needs it)." }

function Invoke-Tool {
    $exe, $rest = $args
    $shown = @($rest | ForEach-Object { if ("$_" -match "\s") { '"' + ($_ -replace '"', '\" ') + '"' } else { "$_" } }) -join " "
    Write-Host ("[BUILD] {0} {1}" -f $exe, $shown) -ForegroundColor Cyan
    & $exe @rest
    if ($LASTEXITCODE -ne 0) { throw "$exe failed (exit $LASTEXITCODE)" }
}
function Write-Stage([string]$name) {
    Write-Host ""
    Write-Host ("========== {0} ==========" -f $name) -ForegroundColor Yellow
}
function Fwd($p) { $p -replace '\\', '/' }

Write-Stage "Prepare build directories and root filesystem staging"
# Recreate a clean staged rootfs: /magic is the only volume-root entry.
$rootfs = "$b\rootfs"
if (Test-Path $rootfs) { Remove-Item $rootfs -Recurse -Force }
$sys32 = "$rootfs\magic\System32"
$drivers = "$sys32\drivers"
New-Item -ItemType Directory -Force "$b\obj", "$b\esp\EFI\BOOT", "$b\esp\EFI\magic", $drivers, "$rootfs\magic\Program Files", "$rootfs\magic\Users\Public" | Out-Null

$common = "--target=x86_64-unknown-windows", "-ffreestanding", "-mno-red-zone",
          "-fno-stack-protector", "-mgeneral-regs-only", "-O2", "-Wall", "-Wno-unused-function",
          "-I$root\shared", "-I$root\boot"

# compile + link one UEFI application
function Build-Efi($name, $out, [string[]]$extra = @()) {
    Invoke-Tool clang @common @extra -fshort-wchar `
        "-isystem$edk2\MdePkg\Include" "-isystem$edk2\MdePkg\Include\X64" `
        -c "$root\boot\$name.c" -o "$b\obj\$name.o"
    Invoke-Tool lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib `
        "/out:$out" "$b\obj\$name.o"
}

Write-Stage "UEFI boot applications"
# ---- ESP (FAT): stage 0 = firmware fallback, stage 1 = bootuni, stage 2 = loadufi ----
Build-Efi bootx64 "$b\esp\EFI\BOOT\BOOTX64.EFI"
[string[]]$extra = @()
if ($KeepFallback) { $extra = @("-DKEEP_FALLBACK") }
Build-Efi bootuni "$b\esp\EFI\magic\bootuni.efi" $extra
Build-Efi loadufi "$b\esp\EFI\magic\loadufi.efi"

Write-Stage "HAL and kernel (including SMP trampoline)"
# ---- the kernel goes to the ext4 volume (magic\System32) ----
Invoke-Tool clang @common -DHAL_BUILD -c "$root\hal\hal.c" -o "$b\obj\hal.o"
Invoke-Tool lld-link /dll /noentry /subsystem:native /nodefaultlib "/implib:$b\obj\hal.lib" /base:0xFFFFFFFF81000000 /fixed:no "/out:$sys32\hal.dll" "$b\obj\hal.o"
Invoke-Tool clang @common -c "$root\kernel\kernel.c" -o "$b\obj\kernel.o"
# The kernel references real-mode/protected-mode/long-mode AP startup labels.
# Assemble the existing trampoline and link it into the kernel; do not stub symbols.
Invoke-Tool clang @common -c "$root\kernel\smp_trampoline.S" -o "$b\obj\smp_trampoline.o"
Invoke-Tool lld-link /subsystem:native /entry:kmain /nodefaultlib /dynamicbase `
    /base:0xFFFFFFFF80200000 /fixed:no "/out:$sys32\krnl.exe" "$b\obj\kernel.o" "$b\obj\smp_trampoline.o" "$b\obj\hal.lib"

# Guard the filesystem layout before formatting the ext4 partition.
$topLevel = @(Get-ChildItem -Force $rootfs)
if ($topLevel.Count -ne 1 -or $topLevel[0].Name -ne "magic" -or -not $topLevel[0].PSIsContainer) {
    throw "rootfs must contain exactly one top-level directory: magic"
}
if (-not (Test-Path "$drivers" -PathType Container)) {
    throw "missing magic\System32\drivers directory"
}

Write-Stage "Create GPT virtual disk and FAT32 EFI System Partition"
# ---- build one GPT disk: FAT32 ESP + ext4 system partition ----
$feats = "^has_journal,^metadata_csum,^64bit,^resize_inode"   # keep it simple for the loader
$usedLetters = [IO.DriveInfo]::GetDrives() | ForEach-Object { $_.Name.Substring(0, 1).ToUpperInvariant() }
$espLetter = @("Z", "Y", "X", "W", "V", "U", "T", "S") |
    Where-Object { $_ -notin $usedLetters } | Select-Object -First 1
if (-not $espLetter) { throw "no free drive letter is available to stage the EFI System Partition" }
$espRoot = "$($espLetter):\"
Detach-Vhd                                   # a failed earlier run may have left it attached
Remove-Item $vhd, $rawimg -Force -ErrorAction SilentlyContinue
if (Test-Path $vhd) { throw "Cannot recreate disk because the old VHDX still exists or is locked: $vhd. Close QEMU and detach it in Disk Management, then retry." }
if (Test-Path $rawimg) { throw "Cannot remove stale temporary raw image: $rawimg" }

try {
    [void](Run-Diskpart @"
create vdisk file="$vhd" maximum=256 type=expandable
select vdisk file="$vhd"
attach vdisk
convert gpt
select partition 1
delete partition override
create partition efi size=128
format fs=fat32 quick label=MAGICESP
assign letter=$espLetter
create partition primary size=96
"@)
    if (-not (Test-Path $espRoot)) { throw "the EFI System Partition was not mounted at $espRoot" }
    Copy-Item "$b\esp\*" $espRoot -Recurse -Force

    $disk = Get-DiskImage -ImagePath $vhd | Get-Disk
    $partitions = @(Get-Partition -DiskNumber $disk.Number)
    if ($partitions.Count -ne 2) {
        throw "expected exactly two GPT partitions, found $($partitions.Count)"
    }
    $rootPartition = $partitions |
        Where-Object { $_.Size -eq 96MB } | Select-Object -First 1
    if (-not $rootPartition) { throw "the ext4 partition was not created" }
    $extOffset = [uint64]$rootPartition.Offset
    $extBlocks = [uint64][Math]::Floor($rootPartition.Size / 4096)
} finally {
    Detach-Vhd                               # also unmounts the ESP folder
}

Write-Stage "Format ext4 system partition (native Windows mke2fs)"
# Format ext4 directly inside partition 2 using a native Windows mke2fs.exe.
# No WSL, MSYS2, or shell fallback is permitted.
if (-not (Test-Path $mke2fs -PathType Leaf)) {
    $found = Get-Command mke2fs.exe -ErrorAction SilentlyContinue
    if ($found) { $mke2fs = $found.Source }
}
if (-not (Test-Path $mke2fs -PathType Leaf)) {
    throw "Native mke2fs.exe is required to create the ext4 partition. Place a standalone Windows e2fsprogs mke2fs.exe at '$root\tools\mke2fs.exe' or set `$env:MKE2FS to its full path. This build intentionally does not use WSL or MSYS2."
}
Write-Stage "Convert VHDX to temporary raw image for ext4 formatting"
# mke2fs offset writes to a raw disk layout, not to the VHDX container format.
Invoke-Tool qemu-img convert -f vhdx -O raw $vhd $rawimg

Write-Stage "Format ext4 system partition in temporary raw image"
Invoke-Tool $mke2fs -t ext4 -b 4096 -L magic -O $feats `
    -d (Fwd "$b\rootfs") -E "offset=$extOffset" (Fwd $rawimg) "$extBlocks"

Write-Stage "Repack finished disk as VHDX"
Remove-Item $vhd -Force
Invoke-Tool qemu-img convert -f raw -O vhdx $rawimg $vhd
Remove-Item $rawimg -Force

Write-Host "build ok" -ForegroundColor Green
if ($Run) { & "$PSScriptRoot\run.ps1" }

