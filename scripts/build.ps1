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
$mke2fs = "C:\msys64\usr\bin\mke2fs.exe"
$vhd    = "$b\magic-disk.vhd"
$diskimg = "$b\magic-disk.img"

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
    & $exe @rest
    if ($LASTEXITCODE -ne 0) { throw "$exe failed (exit $LASTEXITCODE)" }
}
function Fwd($p) { $p -replace '\\', '/' }

$sys32 = "$b\rootfs\magic\System32"
New-Item -ItemType Directory -Force "$b\obj", "$b\esp\EFI\BOOT", "$b\esp\EFI\magic", $sys32 | Out-Null

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

# ---- ESP (FAT): stage 0 = firmware fallback, stage 1 = bootuni, stage 2 = loadufi ----
Build-Efi bootx64 "$b\esp\EFI\BOOT\BOOTX64.EFI"
[string[]]$extra = @()
if ($KeepFallback) { $extra = @("-DKEEP_FALLBACK") }
Build-Efi bootuni "$b\esp\EFI\magic\bootuni.efi" $extra
Build-Efi loadufi "$b\esp\EFI\magic\loadufi.efi"

# ---- the kernel goes to the ext4 volume (magic\System32) ----
Invoke-Tool clang @common -DHAL_BUILD -c "$root\hal\hal.c" -o "$b\obj\hal.o"
Invoke-Tool lld-link /dll /noentry /subsystem:native /nodefaultlib "/implib:$b\obj\hal.lib" "/out:$sys32\hal.dll" "$b\obj\hal.o"
Invoke-Tool clang @common -c "$root\kernel\kernel.c" -o "$b\obj\kernel.o"
Invoke-Tool lld-link /subsystem:native /entry:kmain /nodefaultlib /dynamicbase `
    "/out:$sys32\krnl.exe" "$b\obj\kernel.o" "$b\obj\hal.lib"

# ---- build one GPT disk: FAT32 ESP + ext4 system partition ----
$feats = "^has_journal,^metadata_csum,^64bit,^resize_inode"   # keep it simple for the loader
$usedLetters = [IO.DriveInfo]::GetDrives() | ForEach-Object { $_.Name.Substring(0, 1).ToUpperInvariant() }
$espLetter = @("Z", "Y", "X", "W", "V", "U", "T", "S") |
    Where-Object { $_ -notin $usedLetters } | Select-Object -First 1
if (-not $espLetter) { throw "no free drive letter is available to stage the EFI System Partition" }
$espRoot = "$($espLetter):\"
Detach-Vhd                                   # a failed earlier run may have left it attached
Remove-Item $vhd, $diskimg -ErrorAction SilentlyContinue

try {
    [void](Run-Diskpart @"
create vdisk file="$vhd" maximum=256 type=fixed
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

# Format ext4 directly inside partition 2 of the detached VHD.
if (Test-Path $mke2fs) {
    Invoke-Tool $mke2fs -t ext4 -b 4096 -L magic -O $feats `
        -d (Fwd "$b\rootfs") -E "offset=$extOffset" (Fwd $vhd) "$extBlocks"
} else {
    $wr = (wsl wslpath -a (Fwd "$b\rootfs")).Trim()
    $wi = (wsl wslpath -a (Fwd $vhd)).Trim()
    Invoke-Tool wsl mke2fs -t ext4 -b 4096 -L magic -O $feats `
        -d $wr -E "offset=$extOffset" $wi "$extBlocks"
}

Invoke-Tool qemu-img convert -f vpc -O raw $vhd $diskimg
Remove-Item $vhd

Write-Host "build ok" -ForegroundColor Green
if ($Run) { & "$PSScriptRoot\run.ps1" }

