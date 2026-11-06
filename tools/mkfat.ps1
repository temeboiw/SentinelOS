# mkfat.ps1 - builds a minimal FAT12 disk image containing given files.
# Usage: pwsh -File mkfat.ps1 <output.img> <size_mb> <src1=dst1> [<src2=dst2> ...]
param(
    [string]$Out,
    [int]$SizeMb,
    [string[]]$Files
)

# PowerShell 5.1 only binds the first extra positional arg to [string[]]$Files;
# the rest land in $args. Combine them.
$allFiles = if ($args.Count -gt 0) { $Files + $args } else { $Files }

$SECTOR = 512
$script:img = $null
$script:fat = $null
$script:fatStart = 0
$script:fatSectors = 0
$script:rootStart = 0
$script:dataStart = 0
$script:clusterSectors = 1
$script:nextCluster = 2
$script:totalSectors = 0

function Write-Sector($lba, [byte[]]$data) {
    $pos = [long]$lba * $SECTOR
    for ($i = 0; $i -lt $data.Length; $i++) { $script:img[$pos + $i] = $data[$i] }
}
function Read-Sector($lba) {
    $data = New-Object byte[] $SECTOR
    $pos = [long]$lba * $SECTOR
    for ($i = 0; $i -lt $SECTOR; $i++) { $data[$i] = $script:img[$pos + $i] }
    return $data
}
function Get-Fat12($cl) {
    $off = $cl + [math]::Floor($cl / 2)
    $b0 = $script:fat[$off]; $b1 = $script:fat[$off + 1]
    if ($cl -band 1) { $v = ($b0 -band 0x0F) -bor (($b1 -band 0x0F) -shl 4) }
    else { $v = $b0 -bor (($b1 -band 0x0F) -shl 8) }
    return $v -band 0xFFF
}
function Set-Fat12($cl, $v) {
    $off = $cl + [math]::Floor($cl / 2)
    if ($cl -band 1) {
        $script:fat[$off] = ($script:fat[$off] -band 0xF0) -bor (($v -band 0x0F) -shl 4)
        $script:fat[$off + 1] = [byte](($v -band 0x0F0) -shr 4)
    } else {
        $script:fat[$off] = [byte]($v -band 0xFF)
        $script:fat[$off + 1] = ($script:fat[$off + 1] -band 0x0F) -bor [byte](($v -band 0x0F00) -shr 8)
    }
}
function Alloc-Cluster {
    $script:nextCluster = $script:nextCluster + 1
    $c = $script:nextCluster - 1
    Set-Fat12 $c 0xFFF
    return $c
}
function Write-FileData([byte[]]$data) {
    if ($data.Length -eq 0) { return 0 }
    $first = Alloc-Cluster
    $cur = $first; $off = 0
    while ($off -lt $data.Length) {
        $n = [math]::Min($data.Length - $off, $SECTOR)
        $sec = New-Object byte[] $SECTOR
        [Array]::Copy($data, $off, $sec, 0, $n)
        Write-Sector ($script:dataStart + ($cur - 2) * $script:clusterSectors) $sec
        $off += $n
        if ($off -lt $data.Length) {
            $nxt = Alloc-Cluster
            Set-Fat12 $cur $nxt
            $cur = $nxt
        }
    }
    return $first
}
function Name-Encode($path) {
    $out = New-Object byte[] 11
    for ($i = 0; $i -lt 11; $i++) { $out[$i] = 0x20 }
    $dot = $path.LastIndexOf('.')
    $nameEnd = if ($dot -ge 0) { $dot } else { $path.Length }
    $ni = 0
    for ($i = 0; $i -lt $nameEnd -and $ni -lt 8; $i++) {
        $ch = $path[$i]
        if ($ch -eq '/' -or $ch -eq '\') { continue }
        $out[$ni] = [byte][char]::ToUpper($ch)
        $ni++
    }
    if ($dot -ge 0) {
        $di = 0
        for ($i = $dot + 1; $i -lt $path.Length -and $di -lt 3; $i++) {
            $out[8 + $di] = [byte][char]::ToUpper($path[$i])
            $di++
        }
    }
    return $out
}
function Ensure-Dirs($dst) {
    $parts = $dst -split '/'
    $dirCl = 0
    foreach ($comp in $parts) {
        if ($comp -eq '' -or $comp -eq '.') { continue }
        $n11 = Name-Encode $comp
        $entries = if ($dirCl -eq 0) { 256 } else { $SECTOR / 32 }
        $base = if ($dirCl -eq 0) { $script:rootStart } else { $script:dataStart + ($dirCl - 2) * $script:clusterSectors }
        $found = -1
        for ($e = 0; $e -lt $entries; $e++) {
            $sec = $base + [math]::Floor(($e * 32) / $SECTOR)
            $off = ($e * 32) % $SECTOR
            $ent = Read-Sector $sec
            if ($ent[$off] -eq 0x00) { break }
            if ($ent[$off] -eq 0xE5) { continue }
            $match = $true
            for ($k = 0; $k -lt 11; $k++) { if ($ent[$off + $k] -ne $n11[$k]) { $match = $false; break } }
            if ($match) { $found = $e; break }
        }
        if ($found -ge 0) {
            $sec = $base + [math]::Floor(($found * 32) / $SECTOR)
            $off = ($found * 32) % $SECTOR
            $ent = Read-Sector $sec
            $dirCl = $ent[$off + 26] -bor ($ent[$off + 27] -shl 8)
            continue
        }
        $newcl = Alloc-Cluster
        $secbuf = New-Object byte[] $SECTOR
        [Array]::Copy($n11, 0, $secbuf, 0, 11)
        $secbuf[11] = 0x10
        $secbuf[26] = [byte]($newcl -band 0xFF)
        $secbuf[27] = [byte](($newcl -band 0xFF00) -shr 8)
        $secbuf[32] = 0x2E; $secbuf[33] = 0x2E
        for ($k = 2; $k -lt 11; $k++) { $secbuf[32 + $k] = 0x20 }
        $secbuf[32 + 11] = 0x10
        $parent = if ($dirCl -eq 0) { 0 } else { $dirCl }
        $secbuf[32 + 26] = [byte]($parent -band 0xFF)
        $secbuf[32 + 27] = [byte](($parent -band 0xFF00) -shr 8)
        Write-Sector ($script:dataStart + ($newcl - 2) * $script:clusterSectors) $secbuf
        $pEntries = if ($dirCl -eq 0) { 256 } else { $SECTOR / 32 }
        $pBase = if ($dirCl -eq 0) { $script:rootStart } else { $script:dataStart + ($dirCl - 2) * $script:clusterSectors }
        $slot = -1
        for ($e = 0; $e -lt $pEntries; $e++) {
            $sec = $pBase + [math]::Floor(($e * 32) / $SECTOR)
            $off = ($e * 32) % $SECTOR
            $ent = Read-Sector $sec
            if ($ent[$off] -eq 0x00 -or $ent[$off] -eq 0xE5) { $slot = $e; break }
        }
        if ($slot -lt 0) { throw "dir full" }
        $pSec = $pBase + [math]::Floor(($slot * 32) / $SECTOR)
        $pOff = ($slot * 32) % $SECTOR
        $ent = Read-Sector $pSec
        [Array]::Copy($n11, 0, $ent, $pOff, 11)
        $ent[$pOff + 11] = 0x10
        $ent[$pOff + 26] = [byte]($newcl -band 0xFF)
        $ent[$pOff + 27] = [byte](($newcl -band 0xFF00) -shr 8)
        $ent[$pOff + 28] = 0; $ent[$pOff + 29] = 0
        $ent[$pOff + 30] = 0; $ent[$pOff + 31] = 0
        Write-Sector $pSec $ent
        $dirCl = $newcl
    }
    return $dirCl
}
function Add-File($src, $dst) {
    $data = [System.IO.File]::ReadAllBytes($src)
    $lastslash = $dst.LastIndexOf('/')
    if ($lastslash -ge 0) {
        $dirstr = $dst.Substring(0, $lastslash)
        $fname = $dst.Substring($lastslash + 1)
    } else {
        $dirstr = ""
        $fname = $dst
    }
    $dircl = Ensure-Dirs $dirstr
    $n11 = Name-Encode $fname
    $firstCl = Write-FileData $data
    $entries = if ($dircl -eq 0) { 256 } else { $SECTOR / 32 }
    $base = if ($dircl -eq 0) { $script:rootStart } else { $script:dataStart + ($dircl - 2) * $script:clusterSectors }
    $slot = -1
    for ($e = 0; $e -lt $entries; $e++) {
        $sec = $base + [math]::Floor(($e * 32) / $SECTOR)
        $off = ($e * 32) % $SECTOR
        $ent = Read-Sector $sec
        if ($ent[$off] -eq 0x00 -or $ent[$off] -eq 0xE5) { $slot = $e; break }
    }
    if ($slot -lt 0) { throw "dir full for $dst" }
    $pSec = $base + [math]::Floor(($slot * 32) / $SECTOR)
    $pOff = ($slot * 32) % $SECTOR
    $ent = Read-Sector $pSec
    [Array]::Copy($n11, 0, $ent, $pOff, 11)
    $ent[$pOff + 11] = 0x20
    $ent[$pOff + 26] = [byte]($firstCl -band 0xFF)
    $ent[$pOff + 27] = [byte](($firstCl -band 0xFF00) -shr 8)
    $ent[$pOff + 28] = 0; $ent[$pOff + 29] = 0
    $ent[$pOff + 30] = [byte]($data.Length -band 0xFF)
    $ent[$pOff + 31] = [byte](($data.Length -band 0xFF00) -shr 8)
    Write-Sector $pSec $ent
}

# ---- main ----
$script:totalSectors = $SizeMb * 2048
$rootEntries = 256
$rootSectors = [math]::Ceiling(($rootEntries * 32) / $SECTOR)
$dataSectors = $script:totalSectors - 1 - $rootSectors
$clusters = $dataSectors / $script:clusterSectors
$script:fatSectors = [math]::Ceiling(($clusters * 3 / 2) / $SECTOR)
if ($script:fatSectors -lt 1) { $script:fatSectors = 1 }
$script:fatStart = 1
$script:rootStart = 1 + 2 * $script:fatSectors
$script:dataStart = $script:rootStart + $rootSectors

$script:img = New-Object byte[] ([long]$script:totalSectors * $SECTOR)
$script:fat = New-Object byte[] ([long]$script:fatSectors * $SECTOR)
$script:fat[0] = 0xF0; $script:fat[1] = 0xFF; $script:fat[2] = 0xFF

$b = New-Object byte[] $SECTOR
$b[0] = 0xEB; $b[1] = 0x3C; $b[2] = 0x90
[System.Text.Encoding]::ASCII.GetBytes("MKFAT12  ", 0, 8, $b, 3)
[System.BitConverter]::GetBytes([uint16]$SECTOR).CopyTo($b, 11)
$b[13] = $script:clusterSectors
[System.BitConverter]::GetBytes([uint16]1).CopyTo($b, 14)
$b[16] = 2
[System.BitConverter]::GetBytes([uint16]$rootEntries).CopyTo($b, 17)
$tot16 = if ($script:totalSectors -gt 0xFFFF) { 0 } else { [uint16]$script:totalSectors }
[System.BitConverter]::GetBytes($tot16).CopyTo($b, 19)
$b[21] = 0xF0
[System.BitConverter]::GetBytes([uint16]$script:fatSectors).CopyTo($b, 22)
[System.BitConverter]::GetBytes([uint16]18).CopyTo($b, 24)
[System.BitConverter]::GetBytes([uint16]2).CopyTo($b, 26)
$tb = [System.BitConverter]::GetBytes([uint32]$script:totalSectors)
for ($i = 0; $i -lt 4; $i++) { $b[36 + $i] = $tb[$i] }
$b[40] = 0x80; $b[42] = 0x29
$vb = [System.BitConverter]::GetBytes([uint32]0x12345678)
for ($i = 0; $i -lt 4; $i++) { $b[43 + $i] = $vb[$i] }
[System.Text.Encoding]::ASCII.GetBytes("NO NAME    ", 0, 11, $b, 47)
[System.Text.Encoding]::ASCII.GetBytes("FAT12   ", 0, 8, $b, 54)
$b[510] = 0x55; $b[511] = 0xAA
Write-Sector 0 $b

$debug = "C:\SentinelOS\build/mkfat_debug.txt"
"allFiles count: $($allFiles.Count)" | Out-File $debug -Encoding utf8
foreach ($f in $allFiles) {
    "Processing: $f" | Out-File $debug -Append -Encoding utf8
    $colon = $f.IndexOf('=')
    if ($colon -lt 0) { throw "bad file spec $f" }
    $src = $f.Substring(0, $colon)
    $dst = $f.Substring($colon + 1)
    "  src=$src dst=$dst" | Out-File $debug -Append -Encoding utf8
    Add-File $src $dst
    "  done $dst" | Out-File $debug -Append -Encoding utf8
}

for ($copy = 0; $copy -lt 2; $copy++) {
    Write-Sector ($script:fatStart + $copy * $script:fatSectors) $script:fat
}

[System.IO.File]::WriteAllBytes($Out, $script:img)
Write-Host "wrote $Out ($($script:totalSectors) sectors, FAT12, $($script:fatSectors) FAT sectors)"
