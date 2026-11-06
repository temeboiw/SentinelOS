# mkgpt.ps1 - builds a GPT disk image with a FAT ESP partition.
# Usage: pwsh -File mkgpt.ps1 <out.img> <disk_size_mb> <fat_image.img>
# Creates: protective MBR (LBA 0), GPT header (LBA 1), partition entries (LBA 2-33),
# and copies the FAT image into the ESP partition starting at LBA 2048.
param(
    [string]$Out,
    [int]$DiskMb,
    [string]$FatImage
)

$SECTOR = 512
$diskSectors = [long]$DiskMb * 2048
$lastLba = $diskSectors - 1
$espStart = 2048

# read fat image
$fatBytes = [System.IO.File]::ReadAllBytes($FatImage)
$fatSectors = [math]::Ceiling($fatBytes.Length / $SECTOR)
$espEnd = $espStart + $fatSectors - 1

# GUIDs (little-endian binary)
function Guid-FromString($s) {
    # s like "C12A7328-F81F-11D2-BA4B-00A0C93EC93B" -> 16 bytes LE
    $parts = $s -split '-'
    $bytes = New-Object byte[] 16
    # time_low (4) LE, time_mid (2) LE, time_hi (2) LE, then 2 bytes, then 6 bytes
    [Array]::Copy([System.BitConverter]::GetBytes([uint32]("0x" + $parts[0])), 0, $bytes, 0, 4)
    [Array]::Copy([System.BitConverter]::GetBytes([uint16]("0x" + $parts[1])), 0, $bytes, 4, 2)
    [Array]::Copy([System.BitConverter]::GetBytes([uint16]("0x" + $parts[2])), 0, $bytes, 6, 2)
    $bytes[8] = [byte]([convert]::ToInt32($parts[3].Substring(0,2),16))
    $bytes[9] = [byte]([convert]::ToInt32($parts[3].Substring(2,2),16))
    $i = 10
    for ($j = 0; $j -lt $parts[4].Length; $j += 2) {
        $bytes[$i] = [byte]([convert]::ToInt32($parts[4].Substring($j,2),16)); $i++
    }
    return $bytes
}
$gptType = Guid-FromString "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"  # EFI System Partition
$gptPartGuid = Guid-FromString "5B1B31A1-9562-11D2-8E3F-00A0C969723B"

# CRC32 (poly 0xEDB88320)
$script:crcTable = New-Object byte[] 256
for ($i = 0; $i -lt 256; $i++) {
    $c = $i
    for ($k = 0; $k -lt 8; $k++) {
        if ($c -band 1) { $c = ($c -shr 1) -bxor 0xEDB88320 }
        else { $c = $c -shr 1 }
    }
    $script:crcTable[$i] = [byte]($c -band 0xFF)
}
function Crc32([byte[]]$data, $len) {
    $crc = 0xFFFFFFFF
    for ($i = 0; $i -lt $len; $i++) {
        $idx = [byte](($crc -band 0xFF) -bxor $data[$i])
        $crc = ($crc -shr 8) -bxor $script:crcTable[$idx]
    }
    return $crc -bxor 0xFFFFFFFF
}

$img = New-Object byte[] ([long]$diskSectors * $SECTOR)

function W($lba, [byte[]]$data) {
    $pos = [long]$lba * $SECTOR
    [Array]::Copy($data, 0, $img, $pos, [math]::Min($data.Length, $SECTOR))
}
function W64($arr, $off, $v) {
    $b = [System.BitConverter]::GetBytes([uint64]$v)
    for ($i = 0; $i -lt 8; $i++) { $arr[$off + $i] = $b[$i] }
}
function W32($arr, $off, $v) {
    $b = [System.BitConverter]::GetBytes([uint32]$v)
    for ($i = 0; $i -lt 4; $i++) { $arr[$off + $i] = $b[$i] }
}

# ---- Protective MBR (LBA 0) ----
$mbr = New-Object byte[] $SECTOR
# partition entry at offset 446: type 0xEE at 450, start LBA 1 at 454 (LE32), size
$mbr[446 + 4] = 0xEE   # partition type = GPT protective
W32 $mbr 454 1        # start LBA = 1 (GPT header)
W32 $mbr 458 ($fatSectors)  # size in sectors (approx)
$mbr[510] = 0x55; $mbr[511] = 0xAA
W 0 $mbr

# ---- Partition Entry Array (LBA 2..33) ----
$ent = New-Object byte[] 128
[Array]::Copy($gptType, 0, $ent, 0, 16)       # partition type GUID
[Array]::Copy($gptPartGuid, 0, $ent, 16, 16)  # unique partition GUID
W64 $ent 32 $espStart                         # first LBA
W64 $ent 40 $espEnd                           # last LBA
W64 $ent 48 1                                 # attributes: bit 0 = required (ESP)
# name UTF-16LE "ESP"
$name = [System.Text.Encoding]::Unicode.GetBytes("ESP")
[Array]::Copy($name, 0, $ent, 56, $name.Length)
# write 128 entries (only first used)
$entBlock = New-Object byte[] ($SECTOR * 32)
[Array]::Copy($ent, 0, $entBlock, 0, 128)
for ($lba = 2; $lba -lt 34; $lba++) { W $lba $entBlock }

# ---- partition array CRC (computed BEFORE header CRC) ----
$partCrc = Crc32 $entBlock ($SECTOR * 32)

# ---- GPT Header (LBA 1) ----
$hdr = New-Object byte[] $SECTOR
[System.Text.Encoding]::ASCII.GetBytes("EFI PART", 0, 8, $hdr, 0)
W32 $hdr 8 0x00010000          # revision
W32 $hdr 12 92                 # header size
W32 $hdr 16 0                  # header crc (placeholder, will fill after)
W32 $hdr 20 0                  # reserved
W64 $hdr 24 1                  # current LBA
W64 $hdr 32 $lastLba           # backup LBA
W64 $hdr 40 34                 # first usable LBA
W64 $hdr 48 ($diskSectors - 34) # last usable LBA
[Array]::Copy($gptPartGuid, 0, $hdr, 56, 16)  # disk GUID
W64 $hdr 72 2                  # partition entry LBA
W32 $hdr 80 128                # number of partitions
W32 $hdr 84 128                # entry size
W32 $hdr 88 $partCrc           # partition array crc (actual value)
$hdrCrc = Crc32 $hdr 92        # compute header CRC with correct partition entry crc
W32 $hdr 16 $hdrCrc            # write header crc
W 1 $hdr                       # write header to image

# ---- backup GPT header at last LBA ----
$bhdr = $hdr.Clone()
W64 $bhdr 24 $lastLba          # current LBA = last
W64 $bhdr 32 1                 # backup LBA = 1
$bhdrCrc = Crc32 $bhdr 92
W32 $bhdr 16 $bhdrCrc
W32 $bhdr 88 $partCrc          # backup partition array crc same
W $lastLba $bhdr
# backup partition array at lastLba-32 .. lastLba-1
$bakStart = $lastLba - 32
for ($i = 0; $i -lt 32; $i++) { W ($bakStart + $i) $entBlock }

# ---- copy FAT image into ESP partition at LBA 2048 ----
for ($i = 0; $i -lt $fatBytes.Length; $i++) {
    $img[([long]$espStart * $SECTOR) + $i] = $fatBytes[$i]
}

[System.IO.File]::WriteAllBytes($Out, $img)
Write-Host "wrote $Out ($diskSectors sectors, $DiskMb MB), ESP at LBA $espStart-$espEnd ($fatSectors sectors)"
