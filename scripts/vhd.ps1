<#
.SYNOPSIS
  Safely mount and dismount the SentinelOS VHDX (build\magic-disk.vhdx).

.EXAMPLE
  .\scripts\vhd.ps1 Status
.EXAMPLE
  .\scripts\vhd.ps1 Verify            # mount read-only, check key files, dismount
.EXAMPLE
  .\scripts\vhd.ps1 Mount             # leaves it mounted, prints the drive letters
.EXAMPLE
  .\scripts\vhd.ps1 Dismount
.EXAMPLE
  .\scripts\vhd.ps1 Clean            # detach every leftover Sentinel disk (the extra 'Local Disk' drives)
.EXAMPLE
  .\scripts\vhd.ps1 Run -Script { param($Esp,$Sys) Get-ChildItem $Sys }   # always dismounts after
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('Mount', 'Dismount', 'Status', 'Verify', 'Run', 'Clean')]
    [string]$Action = 'Status',
    [string]$Path,
    [switch]$ReadOnly,
    [switch]$Force,
    [scriptblock]$Script
)
$ErrorActionPreference = 'Stop'

if (-not $Path) {
    $root = if ($PSScriptRoot) { Split-Path $PSScriptRoot -Parent } else { (Get-Location).Path }
    $Path = Join-Path $root 'build\magic-disk.vhdx'
}
$Path = [IO.Path]::GetFullPath($Path)

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { throw 'Run this from an Administrator PowerShell (mounting a VHDX needs it).' }

$efiGuid   = 'c12a7328-f81f-11d2-ba4b-00a0c93ec93b'
$basicGuid = 'ebd0a0a2-b9e5-4433-87c0-68b6b72699c7'

function Get-Img { Get-DiskImage -ImagePath $Path -ErrorAction SilentlyContinue }

function Assert-Exists {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw ("VHDX not found: $Path`n" +
               "scripts\build.ps1 deletes it at the start of every run, so a failed or aborted build leaves none.`n" +
               "Run .\scripts\build.ps1 from an Administrator PowerShell and fix whatever stops it.")
    }
    if ((Get-Item -LiteralPath $Path).Length -lt 1MB) {
        throw "VHDX is unexpectedly small: $Path. Rebuild it with .\scripts\build.ps1"
    }
}

function Assert-NotInUse {
    # QEMU holds the file open; mounting it too can corrupt the NTFS volume.
    $leaf = Split-Path $Path -Leaf
    $busy = @(Get-CimInstance Win32_Process -Filter "Name like 'qemu-system%'" -ErrorAction SilentlyContinue |
              Where-Object { $_.CommandLine -and $_.CommandLine -like "*$leaf*" })
    if ($busy.Count -gt 0) {
        throw "QEMU (pid $($busy[0].ProcessId)) is using $leaf. Close it first; mounting a disk a VM has open can corrupt it."
    }
}

function Get-Parts {
    $img = Get-Img
    if (-not $img -or -not $img.Attached) { return @() }
    $disk = $img | Get-Disk
    @(Get-Partition -DiskNumber $disk.Number)
}

function Pick-Letters([int]$count) {
    $used = [IO.DriveInfo]::GetDrives() | ForEach-Object { $_.Name.Substring(0, 1).ToUpperInvariant() }
    $free = @('Z','Y','X','W','V','U','T','S','R','Q','P','O','N','M') | Where-Object { $_ -notin $used }
    if (@($free).Count -lt $count) { throw 'Not enough free drive letters (need 2).' }
    @($free | Select-Object -First $count)
}

# Returns @{ Esp='Z:\'; Sys='Y:\'; Mounted=$true/$false } where Mounted = we attached it in this call.
function Mount-Sentinel {
    Assert-Exists
    $img = Get-Img
    $weMounted = $false
    if (-not $img -or -not $img.Attached) {
        Assert-NotInUse
        $access = if ($ReadOnly) { 'ReadOnly' } else { 'ReadWrite' }
        Write-Host "Mounting ($access): $Path"
        [void](Mount-DiskImage -ImagePath $Path -StorageType VHDX -Access $access -NoDriveLetter -PassThru)
        $weMounted = $true
    } else {
        Write-Host 'Already attached; reusing it.'
    }
    try {
        $parts = Get-Parts
        $efi = @($parts | Where-Object { "$($_.GptType)".Trim('{}').ToLowerInvariant() -eq $efiGuid })
        $sys = @($parts | Where-Object { "$($_.GptType)".Trim('{}').ToLowerInvariant() -eq $basicGuid })
        if ($efi.Count -ne 1 -or $sys.Count -ne 1) {
            $sum = ($parts | ForEach-Object { "#$($_.PartitionNumber) $($_.GptType) $($_.Size)" }) -join '; '
            throw "Unexpected partition layout (want 1 EFI + 1 basic data): $sum"
        }
        $need = @()
        if (-not $efi[0].DriveLetter -or $efi[0].DriveLetter -eq [char]0) { $need += $efi[0] }
        if (-not $sys[0].DriveLetter -or $sys[0].DriveLetter -eq [char]0) { $need += $sys[0] }
        $letters = @(Pick-Letters $need.Count)
        for ($i = 0; $i -lt $need.Count; $i++) {
            Add-PartitionAccessPath -DiskNumber $need[$i].DiskNumber -PartitionNumber $need[$i].PartitionNumber -AccessPath ($letters[$i] + ':')
        }
        $parts = Get-Parts
        $espL = ($parts | Where-Object { "$($_.GptType)".Trim('{}').ToLowerInvariant() -eq $efiGuid   }).DriveLetter
        $sysL = ($parts | Where-Object { "$($_.GptType)".Trim('{}').ToLowerInvariant() -eq $basicGuid }).DriveLetter
        if (-not $espL -or -not $sysL) { throw 'Drive letters were not assigned.' }
        if (-not (Test-Path "$($sysL):\")) { throw "System volume $($sysL): is not readable (is it formatted NTFS?)." }
        Write-Host "ESP    -> $($espL):\"
        Write-Host "System -> $($sysL):\"
        return @{ Esp = "$($espL):\"; Sys = "$($sysL):\"; Mounted = $weMounted }
    } catch {
        if ($weMounted) { try { [void](Dismount-DiskImage -ImagePath $Path) } catch {} }
        throw
    }
}

function Dismount-Sentinel {
    $img = Get-Img
    if (-not $img -or -not $img.Attached) { Write-Host 'Not mounted. Nothing to do.'; return }
    $letters = @(Get-Parts | Where-Object { $_.DriveLetter -and $_.DriveLetter -ne [char]0 } | ForEach-Object { $_.DriveLetter })
    # Do not sit inside the drive we are about to release.
    foreach ($l in $letters) {
        if ((Get-Location).Path -like "$($l):*") { Set-Location $env:SystemDrive\ }
    }
    foreach ($l in $letters) {
        try { Write-VolumeCache -DriveLetter $l } catch { }   # flush writes before detaching
    }
    $ok = $false
    for ($try = 1; $try -le 5 -and -not $ok; $try++) {
        try { [void](Dismount-DiskImage -ImagePath $Path) } catch { }
        Start-Sleep -Milliseconds 700
        $now = Get-Img
        $ok = (-not $now) -or (-not $now.Attached)
    }
    if (-not $ok) {
        throw ("Could not dismount $Path.`n" +
               "Close any Explorer window, terminal, editor or QEMU using $($letters -join ': ')`: and run this again.")
    }
    Write-Host 'Dismounted cleanly.' -ForegroundColor Green
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

function Show-Status {
    Write-Host "Path:   $Path"
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        Write-Host 'Exists: NO  (run .\scripts\build.ps1 from an Administrator PowerShell)' -ForegroundColor Red
        return
    }
    Write-Host ("Exists: yes ({0:N1} MB on disk)" -f ((Get-Item -LiteralPath $Path).Length / 1MB))
    $img = Get-Img
    if (-not $img -or -not $img.Attached) { Write-Host 'State:  dismounted'; return }
    Write-Host 'State:  MOUNTED' -ForegroundColor Yellow
    Get-Parts | Select-Object PartitionNumber, DriveLetter, Type, @{n='SizeMB';e={[math]::Round($_.Size/1MB)}} | Format-Table -AutoSize
}

function Verify-Files($m) {
    $checks = @(
        @($m.Esp, 'EFI\BOOT\BOOTX64.EFI'),
        @($m.Esp, 'EFI\magic\bootuni.efi'),
        @($m.Sys, 'smss.exe'),
        @($m.Sys, 'System32\smss.exe'),
        @($m.Sys, 'magic\smss.exe'),
        @($m.Sys, 'magic\System32\smss.exe'),
        @($m.Sys, 'krnl.exe'),
        @($m.Sys, 'hal.dll'),
        @($m.Sys, 'sentlogon.exe'),
        @($m.Sys, 'explorer.exe')
    )
    $rows = foreach ($c in $checks) {
        $f = Join-Path $c[0] $c[1]
        $item = Get-Item -LiteralPath $f -ErrorAction SilentlyContinue
        [pscustomobject]@{ File = $f; Exists = [bool]$item; Bytes = $(if ($item) { $item.Length } else { 0 }) }
    }
    $rows | Format-Table -AutoSize
    $bad = @($rows | Where-Object { -not $_.Exists -or $_.Bytes -le 0 })
    if ($bad.Count) { Write-Host "$($bad.Count) file(s) missing or empty." -ForegroundColor Red } else { Write-Host 'All key files present.' -ForegroundColor Green }
}

switch ($Action) {
    'Status'   { Show-Status }
    'Clean'    { Clear-StaleDisks (Split-Path $Path -Parent) -Force:$Force }
    'Mount'    { $null = Mount-Sentinel; Write-Host 'Remember: .\scripts\vhd.ps1 Dismount  (before running QEMU or build.ps1)' -ForegroundColor Yellow }
    'Dismount' { Dismount-Sentinel }
    'Verify'   {
        $script:ReadOnly = [switch]$true
        $m = $null
        try { $m = Mount-Sentinel; Verify-Files $m }
        finally { if ($m -and $m.Mounted) { Dismount-Sentinel } }
    }
    'Run' {
        if (-not $Script) { throw 'Run needs -Script { param($Esp,$Sys) ... }' }
        $m = $null
        try { $m = Mount-Sentinel; & $Script $m.Esp $m.Sys }
        finally { if ($m -and $m.Mounted) { Dismount-Sentinel } }
    }
}