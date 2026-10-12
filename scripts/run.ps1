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

qemu-system-x86_64 -machine q35 -m 256M `
  -drive "if=pflash,format=raw,readonly=on,file=$code" `
  -drive "if=pflash,format=raw,file=$vars" `
  -drive "format=raw,file=$disk,index=0" `
  -serial stdio -net none