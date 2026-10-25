# Launch magic OS in QEMU (build first with build.ps1)
$root  = Split-Path $PSScriptRoot -Parent
$b     = "$root\build"
$disk  = "$b\magic-disk.img"
$share = "C:\Program Files\qemu\share"
$code  = "$share\edk2-x86_64-code.fd"
$vars  = "$b\vars.fd"          # persistent UEFI variables (boot entries live here)

if (-not (Test-Path $vars)) {
    $tmpl = "$share\edk2-i386-vars.fd"
    if (-not (Test-Path $tmpl)) { throw "missing $tmpl - check: dir `"$share\edk2-*`"" }
    Copy-Item $tmpl $vars
}

# Set SENTINELOS_RAM_MB to test larger guest RAM (maximum managed by this
# kernel build: 128 GiB). Keep it configurable for smaller development PCs.
$ramMb = 1024
if ($env:SENTINELOS_RAM_MB) {
    if ($env:SENTINELOS_RAM_MB -notmatch '^[1-9][0-9]*$') {
        throw "SENTINELOS_RAM_MB must be a positive integer (MiB)."
    }
    $ramMb = [int64]$env:SENTINELOS_RAM_MB
}
if ($ramMb -gt 131072) {
    throw "This kernel build manages at most 131072 MiB (128 GiB) of RAM."
}
Write-Host "SentinelOS: QEMU guest RAM $ramMb MiB"

qemu-system-x86_64 -machine q35 -m "${ramMb}M" `
  -drive "if=pflash,format=raw,readonly=on,file=$code" `
  -drive "if=pflash,format=raw,file=$vars" `
  -drive "format=raw,file=$disk,index=0" `
  -serial stdio -net none
