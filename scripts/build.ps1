# SentinelOS native Windows-host build entry point.
# Uses Windows LLVM + Git for Windows Bash (MSYS), never WSL. The canonical
# build script compiles all images and stages rootfs/ESP; PowerShell creates VHDX.
# Run from an Administrator PowerShell because DiskPart creates the VHDX.
param([switch]$Clean, [switch]$Run, [switch]$KeepFallback, [switch]$DropFallback, [switch]$ResetNvram)
$ErrorActionPreference = "Stop"
# run.ps1 boots with -bios, so UEFI variables are NOT persisted between runs. The
# first-boot stage (bootuni.efi) registers a boot entry in NVRAM and then deletes
# \EFI\BOOT\BOOTX64.EFI from the ESP. With volatile NVRAM that leaves a disk
# nothing can boot on the second run ("failed to load Boot0002 ... Not Found",
# then PXE). So keep the fallback loader unless -DropFallback is given.
if (-not $DropFallback) { $KeepFallback = $true }
$root = Split-Path $PSScriptRoot -Parent
$b = Join-Path $root "build"
$vhd = Join-Path $b "magic-disk.vhdx"
$vhdFinal = $vhd
# Build into a scratch image and swap it in only after it is verified, so a
# failed build can never leave you with no disk at all.
$vhd = Join-Path $b "magic-disk.building.vhdx"
trap {
    Write-Host ""
    Write-Host "BUILD FAILED: $($_.Exception.Message)" -ForegroundColor Red
    if (Test-Path -LiteralPath $vhdFinal) { Write-Host "Previous disk image kept: $vhdFinal" -ForegroundColor Yellow }
    break
}

# This entry point relies on Windows-only APIs (DiskPart, Get-DiskImage, and
# Windows security principals). Fail clearly when invoked under PowerShell Core
# on Linux instead of throwing a confusing WindowsPrincipal platform exception.
if (-not $IsWindows) {
    throw "scripts/build.ps1 is Windows-host-only. On Linux/Ubuntu/proot, run: bash ./scripts/build.sh (or bash ./scripts/build.sh --no-disk-image to compile and stage without creating a disk image)."
}

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
function Detach-Image([string]$p) {
    if (-not (Test-Path -LiteralPath $p)) { return }
    $cmd = "select vdisk file=" + [char]34 + $p + [char]34 + [Environment]::NewLine + "detach vdisk"
    [void](Run-Diskpart $cmd -IgnoreErrors)
    # diskpart errors are ignored above, so prove it detached and retry with the
    # storage cmdlet. A silent failure here is what leaves extra drives behind.
    $img = Get-DiskImage -ImagePath $p -ErrorAction SilentlyContinue
    if ($img -and $img.Attached) {
        try { [void](Dismount-DiskImage -ImagePath $p) } catch { }
        $img = Get-DiskImage -ImagePath $p -ErrorAction SilentlyContinue
        if ($img -and $img.Attached) { Write-Warning "Could not detach $p. Run .\scripts\vhd.ps1 Clean" }
    }
}
function Get-VirtualDiskInfo {
    foreach ($d in @(Get-Disk | Where-Object { $_.BusType -eq 'File Backed Virtual' })) {
        $ip = $null
        try { $ip = (Get-DiskImage -DevicePath ("\\.\PhysicalDrive" + $d.Number) -ErrorAction Stop).ImagePath } catch { }
        [pscustomobject]@{ Number = $d.Number; ImagePath = $ip }
    }
}
# Detach every attached virtual disk that belongs to this project. Leftovers show
# up in Explorer as extra "Local Disk" drives. Disks from other projects are left
# alone, and orphans with an unknown image are only touched with -Force.
function Clear-StaleDisks([string]$projectBuildDir, [switch]$Force) {
    $found = 0
    foreach ($v in @(Get-VirtualDiskInfo)) {
        $mine = $v.ImagePath -and ($v.ImagePath -like "$projectBuildDir\*" -or $v.ImagePath -like "*magic-disk*")
        if (-not ($mine -or ((-not $v.ImagePath) -and $Force))) { continue }
        $found++
        Write-Host ("Detaching leftover disk {0}: {1}" -f $v.Number, $(if ($v.ImagePath) { $v.ImagePath } else { "(image file gone)" })) -ForegroundColor Yellow
        foreach ($l in @(Get-Partition -DiskNumber $v.Number -ErrorAction SilentlyContinue | ForEach-Object { $_.DriveLetter } | Where-Object { $_ -and $_ -ne [char]0 })) {
            try { Write-VolumeCache -DriveLetter $l } catch { }
        }
        try { [void](Dismount-DiskImage -DevicePath ("\\.\PhysicalDrive" + $v.Number)) }
        catch { Write-Warning ("Could not detach disk {0}: {1}" -f $v.Number, $_.Exception.Message) }
    }
    Start-Sleep -Milliseconds 500
    $left = @(Get-VirtualDiskInfo | Where-Object { $_.ImagePath -and ($_.ImagePath -like "$projectBuildDir\*" -or $_.ImagePath -like "*magic-disk*") })
    if ($left.Count -gt 0) { Write-Warning ("{0} Sentinel disk(s) are still attached. Close Explorer windows or terminals open on them, then run .\scripts\vhd.ps1 Clean (or reboot)." -f $left.Count) }
    elseif ($found -gt 0) { Write-Host "Leftover disks cleaned up." -ForegroundColor Green }
}
function Detach-Vhd { Detach-Image $vhd }
# Copy a tree largest-files-first. Big files then land in one contiguous run on
# the fresh NTFS volume, which is all the kernel's read-only NTFS driver supports
# (single runlist, no ATTRIBUTE_LIST).
function Copy-Tree([string]$src, [string]$dst) {
    $src = (Resolve-Path -LiteralPath $src).Path.TrimEnd('\')
    foreach ($d in Get-ChildItem -LiteralPath $src -Directory -Recurse -Force) {
        $rel = $d.FullName.Substring($src.Length).TrimStart('\')
        New-Item -ItemType Directory -Force -Path (Join-Path $dst $rel) | Out-Null
    }
    foreach ($f in (Get-ChildItem -LiteralPath $src -File -Recurse -Force | Sort-Object Length -Descending)) {
        $rel = $f.FullName.Substring($src.Length).TrimStart('\')
        Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $dst $rel) -Force
    }
}

if ($Clean) {
    if (Test-Path -LiteralPath $b) { Detach-Vhd; Detach-Image $vhdFinal }
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
Clear-StaleDisks $b   # drop disks leaked by earlier builds before making a new one

# Always discard the previous disk image at build start. This prevents a failed
# or partial build from leaving a stale VHDX that can accidentally be booted.
if (Test-Path -LiteralPath $vhd) {
    Write-Stage "Remove previous VHDX before rebuilding"
    Detach-Vhd
    Remove-Item -LiteralPath $vhd -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $vhd) {
        throw "Cannot remove previous VHDX: $vhd. Close QEMU and detach the disk in Disk Management, then run the build again."
    }
    Write-Host "Removed previous disk image: $vhd" -ForegroundColor Yellow
}

# Locate Git for Windows Bash. This is a Windows process; WSL is never invoked.
$bashCandidates = @(
    (Join-Path $env:ProgramFiles "Git\bin\bash.exe"),
    (Join-Path ${env:ProgramFiles(x86)} "Git\bin\bash.exe"),
    (Join-Path $env:LOCALAPPDATA "Programs\Git\bin\bash.exe")
)
$bash = $bashCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -First 1
if (-not $bash) {
    $bashCommand = Get-Command bash.exe -ErrorAction SilentlyContinue
    if ($bashCommand) { $bash = $bashCommand.Source }
}
if (-not $bash) {
    throw "Git for Windows Bash is required (not WSL). Install Git for Windows and native LLVM for Windows, then rerun."
}
$clang = Get-Command clang.exe -ErrorAction SilentlyContinue
$clangxx = Get-Command clang++.exe -ErrorAction SilentlyContinue
$lld = Get-Command lld.exe -ErrorAction SilentlyContinue
if (-not $clang -or -not $clangxx -or -not $lld) {
    throw "Native LLVM tools are missing from PATH. Install LLVM for Windows (clang.exe, clang++.exe, lld.exe); WSL is not used."
}
$edkCandidates = @()
if ($env:EDK2) { $edkCandidates += $env:EDK2 }
$edkCandidates += (Join-Path $root "edk2")
$edkCandidates += "C:\edk2"
$edk2 = $edkCandidates | Where-Object {
    $_ -and (Test-Path -LiteralPath (Join-Path $_ "MdePkg\Include\Uefi.h") -PathType Leaf)
} | Select-Object -First 1
if (-not $edk2) {
    throw "EDK2 headers were not found. Set EDK2 to an EDK2 checkout containing MdePkg\Include\Uefi.h. WSL is not used."
}
$env:EDK2 = $edk2
Write-Host "EDK2 headers: $edk2"
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
    # Windows may create the standard 16 MiB Microsoft Reserved (MSR)
    # partition automatically when preparing a GPT VHDX. It is not a data
    # partition and must not be mistaken for the ESP or MAGIC volume.
    $efiGuid = "{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}"
    $basicGuid = "{ebd0a0a2-b9e5-4433-87c0-68b6b72699c7}"
    $msrGuid = "{e3c9e316-0b5c-4db8-817d-f92df00215ae}"
    $efiPartitions = @($partitions | Where-Object { "$($_.GptType)".Trim("{}").ToLowerInvariant() -eq $efiGuid.Trim("{}") })
    $basicPartitions = @($partitions | Where-Object { "$($_.GptType)".Trim("{}").ToLowerInvariant() -eq $basicGuid.Trim("{}") })
    $msrPartitions = @($partitions | Where-Object { "$($_.GptType)".Trim("{}").ToLowerInvariant() -eq $msrGuid.Trim("{}") })
    if ($efiPartitions.Count -ne 1 -or $basicPartitions.Count -ne 1 -or $partitions.Count -ne (2 + $msrPartitions.Count) -or $msrPartitions.Count -gt 1) {
        $partitionSummary = ($partitions | Sort-Object PartitionNumber | ForEach-Object {
            "number=$($_.PartitionNumber), type=$($_.Type), gptType=$($_.GptType), size=$($_.Size), drive=$($_.DriveLetter)"
        }) -join [Environment]::NewLine
        throw "Expected one EFI System Partition and one MAGIC basic-data partition, optionally plus one MSR; found an unexpected GPT layout on VHDX '$vhd':$([Environment]::NewLine)$partitionSummary"
    }
    $rootPartition = $basicPartitions[0]
    if (-not $rootPartition) { throw "NTFS system partition was not created." }
    if (-not (Test-Path -LiteralPath $sysRoot)) { throw "NTFS system partition was not mounted at $sysRoot" }

    # Copy the whole rootfs recursively: all built EXEs, DLLs, drivers, fonts,
    # assets, and any future files produced by build.sh are staged automatically.
    Copy-Tree $rootfs $sysRoot

    # The current kernel VFS looks up user-mode images by basename at the
    # mounted system-volume root. rootfs\magic is the logical SentinelOS
    # tree, so stage its contents at the volume root as well as preserving
    # the enclosing \magic directory for bootloader/path compatibility.
    $magicTree = Join-Path $rootfs "magic"
    Copy-Tree $magicTree $sysRoot

    foreach ($relative in @("smss.exe","sentlogon.exe","bootvid.exe","explorer.exe","fileexplorer.exe","krnl.exe","hal.dll","System32\smss.exe","System32\sentlogon.exe","System32\bootvid.exe","System32\explorer.exe","System32\fileexplorer.exe","System32\krnl.exe","System32\hal.dll")) {
        $copied = Join-Path $sysRoot $relative
        if (-not (Test-Path -LiteralPath $copied -PathType Leaf) -or (Get-Item -LiteralPath $copied).Length -le 0) {
            throw "Staging verification failed; missing copied file: $copied"
        }
    }

    # Prove the image holds exactly what was built: every file present at the
    # volume root AND under \magic with the same byte length as the source.
    $mismatch = @()
    foreach ($f in Get-ChildItem -LiteralPath $magicTree -File -Recurse -Force) {
        $rel = $f.FullName.Substring($magicTree.Length).TrimStart('\')
        foreach ($dest in @((Join-Path $sysRoot $rel), (Join-Path (Join-Path $sysRoot "magic") $rel))) {
            $g = Get-Item -LiteralPath $dest -ErrorAction SilentlyContinue
            if (-not $g -or $g.Length -ne $f.Length) { $mismatch += $dest }
        }
    }
    if ($mismatch.Count -gt 0) {
        throw ("Staging verification failed for {0} file(s), e.g.:`r`n{1}" -f $mismatch.Count, (($mismatch | Select-Object -First 10) -join "`r`n"))
    }
    # The kernel rejects anything that is not an MZ PE, so check the boot chain.
    foreach ($relative in @("smss.exe","krnl.exe","sentlogon.exe","explorer.exe")) {
        $fs = [IO.File]::OpenRead((Join-Path $sysRoot $relative))
        try { $mz = New-Object byte[] 2; [void]$fs.Read($mz, 0, 2) } finally { $fs.Close() }
        if ($mz[0] -ne 0x4D -or $mz[1] -ne 0x5A) { throw "$relative on the image is not a PE file (no MZ header)." }
    }
    foreach ($l in @($espLetter, $sysLetter)) { try { Write-VolumeCache -DriveLetter $l } catch { } }
    Write-Host ("Verified {0} files; smss.exe is {1} bytes at the volume root." -f @(Get-ChildItem -LiteralPath $magicTree -File -Recurse -Force).Count, (Get-Item -LiteralPath (Join-Path $sysRoot "smss.exe")).Length) -ForegroundColor Green
} finally {
    Detach-Vhd
}
Write-Stage "Install verified disk image"
Detach-Image $vhdFinal
Remove-Item -LiteralPath $vhdFinal -Force -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $vhdFinal) {
    throw "Cannot replace $vhdFinal (locked). Close QEMU or run .\scripts\vhd.ps1 Dismount, then rerun. The verified new image is $vhd"
}
Move-Item -LiteralPath $vhd -Destination $vhdFinal -Force
$vhd = $vhdFinal
Clear-StaleDisks $b
Write-Host "build ok: complete build.sh output staged to $vhd" -ForegroundColor Green
if ($Run) { & (Join-Path $PSScriptRoot "run.ps1") }