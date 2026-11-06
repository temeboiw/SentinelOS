#!/usr/bin/env python3
# make_disk_image.py — Build a GPT disk image: FAT32 ESP + NTFS system volume.
#
# Replaces the missing host toolchain (sfdisk, mkfs.vfat, mkntfs, ntfscp,
# ntfscat, mmd, mcopy, dd, gcc) with pure Python (python3). qemu-img is still
# used by build.sh to create the backing raw file, but all partitioning and
# filesystem authoring happens here.
#
# Layout (512-byte sectors):
#   LBA 0      : protective MBR (type 0xEE)
#   LBA 1      : primary GPT header
#   LBA 2..33  : primary GPT partition entries (128 x 128 bytes)
#   LBA 2048   : ESP (FAT32) — /EFI/BOOT/BOOTX64.EFI + /EFI/magic/*.efi
#   LBA NTFS_START : NTFS system volume — mirror of build/rootfs/magic
#   last LBA   : backup GPT header (with backup entries just before it)
#
# Usage:
#   python3 make_disk_image.py <diskimg> <esp_dir> <rootfs_dir> \
#       <esp_mib> <ntfs_mib> <ntfs_start_lba> <disk_sectors> [sector_size=512]

import os
import sys
import struct
import binascii
import uuid as uuidmod
from collections import defaultdict

SECTOR = 512
ESP_TYPE_GUID = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"
NTFS_TYPE_GUID = "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7"


def le16(v):
    return struct.pack("<H", v & 0xFFFF)


def le32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def le64(v):
    return struct.pack("<Q", v & 0xFFFFFFFFFFFFFFFF)


def crc32(data):
    return binascii.crc32(data) & 0xFFFFFFFF


def guid_to_bytes(g):
    if isinstance(g, uuidmod.UUID):
        return g.bytes_le
    return uuidmod.UUID(g).bytes_le


# =====================================================================
# FAT32 (EFI System Partition)
# =====================================================================

def name83(name):
    """Convert a filename to an 11-byte FAT 8.3 directory name (uppercase)."""
    if name == ".":
        return b".          "  # dot + 10 spaces
    if name == "..":
        return b"..         "  # dot dot + 9 spaces
    base, dot, ext = name.partition(".")
    base = base.upper().ljust(8)[:8].encode("ascii")
    ext = ext.upper().ljust(3)[:3].encode("ascii")
    return base + ext


def dir_entry(name, attr, cluster, size):
    """32-byte FAT directory entry."""
    e = bytearray(32)
    e[0:11] = name83(name)
    e[11] = attr & 0xFF
    e[20:22] = le16(cluster >> 16)          # high 2 bytes of first cluster
    e[22:24] = le16(0)                      # write time
    e[24:26] = le16(0)                      # write date
    e[26:28] = le16(cluster & 0xFFFF)       # low 2 bytes of first cluster
    e[28:32] = le32(size & 0xFFFFFFFF)      # file size
    return bytes(e)


def build_fat32(files, total_sectors, label="MAGICESP", esp_start=2048):
    """Build a FAT32 filesystem image.

    files: dict {relpath: data_bytes} using '/' separators.
    Fixed ESP tree:
        / (root)  -> contains "EFI" dir
        /EFI      -> contains "BOOT" dir, "magic" dir
        /EFI/BOOT -> contains BOOTX64.EFI
        /EFI/magic-> contains BOOTUNI.EFI, LOADUFI.EFI
    """
    spc = 8                      # sectors per cluster (4 KiB clusters)
    cluster_size = spc * SECTOR
    reserved = 32                # reserved sectors (boot + FSInfo + backup)
    nfats = 2
    media = 0xF8

    # Fixed-point iteration to size the FAT so it exactly covers the clusters.
    fat_size = max(1, (total_sectors // spc * 4 + SECTOR - 1) // SECTOR)
    for _ in range(10):
        data_sectors = total_sectors - reserved - nfats * fat_size
        nclusters = data_sectors // spc
        new_fat = (nclusters * 4 + SECTOR - 1) // SECTOR
        if new_fat == fat_size:
            break
        fat_size = new_fat
    data_sectors = total_sectors - reserved - nfats * fat_size
    nclusters = data_sectors // spc
    first_data_sector = reserved + nfats * fat_size

    EOF = 0x0FFFFFFF
    fat = [0] * (nclusters + 2)
    fat[0] = 0xFFFFFFF8 | media
    fat[1] = 0xFFFFFFFF

    # Fixed directory tree (matches the UEFI fallback boot path required by
    # bootx64.c: it loads \EFI\magic\bootuni.efi from the ESP).
    dirs = [  # (relpath, parent_relpath)
        ("", None),
        ("EFI", ""),
        ("EFI/BOOT", "EFI"),
        ("EFI/magic", "EFI"),
    ]
    cluster_of = {}
    next_cluster = 2
    for relpath, _ in dirs:
        cluster_of[relpath] = next_cluster
        next_cluster += 1
    # File clusters follow the directory clusters. Allocate a contiguous run
    # of ncl clusters per file so multi-cluster files don't overlap the next
    # file's starting cluster.
    file_clusters = {}
    for relpath in sorted(files):
        file_clusters[relpath] = next_cluster
        next_cluster += max(1, (len(files[relpath]) + cluster_size - 1) // cluster_size)

    # FAT chains: directories occupy a single cluster (EOF).
    for relpath in cluster_of:
        fat[cluster_of[relpath]] = EOF
    # Files: one contiguous run per file, last cluster = EOF.
    for relpath in sorted(files):
        data = files[relpath]
        ncl = max(1, (len(data) + cluster_size - 1) // cluster_size)
        start = file_clusters[relpath]
        for i in range(ncl):
            c = start + i
            fat[c] = EOF if i == ncl - 1 else c + 1

    def data_offset(cluster):
        return (first_data_sector + (cluster - 2) * spc) * SECTOR

    esp = bytearray(total_sectors * SECTOR)

    # Write both FAT copies.
    fat_bytes_offset = reserved * SECTOR
    for copy in range(nfats):
        base = fat_bytes_offset + copy * fat_size * SECTOR
        for i, v in enumerate(fat):
            struct.pack_into("<I", esp, base + i * 4, v)

    # Write directory clusters.
    def parent_cluster(relpath):
        for r, p in dirs:
            if r == relpath:
                return cluster_of[p] if p is not None else cluster_of[""]
        return cluster_of[""]

    for relpath, parent in dirs:
        c = cluster_of[relpath]
        off = data_offset(c)
        buf = bytearray()
        buf += dir_entry(".", 0x10, c, 0)
        buf += dir_entry("..", 0x10, parent_cluster(relpath), 0)
        # Children: subdirectories.
        for r, p in dirs:
            if p == relpath:
                buf += dir_entry(r.rsplit("/", 1)[-1], 0x10, cluster_of[r], 0)
        # Children: files.
        for rel in sorted(files):
            rparent = rel.rsplit("/", 1)[0] if "/" in rel else ""
            if rparent == relpath:
                buf += dir_entry(rel.rsplit("/", 1)[-1], 0x20,
                                 file_clusters[rel], len(files[rel]))
        # Pad the rest of the cluster with zeros.
        esp[off:off + len(buf)] = buf

    # Write file data clusters.
    for relpath in sorted(files):
        data = files[relpath]
        ncl = max(1, (len(data) + cluster_size - 1) // cluster_size)
        start = file_clusters[relpath]
        for i in range(ncl):
            c = start + i
            chunk = data[i * cluster_size:(i + 1) * cluster_size]
            off = data_offset(c)
            esp[off:off + len(chunk)] = chunk

    # ---- FAT32 boot sector (LBA 0 of the ESP image) ----
    bs = bytearray(SECTOR)
    bs[0:3] = b"\xeb\x58\x90"                    # jump
    bs[3:11] = b"MSWIN4.1"                       # OEM
    struct.pack_into("<H", bs, 11, SECTOR)       # bytes/sector
    bs[13] = spc                                 # sectors/cluster
    struct.pack_into("<H", bs, 14, reserved)     # reserved sectors
    bs[16] = nfats                               # number of FATs
    struct.pack_into("<H", bs, 17, 0)            # root entries (0 = FAT32)
    struct.pack_into("<H", bs, 19, 0)            # total sectors 16 (0 = FAT32)
    bs[21] = media                               # media descriptor
    struct.pack_into("<H", bs, 22, 0)            # FAT size 16 (0 = FAT32)
    struct.pack_into("<H", bs, 24, 0)            # sectors/track
    struct.pack_into("<H", bs, 26, 0)            # heads
    struct.pack_into("<I", bs, 28, esp_start)        # hidden sectors (ESP starts at LBA esp_start)
    struct.pack_into("<I", bs, 32, total_sectors)# total sectors 32 (4 bytes only!)
    struct.pack_into("<I", bs, 36, fat_size)     # FAT32 FAT size (sectors) at offset 0x24
    struct.pack_into("<H", bs, 42, 0)            # version at offset 0x2A
    struct.pack_into("<I", bs, 44, cluster_of[""])# root directory cluster at offset 0x2C
    struct.pack_into("<H", bs, 48, 1)            # FS info sector at offset 0x30
    struct.pack_into("<H", bs, 50, 6)            # backup boot sector at offset 0x32
    bs[60] = 0x80                                # physical drive number at offset 0x3C
    bs[62] = 0x29                                # extended boot signature at offset 0x3E
    struct.pack_into("<I", bs, 63, 0x12345678)   # volume serial at offset 0x3F
    bs[67:78] = label.encode("ascii").ljust(11)  # volume label at offset 0x43
    bs[78:86] = b"FAT32   "                      # file system type at offset 0x4E
    bs[510:512] = b"\x55\xaa"                    # boot signature
    esp[0:SECTOR] = bs

    # ---- FS Info sector (sector 1) ----
    fsi = bytearray(SECTOR)
    struct.pack_into("<I", fsi, 0, 0x52526141)   # lead signature "RRaA"
    struct.pack_into("<I", fsi, 484, 0x72417245) # structure signature "RrAa"
    struct.pack_into("<I", fsi, 488, nclusters)  # free cluster count
    struct.pack_into("<I", fsi, 492, cluster_of[""])  # most recently allocated
    struct.pack_into("<I", fsi, 508, 0x000055AA) # trail signature
    esp[SECTOR:2 * SECTOR] = fsi

    # ---- Backup boot sector (sector 6) ----
    esp[6 * SECTOR:7 * SECTOR] = bs

    return bytes(esp)

# =====================================================================
# NTFS (system volume) — minimal but structurally consistent writer.
# =====================================================================

MFT_RECORD_SIZE = 4096  # 1 cluster (spc=8) per MFT record


def mft_record():
    return bytearray(MFT_RECORD_SIZE)


def file_record_base():
    r = mft_record()
    r[0:4] = b"FILE"
    # update sequence array offset & count (fixup). We keep a minimal fixup:
    # one entry covering the first sector; the rest we leave as-is (our reader
    # ignores fixups). sector 0 last-2-bytes will be replaced by array[0].
    struct.pack_into("<H", r, 4, 0x20)   # offset to update sequence array
    struct.pack_into("<H", r, 6, 1)      # count of array entries
    # sequence number, link count, offset to first attribute, etc.
    struct.pack_into("<H", r, 16, 1)     # sequence number
    struct.pack_into("<H", r, 18, 1)     # hard link count
    struct.pack_into("<H", r, 20, 0x38)  # offset to first attribute (56)
    struct.pack_into("<H", r, 22, 0x0001)  # flags (in_use)
    struct.pack_into("<I", r, 24, MFT_RECORD_SIZE)  # allocated size
    struct.pack_into("<I", r, 28, MFT_RECORD_SIZE)  # used size
    struct.pack_into("<Q", r, 32, 0)     # base file record (0 = base)
    struct.pack_into("<H", r, 40, 0)     # next attribute instance
    # update sequence array: save original last-2 bytes of sector 0, then write fixup
    orig = struct.unpack("<H", r[510:512])[0]
    struct.pack_into("<H", r, 0x20, orig)
    struct.pack_into("<H", r, 510, orig)  # sector 0 fixup (no-op: same value)
    return r


def add_attribute(record, offset, atype, value, res=0, name=b""):
    """Append an attribute at `offset`; return new offset. Aligns to 8.

    Writes directly into `record`. Supports resident (res=0) and
    nonresident (res=1) attributes.
    """
    while offset % 8 != 0:
        offset += 1
    name_len = len(name) // 2  # name is UTF-16, count of chars
    if res == 0:
        # resident: name then value stored inline
        val_off = offset + 22 + name_len
        total = 22 + name_len + len(value)
        struct.pack_into("<I", record, offset + 0, atype)            # type
        struct.pack_into("<I", record, offset + 4, total)            # length
        struct.pack_into("<B", record, offset + 8, res)              # nonresident flag
        struct.pack_into("<B", record, offset + 9, name_len)
        struct.pack_into("<H", record, offset + 10, offset + 22)     # name offset
        struct.pack_into("<H", record, offset + 12, 0)               # flags
        struct.pack_into("<H", record, offset + 14, 0)               # instance
        struct.pack_into("<H", record, offset + 16, len(value))      # value length
        struct.pack_into("<H", record, offset + 18, val_off - offset)# value offset
        record[offset + 20] = 0                                      # reserved
        record[offset + 21] = 0                                      # indexing flags
        record[offset + 22:offset + 22 + name_len] = name
        record[val_off:val_off + len(value)] = value
        return offset + total
    else:
        # nonresident: fixed part is 16 bytes (0-15); caller appends run array
        struct.pack_into("<I", record, offset + 0, atype)            # type
        struct.pack_into("<I", record, offset + 4, 16 + name_len)    # length (header only)
        struct.pack_into("<B", record, offset + 8, res)              # nonresident flag
        struct.pack_into("<B", record, offset + 9, name_len)
        struct.pack_into("<H", record, offset + 10, offset + 16)     # name offset
        struct.pack_into("<H", record, offset + 12, 0)               # flags
        struct.pack_into("<H", record, offset + 14, 0)               # instance
        record[offset + 16:offset + 16 + name_len] = name
        return offset + 16 + name_len


def add_standard_information(record, offset, mft_ref):
    # $STANDARD_INFORMATION (0x10), resident, 48 bytes of timestamps=0.
    value = bytearray(48)
    struct.pack_into("<Q", value, 0, 0)   # create
    struct.pack_into("<Q", value, 8, 0)   # modified
    struct.pack_into("<Q", value, 16, 0)  # mft changed
    struct.pack_into("<Q", value, 24, 0)  # read
    struct.pack_into("<Q", value, 32, 0)  # allocated size (updated later)
    struct.pack_into("<Q", value, 40, 0)  # real size
    return add_attribute(record, offset, 0x10, bytes(value), res=0)


def add_file_name(record, offset, parent_ref, name, is_dir, alloc_size, real_size):
    """$FILE_NAME (0x30), resident."""
    name_utf16 = name.encode("utf-16-le")
    # parent reference: 6 bytes (48-bit record + 16-bit sequence), LE.
    parent_bytes = le64(parent_ref & 0x0000FFFFFFFFFFFF)[:6] + b"\x01\x00"
    now = 0
    value = bytearray()
    value += parent_bytes                                  # 0-5 parent ref
    value += le64(now)                                     # 6-13 creation
    value += le64(now)                                     # 14-21 modified
    value += le64(now)                                     # 22-29 mft changed
    value += le64(now)                                     # 30-37 read
    value += le64(alloc_size)                              # 38-45 allocated size
    value += le64(real_size)                               # 46-53 real size
    value += le16(0x0001 if is_dir else 0x0000)            # 54-55 flags
    value += le16(0)                                       # reparse
    value += bytes([len(name)])                            # name length (chars) at offset 60
    value += bytes([3 if is_dir else 0])                   # namespace (3=Win32) at offset 61
    value += b"\x00\x00"                                   # reserved at offsets 62-63
    value += name_utf16                                    # name at offset 64
    return add_attribute(record, offset, 0x30, bytes(value), res=0)


def encode_data_run(length, start, prev):
    """Encode one data run: (length_clusters, start_cluster, prev_cluster)."""
    offset = start - prev
    # bytes needed
    def nbytes(v):
        if v == 0:
            return 0
        n = 0
        while v:
            n += 1
            v >>= 8
        return n
    lb = nbytes(length)
    ob = nbytes(offset)
    out = bytearray()
    out.append((ob << 4) | lb)
    if lb:
        out += length.to_bytes(lb, "little")
    if ob:
        out += offset.to_bytes(ob, "little")
    return bytes(out)


def finish_record(record, offset):
    # terminator attribute
    while offset % 8 != 0:
        offset += 1
    struct.pack_into("<I", record, offset, 0xFFFFFFFF)
    # update used size
    used = offset + 4
    struct.pack_into("<I", record, 28, used)
    return used


def build_ntfs(total_sectors, files):
    """Build an NTFS image.

    files: dict {relpath: data_bytes} with '/' separators, rooted at \\magic.
    Tree: \\magic (root) -> System32, Program Files -> files.
    """
    spc = 8
    cluster_size = spc * SECTOR

    # ---- Assign MFT records and data clusters ----
    # MFT record layout:
    #   0: $MFT, 1: $MFTMirr, 2: $Root, 3: $Bitmap,
    #   4: magic(root dir), 5: System32, 6: Program Files, 7+: files
    # We enumerate dirs first then files.
    # files: relpath -> (data, parent_dir_relpath)
    file_info = {}
    for relpath, data in files.items():
        parent = relpath.rsplit("/", 1)[0] if "/" in relpath else "magic"
        file_info[relpath] = (data, parent)

    record_of = {}   # path -> mft record number
    next_record = 0
    def get_record(path):
        nonlocal next_record
        if path not in record_of:
            record_of[path] = next_record
            next_record += 1
        return record_of[path]

    get_record("$MFT")       # 0
    get_record("$MFTMirr")   # 1
    get_record("$Root")      # 2
    get_record("$Bitmap")    # 3
    # Collect every directory implied by the file paths (NTFS root = \magic).
    all_dirs = {"magic"}
    for relpath in file_info:
        parts = relpath.split("/")
        for i in range(1, len(parts)):
            d = "/".join(parts[:i])
            if d != relpath:
                all_dirs.add(d)
    dir_records_order = sorted(all_dirs, key=lambda d: (d.count("/"), d))
    for path in dir_records_order:
        record_of[path] = get_record(path)
    for relpath in sorted(file_info):
        get_record(relpath)  # files after directories

    nmft = next_record
    # ---- Cluster allocation ----
    # LCN 0: boot sector. LCN 1: unused.
    # LCN 2 .. 2+nmft-1: MFT (one cluster per record).
    # LCN 2+nmft: MFTMirr (copy of record 0).
    # LCN 2+nmft+1: $Bitmap.
    # then file data clusters.
    mft_start_lcn = 2
    mftmirr_lcn = 2 + nmft
    bitmap_lcn = 2 + nmft + 1
    data_start_lcn = bitmap_lcn + 1

    # assign file data clusters (contiguous per file)
    file_clusters = {}
    next_lcn = data_start_lcn
    for relpath in sorted(file_info):
        data, _ = file_info[relpath]
        ncl = max(1, (len(data) + cluster_size - 1) // cluster_size)
        file_clusters[relpath] = (next_lcn, ncl)
        next_lcn += ncl
    total_lcns = next_lcn  # clusters 0..total_lcns-1 used

    # ---- Build MFT records ----
    records = [None] * nmft

    # --- Record 0: $MFT ---
    r0 = file_record_base()
    off = 0x38
    off = add_standard_information(r0, off, 0)
    off = add_file_name(r0, off, 0, "$MFT", True, nmft * cluster_size, nmft * cluster_size)
    # $DATA nonresident for MFT (fixed part at h_off, run array at h_off+64)
    h_off = off
    # NTFS attributes are 8-byte aligned; the reader aligns after each
    # attribute, so align h_off before starting $DATA to keep the run offset
    # field (relative to attribute start) consistent.
    h_off = (h_off + 7) & ~7
    run_off = h_off + 64
    struct.pack_into("<I", r0, h_off + 0, 0x80)                    # type
    struct.pack_into("<B", r0, h_off + 8, 1)                       # nonresident flag
    struct.pack_into("<Q", r0, h_off + 16, 0)                      # lowest VCN
    struct.pack_into("<Q", r0, h_off + 24, nmft - 1)               # highest VCN
    struct.pack_into("<H", r0, h_off + 32, run_off - h_off)        # data run array offset (attr-relative)
    struct.pack_into("<H", r0, h_off + 34, 0)
    struct.pack_into("<Q", r0, h_off + 40, nmft * cluster_size)    # allocated size
    struct.pack_into("<Q", r0, h_off + 48, nmft * cluster_size)    # real data size
    struct.pack_into("<Q", r0, h_off + 56, nmft * cluster_size)    # initialized size
    datalen = run_off + 0 - h_off  # placeholder, fixed below
    run_array = bytearray()
    run_array += encode_data_run(nmft, mft_start_lcn, 0)
    run_array.append(0x00)
    r0[run_off:run_off + len(run_array)] = run_array
    off = run_off + len(run_array)
    datalen = off - h_off
    while datalen % 8:
        datalen += 1
    struct.pack_into("<I", r0, h_off + 4, datalen)                 # total attribute length
    end = off
    while end % 8:
        end += 1
    struct.pack_into("<I", r0, end, 0xFFFFFFFF)
    struct.pack_into("<I", r0, 24, off)
    struct.pack_into("<I", r0, 28, off)
    records[0] = r0

    # --- Record 1: $MFTMirr (copy of record 0, nonresident $DATA at mftmirr_lcn) ---
    r1 = file_record_base()
    struct.pack_into("<H", r1, 16, 1)
    off = 0x38
    off = add_standard_information(r1, off, 1)
    off = add_file_name(r1, off, 0, "$MFTMirr", True, cluster_size, cluster_size)
    h_off = off
    h_off = (h_off + 7) & ~7
    run_off = h_off + 64
    struct.pack_into("<I", r1, h_off + 0, 0x80)                    # type
    struct.pack_into("<B", r1, h_off + 8, 1)                       # nonresident flag
    struct.pack_into("<Q", r1, h_off + 16, 0)                      # lowest VCN
    struct.pack_into("<Q", r1, h_off + 24, 0)                      # highest VCN = 0
    struct.pack_into("<H", r1, h_off + 32, run_off - h_off)        # data run array offset (attr-relative)
    struct.pack_into("<H", r1, h_off + 34, 0)
    struct.pack_into("<Q", r1, h_off + 40, cluster_size)           # allocated size
    struct.pack_into("<Q", r1, h_off + 48, cluster_size)           # real data size
    struct.pack_into("<Q", r1, h_off + 56, cluster_size)           # initialized size
    run_array = bytearray()
    run_array += encode_data_run(1, mftmirr_lcn, 0)
    run_array.append(0x00)
    r1[run_off:run_off + len(run_array)] = run_array
    off = run_off + len(run_array)
    datalen = off - h_off
    while datalen % 8:
        datalen += 1
    struct.pack_into("<I", r1, h_off + 4, datalen)                 # total attribute length
    end = off
    while end % 8:
        end += 1
    struct.pack_into("<I", r1, end, 0xFFFFFFFF)
    struct.pack_into("<I", r1, 24, off)
    struct.pack_into("<I", r1, 28, off)
    records[1] = r1

    # --- Record 2: $Root (root directory, name "", parent = itself) ---
    def root_attrs(r, off):
        off = add_standard_information(r, off, 2)
        off = add_file_name(r, off, 2, "", True, cluster_size, cluster_size)
        # $INDEX_ROOT ($I30) resident — minimal, no entries (we rely on $FILE_NAME)
        struct.pack_into("<I", r, off + 0, 0x90)   # type
        struct.pack_into("<I", r, off + 4, 16)     # length (header only)
        struct.pack_into("<B", r, off + 8, 0)      # resident
        off += 16
        return off
    r2 = file_record_base()
    struct.pack_into("<H", r2, 16, 2)
    off = 0x38
    off = root_attrs(r2, off)
    end = finish_record(r2, off)
    struct.pack_into("<I", r2, 24, end + 4)
    struct.pack_into("<I", r2, 28, end + 4)
    records[2] = r2

    # --- Record 3: $Bitmap ---
    r3 = file_record_base()
    struct.pack_into("<H", r3, 16, 3)
    off = 0x38
    off = add_standard_information(r3, off, 3)
    off = add_file_name(r3, off, 2, "$Bitmap", False, cluster_size, cluster_size)
    h_off = off
    h_off = (h_off + 7) & ~7
    run_off = h_off + 64
    struct.pack_into("<I", r3, h_off + 0, 0x80)                    # type
    struct.pack_into("<B", r3, h_off + 8, 1)                       # nonresident flag
    struct.pack_into("<Q", r3, h_off + 16, 0)                      # lowest VCN
    struct.pack_into("<Q", r3, h_off + 24, 0)                      # highest VCN = 0
    struct.pack_into("<H", r3, h_off + 32, run_off - h_off)        # data run array offset (attr-relative)
    struct.pack_into("<H", r3, h_off + 34, 0)
    struct.pack_into("<Q", r3, h_off + 40, cluster_size)           # allocated size
    struct.pack_into("<Q", r3, h_off + 48, cluster_size)           # real data size
    struct.pack_into("<Q", r3, h_off + 56, cluster_size)           # initialized size
    run_array = bytearray()
    run_array += encode_data_run(1, bitmap_lcn, 0)
    run_array.append(0x00)
    r3[run_off:run_off + len(run_array)] = run_array
    off = run_off + len(run_array)
    datalen = off - h_off
    while datalen % 8:
        datalen += 1
    struct.pack_into("<I", r3, h_off + 4, datalen)                 # total attribute length
    end = off
    while end % 8:
        end += 1
    struct.pack_into("<I", r3, end, 0xFFFFFFFF)
    struct.pack_into("<I", r3, 24, off)
    struct.pack_into("<I", r3, 28, off)
    records[3] = r3

    # --- Directory records (magic, System32, Program Files, drivers) ---
    def dir_attrs(r, off, name, parent_rec, alloc, real):
        off = add_standard_information(r, off, parent_rec)
        off = add_file_name(r, off, parent_rec, name, True, alloc, real)
        # $INDEX_ROOT ($I30) resident — minimal, no entries (we rely on $FILE_NAME)
        struct.pack_into("<I", r, off + 0, 0x90)   # type
        struct.pack_into("<I", r, off + 4, 16)     # length (header only)
        struct.pack_into("<B", r, off + 8, 0)      # resident
        off += 16
        return off

    parent_of = {"magic": 2}
    name_of = {"magic": "magic"}
    for path in dir_records_order:
        if path == "magic":
            continue
        parent_of[path] = record_of[path.rsplit("/", 1)[0]]
        name_of[path] = path.rsplit("/", 1)[-1]
    for path in dir_records_order:
        rec = record_of[path]
        r = file_record_base()
        struct.pack_into("<H", r, 16, rec)
        off = 0x38
        off = dir_attrs(r, off, name_of[path], parent_of[path], cluster_size, cluster_size)
        end = finish_record(r, off)
        struct.pack_into("<I", r, 24, end + 4)
        struct.pack_into("<I", r, 28, end + 4)
        records[rec] = r

    # --- File records ---
    for relpath in sorted(file_info):
        data, parent = file_info[relpath]
        rec = record_of[relpath]
        start_lcn, ncl = file_clusters[relpath]  # file_clusters stores (lcn, ncl)
        alloc = ncl * cluster_size
        real = len(data)
        r = file_record_base()
        struct.pack_into("<H", r, 16, rec)
        off = 0x38
        off = add_standard_information(r, off, record_of[parent])
        off = add_file_name(r, off, record_of[parent], relpath.rsplit("/", 1)[-1],
                            False, alloc, real)
        h_off = off
        h_off = (h_off + 7) & ~7
        run_off = h_off + 64
        struct.pack_into("<I", r, h_off + 0, 0x80)                    # type
        struct.pack_into("<B", r, h_off + 8, 1)                       # nonresident flag
        struct.pack_into("<Q", r, h_off + 16, 0)                      # lowest VCN
        struct.pack_into("<Q", r, h_off + 24, ncl - 1)                # highest VCN
        struct.pack_into("<H", r, h_off + 32, run_off - h_off)        # data run array offset (attr-relative)
        struct.pack_into("<H", r, h_off + 34, 0)
        struct.pack_into("<Q", r, h_off + 40, alloc)                  # allocated size
        struct.pack_into("<Q", r, h_off + 48, real)                   # real data size
        struct.pack_into("<Q", r, h_off + 56, real)                   # initialized size
        run_array = bytearray()
        run_array += encode_data_run(ncl, start_lcn, 0)
        run_array.append(0x00)
        r[run_off:run_off + len(run_array)] = run_array
        off = run_off + len(run_array)
        datalen = off - h_off
        while datalen % 8:
            datalen += 1
        struct.pack_into("<I", r, h_off + 4, datalen)                 # total attribute length
        end = off
        while end % 8:
            end += 1
        struct.pack_into("<I", r, end, 0xFFFFFFFF)
        struct.pack_into("<I", r, 24, end + 4)
        struct.pack_into("<I", r, 28, end + 4)
        records[rec] = r

    # ---- Assemble MFT image ----
    mft = bytearray(nmft * MFT_RECORD_SIZE)
    for i, rec in enumerate(records):
        if rec is None:
            rec = bytearray(MFT_RECORD_SIZE)
        mft[i * MFT_RECORD_SIZE:(i + 1) * MFT_RECORD_SIZE] = rec

    # MFTMirr = copy of record 0
    mftmirr = bytearray(MFT_RECORD_SIZE)
    mftmirr[:] = records[0]

    # ---- $Bitmap ----
    # Mark used clusters: boot(0), MFT(2..2+nmft-1), MFTMirr(mftmirr_lcn),
    # bitmap(bitmap_lcn), file data clusters.
    nbitmap_clusters = max(1, (total_lcns + cluster_size * 8 - 1) // (cluster_size * 8))
    # we use one bitmap cluster (4096 bytes = 32768 bits) — enough for our size.
    bitmap = bytearray(cluster_size)
    def set_bit(lcn):
        bitmap[lcn // 8] |= (1 << (lcn % 8))
    set_bit(0)  # boot sector cluster
    for lcn in range(mft_start_lcn, mft_start_lcn + nmft):
        set_bit(lcn)
    set_bit(mftmirr_lcn)
    set_bit(bitmap_lcn)
    for relpath in sorted(file_info):
        start, ncl = file_clusters[relpath]
        for lcn in range(start, start + ncl):
            set_bit(lcn)

    # ---- Boot sector (VBR) ----
    bs = bytearray(SECTOR)
    bs[0:3] = b"\xeb\x52\x90"
    bs[3:11] = b"NTFS    "
    struct.pack_into("<H", bs, 11, SECTOR)       # bytes per sector
    bs[13] = spc                                  # sectors per cluster
    struct.pack_into("<H", bs, 14, 0)             # reserved sectors
    bs[16] = 0xF8                                 # media
    struct.pack_into("<H", bs, 17, 0)
    struct.pack_into("<H", bs, 19, 0)             # sectors per track
    struct.pack_into("<H", bs, 21, 0)             # heads
    struct.pack_into("<I", bs, 23, 0)             # hidden sectors
    struct.pack_into("<Q", bs, 27, total_sectors) # total sectors
    struct.pack_into("<Q", bs, 35, mft_start_lcn) # MFT start LCN
    struct.pack_into("<Q", bs, 43, mftmirr_lcn)   # MFTMirr LCN
    bs[51] = 0x01                                 # clusters per MFT record (1)
    struct.pack_into("<I", bs, 56, 1)             # clusters per index block
    struct.pack_into("<Q", bs, 64, 0x1234567890ABCDEF)  # volume serial
    bs[510:512] = b"\x55\xaa"
    # (no real boot code; firmware does not boot NTFS)

    # ---- Assemble full NTFS image ----
    ntfs = bytearray(total_sectors * SECTOR)
    ntfs[0:SECTOR] = bs
    # MFT at mft_start_lcn
    mft_off = mft_start_lcn * cluster_size
    ntfs[mft_off:mft_off + len(mft)] = mft
    # MFTMirr
    mm_off = mftmirr_lcn * cluster_size
    ntfs[mm_off:mm_off + len(mftmirr)] = mftmirr
    # Bitmap
    bm_off = bitmap_lcn * cluster_size
    ntfs[bm_off:bm_off + len(bitmap)] = bitmap
    # File data
    for relpath in sorted(file_info):
        data, _ = file_info[relpath]
        start, ncl = file_clusters[relpath]
        off = start * cluster_size
        ntfs[off:off + len(data)] = data

    return bytes(ntfs)


# =====================================================================
# GPT disk image assembly
# =====================================================================

def protective_mbr(disk_sectors):
    mbr = bytearray(SECTOR)
    # partition entry 0: protective (0xEE), start LBA 1, size disk_sectors-1
    mbr[446] = 0x00                            # not bootable
    mbr[447:450] = b"\x00\x00\x00"             # CHS start
    mbr[450] = 0xEE                            # type = EFI protective
    mbr[451:454] = b"\xff\xff\xff"             # CHS end
    struct.pack_into("<I", mbr, 454, 1)        # LBA start
    struct.pack_into("<I", mbr, 458, min(disk_sectors - 1, 0xFFFFFFFF))  # size
    mbr[510:512] = b"\x55\xaa"
    return bytes(mbr)


def partition_entry(type_guid, unique_guid, start_lba, end_lba, name, attr=0x1):
    e = bytearray(128)
    e[0:16] = guid_to_bytes(type_guid)         # partition type
    e[16:32] = guid_to_bytes(unique_guid)      # unique GUID
    struct.pack_into("<Q", e, 32, start_lba)   # starting LBA
    struct.pack_into("<Q", e, 40, end_lba)     # ending LBA (inclusive)
    struct.pack_into("<Q", e, 48, attr)        # attributes
    name_utf16 = name.encode("utf-16-le")
    e[56:56 + len(name_utf16)] = name_utf16    # partition name (36 chars max)
    return bytes(e)


def gpt_header(my_lba, backup_lba, first_usable, last_usable, disk_guid,
               entries_lba, num_entries, entries_crc):
    h = bytearray(92)
    h[0:8] = b"EFI PART"                       # signature
    struct.pack_into("<I", h, 8, 0x00010000)   # revision 1.0
    struct.pack_into("<I", h, 12, 92)          # header size
    struct.pack_into("<I", h, 16, 0)           # CRC (filled later)
    struct.pack_into("<I", h, 20, 0)           # reserved
    struct.pack_into("<Q", h, 24, my_lba)      # current LBA
    struct.pack_into("<Q", h, 32, backup_lba)  # backup LBA
    struct.pack_into("<Q", h, 40, first_usable)# first usable LBA
    struct.pack_into("<Q", h, 48, last_usable) # last usable LBA
    h[56:72] = guid_to_bytes(disk_guid)        # disk GUID
    struct.pack_into("<Q", h, 72, entries_lba) # partition entries LBA
    struct.pack_into("<I", h, 80, num_entries) # number of entries
    struct.pack_into("<I", h, 84, 128)         # entry size
    struct.pack_into("<I", h, 88, entries_crc) # CRC of partition array
    # header CRC over first 92 bytes with CRC field zeroed
    crc = crc32(bytes(h))
    struct.pack_into("<I", h, 16, crc)
    return bytes(h)


def build_disk_image(diskimg, esp_dir, rootfs_dir, esp_mib, ntfs_mib,
                     ntfs_start_lba, disk_sectors, sector_size=SECTOR):
    assert sector_size == SECTOR
    esp_bytes_total = esp_mib * 1024 * 1024
    ntfs_bytes_total = ntfs_mib * 1024 * 1024
    esp_total_sectors = esp_bytes_total // SECTOR
    ntfs_total_sectors = ntfs_bytes_total // SECTOR

    esp_start = 2048
    # ---- Collect ESP files ----
    esp_files = {}
    esp_root = os.path.abspath(esp_dir)
    for rel in ["EFI/BOOT/BOOTX64.EFI", "EFI/magic/BOOTUNI.EFI", "EFI/magic/LOADUFI.EFI"]:
        p = os.path.join(esp_root, rel)
        if os.path.isfile(p):
            with open(p, "rb") as f:
                esp_files[rel] = f.read()
        else:
            print("warning: ESP file missing: %s" % p, file=sys.stderr)
    esp_image = build_fat32(esp_files, esp_total_sectors, label="MAGICESP", esp_start=esp_start)

    # ---- Collect NTFS files (mirror of rootfs/magic) ----
    ntfs_files = {}
    magic_root = os.path.join(rootfs_dir, "magic")
    for dirpath, dirnames, filenames in os.walk(magic_root):
        for fn in filenames:
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, magic_root).replace(os.sep, "/")
            # The NTFS system volume is rooted at \magic (per the V10 interface
            # contract), so keep the "magic/" path component.
            with open(full, "rb") as f:
                ntfs_files["magic/" + rel] = f.read()
    if not ntfs_files:
        print("error: no files found under %s" % magic_root, file=sys.stderr)
        return 1
    ntfs_image = build_ntfs(ntfs_total_sectors, ntfs_files)

    # ---- GPT structures ----
    disk_guid = uuidmod.uuid4()
    num_parts = 2
    entries_lba = 2
    first_usable = 34
    esp_end = esp_start + esp_total_sectors - 1
    ntfs_start = ntfs_start_lba
    ntfs_end = ntfs_start + ntfs_total_sectors - 1
    last_usable = disk_sectors - 34  # keep room for backup entries + header
    if ntfs_end > last_usable:
        print("error: NTFS partition (%d) exceeds last usable LBA %d" % (ntfs_end, last_usable), file=sys.stderr)
        return 1

    unique_esp = uuidmod.uuid4()
    unique_ntfs = uuidmod.uuid4()

    part_entries = bytearray()
    part_entries += partition_entry(ESP_TYPE_GUID, unique_esp, esp_start, esp_end, "MAGICESP", attr=0x1)
    part_entries += partition_entry(NTFS_TYPE_GUID, unique_ntfs, ntfs_start, ntfs_end, "magic", attr=0x0)
    # pad to 128 entries
    part_entries += b"\x00" * (128 * 128 - len(part_entries))
    entries_crc = crc32(bytes(part_entries))

    primary_header = gpt_header(1, disk_sectors - 1, first_usable, last_usable,
                                disk_guid, entries_lba, 128, entries_crc)
    backup_header = gpt_header(disk_sectors - 1, 1, first_usable, last_usable,
                               disk_guid, entries_lba, 128, entries_crc)

    # ---- Assemble disk image ----
    img = bytearray(disk_sectors * SECTOR)

    # LBA 0: protective MBR (GPT-only disk, no BIOS/legacy boot code).
    img[0:SECTOR] = protective_mbr(disk_sectors)

    # primary GPT header at LBA 1.
    img[SECTOR:2 * SECTOR] = primary_header
    # primary partition entries
    img[2 * SECTOR:2 * SECTOR + len(part_entries)] = part_entries
    # ESP
    esp_off = esp_start * SECTOR
    img[esp_off:esp_off + len(esp_image)] = esp_image
    # NTFS
    ntfs_off = ntfs_start * SECTOR
    img[ntfs_off:ntfs_off + len(ntfs_image)] = ntfs_image
    # backup partition entries (32 sectors before last LBA)
    backup_entries_lba = disk_sectors - 33
    img[backup_entries_lba * SECTOR:backup_entries_lba * SECTOR + len(part_entries)] = part_entries
    # backup GPT header at last LBA
    img[(disk_sectors - 1) * SECTOR:disk_sectors * SECTOR] = backup_header

    # Write out (sparse-friendly: write whole buffer).
    tmp = diskimg + ".tmp"
    with open(tmp, "wb") as f:
        f.write(bytes(img))
    os.replace(tmp, diskimg)
    print("built %s (%d MiB, ESP %d MiB @ LBA %d, NTFS %d MiB @ LBA %d)"
          % (diskimg, disk_sectors // 2048, esp_mib, esp_start, ntfs_mib, ntfs_start))
    print("  ESP files: %s" % ", ".join(sorted(esp_files)))
    print("  NTFS files: %d" % len(ntfs_files))
    return 0


def main():
    if len(sys.argv) < 8:
        print(__doc__)
        return 2
    diskimg = sys.argv[1]
    esp_dir = sys.argv[2]
    rootfs_dir = sys.argv[3]
    esp_mib = int(sys.argv[4])
    ntfs_mib = int(sys.argv[5])
    ntfs_start_lba = int(sys.argv[6])
    disk_sectors = int(sys.argv[7])
    sector_size = int(sys.argv[8]) if len(sys.argv) > 8 else SECTOR
    return build_disk_image(diskimg, esp_dir, rootfs_dir, esp_mib, ntfs_mib,
                            ntfs_start_lba, disk_sectors, sector_size)


if __name__ == "__main__":
    sys.exit(main())
