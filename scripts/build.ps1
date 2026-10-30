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
# The VHDX partitions are staged through NTFS folder mount points under build\mnt,
# NOT drive letters, so the build can never run out of letters or leave extra
# "Local Disk" drives behind in Explorer.
$mnt = Join-Path $b "mnt"
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

# Share one PowerShell entry point across native Windows and Linux/proot.
# Windows uses the native VHDX + DiskPart workflow below. Linux delegates to
# build.sh, which uses host-native Linux tooling and creates the Linux disk image.
# Detect the OS with RuntimeInformation so this also works in Windows PowerShell
# 5.1, where the PowerShell Core $IsWindows variable does not exist.
$isWindowsHost = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
    [System.Runtime.InteropServices.OSPlatform]::Windows
)
if (-not $isWindowsHost) {
    $bash = Get-Command bash -ErrorAction SilentlyContinue
    if (-not $bash) { throw "Bash is required on Linux/proot. Install it with: apt install bash" }

    $linuxArgs = @()
    if ($Clean) { $linuxArgs += "--clean" }
    if ($Run) { $linuxArgs += "--run" }
    if ($ResetNvram) { $linuxArgs += "--reset-nvram" }
    if ($KeepFallback -or -not $DropFallback) { $linuxArgs += "--keep-fallback" }

    Write-Host "Linux/proot host detected; forwarding build to scripts/build.sh" -ForegroundColor Cyan
    & $bash.Source (Join-Path $PSScriptRoot "build.sh") @linuxArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Linux/proot build failed (exit $LASTEXITCODE)."
    }
    return
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
        [pscustomobject]@{ Number = $d.Number; ImagePath = $ip; Size = $d.Size; IsOffline = $d.IsOffline }
    }
}
# Detach every attached virtual disk that belongs to this project. Leftovers show
# up in Explorer as extra "Local Disk" drives and eat drive letters.
#  - disks whose image is under the build dir / named magic-disk*
#  - orphans: the image file is gone so the path cannot be resolved. The build always
#    creates its disk at exactly 128 GiB, so unresolved file-backed disks of that size
#    are ours. Any other unresolved disk is only touched with -Force.
function Test-ProjectDisk($v, [string]$projectBuildDir, [switch]$Force) {
    if ($v.ImagePath) { return ($v.ImagePath -like "$projectBuildDir\*" -or $v.ImagePath -like "*magic-disk*") }
    return ($Force -or $v.Size -eq 128GB)
}
function Release-DiskLetters([int]$diskNumber) {
    foreach ($p in @(Get-Partition -DiskNumber $diskNumber -ErrorAction SilentlyContinue)) {
        foreach ($ap in @($p.AccessPaths | Where-Object { $_ -and $_ -notlike "\\?\Volume*" })) {
            try { Remove-PartitionAccessPath -DiskNumber $diskNumber -PartitionNumber $p.PartitionNumber -AccessPath $ap -ErrorAction Stop } catch { }
        }
    }
}
function Clear-StaleDisks([string]$projectBuildDir, [switch]$Force) {
    $found = 0
    foreach ($v in @(Get-VirtualDiskInfo)) {
        if (-not (Test-ProjectDisk $v $projectBuildDir -Force:$Force)) { continue }
        $found++
        Write-Host ("Detaching leftover disk {0}: {1}" -f $v.Number, $(if ($v.ImagePath) { $v.ImagePath } else { "(image file gone)" })) -ForegroundColor Yellow
        foreach ($l in @(Get-Partition -DiskNumber $v.Number -ErrorAction SilentlyContinue | ForEach-Object { $_.DriveLetter } | Where-Object { $_ -and $_ -ne [char]0 })) {
            try { Write-VolumeCache -DriveLetter $l } catch { }
        }
        $dev = "\\.\PhysicalDrive" + $v.Number
        try { [void](Dismount-DiskImage -DevicePath $dev -ErrorAction Stop) } catch { }
        if ($v.ImagePath -and (Get-Disk -Number $v.Number -ErrorAction SilentlyContinue)) { Detach-Image $v.ImagePath }
        # Orphans often refuse to dismount. Free their drive letters and take them
        # offline so they stop showing up and stop consuming letters (a reboot removes them).
        if (Get-Disk -Number $v.Number -ErrorAction SilentlyContinue) {
            Release-DiskLetters $v.Number
            try { Set-Disk -Number $v.Number -IsOffline $true -ErrorAction Stop } catch { }
        }
    }
    Start-Sleep -Milliseconds 500
    $left = @(Get-VirtualDiskInfo | Where-Object { (Test-ProjectDisk $_ $projectBuildDir -Force:$Force) -and -not $_.IsOffline })
    if ($left.Count -gt 0) { Write-Warning ("{0} Sentinel disk(s) are still attached. Close Explorer windows or terminals open on them, then run .\scripts\vhd.ps1 Clean (or reboot)." -f $left.Count) }
    elseif ($found -gt 0) { Write-Host "Leftover disks cleaned up." -ForegroundColor Green }
}
# Remove the build\mnt folder mount points. A mount point left on disk is a dangling
# reparse point, so remove it explicitly (non-recursive: never touches volume contents).
function Remove-MountPoints {
    foreach ($n in @("esp", "sys")) {
        $p = Join-Path $mnt $n
        if (-not (Test-Path -LiteralPath $p)) { continue }
        $item = Get-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue
        if ($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            [void](& mountvol.exe ($p + "\") /D 2>&1)
        }
        try { [IO.Directory]::Delete($p, $false) } catch { }
    }
    try { [IO.Directory]::Delete($mnt, $false) } catch { }
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
    if (Test-Path -LiteralPath $b) { Remove-MountPoints; Detach-Vhd; Detach-Image $vhdFinal; Clear-StaleDisks $b }
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
Remove-MountPoints
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
# The NTFS system partition is staged through a folder mount point. DiskPart refuses to
# mount an EFI System Partition on a folder, so the ESP alone gets one temporary letter.
if ((Get-Volume -FilePath $b -ErrorAction Stop).FileSystem -ne "NTFS") { throw "The build folder must be on an NTFS volume (folder mount points are used instead of drive letters): $b" }
$usedLetters = [IO.DriveInfo]::GetDrives() | ForEach-Object { $_.Name.Substring(0, 1).ToUpperInvariant() }
$espLetter = ([char[]]"ZYXWVUTSRQPONMLKJIHGFED") | Where-Object { "$_" -notin $usedLetters } | Select-Object -First 1
if (-not $espLetter) { throw "One free drive letter is required to stage the EFI System Partition (DiskPart cannot mount an ESP on a folder). Free one with 'mountvol /R' or reboot, then retry." }
$espRoot = "$($espLetter):\"
$sysRoot = Join-Path $mnt "sys"
Remove-MountPoints
New-Item -ItemType Directory -Force -Path $sysRoot | Out-Null
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
assign mount="$sysRoot"
"@)
    $espVol = Get-Volume -FilePath $espRoot -ErrorAction SilentlyContinue
    if (-not $espVol -or $espVol.FileSystemLabel -ne "MAGICESP") { throw "EFI System Partition was not mounted at $espRoot" }
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
    $sysVol = Get-Volume -FilePath $sysRoot -ErrorAction SilentlyContinue
    if (-not $sysVol -or $sysVol.FileSystemLabel -ne "MAGIC") { throw "NTFS system partition was not mounted at $sysRoot" }

    # Copy the whole rootfs recursively: all built EXEs, DLLs, drivers, fonts,
    # assets, and any future files produced by build.sh are staged automatically.
    Copy-Tree $rootfs $sysRoot

    # The current kernel VFS looks up user-mode images by basename at the
    # mounted system-volume root. rootfs\magic is the logical SentinelOS
    # tree, so stage its contents at the volume root as well as preserving
    # the enclosing \magic directory for bootloader/path compatibility.
    $magicTree = Join-Path $rootfs "magic"
    Copy-Tree $magicTree $sysRoot

    foreach ($relative in @("System32\smss.exe","System32\sentlogon.exe","System32\bootvid.exe","System32\explorer.exe","System32\fileexplorer.exe","System32\krnl.exe","System32\hal.dll")) {
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
    foreach ($relative in @("System32\smss.exe","System32\krnl.exe","System32\sentlogon.exe","System32\explorer.exe")) {
        $fs = [IO.File]::OpenRead((Join-Path $sysRoot $relative))
        try { $mz = New-Object byte[] 2; [void]$fs.Read($mz, 0, 2) } finally { $fs.Close() }
        if ($mz[0] -ne 0x4D -or $mz[1] -ne 0x5A) { throw "$relative on the image is not a PE file (no MZ header)." }
    }
    foreach ($m in @($espRoot, $sysRoot)) { try { Write-VolumeCache -Path $m } catch { } }
    Write-Host ("Verified {0} files; System32\smss.exe is {1} bytes at the expected system path." -f @(Get-ChildItem -LiteralPath $magicTree -File -Recurse -Force).Count, (Get-Item -LiteralPath (Join-Path $sysRoot "System32\smss.exe")).Length) -ForegroundColor Green
} finally {
    Remove-MountPoints
    Detach-Vhd
    Clear-StaleDisks $b   # never leave an attached disk behind, even when the build failed
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
