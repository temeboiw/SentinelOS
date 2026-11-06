# SentinelOS native Windows-host build entry point.
# Compiles all PE/EFI images with native Windows LLVM (clang.exe, clang++.exe, lld.exe)
# and creates an expandable GPT VHDX with a FAT32 ESP and NTFS system partition.
# Run from an Administrator PowerShell because DiskPart creates the VHDX.
#
#   .\scripts\build.ps1
#   .\scripts\build.ps1 -Run
#   .\scripts\build.ps1 -Clean
#   .\scripts\build.ps1 -ResetNvram
#   .\scripts\build.ps1 -KeepFallback

param([switch]$Clean, [switch]$Run, [switch]$KeepFallback, [switch]$ResetNvram)
$ErrorActionPreference = "Stop"

$root = Split-Path $PSScriptRoot -Parent
$b = Join-Path $root "build"
$mnt = Join-Path $b "mnt"
$vhdBuilding = Join-Path $b "magic-disk.building.vhdx"
$vhdFinal = Join-Path $b "magic-disk.vhdx"
$buildLog = Join-Path $b "build.log"

function Write-BuildLog([string]$message) {
    $line = "[{0}] {1}" -f (Get-Date -Format o), $message
    Add-Content -LiteralPath $buildLog -Value $line -Encoding UTF8
    Write-Host $line
}

function Write-Stage([string]$name) {
    Write-Host ""
    Write-Host ("========== {0} ==========" -f $name) -ForegroundColor Yellow
}

function Run-Diskpart([string]$text, [switch]$IgnoreErrors) {
    $f = Join-Path $b "dp.txt"
    Set-Content -LiteralPath $f -Value $text -Encoding ASCII
    $out = (& diskpart /s $f) -join [Environment]::NewLine
    if ($LASTEXITCODE -ne 0 -and -not $IgnoreErrors) { throw "diskpart failed:`n$out" }
    return $out
}

function Detach-Image([string]$p) {
    if (-not (Test-Path -LiteralPath $p)) { return }
    $cmd = "select vdisk file=" + [char]34 + $p + [char]34 + [Environment]::NewLine + "detach vdisk"
    [void](Run-Diskpart $cmd -IgnoreErrors)
    $img = Get-DiskImage -ImagePath $p -ErrorAction SilentlyContinue
    if ($img -and $img.Attached) {
        try { [void](Dismount-DiskImage -ImagePath $p) } catch { }
        $img = Get-DiskImage -ImagePath $p -ErrorAction SilentlyContinue
        if ($img -and $img.Attached) { Write-Warning "Could not detach $p" }
    }
}

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

function Detach-Vhd { Detach-Image $vhdBuilding }

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

function Test-Pe32Plus([string]$path) {
    try {
        $fs = [IO.File]::OpenRead($path)
        try {
            $dos = New-Object byte[] 2
            [void]$fs.Read($dos, 0, 2)
            if ($dos[0] -ne 0x4D -or $dos[1] -ne 0x5A) { return $false }
            $peOff = New-Object byte[] 4
            $fs.Position = 0x3C
            [void]$fs.Read($peOff, 0, 4)
            $offset = [BitConverter]::ToInt32($peOff, 0)
            $fs.Position = $offset
            $sig = New-Object byte[] 4
            [void]$fs.Read($sig, 0, 4)
            if ($sig[0] -ne 0x50 -or $sig[1] -ne 0x45 -or $sig[2] -ne 0 -or $sig[3] -ne 0) { return $false }
            $machine = New-Object byte[] 2
            [void]$fs.Read($machine, 0, 2)
            $m = [BitConverter]::ToUInt16($machine, 0)
            return $m -eq 0x8664
        } finally { $fs.Close() }
    } catch { return $false }
}

# ========================================================================
# Clean
# ========================================================================
if ($Clean) {
    Remove-MountPoints; Detach-Vhd; Detach-Image $vhdFinal
    Remove-Item -LiteralPath $b -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "Cleaned $b" -ForegroundColor Green
    return
}

if ($ResetNvram) {
    Remove-Item -LiteralPath (Join-Path $b "vars.fd") -Force -ErrorAction SilentlyContinue
    Write-Host "NVRAM reset - rebuilding boot files and disk image."
}

# ========================================================================
# Admin check
# ========================================================================
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { throw "Run this from an Administrator PowerShell (DiskPart needs it)." }

# ========================================================================
# Directory setup
# ========================================================================
New-Item -ItemType Directory -Force -Path $b | Out-Null
Remove-MountPoints

# Discard previous building image so a failed build never leaves a stale VHDX
if (Test-Path -LiteralPath $vhdBuilding) {
    Write-Stage "Remove previous VHDX before rebuilding"
    Detach-Vhd
    Remove-Item -LiteralPath $vhdBuilding -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $vhdBuilding) {
        throw "Cannot remove previous VHDX: $vhdBuilding. Close QEMU and detach the disk in Disk Management, then run the build again."
    }
    Write-Host "Removed previous disk image: $vhdBuilding" -ForegroundColor Yellow
}

# ========================================================================
# Tool detection
# ========================================================================
$clangPath = (Get-Command clang.exe -ErrorAction SilentlyContinue).Source
$clangxxPath = (Get-Command clang++.exe -ErrorAction SilentlyContinue).Source
$lldPath = (Get-Command lld-link.exe -ErrorAction SilentlyContinue).Source
if (-not $clangPath -or -not $clangxxPath -or -not $lldPath) {
    throw "Native LLVM tools are missing from PATH. Install LLVM for Windows (clang.exe, clang++.exe, lld-link.exe); WSL is not used."
}
Write-Host "  -> clang:   $clangPath"
Write-Host "  -> clang++: $clangxxPath"
Write-Host "  -> lld:     $lldPath"

# ========================================================================
# EDK2 detection
# ========================================================================
$edkCandidates = @()
if ($env:EDK2) { $edkCandidates += $env:EDK2 }
$edkCandidates += (Join-Path $root "edk2")
$edkCandidates += "C:\edk2"
$edk2 = $edkCandidates | Where-Object {
    $_ -and (Test-Path -LiteralPath (Join-Path $_ "MdePkg\Include\Uefi.h") -PathType Leaf)
} | Select-Object -First 1
if (-not $edk2) {
    throw "EDK2 headers were not found. Set EDK2 to an EDK2 checkout containing MdePkg\Include\Uefi.h."
}
$env:EDK2 = $edk2
Write-Host "EDK2 headers: $edk2"

# ========================================================================
# OVMF firmware
# ========================================================================
Write-Stage "Provision UEFI firmware (OVMF) at build\ovmf.fd"
$ovmfDest = Join-Path $b "ovmf.fd"
$ovmfCandidates = @(
    (Join-Path $edk2 "Build\OvmfX64\RELEASE_GCC5\FV\OVMF.fd"),
    (Join-Path $edk2 "Build\OvmfX64\DEBUG_GCC5\FV\OVMF.fd"),
    (Join-Path $edk2 "Build\OvmfX64\RELEASE_VS2019\FV\OVMF.fd"),
    (Join-Path $edk2 "OvmfPkg\Build\OvmfX64\RELEASE_GCC5\FV\OVMF.fd"),
    (Join-Path $env:USERPROFILE "Downloads\RELEASEX64_OVMF.fd")
)
$ovmfSrc = $ovmfCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if ($ovmfSrc) {
    Copy-Item -LiteralPath $ovmfSrc -Destination $ovmfDest -Force
    Write-Host "Provisioned OVMF firmware from $ovmfSrc -> $ovmfDest" -ForegroundColor Green
} elseif (Test-Path -LiteralPath $ovmfDest -PathType Leaf) {
    Write-Warning "No built OVMF.fd found under $edk2; keeping existing build\ovmf.fd."
} else {
    throw "No OVMF firmware available. Build OVMF from $edk2 (OvmfPkg, X64) producing Build\OvmfX64\*/FV/OVMF.fd, then place it at build\ovmf.fd."
}

# ========================================================================
# Paths
# ========================================================================
$esp = Join-Path $b "esp"
$rootfs = Join-Path $b "rootfs"
$sys32 = Join-Path $rootfs "magic\System32"
$drivers = Join-Path $sys32 "drivers"

# ========================================================================
# Stage directories
# ========================================================================
Write-Stage "Stage directory tree"
Remove-Item -LiteralPath $rootfs -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path "$b\obj" | Out-Null
New-Item -ItemType Directory -Force -Path "$esp\EFI\BOOT" | Out-Null
New-Item -ItemType Directory -Force -Path "$esp\EFI\magic" | Out-Null
New-Item -ItemType Directory -Force -Path "$sys32\drivers" | Out-Null
New-Item -ItemType Directory -Force -Path "$rootfs\magic\Program Files" | Out-Null
New-Item -ItemType Directory -Force -Path "$rootfs\magic\Users\Public" | Out-Null

# Logo
$logoSrc = Join-Path $root "resources\logo.bmp"
if (-not (Test-Path -LiteralPath $logoSrc -PathType Leaf) -or (Get-Item -LiteralPath $logoSrc).Length -le 0) {
    throw "error: missing SentinelOS logo: $logoSrc"
}
Copy-Item -LiteralPath $logoSrc -Destination (Join-Path $rootfs "magic\logo.bmp") -Force
Write-Host "  -> SentinelOS logo staged at /magic/logo.bmp"

# Font
$fontSrc = Join-Path $root "assets\fonts\Lato-Regular.ttf"
if (-not (Test-Path -LiteralPath $fontSrc -PathType Leaf)) {
    $fontCandidates = @(
        "C:\Windows\Fonts\Lato-Regular.ttf"
    )
    foreach ($c in $fontCandidates) {
        if (Test-Path -LiteralPath $c -PathType Leaf) { $fontSrc = $c; break }
    }
}
if (Test-Path -LiteralPath $fontSrc -PathType Leaf) {
    Copy-Item -LiteralPath $fontSrc -Destination (Join-Path $sys32 "Lato-Regular.ttf") -Force
    Write-Host ("  -> Lato TrueType font ({0} bytes)" -f (Get-Item -LiteralPath $fontSrc).Length)
} else {
    Write-Warning "Lato-Regular.ttf not found; USER32 will use its fallback glyphs"
}

# ========================================================================
# Compiler flags
# ========================================================================
$COMMON = @(
    "--target=x86_64-unknown-windows",
    "-ffreestanding",
    "-mno-red-zone",
    "-fno-stack-protector",
    "-mno-stack-arg-probe",
    "-mgeneral-regs-only",
    "-O2",
    "-Wall",
    "-Wno-unused-function",
    "-I$($root)\shared",
    "-I$($root)\boot"
)

$EDK_INC = @(
    "-isystem$($edk2)\MdePkg\Include",
    "-isystem$($edk2)\MdePkg\Include\X64"
)

$USER_CFLAGS = @(
    "-target", "x86_64-windows-gnu",
    "-ffreestanding",
    "-fno-stack-protector",
    "-fno-pic",
    "-mno-red-zone",
    "-mno-stack-arg-probe",
    "-mgeneral-regs-only",
    "-O2",
    "-Wall",
    "-I$($root)\user",
    "-I$($root)\shared"
)

$USER_CXXFLAGS = @(
    "-target", "x86_64-windows-gnu",
    "-ffreestanding",
    "-fno-exceptions",
    "-fno-rtti",
    "-fno-threadsafe-statics",
    "-fno-use-cxa-atexit",
    "-fno-builtin",
    "-fno-stack-protector",
    "-fno-pic",
    "-mno-red-zone",
    "-mno-stack-arg-probe",
    "-mgeneral-regs-only",
    "-O2",
    "-Wall",
    "-I$($root)\user",
    "-I$($root)\shared"
)

# Helper: concatenate all argument arrays into one
function Join-Args {
    param($Parts)
    $result = @()
    foreach ($p in $Parts) {
        if ($p -is [array]) { $result += $p }
        else { $result += $p }
    }
    return $result
}

function Invoke-Clang([string[]]$cmdArgs) {
    $cmdline = ($cmdArgs | ForEach-Object { if ($_ -match ' ') { '"' + $_ + '"' } else { $_ } }) -join " "
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $clangPath
    $psi.Arguments = $cmdline
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    $p.WaitForExit()
    if ($p.ExitCode -ne 0) { throw "clang failed (exit $($p.ExitCode)). cmdline: $cmdline" }
}

function Invoke-Clangxx([string[]]$cmdArgs) {
    $cmdline = ($cmdArgs | ForEach-Object { if ($_ -match ' ') { '"' + $_ + '"' } else { $_ } }) -join " "
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $clangxxPath
    $psi.Arguments = $cmdline
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    $p.WaitForExit()
    if ($p.ExitCode -ne 0) { throw "clang++ failed (exit $($p.ExitCode)). cmdline: $cmdline" }
}

function Invoke-Lld([string[]]$cmdArgs) {
    $cmdline = ($cmdArgs | ForEach-Object { if ($_ -match ' ') { '"' + $_ + '"' } else { $_ } }) -join " "
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $lldPath
    $psi.Arguments = $cmdline
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    $stdout = $p.StandardOutput.ReadToEnd()
    $stderr = $p.StandardError.ReadToEnd()
    $p.WaitForExit()
    if ($stdout) { Write-Output $stdout }
    if ($stderr) { Write-Output $stderr }
    if ($p.ExitCode -ne 0) { throw "lld failed (exit $($p.ExitCode)). cmdline: $cmdline" }
}

function Build-Efi([string]$name, [string]$out, [string[]]$extra = @()) {
    $obj = Join-Path $b "obj\$name.o"
    $cargs = $COMMON + @("-fshort-wchar") + $EDK_INC + @("-c") + @(Join-Path $root "boot\$name.c") + @("-o") + @($obj)
    $cmdline = ($cargs -join " ")
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $clangPath
    $psi.Arguments = $cmdline
    $psi.UseShellExecute = $false
    $p = [System.Diagnostics.Process]::Start($psi)
    $p.WaitForExit()
    if ($p.ExitCode -ne 0) { throw "clang failed for $name (exit $($p.ExitCode)). cmdline: $cmdline" }
    $largs = @("-flavor") + @("link") + @("/subsystem:efi_application") + @("/entry:efi_main") + @("/nodefaultlib") + @("/out:$out") + @($obj)
    $cmdline = ($largs -join " ")
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $lldPath
    $psi.Arguments = $cmdline
    $psi.UseShellExecute = $false
    $p = [System.Diagnostics.Process]::Start($psi)
    $p.WaitForExit()
    if ($p.ExitCode -ne 0) { throw "lld failed for $name (exit $($p.ExitCode)). cmdline: $cmdline" }
}

function Build-Driver([string]$name, [string]$source, [string]$base, [string]$entry = "DriverEntry", [string[]]$extra = @()) {
    $obj = Join-Path $b "obj\$name`_driver.o"
    $cargs = $COMMON + $extra + @("-c") + @(Join-Path $root $source) + @("-o") + @($obj)
    Invoke-Clang $cargs
    $largs = @("-flavor") + @("link") + @("/subsystem:native") + @("/entry:$entry") + @("/nodefaultlib") + @("/dynamicbase") + @("/base:$base") + @("/fixed:no") + @("/out:$($drivers)\$name.sys") + @($obj)
    Invoke-Lld $largs
}

function Build-SimpleDll([string]$name, [string]$source, [string]$base, [string[]]$exports, [switch]$NoBuiltin, [string[]]$extraFlags = @(), [string[]]$extraObjs = @(), [string[]]$extraLibs = @()) {
    Write-Host "  -> $name.dll"
    $cflags = $USER_CFLAGS
    if ($NoBuiltin) { $cflags = $cflags + @("-fno-builtin") }
    $cflags = $cflags + $extraFlags
    $obj = Join-Path $b "obj\$name.o"
    Invoke-Clang ($cflags + @("-c") + @(Join-Path $root "user\$source") + @("-o") + @($obj))
    $allObjs = $obj + $extraObjs
    $allArgs = @("/dll") + @("/noentry") + @("/subsystem:native") + @("/nodefaultlib") + @("/fixed:no") + @("/base:$base") + $exports + @("/out:$($sys32)\$name.dll") + $allObjs + $extraLibs
    Invoke-Lld $allArgs
}

function Build-SimpleExe([string]$name, [string]$source, [string]$base, [string]$entry, [string[]]$extraFlags = @(), [string[]]$extraObjs = @(), [string[]]$extraLibs = @(), [string]$subsystem = "native") {
    Write-Host "  -> $name.exe"
    $cflags = $USER_CFLAGS + $extraFlags
    $obj = Join-Path $b "obj\$name.o"
    Invoke-Clang ($cflags + @("-c") + @(Join-Path $root "user\$source") + @("-o") + @($obj))
    $allObjs = $obj + $extraObjs
    $allArgs = @("/subsystem:$subsystem") + @("/entry:$entry") + @("/nodefaultlib") + @("/fixed:no") + @("/base:$base") + @("/out:$($sys32)\$name.exe") + $allObjs + $extraLibs
    Invoke-Lld $allArgs
}

function Build-UserTestAppExe([string]$name, [string]$source, [string]$base, [string]$entry, [string[]]$extraFlags = @(), [string[]]$extraObjs = @(), [string[]]$extraLibs = @(), [string]$subsystem = "windows", [bool]$isCpp = $false) {
    Write-Host "  -> $name.exe"
    $cflags = if ($isCpp) { $USER_CXXFLAGS } else { $USER_CFLAGS }
    $cflags = $cflags + $extraFlags
    $obj = Join-Path $b "obj\$name.o"
    $compiler = if ($isCpp) { $clangxxPath } else { $clangPath }
    & $compiler ($cflags + @("-c") + @(Join-Path $root $source) + @("-o") + @($obj))
    if ($LASTEXITCODE -ne 0) { $cname = if ($isCpp) { "clang++" } else { "clang" }; throw "$cname failed (exit $LASTEXITCODE)." }
    $allObjs = $obj + $extraObjs
    $allArgs = @("/machine:x64") + @("/subsystem:$subsystem") + @("/entry:$entry") + @("/nodefaultlib") + @("/fixed:no") + @("/base:$base") + @("/out:$dst") + $allObjs + $extraLibs
    Invoke-Lld $allArgs
}

# ========================================================================
Write-Stage "Kernel driver modules"
# ========================================================================

Build-Driver "ata" "drivers\ata\ata.c" "0xFFFFFFFF90000000"
if (-not (Test-Path -LiteralPath (Join-Path $drivers "ata.sys") -PathType Leaf) -or (Get-Item -LiteralPath (Join-Path $drivers "ata.sys")).Length -le 0) { throw "error: ATA driver module was not produced" }

Build-Driver "hda" "drivers\audio\hda.c" "0xFFFFFFFF90000000"
if (-not (Test-Path -LiteralPath (Join-Path $drivers "hda.sys") -PathType Leaf) -or (Get-Item -LiteralPath (Join-Path $drivers "hda.sys")).Length -le 0) { throw "error: HDA driver module was not produced" }

# ========================================================================
Write-Stage "UEFI stages"
# ========================================================================

$EXTRA = @()
if ($KeepFallback) { $EXTRA = @("-DKEEP_FALLBACK") }

Build-Efi "bootx64" (Join-Path $esp "EFI\BOOT\BOOTX64.EFI")
Build-Efi "bootuni" (Join-Path $esp "EFI\magic\bootuni.efi") $EXTRA
Build-Efi "loadufi" (Join-Path $esp "EFI\magic\loadufi.efi")

foreach ($efi in @("$esp\EFI\BOOT\BOOTX64.EFI", "$esp\EFI\magic\bootuni.efi", "$esp\EFI\magic\loadufi.efi")) {
    if (-not (Test-Path -LiteralPath $efi -PathType Leaf) -or (Get-Item -LiteralPath $efi).Length -le 0) {
        throw "FATAL: missing or empty UEFI image: $efi"
    }
    Write-BuildLog ("  bin: {0} ({1} bytes)" -f $efi, (Get-Item -LiteralPath $efi).Length)
}

# ========================================================================
Write-Stage "HAL + kernel"
# ========================================================================

# hal.dll
$obj = Join-Path $b "obj\hal.o"
Invoke-Clang ($COMMON + @("-DHAL_BUILD") + @("-c") + @(Join-Path $root "hal\hal.c") + @("-o") + @($obj))
Invoke-Lld (@("/dll") + @("/noentry") + @("/subsystem:native") + @("/nodefaultlib") + @("/base:0xFFFFFFFF81000000") + @("/fixed:no") + @("/implib:$($b)\obj\hal.lib") + @("/out:$($sys32)\hal.dll") + @($obj))

# krnl.exe
$objKernel = Join-Path $b "obj\kernel.o"
$objTrampoline = Join-Path $b "obj\smp_trampoline.o"
Invoke-Clang ($COMMON + @("-c") + @(Join-Path $root "kernel\kernel.c") + @("-o") + @($objKernel))
Invoke-Clang ($COMMON + @("-c") + @(Join-Path $root "kernel\smp_trampoline.S") + @("-o") + @($objTrampoline))
Invoke-Lld (@("/subsystem:native") + @("/entry:kmain") + @("/nodefaultlib") + @("/dynamicbase") + @("/base:0xFFFFFFFF80200000") + @("/fixed:no") + @("/out:$($sys32)\krnl.exe") + @($objKernel) + @($objTrampoline) + @("$($b)\obj\hal.lib"))

# ========================================================================
Write-Stage "User-mode PE images"
# ========================================================================

# vulkan.dll (CPU Vulkan core)
$obj = Join-Path $b "obj\vulkan.o"
Invoke-Clang ($USER_CFLAGS + @("-c") + @(Join-Path $root "user\vulkan.c") + @("-o") + @($obj))
$objSoftpipe = Join-Path $b "obj\vk_softpipe.o"
$softpipeFlags = $USER_CFLAGS | Where-Object { $_ -ne "-mgeneral-regs-only" }
Invoke-Clang ($softpipeFlags + @("-c") + @(Join-Path $root "user\vk_softpipe.c") + @("-o") + @($objSoftpipe))
$vulkanExports = @(
    "/export:vkEnumerateInstanceVersion", "/export:vkEnumerateInstanceExtensionProperties", "/export:vkEnumerateInstanceLayerProperties",
    "/export:vkCreateInstance", "/export:vkDestroyInstance", "/export:vkEnumeratePhysicalDevices",
    "/export:vkGetPhysicalDeviceProperties", "/export:vkGetPhysicalDeviceFeatures", "/export:vkGetPhysicalDeviceFormatProperties",
    "/export:vkGetPhysicalDeviceImageFormatProperties", "/export:vkGetPhysicalDeviceQueueFamilyProperties",
    "/export:vkGetPhysicalDeviceMemoryProperties", "/export:vkCreateDevice", "/export:vkDestroyDevice",
    "/export:vkGetDeviceQueue", "/export:vkDeviceWaitIdle", "/export:vkQueueWaitIdle",
    "/export:vkCreateBuffer", "/export:vkCreateImage", "/export:vkDestroyImage",
    "/export:vkGetImageMemoryRequirements", "/export:vkBindImageMemory", "/export:vkCmdClearColorImage",
    "/export:vkCmdPipelineBarrier", "/export:vkDestroyBuffer", "/export:vkGetBufferMemoryRequirements",
    "/export:vkAllocateMemory", "/export:vkFreeMemory", "/export:vkBindBufferMemory",
    "/export:vkMapMemory", "/export:vkUnmapMemory", "/export:vkFlushMappedMemoryRanges",
    "/export:vkInvalidateMappedMemoryRanges", "/export:vkCreateCommandPool", "/export:vkDestroyCommandPool",
    "/export:vkAllocateCommandBuffers", "/export:vkFreeCommandBuffers", "/export:vkBeginCommandBuffer",
    "/export:vkEndCommandBuffer", "/export:vkResetCommandBuffer", "/export:vkCmdFillBuffer",
    "/export:vkCmdCopyBuffer", "/export:vkCmdCopyBufferToImage", "/export:vkCmdCopyImageToBuffer",
    "/export:vkQueueSubmit", "/export:vkCreateFence", "/export:vkDestroyFence",
    "/export:vkGetFenceStatus", "/export:vkWaitForFences", "/export:vkResetFences",
    "/export:vkCreateSemaphore", "/export:vkDestroySemaphore",
    "/export:vkCreateDescriptorSetLayout", "/export:vkDestroyDescriptorSetLayout",
    "/export:vkCreatePipelineLayout", "/export:vkDestroyPipelineLayout",
    "/export:vkCreateDescriptorPool", "/export:vkDestroyDescriptorPool", "/export:vkResetDescriptorPool",
    "/export:vkAllocateDescriptorSets", "/export:vkFreeDescriptorSets", "/export:vkUpdateDescriptorSets",
    "/export:vkCmdBindDescriptorSets", "/export:vkCreateShaderModule", "/export:vkDestroyShaderModule",
    "/export:vkGetDeviceProcAddr", "/export:vkGetInstanceProcAddr",
    "/export:SentinelVulkanLoaderStatus", "/export:SentinelVulkanGpuStatus",
    "/export:SentinelVulkanSubmitVirGLStream", "/export:SentinelVulkanUploadVirGLBuffer",
    "/export:SentinelVulkanPresentVirGL", "/export:SentinelVulkanCmdDrawTriangle",
    "/export:vkCreateSwapchainKHR", "/export:vkDestroySwapchainKHR",
    "/export:vkGetSwapchainImagesKHR", "/export:vkAcquireNextImageKHR", "/export:vkQueuePresentKHR"
)
Invoke-Lld (@("/dll") + @("/noentry") + @("/subsystem:native") + @("/nodefaultlib") + @("/fixed:no") + @("/dynamicbase") + @("/base:0x60000000") + $vulkanExports + @("/out:$($sys32)\vulkan.dll") + @($obj) + @($objSoftpipe))
Copy-Item -LiteralPath (Join-Path $sys32 "vulkan.dll") -Destination (Join-Path $sys32 "vulkan-1.dll") -Force

# bootvid.exe
Build-SimpleExe "bootvid" "bootvid.c" "0x40600000" "bootvid_main"

# sentlogon.exe
Build-SimpleExe "sentlogon" "sentlogon.c" "0x40500000" "sentlogon_main"

# smss.exe
Build-SimpleExe "smss" "smss.c" "0x40000000" "smss_main"

# taskbar.dll
$taskbarExports = @("/export:TaskbarDraw")
Build-SimpleDll "taskbar" "taskbar.c" "0x40200000" $taskbarExports

# kernel32.dll
$kernel32Exports = @(
    "/export:ExitProcess", "/export:GetStdHandle", "/export:SetStdHandle",
    "/export:LoadLibraryExW", "/export:FindFirstFileExW", "/export:WriteFile",
    "/export:VirtualAlloc", "/export:CreateFileA", "/export:ReadFile", "/export:CloseHandle",
    "/export:QueryDirectoryA", "/export:FindFirstFileA", "/export:FindNextFileA", "/export:FindClose",
    "/export:FindFirstFileW", "/export:FindNextFileW", "/export:SetCurrentDirectoryA",
    "/export:GetLastError", "/export:SetLastError", "/export:GetTickCount64", "/export:Sleep",
    "/export:PeekInput", "/export:ExitWindowsEx", "/export:CreateThread", "/export:WaitForSingleObject",
    "/export:GetCurrentProcessId", "/export:GetCurrentThreadId", "/export:GetCurrentProcess",
    "/export:GetCurrentThread", "/export:GetModuleHandleA", "/export:GetCommandLineA",
    "/export:GetModuleFileNameA", "/export:GetProcAddress", "/export:LoadLibraryA",
    "/export:FreeLibrary", "/export:VirtualFree", "/export:VirtualProtect", "/export:VirtualQuery",
    "/export:GetCurrentDirectoryA", "/export:CreateDirectoryA", "/export:RemoveDirectoryA",
    "/export:DeleteFileA", "/export:MoveFileA", "/export:CopyFileA", "/export:GetFileAttributesA",
    "/export:SetFileAttributesA", "/export:GetFileSize", "/export:SetFilePointer",
    "/export:FlushFileBuffers", "/export:GetTempPathA", "/export:GetFullPathNameA",
    "/export:GetEnvironmentVariableA", "/export:SetEnvironmentVariableA",
    "/export:GetComputerNameA", "/export:GetUserProfileDirectoryA", "/export:GetUserProfileDirectoryW",
    "/export:GetSystemInfo", "/export:GetVersion", "/export:QueryPerformanceCounter",
    "/export:QueryPerformanceFrequency", "/export:GetTickCount", "/export:GetExitCodeProcess",
    "/export:TerminateProcess", "/export:GetProcessHeap", "/export:HeapAlloc",
    "/export:HeapFree", "/export:HeapReAlloc", "/export:HeapSize",
    "/export:LocalAlloc", "/export:LocalFree", "/export:GlobalAlloc", "/export:GlobalFree",
    "/export:GetConsoleMode", "/export:SetConsoleMode", "/export:ReadConsoleA",
    "/export:WriteConsoleA", "/export:GetSystemTime", "/export:GetLocalTime",
    "/export:CreateEventA", "/export:SetEvent", "/export:ResetEvent",
    "/export:CreateMutexA", "/export:ReleaseMutex", "/export:CreateSemaphoreA",
    "/export:ReleaseSemaphore", "/export:WaitForMultipleObjects",
    "/export:GetFileType", "/export:GetCommandLineW", "/export:GetModuleHandleW",
    "/export:GetModuleFileNameW", "/export:CreateFileW", "/export:DeleteFileW",
    "/export:MoveFileW", "/export:CopyFileW", "/export:CreateDirectoryW",
    "/export:RemoveDirectoryW", "/export:GetCurrentDirectoryW", "/export:SetCurrentDirectoryW",
    "/export:GetFullPathNameW", "/export:GetTempPathW", "/export:GetEnvironmentVariableW",
    "/export:SetEnvironmentVariableW", "/export:GetFileAttributesW", "/export:SetFileAttributesW",
    "/export:GetTempFileNameA", "/export:GetTempFileNameW", "/export:GetShortPathNameA",
    "/export:GetLongPathNameA", "/export:GetDiskFreeSpaceA", "/export:GetDiskFreeSpaceExA",
    "/export:GetDriveTypeA", "/export:GetLogicalDrives", "/export:GetLogicalDriveStringsA",
    "/export:GetVolumeInformationA", "/export:GetFileTime", "/export:SetFileTime",
    "/export:GetSystemTimeAsFileTime", "/export:SystemTimeToFileTime",
    "/export:FileTimeToSystemTime", "/export:CompareFileTime",
    "/export:GetFileAttributesExA", "/export:GetFileInformationByHandle",
    "/export:SetEndOfFile", "/export:GetOverlappedResult", "/export:CancelIo",
    "/export:CreatePipe", "/export:PeekNamedPipe", "/export:SetHandleInformation",
    "/export:GetHandleInformation", "/export:DuplicateHandle",
    "/export:CreateProcessA", "/export:GetStartupInfoA", "/export:GetEnvironmentStringsA",
    "/export:FreeEnvironmentStringsA", "/export:GetProcessTimes", "/export:GetThreadTimes",
    "/export:GetPriorityClass", "/export:SetPriorityClass", "/export:GetThreadPriority",
    "/export:SetThreadPriority", "/export:GetCurrentProcessorNumber",
    "/export:GetNativeSystemInfo", "/export:IsProcessorFeaturePresent",
    "/export:GetSystemDirectoryA", "/export:GetWindowsDirectoryA",
    "/export:GetUserDefaultLCID", "/export:GetACP", "/export:GetOEMCP",
    "/export:GetConsoleCP", "/export:GetConsoleOutputCP", "/export:SetConsoleCP",
    "/export:SetConsoleOutputCP", "/export:GetFileSizeEx",
    "/export:CreateFileMappingA", "/export:CreateFileMappingW", "/export:MapViewOfFile",
    "/export:UnmapViewOfFile", "/export:FlushViewOfFile",
    "/export:GetFileInformationByHandleEx", "/export:GetFinalPathNameByHandleA",
    "/export:GetFinalPathNameByHandleW", "/export:GetVolumePathNameA",
    "/export:GetVolumePathNameW", "/export:GetVolumePathNamesForVolumeNameA",
    "/export:GetVolumePathNamesForVolumeNameW", "/export:GetDriveTypeW",
    "/export:GetLogicalDriveStringsW", "/export:GetVolumeInformationW",
    "/export:GetDiskFreeSpaceW", "/export:GetDiskFreeSpaceExW",
    "/export:GetFileAttributesExW", "/export:GetTempPath2A", "/export:GetTempPath2W",
    "/export:GetShortPathNameW", "/export:GetLongPathNameW",
    "/export:GetSystemDirectoryW", "/export:GetWindowsDirectoryW",
    "/export:GetComputerNameW", "/export:GetUserNameA", "/export:GetUserNameW",
    "/export:GetComputerNameExA", "/export:GetComputerNameExW",
    "/export:GetEnvironmentStringsW", "/export:FreeEnvironmentStringsW",
    "/export:ExpandEnvironmentStringsA", "/export:ExpandEnvironmentStringsW",
    "/export:GetModuleHandleExA", "/export:GetModuleHandleExW",
    "/export:GetStartupInfoW", "/export:GetProcessId", "/export:GetThreadId",
    "/export:GetProcessIdOfThread", "/export:OpenProcess", "/export:OpenThread",
    "/export:GetCurrentProcessToken", "/export:GetCurrentThreadToken",
    "/export:GetProcessVersion", "/export:GetProcessShutdownParameters",
    "/export:SetProcessShutdownParameters", "/export:SetErrorMode", "/export:GetErrorMode",
    "/export:GetSystemTimeAdjustment", "/export:GetSystemTimePreciseAsFileTime",
    "/export:GetThreadLocale", "/export:SetThreadLocale",
    "/export:GetUserDefaultLangID", "/export:GetThreadUILanguage",
    "/export:GetUserDefaultUILanguage", "/export:GetSystemDefaultUILanguage",
    "/export:GetSystemDefaultLangID", "/export:GetSystemDefaultLCID",
    "/export:GetConsoleTitleA", "/export:GetConsoleTitleW",
    "/export:SetConsoleTitleW", "/export:GetConsoleScreenBufferInfo",
    "/export:GetConsoleScreenBufferInfoEx", "/export:SetConsoleScreenBufferInfoEx",
    "/export:GetConsoleCursorInfo", "/export:SetConsoleCursorInfo",
    "/export:GetConsoleWindow", "/export:GetConsoleProcessList",
    "/export:GetNumberOfConsoleInputEvents", "/export:GetNumberOfConsoleMouseButtons",
    "/export:GetLargestConsoleWindowSize", "/export:GetConsoleFontSize",
    "/export:GetCurrentConsoleFont", "/export:GetCurrentConsoleFontEx",
    "/export:SetCurrentConsoleFontEx", "/export:GetConsoleHistoryInfo",
    "/export:SetConsoleHistoryInfo", "/export:GetConsoleCommandHistoryA",
    "/export:GetConsoleCommandHistoryW", "/export:GetConsoleAliasesA",
    "/export:GetConsoleAliasesW", "/export:GetConsoleAliasesLengthA",
    "/export:GetConsoleAliasesLengthW", "/export:GetConsoleAliasA",
    "/export:GetConsoleAliasW", "/export:GetConsoleAliasExesA",
    "/export:GetConsoleAliasExesW", "/export:GetConsoleAliasExesLengthA",
    "/export:GetConsoleAliasExesLengthW",
    "/export:AddConsoleAliasA", "/export:AddConsoleAliasW",
    "/export:GetConsoleInputExeNameA", "/export:GetConsoleInputExeNameW",
    "/export:GetConsoleOutputExeNameA", "/export:GetConsoleOutputExeNameW",
    "/export:CreateHardLinkA", "/export:CreateHardLinkW",
    "/export:CreateSymbolicLinkA", "/export:CreateSymbolicLinkW",
    "/export:SetFileValidData", "/export:LockFile", "/export:UnlockFile",
    "/export:LockFileEx", "/export:UnlockFileEx",
    "/export:GetCompressedFileSizeA", "/export:GetCompressedFileSizeW",
    "/export:SearchPathA", "/export:SearchPathW",
    "/export:GetModuleFileNameExA", "/export:GetModuleFileNameExW",
    "/export:HeapValidate", "/export:HeapCompact", "/export:GetProcessHeaps",
    "/export:LocalReAlloc", "/export:LocalSize", "/export:GlobalReAlloc",
    "/export:GlobalSize", "/export:GlobalLock", "/export:GlobalUnlock",
    "/export:GlobalHandle",
    "/export:GetBinaryTypeA", "/export:GetBinaryTypeW",
    "/export:lstrlenA", "/export:lstrlenW",
    "/export:lstrcmpA", "/export:lstrcmpiA",
    "/export:lstrcmpW", "/export:lstrcmpiW",
    "/export:lstrcpyA", "/export:lstrcpynA",
    "/export:lstrcatA",
    "/export:CharLowerA", "/export:CharUpperA",
    "/export:CharLowerBuffA", "/export:CharUpperBuffA",
    "/export:CharLowerW", "/export:CharUpperW",
    "/export:CharLowerBuffW", "/export:CharUpperBuffW",
    "/export:IsCharAlphaA", "/export:IsCharAlphaNumericA",
    "/export:IsCharUpperA", "/export:IsCharLowerA",
    "/export:IsCharAlphaW", "/export:IsCharAlphaNumericW",
    "/export:IsCharUpperW", "/export:IsCharLowerW",
    "/export:CharNextA", "/export:CharPrevA",
    "/export:CharNextW", "/export:CharPrevW",
    "/export:EncodePointer", "/export:DecodePointer",
    "/export:IsBadReadPtr", "/export:IsBadWritePtr",
    "/export:IsBadCodePtr", "/export:IsBadStringPtrA",
    "/export:IsBadStringPtrW",
    "/export:GetStringTypeA", "/export:GetStringTypeW",
    "/export:CompareStringA", "/export:CompareStringW",
    "/export:lstrcpyW", "/export:lstrcpynW",
    "/export:lstrcatW",
    "/export:CharToOemA", "/export:OemToCharA",
    "/export:CharToOemW", "/export:OemToCharW",
    "/export:CharToOemBuffA", "/export:OemToCharBuffA",
    "/export:CharToOemBuffW", "/export:OemToCharBuffW",
    "/export:MulDiv",
    "/export:GetCurrentTime", "/export:GetCPInfo",
    "/export:GetCPInfoExA", "/export:GetCPInfoExW",
    "/export:GetProcessAffinityMask", "/export:SetProcessAffinityMask",
    "/export:GetThreadAffinityMask", "/export:SetThreadAffinityMask",
    "/export:GetProcessPriorityBoost", "/export:SetProcessPriorityBoost",
    "/export:GetThreadPriorityBoost", "/export:SetThreadPriorityBoost",
    "/export:GetCurrentProcessorNumberEx", "/export:SetThreadIdealProcessor",
    "/export:GetThreadIdealProcessorEx", "/export:SetThreadIdealProcessorEx",
    "/export:IsWow64Process", "/export:IsWow64Process2",
    "/export:GetProcessHandleCount", "/export:GetThreadIOPendingFlag",
    "/export:GetProcessIoCounters", "/export:GetThreadDescription",
    "/export:SetThreadDescription", "/export:GetCurrentPackageId",
    "/export:GetCurrentPackageFullName", "/export:GetCurrentPackageFamilyName",
    "/export:GetCurrentApplicationUserModelId",
    "/export:GetSystemTimeAdjustmentPrecise",
    "/export:GetThreadInformation", "/export:SetThreadInformation",
    "/export:GetProcessWorkingSetSize", "/export:SetProcessWorkingSetSize",
    "/export:EmptyWorkingSet",
    "/export:GetProcessMitigationPolicy", "/export:SetProcessMitigationPolicy",
    "/export:GetThreadLocaleName", "/export:GetUserDefaultGeoName",
    "/export:GetGeoInfoA", "/export:GetGeoInfoW",
    "/export:GetTimeZoneInformation", "/export:GetDynamicTimeZoneInformation",
    "/export:SystemTimeToTzSpecificLocalTime",
    "/export:TzSpecificLocalTimeToSystemTime",
    "/export:MultiByteToWideChar", "/export:WideCharToMultiByte",
    "/export:LCMapStringA", "/export:LCMapStringW",
    "/export:LCMapStringEx",
    "/export:GetLocaleInfoA", "/export:GetLocaleInfoW",
    "/export:GetLocaleInfoEx",
    "/export:GetDateFormatA", "/export:GetDateFormatW",
    "/export:GetTimeFormatA", "/export:GetTimeFormatW",
    "/export:GetNumberFormatA", "/export:GetNumberFormatW",
    "/export:GetCurrencyFormatA", "/export:GetCurrencyFormatW",
    "/export:GetCalendarInfoA", "/export:GetCalendarInfoW",
    "/export:GetActiveProcessorCount", "/export:GetMaximumProcessorCount",
    "/export:GetActiveProcessorGroupCount", "/export:GetNumaHighestNodeNumber",
    "/export:GetNumaProcessorNode", "/export:GetNumaAvailableMemoryNode",
    "/export:GetNumaAvailableMemoryNodeEx", "/export:GetNumaNodeProcessorMask",
    "/export:GetNumaProximityNode", "/export:GetNumaProximityNodeEx",
    "/export:CreateFile2", "/export:MoveFileExA", "/export:MoveFileExW",
    "/export:CopyFileExA", "/export:CopyFileExW",
    "/export:ReplaceFileA", "/export:ReplaceFileW",
    "/export:CreateDirectoryExA", "/export:CreateDirectoryExW",
    "/export:CancelIoEx", "/export:GetOverlappedResultEx",
    "/export:WaitForSingleObjectEx", "/export:WaitForMultipleObjectsEx",
    "/export:SleepEx", "/export:SignalObjectAndWait",
    "/export:CreateEventW", "/export:CreateMutexW", "/export:CreateSemaphoreW",
    "/export:OpenEventA", "/export:OpenEventW",
    "/export:OpenMutexA", "/export:OpenMutexW",
    "/export:OpenSemaphoreA", "/export:OpenSemaphoreW",
    "/export:PulseEvent",
    "/export:CreateWaitableTimerA", "/export:CreateWaitableTimerW",
    "/export:SetWaitableTimer", "/export:CancelWaitableTimer",
    "/export:LoadLibraryW", "/export:FreeLibraryAndExitThread",
    "/export:GetDllDirectoryA", "/export:GetDllDirectoryW",
    "/export:SetDllDirectoryA", "/export:SetDllDirectoryW",
    "/export:GetSystemWindowsDirectoryA", "/export:GetSystemWindowsDirectoryW",
    "/export:GetSystemWow64DirectoryA", "/export:GetSystemWow64DirectoryW",
    "/export:GetSystemWow64Directory2A", "/export:GetSystemWow64Directory2W",
    "/export:CreateProcessW", "/export:WinExec",
    "/export:GetVersionExA", "/export:GetVersionExW",
    "/export:VerifyVersionInfoA", "/export:VerifyVersionInfoW",
    "/export:GetSystemTimes",
    "/export:GlobalFlags", "/export:LocalFlags",
    "/export:HeapCreate", "/export:HeapDestroy",
    "/export:HeapLock", "/export:HeapUnlock",
    "/export:HeapSetInformation", "/export:HeapQueryInformation",
    "/export:GlobalMemoryStatusEx", "/export:GlobalMemoryStatus",
    "/export:GetPhysicallyInstalledSystemMemory",
    "/export:GetLogicalProcessorInformation", "/export:GetLogicalProcessorInformationEx",
    "/export:GetProcessDefaultCpuSets", "/export:SetProcessDefaultCpuSets",
    "/export:GetThreadSelectedCpuSets", "/export:SetThreadSelectedCpuSets",
    "/export:GetProcessGroupAffinity", "/export:GetThreadGroupAffinity",
    "/export:SetThreadGroupAffinity",
    "/export:GetNumaNodeProcessorMaskEx",
    "/export:GetProcessVersionEx",
    "/export:GetSystemDefaultLCIDEx", "/export:GetUserDefaultLCIDEx",
    "/export:GetSystemDirectory", "/export:GetWindowsDirectory",
    "/export:GetModuleFileName", "/export:GetModuleHandle",
    "/export:GetCommandLine", "/export:GetTempPath",
    "/export:GetCurrentDirectory", "/export:SetCurrentDirectory",
    "/export:GetEnvironmentVariable", "/export:SetEnvironmentVariable",
    "/export:GetFileAttributes", "/export:SetFileAttributes",
    "/export:GetLargePageMinimum", "/export:GetSystemRegistryQuota",
    "/export:GetVolumeNameForVolumeMountPointA", "/export:GetVolumeNameForVolumeMountPointW",
    "/export:SetVolumeMountPointA", "/export:SetVolumeMountPointW",
    "/export:DeleteVolumeMountPointA", "/export:DeleteVolumeMountPointW",
    "/export:FindFirstVolumeA", "/export:FindFirstVolumeW",
    "/export:FindNextVolumeA", "/export:FindNextVolumeW",
    "/export:FindVolumeClose",
    "/export:FindFirstVolumeMountPointA", "/export:FindFirstVolumeMountPointW",
    "/export:FindNextVolumeMountPointA", "/export:FindNextVolumeMountPointW",
    "/export:FindVolumeMountPointClose",
    "/export:SetVolumeLabelA", "/export:SetVolumeLabelW",
    "/export:GetVolumeInformationByHandleW",
    "/export:GetFirmwareType",
    "/export:GetFirmwareEnvironmentVariableA", "/export:GetFirmwareEnvironmentVariableW",
    "/export:SetFirmwareEnvironmentVariableA", "/export:SetFirmwareEnvironmentVariableW",
    "/export:GetSystemPreferredUILanguages", "/export:GetUserPreferredUILanguages",
    "/export:GetThreadPreferredUILanguages", "/export:GetProcessPreferredUILanguages",
    "/export:SetThreadPreferredUILanguages", "/export:SetProcessPreferredUILanguages",
    "/export:GetProcessMemoryInfo", "/export:GetProcessWorkingSetSizeEx",
    "/export:SetProcessWorkingSetSizeEx", "/export:GetProcessMemoryInfoEx",
    "/export:GetSystemWow64Directory", "/export:GetSystemWindowsDirectory",
    "/export:GetDllDirectory", "/export:SetDllDirectory",
    "/export:CreateFileTransactedA", "/export:CreateFileTransactedW",
    "/export:DeleteFileTransactedA", "/export:DeleteFileTransactedW",
    "/export:MoveFileTransactedA", "/export:MoveFileTransactedW",
    "/export:CopyFileTransactedA", "/export:CopyFileTransactedW",
    "/export:CreateDirectoryTransactedA", "/export:CreateDirectoryTransactedW",
    "/export:RemoveDirectoryTransactedA", "/export:RemoveDirectoryTransactedW",
    "/export:IsDebuggerPresent", "/export:CheckRemoteDebuggerPresent",
    "/export:OutputDebugStringA", "/export:OutputDebugStringW",
    "/export:DebugBreak",
    "/export:GetThreadErrorMode", "/export:SetThreadErrorMode",
    "/export:SetThreadStackGuarantee", "/export:GetThreadStackGuarantee",
    "/export:GetProcessDefaultLayout", "/export:SetProcessDefaultLayout",
    "/export:GetSystemPowerStatus", "/export:SetSystemPowerState",
    "/export:GetSystemDefaultLocaleName", "/export:GetUserDefaultLocaleName",
    "/export:GetLocaleInfoByName", "/export:GetCalendarInfoEx",
    "/export:GetDateFormatEx", "/export:GetTimeFormatEx",
    "/export:GetNumberFormatEx", "/export:GetCurrencyFormatEx",
    "/export:GetDurationFormat", "/export:GetDurationFormatEx",
    "/export:FindNLSString", "/export:FindNLSStringEx",
    "/export:CompareStringEx",
    "/export:SetFileInformationByHandle",
    "/export:GetFileAttributesTransactedA", "/export:GetFileAttributesTransactedW",
    "/export:SetFileAttributesTransactedA", "/export:SetFileAttributesTransactedW",
    "/export:RtlLookupFunctionEntry", "/export:RtlVirtualUnwind",
    "/export:UnhandledExceptionFilter",
    "/export:InitializeCriticalSection", "/export:EnterCriticalSection",
    "/export:TryEnterCriticalSection", "/export:LeaveCriticalSection",
    "/export:DeleteCriticalSection",
    "/export:SetUnhandledExceptionFilter",
    "/export:GetExitCodeThread",
    "/export:RtlCaptureContext",
    "/export:DisableThreadLibraryCalls",
    "/export:InitializeSListHead",
    "/export:TlsGetValue", "/export:TlsSetValue",
    "/export:WakeAllConditionVariable", "/export:SleepConditionVariableSRW",
    "/export:AcquireSRWLockExclusive", "/export:ReleaseSRWLockExclusive",
    "/export:InitializeConditionVariable", "/export:WakeConditionVariable",
    "/export:AreFileApisANSI", "/export:IsValidCodePage",
    "/export:FormatMessageA", "/export:FormatMessageW",
    "/export:FindResourceW", "/export:LoadResource",
    "/export:LockResource", "/export:SizeofResource"
)
$obj = Join-Path $b "obj\kernel32.o"
Invoke-Clang ($USER_CFLAGS + @("-c") + @(Join-Path $root "user\kernel32.c") + @("-o") + @($obj))
Invoke-Lld (@("/dll") + @("/noentry") + @("/subsystem:native") + @("/nodefaultlib") + @("/base:0x41000000") + @("/fixed:no") + @("/implib:$($b)\obj\kernel32.lib") + $kernel32Exports + @("/out:$($sys32)\kernel32.dll") + @($obj))

# winmm.dll
$winmmExports = @("/export:timeGetTime")
$obj = Join-Path $b "obj\winmm.o"
Invoke-Clang ($USER_CFLAGS + @("-fno-builtin") + @("-c") + @(Join-Path $root "user\winmm.c") + @("-o") + @($obj))
Invoke-Lld (@("/dll") + @("/noentry") + @("/subsystem:native") + @("/nodefaultlib") + @("/fixed:no") + @("/base:0x43200000") + $winmmExports + @("/out:$($sys32)\winmm.dll") + @($obj))

# ws2_32.dll
$ws2_32Exports = @(
    "/export:WSAIsBlocking", "/export:inet_pton", "/export:inet_ntop",
    "/export:getaddrinfo", "/export:freeaddrinfo", "/export:getnameinfo",
    "/export:WSAIoctl", "/export:WSAEventSelect", "/export:WSACreateEvent",
    "/export:WSACloseEvent", "/export:WSAWaitForMultipleEvents",
    "/export:WSAEnumNetworkEvents", "/export:WSAAsyncSelect",
    "/export:WSASocketW", "/export:WSARecvFrom", "/export:WSASendTo",
    "/export:GetAddrInfoW", "/export:FreeAddrInfoW", "/export:GetNameInfoW",
    "/export:WSAAddressToStringW", "/export:WSAStringToAddressW",
    "/export:WSASetEvent", "/export:WSAResetEvent",
    "/export:accept,@1", "/export:bind,@2", "/export:closesocket,@3",
    "/export:connect,@4", "/export:getpeername,@5", "/export:getsockname,@6",
    "/export:getsockopt,@7", "/export:htonl,@8", "/export:htons,@9",
    "/export:ioctlsocket,@10", "/export:inet_addr,@11", "/export:inet_ntoa,@12",
    "/export:listen,@13", "/export:ntohl,@14", "/export:ntohs,@15",
    "/export:recv,@16", "/export:recvfrom,@17", "/export:select,@18",
    "/export:send,@19", "/export:sendto,@20", "/export:setsockopt,@21",
    "/export:shutdown,@22", "/export:socket,@23",
    "/export:gethostname,@57",
    "/export:WSAGetLastError,@111", "/export:WSASetLastError,@112",
    "/export:WSAStartup,@115", "/export:WSACleanup,@116",
    "/export:gethostbyaddr,@51", "/export:gethostbyname,@52",
    "/export:getprotobyname,@53", "/export:getprotobynumber,@54",
    "/export:getservbyname,@55", "/export:getservbyport,@56",
    "/export:__WSAFDIsSet,@151"
)
$obj = Join-Path $b "obj\ws2_32.o"
Invoke-Clang ($USER_CFLAGS + @("-fno-builtin") + @("-c") + @(Join-Path $root "user\ws2_32.c") + @("-o") + @($obj))
Invoke-Lld (@("/dll") + @("/noentry") + @("/subsystem:native") + @("/nodefaultlib") + @("/fixed:no") + @("/dynamicbase") + @("/base:0x43400000") + $ws2_32Exports + @("/out:$($sys32)\ws2_32.dll") + @($obj))

# shell32.dll
$shell32Exports = @("/export:CommandLineToArgvW")
$obj = Join-Path $b "obj\shell32.o"
Invoke-Clang ($USER_CFLAGS + @("-fno-builtin") + @("-c") + @(Join-Path $root "user\shell32.c") + @("-o") + @($obj))
Invoke-Lld (@("/dll") + @("/noentry") + @("/subsystem:native") + @("/nodefaultlib") + @("/fixed:no") + @("/dynamicbase") + @("/base:0x43500000") + $shell32Exports + @("/out:$($sys32)\shell32.dll") + @($obj) + @("$($b)\obj\kernel32.lib"))

# imageres.dll
New-Item -ItemType Directory -Force -Path "$sys32\icons" | Out-Null
Copy-Item -LiteralPath "$($root)\assets\icons-bmp\*.bmp" -Destination "$sys32\icons\" -Force
$obj = Join-Path $b "obj\imageres.o"
$imageresExports = @(
    "/export:GetImageResourceCount", "/export:GetImageResourcePathA",
    "/export:GetImageResourceNameA", "/export:FindImageResourceA",
    "/export:GetImageResourceIdA", "/export:GetImageResourceDataA"
)
Invoke-Clang ($USER_CFLAGS + @("-fno-builtin") + @("-c") + @(Join-Path $root "user\imageres.c") + @("-o") + @($obj))
Invoke-Lld (@("/dll") + @("/noentry") + @("/subsystem:native") + @("/nodefaultlib") + @("/fixed:no") + @("/dynamicbase") + @("/base:0x43B00000") + $imageresExports + @("/out:$($sys32)\imageres.dll") + @($obj))

# advapi32.dll
$advapi32Exports = @(
    "/export:RegCloseKey", "/export:RegOpenKeyExW", "/export:RegOpenKeyExA",
    "/export:RegCreateKeyExA", "/export:RegDeleteKeyA", "/export:RegDeleteTreeA",
    "/export:RegDeleteValueA", "/export:RegGetValueW", "/export:RegGetValueA",
    "/export:RegQueryValueExW", "/export:RegQueryValueExA",
    "/export:RegFlushKey", "/export:RegEnumKeyExA", "/export:RegEnumValueA",
    "/export:RegSetValueExA", "/export:RegQueryInfoKeyA", "/export:RegCopyTreeA",
    "/export:RegConnectRegistryA", "/export:RegNotifyChangeKeyValue",
    "/export:RegCreateKeyExW", "/export:RegDeleteKeyW",
    "/export:RegDeleteTreeW", "/export:RegDeleteValueW",
    "/export:RegEnumKeyExW", "/export:RegEnumValueW",
    "/export:RegSetValueExW", "/export:RegQueryInfoKeyW",
    "/export:RegCopyTreeW", "/export:RegConnectRegistryW",
    "/export:RegGetSecurityInfo", "/export:RegSetSecurityInfo",
    "/export:SystemFunction036"
)
$obj = Join-Path $b "obj\advapi32.o"
Invoke-Clang ($USER_CFLAGS + @("-fno-builtin") + @("-c") + @(Join-Path $root "user\advapi32.c") + @("-o") + @($obj))
Invoke-Lld (@("/dll") + @("/noentry") + @("/subsystem:native") + @("/nodefaultlib") + @("/fixed:no") + @("/base:0x43600000") + $advapi32Exports + @("/out:$($sys32)\advapi32.dll") + @($obj))

# iphlpapi.dll
$iphlpapiExports = @(
    "/export:GetAdaptersAddresses", "/export:ConvertInterfaceIndexToLuid",
    "/export:ConvertInterfaceLuidToNameW"
)
$obj = Join-Path $b "obj\iphlpapi.o"
Invoke-Clang ($USER_CFLAGS + @("-fno-builtin") + @("-c") + @(Join-Path $root "user\iphlpapi.c") + @("-o") + @($obj))
Invoke-Lld (@("/dll") + @("/noentry") + @("/subsystem:native") + @("/nodefaultlib") + @("/fixed:no") + @("/base:0x43700000") + $iphlpapiExports + @("/out:$($sys32)\iphlpapi.dll") + @($obj))

# userenv.dll
$userenvExports = @("/export:GetUserProfileDirectoryW")
$obj = Join-Path $b "obj\userenv.o"
Invoke-Clang ($USER_CFLAGS + @("-fno-builtin") + @("-c") + @(Join-Path $root "user\userenv.c") + @("-o") + @($obj))
Invoke-Lld (@("/dll") + @("/noentry") + @("/subsystem:native") + @("/nodefaultlib") + @("/fixed:no") + @("/base:0x43800000") + $userenvExports + @("/out:$($sys32)\userenv.dll") + @($obj))

# comdlg32.dll
$comdlg32Exports = @("/export:GetOpenFileNameA", "/export:GetSaveFileNameA", "/export:CommDlgExtendedError")
$obj = Join-Path $b "obj\comdlg32.o"
Invoke-Clang ($USER_CFLAGS + @("-fno-builtin") + @("-c") + @(Join-Path $root "user\comdlg32.c") + @("-o") + @($obj))
Invoke-Lld (@("/dll") + @("/noentry") + @("/subsystem:native") + @("/nodefaultlib") + @("/fixed:no") + @("/base:0x43900000") + $comdlg32Exports + @("/out:$($sys32)\comdlg32.dll") + @($obj))

# msvcp140.dll
$obj = Join-Path $b "obj\msvcp140.o"
Invoke-Clang ($USER_CFLAGS + @("-fno-builtin") + @("-c") + @(Join-Path $root "user\msvcp140.c") + @("-o") + @($obj))
Invoke-Lld (@("/dll") + @("/noentry") + @("/subsystem:native") + @("/nodefaultlib") + @("/fixed:no") + @("/base:0x43A00000") + @("/def:$($root)\user\msvcp140.def") + @("/out:$($sys32)\msvcp140.dll") + @($obj) + @("$($b)\obj\kernel32.lib"))

# vcruntime140_1.dll
$vcruntime140_1Exports = @("/export:__CxxFrameHandler4")
$obj = Join-Path $b "obj\vcruntime140_1.o"
Invoke-Clang ($USER_CFLAGS + @("-fno-builtin") + @("-c") + @(Join-Path $root "user\vcruntime140_1.c") + @("-o") + @($obj))
Invoke-Lld (@("/dll") + @("/noentry") + @("/subsystem:native") + @("/nodefaultlib") + @("/fixed:no") + @("/base:0x43B00000") + $vcruntime140_1Exports + @("/out:$($sys32)\vcruntime140_1.dll") + @($obj))

# opengl32.dll
$oglFlags = $USER_CFLAGS | Where-Object { $_ -ne "-mgeneral-regs-only" }
$obj = Join-Path $b "obj\opengl32.o"
$opengl32Exports = @(
    "/export:glClearColor", "/export:glClear", "/export:glViewport", "/export:glScissor",
    "/export:glEnable", "/export:glDisable", "/export:glIsEnabled", "/export:glGetError",
    "/export:glGetString", "/export:glDrawArrays", "/export:glDrawElements",
    "/export:glVertexPointer", "/export:glColorPointer", "/export:glTexCoordPointer",
    "/export:glEnableClientState", "/export:glDisableClientState", "/export:glLineWidth",
    "/export:glBlendFunc", "/export:glPixelStorei", "/export:glClearStencil", "/export:glClearDepth",
    "/export:glDepthMask", "/export:glDepthFunc", "/export:glStencilMask", "/export:glStencilFunc",
    "/export:glStencilOp", "/export:glTexParameteri", "/export:glTexImage2D",
    "/export:glBindTexture", "/export:glGenTextures", "/export:glDeleteTextures",
    "/export:glGetIntegerv", "/export:glGetFloatv", "/export:glGetBooleanv", "/export:glReadPixels",
    "/export:glGetDoublev", "/export:glFinish", "/export:glFlush", "/export:wglGetProcAddress",
    "/export:wglMakeCurrent", "/export:wglDeleteContext", "/export:wglCreateContext",
    "/export:wglGetCurrentDC", "/export:wglGetCurrentContext", "/export:wglShareLists",
    "/export:SwapBuffers", "/export:glDrawBuffer", "/export:glReadBuffer",
    "/export:glGetTexParameteriv", "/export:glGetTexLevelParameteriv", "/export:glTexSubImage2D",
    "/export:glColor4f", "/export:glColor4ub", "/export:glClearAccum"
)
Invoke-Clang ($oglFlags + @("-fno-builtin") + @("-c") + @(Join-Path $root "user\opengl32.c") + @("-o") + @($obj))
Invoke-Lld (@("/dll") + @("/noentry") + @("/subsystem:native") + @("/nodefaultlib") + @("/fixed:no") + @("/dynamicbase") + @("/base:0x62000000") + $opengl32Exports + @("/out:$($sys32)\opengl32.dll") + @($obj))

# Universal CRT runtime
$obj = Join-Path $b "obj\ucrt_runtime.o"
$ucrtExports = @(
    "/export:_initterm_e", "/export:_initterm",
    "/export:_errno", "/export:malloc", "/export:calloc", "/export:realloc", "/export:free",
    "/export:_msize", "/export:memcpy", "/export:memmove", "/export:memset",
    "/export:memcmp", "/export:memchr", "/export:strlen", "/export:strcmp",
    "/export:strncmp", "/export:strchr", "/export:strstr", "/export:strrchr",
    "/export:atoi", "/export:strtol", "/export:strtoul", "/export:strtoll", "/export:strtoull",
    "/export:wcstombs", "/export:mbstowcs",
    "/export:isprint", "/export:strspn", "/export:strcspn",
    "/export:bsearch", "/export:qsort",
    "/export:rand", "/export:srand",
    "/export:_initialize_onexit_table", "/export:_execute_onexit_table",
    "/export:_cexit", "/export:_get_errno", "/export:_set_errno",
    "/export:_errno", "/export:_initterm", "/export:_initterm_e",
    "/export:_beginthreadex", "/export:terminate", "/export:__std_terminate",
    "/export:_purecall", "/export:abort", "/export:_invoke_watson",
    "/export:strerror", "/export:_configure_narrow_argv",
    "/export:_initialize_narrow_environment", "/export:_seh_filter_dll",
    "/export:__std_type_info_destroy_list", "/export:__C_specific_handler"
)
Invoke-Clang ($USER_CFLAGS + @("-fno-builtin") + @("-c") + @(Join-Path $root "user\ucrt_runtime.c") + @("-o") + @($obj))
Invoke-Lld (@("/dll") + @("/noentry") + @("/subsystem:native") + @("/nodefaultlib") + @("/fixed:no") + @("/base:0x43000000") + $ucrtExports + @("/out:$($sys32)\api-ms-win-crt-runtime-l1-1-0.dll") + @($obj))

# API-set contract copies
Copy-Item -LiteralPath (Join-Path $sys32 "api-ms-win-crt-runtime-l1-1-0.dll") -Destination (Join-Path $sys32 "ucrtbase.dll") -Force
foreach ($contract in @(
    "api-ms-win-crt-heap-l1-1-0.dll",
    "api-ms-win-crt-string-l1-1-0.dll",
    "api-ms-win-crt-stdio-l1-1-0.dll",
    "api-ms-win-crt-math-l1-1-0.dll",
    "api-ms-win-crt-convert-l1-1-0.dll",
    "api-ms-win-crt-environment-l1-1-0.dll"
)) {
    Copy-Item -LiteralPath (Join-Path $sys32 "api-ms-win-crt-runtime-l1-1-0.dll") -Destination (Join-Path $sys32 $contract) -Force
}

# vcruntime140.dll
$vcruntime140Exports = @(
    "/export:memcpy", "/export:memmove", "/export:memset", "/export:memcmp",
    "/export:memchr", "/export:strlen", "/export:strcmp", "/export:strncmp",
    "/export:strchr", "/export:strstr", "/export:strrchr",
    "/export:malloc", "/export:calloc", "/export:realloc", "/export:free",
    "/export:_msize",
    "/export:__std_type_info_destroy_list", "/export:__C_specific_handler",
    "/export:_purecall", "/export:__std_terminate",
    "/export:_invoke_watson", "/export:_CxxThrowException"
)
Invoke-Lld (@("/dll") + @("/noentry") + @("/subsystem:native") + @("/nodefaultlib") + @("/fixed:no") + @("/base:0x43100000") + $vcruntime140Exports + @("/out:$($sys32)\vcruntime140.dll") + @($obj))

# gdi32.dll
$obj = Join-Path $b "obj\gdi32.o"
$gdi32Exports = @(
    "/export:CreateDCA", "/export:CreateCompatibleDC", "/export:DeleteDC",
    "/export:GetDC", "/export:ReleaseDC", "/export:SelectObject",
    "/export:DeleteObject", "/export:GetStockObject",
    "/export:CreatePen", "/export:CreateSolidBrush", "/export:CreateHatchBrush",
    "/export:GetBkColor", "/export:SetBkColor", "/export:SetTextColor",
    "/export:SetBkMode", "/export:CreateCompatibleBitmap", "/export:GetObjectA",
    "/export:SetPixel", "/export:GetPixel", "/export:MoveToEx",
    "/export:LineTo", "/export:Rectangle", "/export:Ellipse",
    "/export:FillRect", "/export:BitBlt", "/export:StretchBlt",
    "/export:TextOutA", "/export:DrawTextA",
    "/export:GetDeviceCaps", "/export:CreateFontA", "/export:GetTextMetricsA",
    "/export:CreateRectRgn", "/export:SelectClipRgn", "/export:GetMapMode",
    "/export:SetMapMode", "/export:CreatePalette", "/export:GetNearestColor",
    "/export:SetDCBrushColor", "/export:SetDCPenColor",
    "/export:GetTextExtentPoint32A", "/export:SetTextAlign",
    "/export:GetGraphicsMode", "/export:SetGraphicsMode",
    "/export:GetDIBits", "/export:SetDIBits"
)
Invoke-Clang ($USER_CFLAGS + @("-c") + @(Join-Path $root "user\gdi32.c") + @("-o") + @($obj))
Invoke-Lld (@("/dll") + @("/noentry") + @("/subsystem:native") + @("/nodefaultlib") + @("/base:0x41200000") + @("/implib:$($b)\obj\gdi32.lib") + $gdi32Exports + @("/out:$($sys32)\gdi32.dll") + @($obj))

# user32.dll
$objUser32 = Join-Path $b "obj\user32.o"
$objSoft3dUser32 = Join-Path $b "obj\soft3d.o"
Invoke-Clang ($USER_CFLAGS + @("-c") + @(Join-Path $root "user\user32.c") + @("-o") + @($objUser32))
Invoke-Clang ($USER_CFLAGS + @("-c") + @(Join-Path $root "user\soft3d.c") + @("-o") + @($objSoft3dUser32))
$user32Exports = @(
    "/export:CreateWindowExA", "/export:ShowWindow", "/export:UpdateWindow",
    "/export:TextOutA", "/export:InvalidateRect", "/export:GetMessageA",
    "/export:TranslateMessage", "/export:DispatchMessageA", "/export:MessageBoxA",
    "/export:DestroyWindow", "/export:PostQuitMessage", "/export:ExitWindowsEx",
    "/export:DrawTaskbar",
    "/export:S3DGetSurface", "/export:S3DInitializeSurface", "/export:S3DInitialize",
    "/export:S3DClear", "/export:S3DDrawTriangle", "/export:S3DPresent",
    "/export:S3DSubmitVirGL", "/export:S3DPresentVirGL", "/export:S3DClearVirGL",
    "/export:S3DDrawTriangleVirGL", "/export:S3DUploadVirGLBuffer",
    "/export:BeginRedrawBatch", "/export:EndRedrawBatch",
    "/export:GetClientRect", "/export:MoveWindow", "/export:SetWindowPos",
    "/export:GetWindowRect", "/export:IsWindow", "/export:IsWindowVisible",
    "/export:EnableWindow", "/export:IsIconic", "/export:GetForegroundWindow",
    "/export:SetForegroundWindow", "/export:GetActiveWindow", "/export:SetActiveWindow",
    "/export:GetWindowTextA", "/export:SetWindowTextA", "/export:GetClassNameA",
    "/export:RegisterClassA", "/export:UnregisterClassA", "/export:DefWindowProcA",
    "/export:SendMessageA", "/export:PostMessageA", "/export:PeekMessageA",
    "/export:GetKeyState", "/export:GetAsyncKeyState", "/export:GetCursorPos",
    "/export:SetCursorPos", "/export:ShowCursor", "/export:SetCapture",
    "/export:ReleaseCapture", "/export:GetCapture", "/export:GetDesktopWindow",
    "/export:GetDC", "/export:ReleaseDC", "/export:BeginPaint", "/export:EndPaint",
    "/export:GetWindowLongA", "/export:SetWindowLongA", "/export:GetSystemMetrics",
    "/export:SetTimer", "/export:KillTimer", "/export:GetDlgItem",
    "/export:SetDlgItemTextA", "/export:SetDlgItemTextW", "/export:GetDlgItemTextA",
    "/export:GetDlgItemTextW", "/export:CreateMenu", "/export:CreatePopupMenu",
    "/export:AppendMenuA", "/export:DestroyMenu", "/export:TrackPopupMenu",
    "/export:LoadCursorA", "/export:LoadIconA", "/export:LoadImageA",
    "/export:DrawMenuBar", "/export:GetMessagePos", "/export:GetMessageTime",
    "/export:GetDoubleClickTime", "/export:MessageBeep", "/export:RedrawWindow",
    "/export:ScrollWindow", "/export:GetUpdateRect", "/export:SetFocus",
    "/export:GetFocus", "/export:MessageBoxW", "/export:CreateWindowExW",
    "/export:RegisterClassW", "/export:DefWindowProcW", "/export:SendMessageW",
    "/export:PostMessageW", "/export:PeekMessageW", "/export:GetMessageW",
    "/export:CreateDialogParamA", "/export:CreateDialogParamW",
    "/export:DialogBoxParamA", "/export:DialogBoxParamW", "/export:EndDialog",
    "/export:IsDialogMessageA", "/export:IsDialogMessageW",
    "/export:GetWindowTextW", "/export:SetWindowTextW",
    "/export:GetClassNameW", "/export:LoadCursorW", "/export:LoadIconW",
    "/export:LoadImageW", "/export:FindWindowA", "/export:FindWindowW",
    "/export:FindWindowExA", "/export:FindWindowExW", "/export:GetParent",
    "/export:SetParent", "/export:GetWindow", "/export:GetWindowThreadProcessId",
    "/export:GetAncestor", "/export:EnumWindows", "/export:EnumChildWindows",
    "/export:EnumThreadWindows", "/export:GetTopWindow", "/export:BringWindowToTop",
    "/export:SetWindowPlacement", "/export:GetWindowPlacement", "/export:GetWindowInfo",
    "/export:GetWindowModuleFileNameA", "/export:GetWindowContextHelpId",
    "/export:SetWindowContextHelpId", "/export:GetMenu", "/export:SetMenu",
    "/export:GetSubMenu", "/export:GetMenuItemCount", "/export:GetMenuItemID",
    "/export:GetMenuState", "/export:CheckMenuItem", "/export:EnableMenuItem",
    "/export:DeleteMenu", "/export:RemoveMenu", "/export:InsertMenuA",
    "/export:ModifyMenuA", "/export:SetMenuItemInfoA", "/export:GetMenuItemInfoA",
    "/export:GetMenuStringA", "/export:SetMenuDefaultItem",
    "/export:GetMenuDefaultItem", "/export:GetSystemMenu", "/export:GetScrollInfo",
    "/export:SetScrollInfo", "/export:ShowScrollBar", "/export:SetScrollRange",
    "/export:GetScrollRange", "/export:GetScrollPos", "/export:SetScrollPos",
    "/export:SetWindowRgn", "/export:GetWindowRgn", "/export:MapWindowPoints",
    "/export:ClientToScreen", "/export:ScreenToClient", "/export:WindowFromPoint",
    "/export:ChildWindowFromPoint", "/export:SetDoubleClickTime",
    "/export:GetLastActivePopup", "/export:GetWindowTextLengthA",
    "/export:GetWindowTextLengthW", "/export:GetWindowLongPtrA",
    "/export:SetWindowLongPtrA", "/export:GetWindowLongPtrW",
    "/export:SetWindowLongPtrW", "/export:GetClassLongA", "/export:SetClassLongA",
    "/export:GetClassLongPtrA", "/export:SetClassLongPtrA",
    "/export:GetClassLongW", "/export:SetClassLongW",
    "/export:GetClassLongPtrW", "/export:SetClassLongPtrW",
    "/export:GetWindowModuleFileNameW", "/export:GetWindowRgnBox",
    "/export:GetWindowDisplayAffinity", "/export:SetWindowDisplayAffinity",
    "/export:IsChild", "/export:IsWindowUnicode", "/export:IsWindowEnabled",
    "/export:GetWindowOwner", "/export:GetWindowWord", "/export:SetWindowWord",
    "/export:GetWindowLongW", "/export:SetWindowLongW",
    "/export:GetDlgCtrlID", "/export:GetDlgItemInt", "/export:SetDlgItemInt",
    "/export:CheckDlgButton", "/export:IsDlgButtonChecked", "/export:CheckRadioButton",
    "/export:GetNextDlgTabItem", "/export:GetNextDlgGroupItem",
    "/export:GetDialogBaseUnits", "/export:MapDialogRect", "/export:AdjustWindowRect",
    "/export:AdjustWindowRectEx", "/export:AdjustWindowRectExForDpi",
    "/export:GetDpiForWindow", "/export:GetDpiForSystem", "/export:GetSystemMetricsForDpi",
    "/export:GetShellWindow", "/export:ClipCursor", "/export:GetClipCursor",
    "/export:GetTitleBarInfo", "/export:GetMenuBarInfo", "/export:GetGUIThreadInfo",
    "/export:GetLastInputInfo", "/export:GetCursor", "/export:GetCursorInfo",
    "/export:SetCursor", "/export:GetMouseMovePointsEx",
    "/export:GetKeyboardState", "/export:SetKeyboardState",
    "/export:GetKeyboardLayout", "/export:GetKeyboardLayoutList",
    "/export:GetKeyboardLayoutNameA", "/export:GetKeyboardLayoutNameW",
    "/export:ActivateKeyboardLayout", "/export:LoadKeyboardLayoutA",
    "/export:LoadKeyboardLayoutW", "/export:UnloadKeyboardLayout",
    "/export:GetKeyNameTextA", "/export:GetKeyNameTextW",
    "/export:MapVirtualKeyA", "/export:MapVirtualKeyW",
    "/export:MapVirtualKeyExA", "/export:MapVirtualKeyExW",
    "/export:ToUnicode", "/export:ToUnicodeEx",
    "/export:VkKeyScanA", "/export:VkKeyScanW",
    "/export:VkKeyScanExA", "/export:VkKeyScanExW",
    "/export:GetKeyboardType", "/export:GetCaretBlinkTime",
    "/export:SetCaretBlinkTime", "/export:GetCaretPos", "/export:SetCaretPos",
    "/export:CreateCaret", "/export:DestroyCaret", "/export:HideCaret",
    "/export:ShowCaret", "/export:GetMenuStringW", "/export:SetMenuItemBitmaps",
    "/export:GetMenuCheckMarkDimensions", "/export:HiliteMenuItem",
    "/export:InsertMenuW", "/export:AppendMenuW", "/export:ModifyMenuW",
    "/export:SetMenuItemInfoW", "/export:GetMenuItemInfoW",
    "/export:TrackPopupMenuEx", "/export:EndMenu",
    "/export:GetMenuContextHelpId", "/export:SetMenuContextHelpId",
    "/export:GetMenuInfo", "/export:SetMenuInfo",
    "/export:GetMenuItemRect", "/export:SetMenuItemCount",
    "/export:IsZoomed", "/export:GetWindowPos", "/export:UnregisterClassW",
    "/export:ValidateRect", "/export:GetMessageExtraInfo",
    "/export:SetMessageExtraInfo", "/export:GetQueueStatus",
    "/export:GetInputState", "/export:WaitMessage", "/export:ReplyMessage",
    "/export:InSendMessage", "/export:InSendMessageEx",
    "/export:GetWindowDC", "/export:GetDCEx", "/export:GetSysColor",
    "/export:GetSysColorBrush", "/export:SetSysColors",
    "/export:FillRect", "/export:FrameRect", "/export:InvertRect",
    "/export:DrawFocusRect", "/export:OpenIcon", "/export:CloseWindow",
    "/export:ShowOwnedPopups", "/export:FlashWindow", "/export:AnimateWindow",
    "/export:GetDesktopWindowRect", "/export:GetWindowFromDC",
    "/export:GetUpdateRgn", "/export:ExcludeUpdateRgn",
    "/export:GetScrollPos32", "/export:SetScrollPos32",
    "/export:GetWindowThreadProcessIdEx", "/export:IsHungAppWindow",
    "/export:GetActiveWindowThread", "/export:GetWindowLongOffset",
    "/export:SetWindowLongOffset", "/export:GetCursorPosEx",
    "/export:SetCursorPosEx", "/export:GetCaretState",
    "/export:GetDialogControlCount", "/export:GetMenuItemFlags",
    "/export:GetMenuItemTextA", "/export:SetMenuItemTextA",
    "/export:GetWindowStyle", "/export:SetWindowStyle",
    "/export:GetWindowExStyle", "/export:SetWindowExStyle",
    "/export:GetWindowClientOrigin", "/export:SetWindowClientOrigin",
    "/export:DrawEdge", "/export:DrawFrameControl", "/export:DrawCaption",
    "/export:DrawIcon", "/export:DrawIconEx", "/export:DrawTextA",
    "/export:DrawTextW", "/export:GetTextExtentPoint32A",
    "/export:GetTextExtentPoint32W", "/export:GetTextColor", "/export:SetTextColor",
    "/export:GetBkColor", "/export:SetBkColor", "/export:SetBkMode",
    "/export:GetTextAlign", "/export:SetTextAlign",
    "/export:GetDeviceCaps", "/export:GetWindowOrgEx", "/export:SetWindowOrgEx",
    "/export:GetViewportOrgEx", "/export:SetViewportOrgEx",
    "/export:LPtoDP", "/export:DPtoLP", "/export:SaveDC", "/export:RestoreDC",
    "/export:GetDCOrgEx", "/export:GetBrushOrgEx", "/export:SetBrushOrgEx",
    "/export:OpenClipboard", "/export:CloseClipboard", "/export:EmptyClipboard",
    "/export:SetClipboardData", "/export:GetClipboardData",
    "/export:IsClipboardFormatAvailable", "/export:RegisterClipboardFormatA",
    "/export:RegisterClipboardFormatW", "/export:DestroyIcon",
    "/export:DestroyCursor", "/export:CopyIcon", "/export:CopyCursor",
    "/export:GetIconInfo", "/export:LoadBitmapA", "/export:LoadBitmapW",
    "/export:LoadMenuA", "/export:LoadMenuW",
    "/export:TranslateAcceleratorA", "/export:TranslateAcceleratorW",
    "/export:GetClassInfoA", "/export:GetClassInfoW",
    "/export:GetClassInfoExA", "/export:GetClassInfoExW",
    "/export:SystemParametersInfoA", "/export:SystemParametersInfoW",
    "/export:GetWindowTextLength", "/export:GetClassName",
    "/export:GetWindowText", "/export:SetWindowText",
    "/export:GetScrollBarInfo", "/export:GetComboBoxInfo", "/export:GetListBoxInfo",
    "/export:GetWindowModuleFileName",
    "/export:GetWindowDpiAwarenessContext", "/export:AreDpiAwarenessContextsEqual",
    "/export:GetAwarenessFromDpiAwarenessContext", "/export:GetProcessDpiAwareness",
    "/export:SetPropA", "/export:GetPropA", "/export:RemovePropA",
    "/export:SetPropW", "/export:GetPropW", "/export:RemovePropW",
    "/export:SetLayeredWindowAttributes", "/export:GetLayeredWindowAttributes",
    "/export:UpdateLayeredWindow", "/export:RegisterHotKey",
    "/export:UnregisterHotKey", "/export:SetWindowsHookExA",
    "/export:SetWindowsHookExW", "/export:UnhookWindowsHookEx",
    "/export:CallNextHookEx", "/export:GetWindowSubclass",
    "/export:SetWindowSubclass", "/export:RemoveWindowSubclass",
    "/export:DefSubclassProc",
    "/export:GetTextMetricsA", "/export:GetTextMetricsW",
    "/export:GetCharWidth32A", "/export:GetCharWidth32W",
    "/export:GetTextExtentExPointA", "/export:GetTextExtentExPointW",
    "/export:TabbedTextOutA", "/export:TabbedTextOutW",
    "/export:GetClipboardOwner", "/export:GetOpenClipboardWindow",
    "/export:GetClipboardSequenceNumber", "/export:GetClipboardFormatNameA",
    "/export:GetClipboardFormatNameW", "/export:GetFocusThread",
    "/export:GetWindowDisplayAffinityEx", "/export:GetWindowDpiAwarenessContextEx",
    "/export:GetDpiForMonitor", "/export:EnableNonClientDpiScaling",
    "/export:SetThreadDpiAwarenessContext", "/export:GetThreadDpiAwarenessContext",
    "/export:GetDpiFromDpiAwarenessContext", "/export:SetProcessDpiAwarenessContext",
    "/export:GetProcessWindowStation", "/export:GetThreadDesktop",
    "/export:GetUserObjectInformationA", "/export:GetUserObjectInformationW",
    "/export:GetUserObjectSecurity", "/export:SetUserObjectInformationA",
    "/export:SetUserObjectInformationW",
    "/export:EnumDisplayMonitors", "/export:MonitorFromWindow",
    "/export:MonitorFromPoint", "/export:MonitorFromRect",
    "/export:GetMonitorInfoA", "/export:GetMonitorInfoW",
    "/export:EnumDisplaySettingsA", "/export:EnumDisplaySettingsW",
    "/export:ChangeDisplaySettingsA", "/export:ChangeDisplaySettingsW",
    "/export:ChangeDisplaySettingsExA", "/export:ChangeDisplaySettingsExW",
    "/export:LoadAcceleratorsA", "/export:LoadAcceleratorsW",
    "/export:CreateAcceleratorTableA", "/export:CreateAcceleratorTableW",
    "/export:DestroyAcceleratorTable", "/export:CopyAcceleratorTableA",
    "/export:CopyAcceleratorTableW",
    "/export:LoadStringA", "/export:LoadStringW",
    "/export:LoadMenuIndirectA", "/export:LoadMenuIndirectW",
    "/export:RegisterDeviceNotificationA", "/export:RegisterDeviceNotificationW",
    "/export:UnregisterDeviceNotification",
    "/export:SetProcessDPIAware", "/export:IsProcessDPIAware",
    "/export:SetThreadDesktop",
    "/export:OpenWindowStationA", "/export:OpenWindowStationW",
    "/export:CloseWindowStation", "/export:SetProcessWindowStation",
    "/export:EnumWindowStationsA", "/export:EnumWindowStationsW",
    "/export:EnumDesktopsA", "/export:EnumDesktopsW",
    "/export:OpenDesktopA", "/export:OpenDesktopW",
    "/export:CloseDesktop", "/export:SwitchDesktop",
    "/export:GetUserObjectInformation", "/export:SetUserObjectInformation",
    "/export:GetMonitorDpiType", "/export:GetSystemMetricsForMonitor",
    "/export:GetWindowDpi", "/export:GetDpiForWindowEx",
    "/export:GetPhysicalCursorPos", "/export:SetPhysicalCursorPos",
    "/export:GetCursorInfoEx",
    "/export:SetWindowFeedbackSetting", "/export:GetWindowFeedbackSetting",
    "/export:GetWindowCompositionAttribute", "/export:SetWindowCompositionAttribute",
    "/export:GetMenuInfoEx", "/export:SetMenuInfoEx",
    "/export:GetMenuItemIDEx", "/export:GetSubMenuEx",
    "/export:SetWindowRedraw", "/export:GetWindowRedraw",
    "/export:GetWindowArrange", "/export:ArrangeIconicWindows",
    "/export:CascadeWindows", "/export:TileWindows",
    "/export:GetWindowPlacementEx", "/export:GetWindowRectEx",
    "/export:SetWindowPosEx", "/export:MoveWindowEx",
    "/export:SetForegroundWindowEx", "/export:GetForegroundWindowEx",
    "/export:GetPixel", "/export:SetPixel", "/export:SetPixelV",
    "/export:MoveToEx", "/export:LineTo", "/export:Rectangle",
    "/export:Ellipse", "/export:RoundRect", "/export:Polyline",
    "/export:Polygon", "/export:SetROP2", "/export:GetROP2",
    "/export:SetStretchBltMode", "/export:GetStretchBltMode",
    "/export:SetMapMode", "/export:GetMapMode",
    "/export:GetWindowExtEx", "/export:SetWindowExtEx",
    "/export:GetViewportExtEx", "/export:SetViewportExtEx",
    "/export:SetArcDirection", "/export:GetArcDirection",
    "/export:GetWindowRgnEx", "/export:GetClassInfoEx",
    "/export:GetWindowTextExA", "/export:SetWindowTextExA",
    "/export:GetMenuDefaultItemEx", "/export:GetSystemMenuEx",
    "/export:SetMenuEx", "/export:GetMenuEx",
    "/export:GetWindowParent", "/export:GetWindowAncestor",
    "/export:GetWindowChild", "/export:GetWindowTop",
    "/export:GetWindowOwnerEx", "/export:GetWindowEnabled",
    "/export:SetWindowEnabled", "/export:GetWindowVisible",
    "/export:SetWindowVisible", "/export:GetWindowMinimized",
    "/export:GetWindowZoomed", "/export:GetWindowClassNameA",
    "/export:GetWindowClassNameW", "/export:GetMenuItemState",
    "/export:SetMenuItemState", "/export:GetWindowStyleEx",
    "/export:SetWindowStyleEx", "/export:GetWindowUserData",
    "/export:SetWindowUserData", "/export:GetWindowProcedure",
    "/export:SetWindowProcedure", "/export:GetWindowIdentifier",
    "/export:SetWindowIdentifier", "/export:GetWindowContextHelpIdEx",
    "/export:SetWindowContextHelpIdEx", "/export:GetMenuCount",
    "/export:GetMenuItemId", "/export:GetMenuString",
    "/export:AppendMenu", "/export:InsertMenu", "/export:ModifyMenu",
    "/export:DeleteMenuItem", "/export:RemoveMenuItem",
    "/export:CheckMenuItemState", "/export:EnableMenuItemState",
    "/export:RegisterClassExA", "/export:RegisterClassExW",
    "/export:CreateWindowA", "/export:CreateWindowW",
    "/export:DispatchMessageW",
    "/export:CreateDialogIndirectParamA", "/export:CreateDialogIndirectParamW",
    "/export:DialogBoxIndirectParamA", "/export:DialogBoxIndirectParamW",
    "/export:GetRawInputData", "/export:GetRawInputBuffer",
    "/export:GetRawInputDeviceInfoA", "/export:GetRawInputDeviceInfoW",
    "/export:GetRawInputDeviceList", "/export:RegisterRawInputDevices",
    "/export:DefRawInputProc",
    "/export:GetPointerType", "/export:GetPointerInfo",
    "/export:GetPointerInfoHistory", "/export:GetPointerPenInfo",
    "/export:GetPointerTouchInfo", "/export:GetPointerFrameInfo",
    "/export:GetPointerDevice", "/export:GetPointerDeviceRects",
    "/export:GetPointerDevices",
    "/export:GetGestureInfo", "/export:CloseGestureInfoHandle",
    "/export:GetGestureExtraArgs", "/export:SetGestureConfig",
    "/export:GetTouchInputInfo"
)
Invoke-Lld (@("/dll") + @("/noentry") + @("/subsystem:native") + @("/nodefaultlib") + @("/fixed:no") + @("/base:0x41100000") + @("/implib:$($b)\obj\user32.lib") + $user32Exports + @("/out:$($sys32)\user32.dll") + @($objUser32) + @($objSoft3dUser32))

# explorer.exe
Build-SimpleExe "explorer" "explorer.c" "0x40000000" "explorer_main" -subsystem "windows"

# fileexplorer.exe
Build-SimpleExe "fileexplorer" "fileexplorer.c" "0x40100000" "mainCRTStartup" -subsystem "windows"

# cmd.exe
Build-SimpleExe "cmd" "cmd.c" "0x40300000" "mainCRTStartup" -subsystem "windows"

# taskmgr.exe
Build-SimpleExe "taskmgr" "taskmgr.c" "0x40500000" "mainCRTStartup" -subsystem "windows"

# regedit.exe
Build-SimpleExe "regedit" "regedit.c" "0x40800000" "mainCRTStartup" -subsystem "windows"

# settings.exe
$obj = Join-Path $b "obj\settings.o"
Invoke-Clang ($USER_CFLAGS + @("-c") + @(Join-Path $root "user\settings.c") + @("-o") + @($obj))
Invoke-Lld (@("/subsystem:windows") + @("/entry:mainCRTStartup") + @("/nodefaultlib") + @("/fixed:no") + @("/base:0x40700000") + @("/out:$($sys32)\settings.exe") + @($obj))
if (-not (Test-Path -LiteralPath (Join-Path $sys32 "settings.exe") -PathType Leaf)) { throw "FATAL: settings.exe was not produced" }
$settingsFormatBytes = [IO.File]::ReadAllBytes((Join-Path $sys32 "settings.exe"))
if ($settingsFormatBytes[0] -ne 0x4D -or $settingsFormatBytes[1] -ne 0x5A) { throw "FATAL: settings.exe is not a PE executable" }
Write-Host "  -> settings.exe format: PE32+ x86-64"

# msiexec.exe
Build-SimpleExe "msiexec" "msiexec.c" "0x40400000" "mainCRTStartup" -subsystem "windows"

# ========================================================================
Write-Stage "UserTestApps"
# ========================================================================

# game.exe
$gameDst = Join-Path $rootfs "magic\Program Files\game.exe"
$obj = Join-Path $b "obj\game.o"
& $clangxxPath ($USER_CXXFLAGS + @("-c") + @(Join-Path $root "UserTestApps\game.cpp") + @("-o") + @($obj))
if ($LASTEXITCODE -ne 0) { throw "clang++ failed (exit $LASTEXITCODE)." }
Invoke-Lld (@("/machine:x64") + @("/subsystem:windows") + @("/entry:game_mainCRTStartup") + @("/nodefaultlib") + @("/fixed:no") + @("/base:0x42000000") + @("/out:$gameDst") + @($obj))
if (-not (Test-Path -LiteralPath $gameDst -PathType Leaf)) { throw "FATAL: game.exe was not produced" }
Write-Host "  -> game.exe produced"

# win32compat.exe
$win32compatDst = Join-Path $rootfs "magic\Program Files\win32compat.exe"
$obj = Join-Path $b "obj\win32_compat.o"
& $clangxxPath ($USER_CXXFLAGS + @("-x") + @("c") + @("-c") + @(Join-Path $root "UserTestApps\win32_compat.c") + @("-o") + @($obj))
if ($LASTEXITCODE -ne 0) { throw "clang++ failed (exit $LASTEXITCODE)." }
Invoke-Lld (@("/machine:x64") + @("/subsystem:windows") + @("/entry:win32_compat_main") + @("/nodefaultlib") + @("/fixed:no") + @("/base:0x42200000") + @("/out:$win32compatDst") + @($obj) + @("$($b)\obj\kernel32.lib") + @("$($b)\obj\user32.lib"))
if (-not (Test-Path -LiteralPath $win32compatDst -PathType Leaf)) { throw "FATAL: win32compat.exe was not produced" }
$wcBytes = [IO.File]::ReadAllBytes($win32compatDst)
if ($wcBytes[0] -ne 0x4D -or $wcBytes[1] -ne 0x5A) { throw "FATAL: win32compat.exe is not a PE executable" }
Write-Host "  -> win32compat.exe produced"

# maze.exe
$mazeDst = Join-Path $rootfs "magic\Program Files\maze.exe"
$mazeFlags = $USER_CXXFLAGS | Where-Object { $_ -ne "-mgeneral-regs-only" }
$obj = Join-Path $b "obj\maze.o"
$objSoft3dMaze = Join-Path $b "obj\soft3d.o"
& $clangxxPath ($mazeFlags + @("-c") + @(Join-Path $root "UserTestApps\maze.cpp") + @("-o") + @($obj))
if ($LASTEXITCODE -ne 0) { throw "clang++ failed (exit $LASTEXITCODE)." }
Invoke-Lld (@("/machine:x64") + @("/subsystem:windows") + @("/entry:maze_mainCRTStartup") + @("/nodefaultlib") + @("/fixed:no") + @("/base:0x42100000") + @("/out:$mazeDst") + @($obj) + @($objSoft3dMaze))
if (-not (Test-Path -LiteralPath $mazeDst -PathType Leaf)) { throw "FATAL: maze.exe was not produced" }
$mzBytes = [IO.File]::ReadAllBytes($mazeDst)
if ($mzBytes[0] -ne 0x4D -or $mzBytes[1] -ne 0x5A) { throw "FATAL: maze.exe is not a PE executable" }
Write-Host "  -> maze.exe produced"

# ========================================================================
Write-Stage "Checking required images"
# ========================================================================

$requiredSystem32 = @(
    "hal.dll", "krnl.exe", "smss.exe", "bootvid.exe", "sentlogon.exe",
    "explorer.exe", "fileexplorer.exe", "taskbar.dll", "kernel32.dll",
    "winmm.dll", "gdi32.dll", "opengl32.dll",
    "api-ms-win-crt-runtime-l1-1-0.dll", "ucrtbase.dll", "user32.dll",
    "vulkan.dll", "cmd.exe", "taskmgr.exe", "settings.exe", "msiexec.exe",
    "regedit.exe",
    "drivers\ata.sys", "drivers\hda.sys"
)
foreach ($image in $requiredSystem32) {
    $path = Join-Path $sys32 $image
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "FATAL: missing or empty image: $path"
    }
}

# Precreate the persistent registry hive (4 MB zeroed file). NTFS cannot grow
# files at runtime and there is no runtime file-create syscall, so the hive
# must exist before first boot. advapi32 loads it at init and overwrites
# within its preallocated size on every mutation.
$regConfigDir = Join-Path $sys32 "config"
New-Item -ItemType Directory -Force -Path $regConfigDir | Out-Null
$regHivePath = Join-Path $regConfigDir "registry.hive"
[System.IO.File]::WriteAllBytes($regHivePath, (New-Object Byte[] 4194304))
Write-Host "  -> registry.hive precreated (4 MB)"

Write-Host ""
Get-ChildItem -LiteralPath $sys32 -File -Recurse -Force | Sort-Object FullName | ForEach-Object {
    Write-Host ("  -> {0} ({1} bytes)" -f $_.FullName.Substring($sys32.Length + 1), $_.Length)
}

# Reject accidental files at the filesystem root; only /magic is permitted
$rootfsTop = @(Get-ChildItem -LiteralPath $rootfs -Force)
if ($rootfsTop.Count -ne 1 -or $rootfsTop[0].Name -ne "magic" -or -not $rootfsTop[0].PSIsContainer) {
    throw "FATAL: rootfs must contain exactly one top-level directory: magic"
}
if (-not (Test-Path -LiteralPath (Join-Path $rootfs "magic\logo.bmp") -PathType Leaf)) { throw "SentinelOS logo was not staged in rootfs." }

# ========================================================================
Write-Stage "Create expandable GPT VHDX with FAT32 ESP and NTFS MAGIC partition"
# ========================================================================

if ((Get-Volume -FilePath $b -ErrorAction Stop).FileSystem -ne "NTFS") {
    throw "The build folder must be on an NTFS volume (folder mount points are used instead of drive letters): $b"
}

$usedLetters = [IO.DriveInfo]::GetDrives() | ForEach-Object { $_.Name.Substring(0, 1).ToUpperInvariant() }
$espLetter = ([char[]]"ZYXWVUTSRQPONMLKJIHGFED") | Where-Object { "$_" -notin $usedLetters } | Select-Object -First 1
if (-not $espLetter) { throw "One free drive letter is required to stage the EFI System Partition (DiskPart cannot mount an ESP on a folder). Free one with 'mountvol /R' or reboot, then retry." }

$espRoot = "$($espLetter):\"
$sysRoot = Join-Path $mnt "sys"
Remove-MountPoints
New-Item -ItemType Directory -Force -Path $sysRoot | Out-Null

Detach-Vhd
Remove-Item -LiteralPath $vhdBuilding -Force -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $vhdBuilding) { throw "Cannot recreate VHDX because it still exists or is locked: $vhdBuilding. Close QEMU and detach it in Disk Management, then retry." }

try {
    [void](Run-Diskpart @"
create vdisk file="$vhdBuilding" maximum=131072 type=expandable
select vdisk file="$vhdBuilding"
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

    $disk = Get-DiskImage -ImagePath $vhdBuilding | Get-Disk
    $partitions = @(Get-Partition -DiskNumber $disk.Number)
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
        throw "Expected one EFI System Partition and one MAGIC basic-data partition, optionally plus one MSR; found an unexpected GPT layout on VHDX '$vhdBuilding':$([Environment]::NewLine)$partitionSummary"
    }
    $rootPartition = $basicPartitions[0]
    if (-not $rootPartition) { throw "NTFS system partition was not created." }
    $sysVol = Get-Volume -FilePath $sysRoot -ErrorAction SilentlyContinue
    if (-not $sysVol -or $sysVol.FileSystemLabel -ne "MAGIC") { throw "NTFS system partition was not mounted at $sysRoot" }

    # Copy the whole rootfs recursively
    Copy-Tree $rootfs $sysRoot

    # Stage magic tree contents at volume root as well
    $magicTree = Join-Path $rootfs "magic"
    Copy-Tree $magicTree $sysRoot

    # Verify critical files were copied
    foreach ($relative in @("System32\smss.exe", "System32\sentlogon.exe", "System32\bootvid.exe", "System32\explorer.exe", "System32\fileexplorer.exe", "System32\krnl.exe", "System32\hal.dll")) {
        $copied = Join-Path $sysRoot $relative
        if (-not (Test-Path -LiteralPath $copied -PathType Leaf) -or (Get-Item -LiteralPath $copied).Length -le 0) {
            throw "Staging verification failed; missing copied file: $copied"
        }
    }

    # Verify file sizes match source
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

    # Verify PE MZ headers on the image
    foreach ($relative in @("System32\smss.exe", "System32\krnl.exe", "System32\sentlogon.exe", "System32\explorer.exe")) {
        $fs = [IO.File]::OpenRead((Join-Path $sysRoot $relative))
        try { $mz = New-Object byte[] 2; [void]$fs.Read($mz, 0, 2) } finally { $fs.Close() }
        if ($mz[0] -ne 0x4D -or $mz[1] -ne 0x5A) { throw "$relative on the image is not a PE file (no MZ header)." }
    }

    foreach ($m in @($espRoot, $sysRoot)) { try { Write-VolumeCache -Path $m } catch { } }
    Write-Host ("Verified {0} files; System32\smss.exe is {1} bytes at the expected system path." -f @(Get-ChildItem -LiteralPath $magicTree -File -Recurse -Force).Count, (Get-Item -LiteralPath (Join-Path $sysRoot "System32\smss.exe")).Length) -ForegroundColor Green
} finally {
    Remove-MountPoints
    Detach-Vhd
}

# ========================================================================
Write-Stage "Install verified disk image"
# ========================================================================
Detach-Image $vhdFinal
Remove-Item -LiteralPath $vhdFinal -Force -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $vhdFinal) {
    throw "Cannot replace $vhdFinal (locked). Close QEMU or run .\scripts\vhd.ps1 Dismount, then rerun. The verified new image is $vhdBuilding"
}
Move-Item -LiteralPath $vhdBuilding -Destination $vhdFinal -Force
$vhdBuilding = $vhdFinal

# ========================================================================
Write-Stage "Write build log (build\log\build.log)"
# ========================================================================
$manifest = Join-Path $b "log\build.log"
New-Item -ItemType Directory -Force -Path (Split-Path $manifest) | Out-Null
$ovmfSize = (Get-Item $ovmfDest).Length
$sb = New-Object System.Text.StringBuilder
$sb.AppendLine("============================================================") | Out-Null
$sb.AppendLine("SentinelOS V10 ETERNITY - Windows build log") | Out-Null
$sb.AppendLine("Command: .\scripts\build.ps1") | Out-Null
$sb.AppendLine("Timestamp: $(Get-Date -Format o)") | Out-Null
$sb.AppendLine("Host: $env:COMPUTERNAME / $env:USERNAME") | Out-Null
$sb.AppendLine("Build directory: $b") | Out-Null
$sb.AppendLine("Disk image: $vhdBuilding") | Out-Null
$sb.AppendLine("============================================================") | Out-Null
$sb.AppendLine("")
$sb.AppendLine("-- Toolchain / environment --") | Out-Null
$sb.AppendLine("  EDK2 headers: $edk2") | Out-Null
foreach ($t in @("clang.exe", "clang++.exe", "lld-link.exe")) {
    $c = Get-Command $t -ErrorAction SilentlyContinue
    if ($c) { $sb.AppendFormat("  {0} : {1}" + [Environment]::NewLine, $t, $c.Source) | Out-Null }
}
$sb.AppendLine("")
$sb.AppendLine("-- Source files compiled --") | Out-Null
foreach ($s in @("boot/bootx64.c", "boot/bootuni.c", "boot/loadufi.c", "hal/hal.c", "kernel/kernel.c", "kernel/smp_trampoline.S", "drivers/ata/ata.c", "drivers/audio/hda.c", "user/smss.c", "user/bootvid.c", "user/sentlogon.c", "user/taskbar.c", "user/kernel32.c", "user/user32.c", "user/gdi32.c", "user/vulkan.c", "user/ucrt_runtime.c", "user/explorer.c", "user/fileexplorer.c", "user/cmd.c", "user/taskmgr.c", "user/settings.c", "user/msiexec.c", "UserTestApps/game.cpp", "UserTestApps/win32_compat.c", "UserTestApps/maze.cpp")) {
    $sb.AppendFormat("  {0}" + [Environment]::NewLine, $s) | Out-Null
}
$sb.AppendLine("")
$sb.AppendLine("-- UEFI firmware --") | Out-Null
$sb.AppendFormat("  ovmf.fd : {0} bytes ({1})" + [Environment]::NewLine, $ovmfSize, $ovmfDest) | Out-Null
$sb.AppendLine("")
$sb.AppendLine("-- Binaries built and staged --") | Out-Null
foreach ($labelDir in @(@{Label="ESP (EFI system partition)"; Dir=(Join-Path $esp "EFI")}, @{Label="magic\System32"; Dir=$sys32})) {
    $sb.AppendLine("  $($labelDir.Label):") | Out-Null
    $files = @(Get-ChildItem -LiteralPath $labelDir.Dir -File -Recurse -Force | Sort-Object FullName)
    foreach ($f in $files) {
        $rel = $f.FullName.Substring($labelDir.Dir.Length).TrimStart('\')
        $sb.AppendFormat("    {0}  ({1} bytes)" + [Environment]::NewLine, $rel, $f.Length) | Out-Null
    }
}
$sb.AppendLine("")
$sb.AppendLine("-- Required-output validation --") | Out-Null
$sb.AppendLine("  All required build outputs present and non-empty before VHDX creation.") | Out-Null
$sb.AppendLine("")
$sb.AppendLine("build ok: complete build output staged to $vhdBuilding") | Out-Null
[System.IO.File]::WriteAllText($manifest, $sb.ToString())
Write-Host ("Build log written: $manifest") -ForegroundColor Green

Write-Host "build ok: complete build output staged to $vhdBuilding" -ForegroundColor Green

if ($Run) { & (Join-Path $PSScriptRoot "run.ps1") }
