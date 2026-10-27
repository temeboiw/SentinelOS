# Launch SentinelOS in QEMU on native Windows.
# Host CPU and host-rendered VirtIO GPU are requested explicitly.
[CmdletBinding()]
param([switch]$NoGpuAcceleration, [switch]$NoHostCpu)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$b = Join-Path $root "build"
$disk = Join-Path $b "magic-disk.vhdx"
$vars = Join-Path $b "vars.fd"
$qemuCommand = Get-Command "qemu-system-x86_64.exe" -ErrorAction SilentlyContinue
if (-not $qemuCommand) { $qemuCommand = Get-Command "qemu-system-x86_64" -ErrorAction SilentlyContinue }
if (-not $qemuCommand) { throw "qemu-system-x86_64 was not found. Install a Windows QEMU build and add its bin directory to PATH." }
$qemu = $qemuCommand.Source
if (-not (Test-Path $disk -PathType Leaf)) { throw "SentinelOS disk image is missing: $disk. Build the current OS image first." }

# Locate firmware in common Windows QEMU install locations.
$shareCandidates = @()
if ($env:QEMU_SHARE) { $shareCandidates += $env:QEMU_SHARE }
if ($env:ProgramFiles) { $shareCandidates += (Join-Path $env:ProgramFiles "qemu\share") }
if ($env:ProgramW6432) { $shareCandidates += (Join-Path $env:ProgramW6432 "qemu\share") }
$share = $shareCandidates | Where-Object {
    (Test-Path (Join-Path $_ "edk2-x86_64-code.fd")) -and
    ((Test-Path (Join-Path $_ "edk2-i386-vars.fd")) -or (Test-Path (Join-Path $_ "edk2-x86_64-vars.fd")))
} | Select-Object -First 1
if (-not $share) { throw "Could not find edk2-x86_64-code.fd and a writable vars template. Set QEMU_SHARE to the QEMU share directory." }
$code = Join-Path $share "edk2-x86_64-code.fd"
if (-not (Test-Path $vars)) {
    $templates = @((Join-Path $share "edk2-i386-vars.fd"), (Join-Path $share "edk2-x86_64-vars.fd"))
    $template = $templates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $template) { throw "No writable UEFI vars template found in $share" }
    New-Item -ItemType Directory -Force $b | Out-Null
    Copy-Item $template $vars
}
$ramMb = 4096
if ($env:SENTINELOS_RAM_MB) {
    if ($env:SENTINELOS_RAM_MB -notmatch '^[1-9][0-9]*$') { throw "SENTINELOS_RAM_MB must be a positive integer (MiB)." }
    $ramMb = [int64]$env:SENTINELOS_RAM_MB
}
if ($ramMb -gt 131072) { throw "SentinelOS supports at most 131072 MiB (128 GiB) of RAM." }
Write-Host "SentinelOS: RAM $ramMb MiB"

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
Write-Host "SentinelOS: starting QEMU. Unsupported -cpu host or virtio-vga-gl options will be reported by QEMU."
$argsQemu = @("-machine", "q35") + $accelArgs + $cpuArgs + @(
    "-smp", "4",
    "-m", "$($ramMb)M",
    "-drive", "if=pflash,format=raw,readonly=on,file=$code",
    "-drive", "if=pflash,format=raw,file=$vars",
    "-drive", "file=$disk,format=vhdx,if=none,id=d0",
    "-device", "virtio-blk-pci,drive=d0",
    "-netdev", "user,id=net0",
    "-device", "e1000,netdev=net0"
) + $gpuArgs + $displayArgs + @("-serial", "stdio", "-no-reboot")
& $qemu @argsQemu
if ($LASTEXITCODE -ne 0) { throw "QEMU exited with code $LASTEXITCODE. Check WHPX + -cpu host and virtio-vga-gl + SDL OpenGL support in this QEMU build." }
