# Launch SentinelOS from the expandable GPT VHDX built by scripts/build.ps1.
# Disk layout: FAT32 EFI System Partition + NTFS magic system partition.
# Host CPU and host-rendered VirtIO GPU are requested explicitly.
[CmdletBinding()]
param(
    [switch]$NoGpuAcceleration,
    [switch]$NoHostCpu,
    [switch]$Log,
    [switch]$Verify,
    [ValidateRange(1, 64)][int]$CpuCount = 1
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$b = Join-Path $root "build"
$disk = Join-Path $b "magic-disk.vhdx"
$firmware = Join-Path $b "ovmf.fd"
$qemuCommand = Get-Command "qemu-system-x86_64.exe" -ErrorAction SilentlyContinue
if (-not $qemuCommand) { $qemuCommand = Get-Command "qemu-system-x86_64" -ErrorAction SilentlyContinue }
if (-not $qemuCommand) {
    foreach ($p in @((Join-Path $env:ProgramFiles "qemu\qemu-system-x86_64.exe"), "C:\qemu\qemu-system-x86_64.exe")) {
        if (Test-Path -LiteralPath $p -PathType Leaf) { $qemuCommand = Get-Item -LiteralPath $p; break }
    }
}
if (-not $qemuCommand) { throw "qemu-system-x86_64 was not found. Install a Windows QEMU build and add its bin directory to PATH." }
$qemu = if ($qemuCommand.Source) { $qemuCommand.Source } else { $qemuCommand.FullName }
if (-not (Test-Path $disk -PathType Leaf)) { throw "SentinelOS VHDX is missing: $disk. Run .\scripts\build.ps1 first to create the GPT disk with a FAT32 ESP and NTFS system partition." }
$diskInfo = Get-Item $disk
if ($diskInfo.Length -lt 1MB) { throw "SentinelOS VHDX is unexpectedly small ($($diskInfo.Length) bytes): $disk. Rebuild it with .\scripts\build.ps1." }

# The host must not have the disk mounted while the VM boots from it.
$img = Get-DiskImage -ImagePath $disk -ErrorAction SilentlyContinue
if ($img -and $img.Attached) {
    Write-Warning "VHDX is still mounted on the host; dismounting it before boot."
    try { [void](Dismount-DiskImage -ImagePath $disk) }
    catch { throw "Could not dismount $disk. From an Administrator PowerShell run: .\scripts\vhd.ps1 Dismount" }
}
if ($Verify) {
    $v = Join-Path $PSScriptRoot "vhd.ps1"
    if (Test-Path -LiteralPath $v) { & $v Verify } else { Write-Warning "vhd.ps1 not found; skipping -Verify." }
}

# Use the single OVMF image staged in build; do not combine separate code/vars FD files.
if (-not (Test-Path $firmware -PathType Leaf)) { throw "SentinelOS firmware is missing: $firmware. Place the working OVMF image at build\ovmf.fd." }

$ramMb = 4096
if ($env:SENTINELOS_RAM_MB) {
    if ($env:SENTINELOS_RAM_MB -notmatch '^[1-9][0-9]*$') { throw "SENTINELOS_RAM_MB must be a positive integer (MiB)." }
    $ramMb = [int64]$env:SENTINELOS_RAM_MB
}
if ($ramMb -gt 131072) { throw "SentinelOS supports at most 131072 MiB (128 GiB) of RAM." }
Write-Host "SentinelOS: RAM $ramMb MiB"
Write-Host "SentinelOS: virtual CPUs $CpuCount (use -CpuCount 4 to test SMP)"

# Keep the requested host CPU default; some Windows QEMU builds reject it because it is KVM-oriented.
if ($NoHostCpu) {
    $accelArgs = @("-accel", "tcg")
    $cpuArgs = @("-cpu", "max")
    Write-Warning "Host CPU disabled; using TCG and CPU model max."
} else {
    $accelArgs = @("-accel", "whpx")
    $cpuArgs = @("-cpu", "host")
    Write-Host "SentinelOS: requesting WHPX acceleration and -cpu host"
}
if ($NoGpuAcceleration) {
    $gpuArgs = @("-vga", "none", "-device", "virtio-vga")
    $displayArgs = @("-display", "sdl")
    Write-Warning "Host OpenGL disabled; using 2D VirtIO VGA."
} else {
    # Host OpenGL/VirGL uses the NVIDIA Windows driver when supported by this QEMU build.
    # This is host GPU rendering, not direct PCI passthrough.
    $gpuArgs = @("-vga", "none", "-device", "virtio-vga-gl")
    $displayArgs = @("-display", "sdl,gl=on")
    Write-Host "SentinelOS: requesting NVIDIA-backed host OpenGL/VirGL on VirtIO VGA"
}
Write-Host "SentinelOS: boot disk: $disk (GPT / FAT32 ESP + NTFS system volume)"
Write-Host "SentinelOS: starting QEMU. Firmware failures before SentinelOS starts should be isolated with -NoHostCpu -NoGpuAcceleration."
# The platform switch isolates firmware/QEMU compatibility from the default q35 + VirtIO setup.
$machineArgs = @("-machine", "q35")
if ($NoHostCpu) {
    $machineArgs = @("-machine", "q35")
}
$argsQemu = $machineArgs + $accelArgs + $cpuArgs + @(
    "-smp", "$CpuCount",
    "-m", "$($ramMb)M",
    "-bios", "$firmware",
    "-drive", "file=$disk,format=vhdx,if=none,id=d0",
    "-device", "virtio-blk-pci,drive=d0",
    "-netdev", "user,id=net0",
    "-device", "e1000,netdev=net0"
) + $gpuArgs + $displayArgs + @("-serial", "stdio", "-no-reboot")
$logPath = Join-Path $b "serial.log"
if ($Log) { & $qemu @argsQemu 2>&1 | Tee-Object -FilePath $logPath } else { & $qemu @argsQemu }
$qemuExit = $LASTEXITCODE
if ($Log -and (Test-Path -LiteralPath $logPath)) {
    $logText = Get-Content -LiteralPath $logPath -Raw -ErrorAction SilentlyContinue
    if ($logText -match "disk image not found|disk load failed") {
        Write-Warning "Kernel could not load a user-mode image from the system volume. Make sure kernel\user.h has the NTFS image-loader fix, rebuild, and check $logPath."
    }
}
if ($qemuExit -ne 0) { throw "QEMU exited with code $qemuExit. For diagnosis, retry with -CpuCount 1 (default), -NoGpuAcceleration, or -NoHostCpu to isolate firmware, GPU, and WHPX/CPU issues." }