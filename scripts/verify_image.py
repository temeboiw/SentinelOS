#!/usr/bin/env python3
# verify_image.py — Validate a SentinelOS GPT disk image (FAT32 ESP + NTFS).
#
# Parses the image structures directly (no host filesystem tools) and reports:
#   * GPT partition layout (protective MBR, primary/backup headers, entries)
#   * FAT32 ESP contents (boot files present, byte-exact vs source)
#   * NTFS system volume contents (System32 files present, byte-exact vs source)
#
# Usage:
#   python3 verify_image.py <diskimg> <esp_dir> <rootfs_dir> [iso_path]
#
#   iso_path : optional ISO 9660 image to verify (PVD, El Torito catalog, boot image)

import os
import sys
import struct
import uuid as uuidmod

SECTOR = 512


def le16(b, o=0):
    return struct.unpack_from("<H", b, o)[0]


def le32(b, o=0):
    return struct.unpack_from("<I", b, o)[0]


def le64(b, o=0):
    return struct.unpack_from("<Q", b, o)[0]


def guid_str(b, o=0):
    return str(uuidmod.UUID(bytes_le=b[o:o + 16]))


ESP_TYPE = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"
NTFS_TYPE = "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7"

failures = []


def fail(msg):
    failures.append(msg)
    print("  FAIL: %s" % msg)


def ok(msg):
    print("  ok: %s" % msg)


# =====================================================================
# GPT parsing
# =====================================================================

def parse_gpt(img, disk_sectors):
    # Protective MBR at LBA 0
    mbr = img[0:SECTOR]
    if mbr[510:512] != b"\x55\xaa":
        fail("protective MBR missing 0xAA55 signature")
    ptype = mbr[450]
    mbr_start = le32(mbr, 454)
    mbr_size = le32(mbr, 458)
    ok("protective MBR: type=0x%02X start=%d size=%d" % (ptype, mbr_start, mbr_size))
    if ptype != 0xEE:
        fail("protective MBR partition type is 0x%02X, expected 0xEE" % ptype)

    # Primary GPT header at LBA 1
    hdr = img[SECTOR:2 * SECTOR]
    if hdr[0:8] != b"EFI PART":
        fail("primary GPT header signature missing")
    header_size = le32(hdr, 12)
    hdr_crc = le32(hdr, 16)
    my_lba = le64(hdr, 24)
    backup_lba = le64(hdr, 32)
    first_usable = le64(hdr, 40)
    last_usable = le64(hdr, 48)
    disk_guid = guid_str(hdr, 56)
    entries_lba = le64(hdr, 72)
    num_entries = le32(hdr, 80)
    entry_size = le32(hdr, 84)
    entries_crc = le32(hdr, 88)
    ok("primary GPT header: disk=%s entries@LBA%d num=%d size=%d first_usable=%d last_usable=%d"
       % (disk_guid, entries_lba, num_entries, entry_size, first_usable, last_usable))
    # verify header crc
    import binascii
    hdr_zeroed = bytearray(hdr)
    hdr_zeroed[16:20] = b"\x00\x00\x00\x00"
    calc = binascii.crc32(bytes(hdr_zeroed[:header_size])) & 0xFFFFFFFF
    if calc != hdr_crc:
        fail("primary GPT header CRC mismatch (stored=%08X calc=%08X)" % (hdr_crc, calc))
    else:
        ok("primary GPT header CRC valid")

    # Partition entries
    entries_off = entries_lba * SECTOR
    parts = []
    for i in range(num_entries):
        e = img[entries_off + i * entry_size: entries_off + (i + 1) * entry_size]
        if e[0:16] == b"\x00" * 16:
            continue  # unused entry
        ptype_g = guid_str(e, 0)
        unique_g = guid_str(e, 16)
        start = le64(e, 32)
        end = le64(e, 40)
        attr = le64(e, 48)
        name = e[56:56 + 72].decode("utf-16-le", errors="replace").rstrip("\x00")
        parts.append((ptype_g, unique_g, start, end, attr, name))
    return parts, (my_lba, backup_lba, first_usable, last_usable, entries_crc)


def describe_type(guid):
    if guid.lower() == ESP_TYPE.lower():
        return "EFI System Partition (FAT32)"
    if guid.lower() == NTFS_TYPE.lower():
        return "NTFS"
    return "other (%s)" % guid


# =====================================================================
# FAT32 parsing
# =====================================================================

def fat32_read_file(img, part_start_lba, fat_offset, data_offset, spc, cluster_size,
                    root_cluster, fat, target_cluster, size):
    """Read a file's data by following the FAT chain from target_cluster."""
    out = bytearray()
    c = target_cluster
    visited = set()
    while c != 0 and c != 0x0FFFFFFF and c < len(fat):
        if c in visited:
            break  # avoid infinite loop on corrupt chain
        visited.add(c)
        coff = data_offset + (c - 2) * cluster_size
        out += img[coff:coff + cluster_size]
        c = fat[c]
    return bytes(out[:size])


def parse_fat32(img, part_start_lba, part_total_sectors, esp_dir):
    off = part_start_lba * SECTOR
    bs = img[off:off + SECTOR]
    if bs[510:512] != b"\x55\xaa":
        fail("ESP boot sector missing 0xAA55")
        return
    bps = le16(bs, 11)
    spc = bs[13]
    reserved = le16(bs, 14)
    nfats = bs[16]
    fat_size = le32(bs, 40)
    total_sectors = le64(bs, 32)
    root_cluster = le32(bs, 46)
    cluster_size = spc * bps
    fat_offset = (part_start_lba + reserved) * bps
    data_offset = (part_start_lba + reserved + nfats * fat_size) * bps

    ok("ESP FAT32: sectors/cluster=%d reserved=%d nfats=%d fat_size=%d root_cluster=%d total=%d"
       % (spc, reserved, nfats, fat_size, root_cluster, total_sectors))

    # read FAT
    nclusters = (total_sectors - reserved - nfats * fat_size) // spc
    fat = [0] * (nclusters + 2)
    for i in range(nclusters + 2):
        fat[i] = le32(img, fat_offset + i * 4)

    # walk directories from root_cluster
    found = {}  # relpath -> data
    visited_clusters = set()

    def walk(cluster, relprefix):
        if cluster in visited_clusters:
            fail("FAT32 cycle detected at cluster %d (relprefix=%s)" % (cluster, relprefix))
            return
        visited_clusters.add(cluster)
        coff = data_offset + (cluster - 2) * cluster_size
        data = img[coff:coff + cluster_size]
        o = 0
        while o + 32 <= cluster_size:
            e = data[o:o + 32]
            o += 32
            if e[0] == 0x00:
                continue  # unused slot; keep scanning (some writers leave leading gaps)
            if e[0] == 0xE5:
                continue
            attr = e[11]
            name = e[0:11].decode("ascii", errors="replace")
            cluster_hi = le16(e, 20)
            cluster_lo = le16(e, 26)
            fcluster = (cluster_hi << 16) | cluster_lo
            fsize = le32(e, 28)
            if attr & 0x08:  # volume label
                continue
            basename = name[0:8].rstrip() + "." + name[8:11].rstrip() if name[8:11].strip() else name[0:8].rstrip()
            if attr & 0x10:
                # directory
                if basename in (".", "..") or not basename:
                    continue
                sub = (relprefix + basename + "/").replace(" /", "/")
                walk(fcluster, sub)
            else:
                fdata = fat32_read_file(img, part_start_lba, fat_offset, data_offset,
                                        spc, cluster_size, root_cluster, fat,
                                        fcluster, fsize)
                found[relprefix + basename] = fdata

    walk(root_cluster, "")

    # Expected ESP files (FAT32 stores names uppercase; match case-insensitively)
    expected = ["EFI/BOOT/BOOTX64.EFI", "EFI/magic/BOOTUNI.EFI", "EFI/magic/LOADUFI.EFI"]
    found_ci = {k.lower(): (k, v) for k, v in found.items()}
    print("  ESP files found: %s" % ", ".join(sorted(found)))
    for exp in expected:
        exp_key = exp.lower()
        if exp_key not in found_ci:
            fail("ESP missing file: %s" % exp)
            continue
        actual_key, fdata = found_ci[exp_key]
        src = os.path.join(esp_dir, exp)
        if not os.path.isfile(src):
            fail("source ESP file missing: %s" % src)
            continue
        with open(src, "rb") as f:
            srcdata = f.read()
        if fdata == srcdata:
            ok("ESP %s byte-exact (%d bytes)" % (exp, len(srcdata)))
        else:
            fail("ESP %s content MISMATCH (image=%d src=%d)"
                 % (exp, len(fdata), len(srcdata)))
    return found


# =====================================================================
# NTFS parsing
# =====================================================================

def parse_ntfs(img, part_start_lba, part_total_sectors, rootfs_dir):
    off = part_start_lba * SECTOR
    bs = img[off:off + SECTOR]
    if bs[510:512] != b"\x55\xaa":
        fail("NTFS boot sector missing 0xAA55")
        return {}, {}
    bps = le16(bs, 11)
    spc = bs[13]
    mft_start_lcn = le64(bs, 35)
    cluster_size = spc * bps
    total_sectors = le64(bs, 27)
    ok("NTFS: bytes/sector=%d sectors/cluster=%d cluster_size=%d MFT_lcn=%d total=%d"
       % (bps, spc, cluster_size, mft_start_lcn, total_sectors))

    nmft = (part_total_sectors * SECTOR) // cluster_size  # upper bound; MFT size unknown
    # Read MFT: number of records = first $DATA run length of record 0. We don't
    # know it yet; read records until we run out of plausible FILE records.
    mft_off = off + mft_start_lcn * cluster_size

    def read_record(i):
        return img[mft_off + i * cluster_size: mft_off + (i + 1) * cluster_size]

    records = {}  # mft_ref -> dict of attributes
    # scan records 0..nmft-ish; stop when no more FILE signatures
    max_scan = min(4096, (part_total_sectors * SECTOR) // cluster_size)
    for i in range(max_scan):
        rec = read_record(i)
        if rec[0:4] != b"FILE":
            continue
        seq = le16(rec, 16)
        attrs_off = le16(rec, 20)
        ref = (i, seq)
        file_name = None
        data_runs = []
        o = attrs_off
        while o + 4 <= cluster_size and le32(rec, o) != 0xFFFFFFFF:
            atype = le32(rec, o)
            alen = le32(rec, o + 4)
            if alen == 0:
                break
            nonres = rec[o + 8]
            if atype == 0x30 and nonres == 0:  # $FILE_NAME resident
                # value offset
                voff = o + le16(rec, o + 18)
                vlen = le16(rec, o + 16)
                parent_ref = le64(rec, voff + 0) & 0x0000FFFFFFFFFFFF
                name_len = rec[voff + 60]        # name length (8-byte parent ref shifts by 2)
                namespace = rec[voff + 61]
                name = rec[voff + 64:voff + 64 + name_len * 2].decode("utf-16-le", errors="replace")
                file_name = (name, parent_ref)
            elif atype == 0x80 and nonres == 1:  # $DATA nonresident
                run_off = o + le16(rec, o + 32)
                allocated = le64(rec, o + 40)
                real = le64(rec, o + 48)
                # parse runs
                runs = []
                p = run_off
                prev = 0
                while p < o + alen and p < o + cluster_size:
                    hb = rec[p]
                    if hb == 0x00:
                        break
                    lb = hb & 0x0F
                    ob = (hb >> 4) & 0x0F
                    p += 1
                    length = int.from_bytes(rec[p:p + lb], "little") if lb else 0
                    p += lb
                    offset = int.from_bytes(rec[p:p + ob], "little") if ob else 0
                    p += ob
                    runs.append((length, prev + offset))
                    prev = prev + offset
                data_runs = (runs, allocated, real)
            o += alen
            # NTFS attributes are 8-byte aligned within the MFT record;
            # add_attribute() aligns each attribute start, so there may be
            # padding bytes between attributes that we must skip.
            o = (o + 7) & ~7
            if alen < 4:
                break
        records[ref] = {"name": file_name, "runs": data_runs, "seq": seq,
                        "used": le32(rec, 28)}

    # Build tree: map parent_ref -> list of (name, mft_ref)
    ok("NTFS MFT records scanned: %d" % len(records))

    def read_file_data(ref):
        rec = records.get(ref)
        if not rec or not rec["runs"]:
            return None
        runs, allocated, real = rec["runs"]
        out = bytearray()
        for length, start_lcn in runs:
            foff = part_start_lba * SECTOR + start_lcn * cluster_size
            chunk = img[foff:foff + length * cluster_size]
            out += chunk
        return bytes(out[:real])

    # find files by walking $FILE_NAME parent refs from $Root
    files = {}  # relpath -> data
    root_name = None
    for ref, rec in records.items():
        nm = rec["name"]
        if nm and nm[0] == "" and nm[1] == ref[0]:  # root: parent == itself
            root_name = ref

    def children_of(parent_ref):
        out = []
        for ref, rec in records.items():
            nm = rec["name"]
            if nm and nm[1] == parent_ref:
                out.append((nm[0], ref))
        return out

    # Walk tree from root: magic -> ...
    def walk_tree(ref, relprefix):
        for name, child_ref in children_of(ref[0]):
            if not name or child_ref == ref:
                continue
            rel = relprefix + name
            rec = records[child_ref]
            if rec["runs"]:
                data = read_file_data(child_ref)
                if data is not None:
                    files[rel] = data
            else:
                walk_tree(child_ref, rel + "/")

    if root_name is not None:
        walk_tree(root_name, "")
    else:
        fail("NTFS root directory not found")

    print("  NTFS files found: %d" % len(files))
    # Show some System32 files
    sys32 = [p for p in files if p.startswith("magic/System32/") and not p.startswith("magic/System32/drivers/")]
    print("  NTFS System32 files: %d" % len(sys32))

    # Verify required System32 files byte-exact vs source
    magic_root = os.path.join(rootfs_dir, "magic")
    required = [
        "magic/System32/krnl.exe", "magic/System32/hal.dll", "magic/System32/smss.exe",
        "magic/System32/bootvid.exe", "magic/System32/sentlogon.exe",
        "magic/System32/settings.exe", "magic/System32/cmd.exe", "magic/System32/explorer.exe",
        "magic/System32/kernel32.dll", "magic/System32/user32.dll", "magic/System32/gdi32.dll",
    ]
    for req in required:
        src = os.path.join(magic_root, req[len("magic/"):])
        if req not in files:
            fail("NTFS missing file: %s" % req)
        elif not os.path.isfile(src):
            fail("source missing: %s" % src)
        else:
            with open(src, "rb") as f:
                srcdata = f.read()
            if files[req] == srcdata:
                ok("NTFS %s byte-exact (%d bytes)" % (req, len(srcdata)))
            else:
                fail("NTFS %s content MISMATCH (image=%d src=%d)"
                     % (req, len(files[req]), len(srcdata)))

    # Report a few file names found
    sample = sorted(files)[:15]
    print("  NTFS sample files: %s" % sample)
    return files


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    diskimg = sys.argv[1]
    esp_dir = sys.argv[2]
    rootfs_dir = sys.argv[3]
    with open(diskimg, "rb") as f:
        img = f.read()
    disk_sectors = len(img) // SECTOR

    print("== GPT partition layout")
    parts, gpt = parse_gpt(img, disk_sectors)
    for ptype_g, unique_g, start, end, attr, name in parts:
        print("  partition: %s start=%d end=%d sectors=%d name='%s' attr=%d"
              % (describe_type(ptype_g), start, end, end - start + 1, name, attr))

    # Map partition type -> parse
    esp = None
    ntfs = None
    for ptype_g, unique_g, start, end, attr, name in parts:
        if ptype_g.lower() == ESP_TYPE.lower():
            esp = (start, end - start + 1)
        elif ptype_g.lower() == NTFS_TYPE.lower():
            ntfs = (start, end - start + 1)

    if esp:
        print("\n== FAT32 ESP (LBA %d, %d sectors)" % esp)
        parse_fat32(img, esp[0], esp[1], esp_dir)
    else:
        fail("no ESP partition found")

    if ntfs:
        print("\n== NTFS system volume (LBA %d, %d sectors)" % ntfs)
        parse_ntfs(img, ntfs[0], ntfs[1], rootfs_dir)
    else:
        fail("no NTFS partition found")

    # Optional ISO verification (4th argument = ISO path)
    if len(sys.argv) >= 5:
        iso_path = sys.argv[4]
        print("\n== ISO 9660 + El Torito (LBA %d bytes/sector)" % ISO_SECTOR)
        verify_iso(iso_path)

    print("\n== RESULT: %s" % ("FAILURES: %d" % len(failures) if failures else "ALL CHECKS PASSED"))
    for f in failures:
        print("  - %s" % f)
    return 1 if failures else 0


# =====================================================================
# ISO 9660 + El Torito verification
# =====================================================================

ISO_SECTOR = 2048


def verify_iso(iso_path):
    """Verify ISO 9660 PVD, El Torito boot record, catalog, and boot image."""
    with open(iso_path, "rb") as f:
        iso = f.read()

    total_sectors = len(iso) // ISO_SECTOR

    # LBA 16: Primary Volume Descriptor
    pvd = iso[16 * ISO_SECTOR:17 * ISO_SECTOR]
    if pvd[0:6] != b"\x01CD001":
        fail("ISO PVD missing CD001 signature at LBA 16")
        return
    vol_id = pvd[40:72].split(b"\x00")[0].decode("ascii", errors="replace")
    vol_space_le = struct.unpack_from("<I", pvd, 80)[0]
    ok("ISO PVD at LBA 16: volume_id=%r volume_space_sectors=%d" % (vol_id, vol_space_le))
    if vol_space_le != total_sectors:
        fail("ISO volume space size %d != actual %d sectors" % (vol_space_le, total_sectors))

    # LBA 17: El Torito Boot Record Volume Descriptor
    brd = iso[17 * ISO_SECTOR:18 * ISO_SECTOR]
    if brd[0:6] != b"\x00CD001":
        fail("ISO El Torito Boot Record missing CD001 signature at LBA 17")
        return
    if brd[7:7 + 23] != b"EL TORITO SPECIFICATION":
        fail("ISO Boot Record missing EL TORITO SPECIFICATION string")
        return
    catalog_lba_le = struct.unpack_from("<I", brd, 120)[0]
    ok("ISO El Torito Boot Record at LBA 17: boot catalog at LBA %d" % catalog_lba_le)

    # Boot Catalog (at catalog_lba)
    cat_off = catalog_lba_le * ISO_SECTOR
    cat = iso[cat_off:cat_off + 512]
    if cat[0] != 0x01:
        fail("Boot Catalog validation entry header ID is 0x%02X, expected 0x01" % cat[0])
        return
    idstr = cat[4:4 + 16].split(b"\x00")[0]
    if idstr != b"EL TORITO SPEC":
        fail("Boot Catalog ID string is %r, expected b'EL TORITO SPEC'" % idstr)
        return
    # Checksum: make the sum of all bytes except the checksum field,
    # then negate and store as 16-bit LE at [28:30]. Verify by recomputing.
    s = sum(cat[:28]) + sum(cat[30:])
    expected = (-s) & 0xFFFF
    stored = struct.unpack_from("<H", cat, 28)[0]
    if stored != expected:
        fail("Boot Catalog checksum mismatch: stored=0x%04X expected=0x%04X" % (stored, expected))
        return
    ok("Boot Catalog at LBA %d: validation entry OK, checksum valid" % catalog_lba_le)

    # Section entry at offset 32: EFI boot image
    ent = cat[32:64]
    if ent[0] != 0x88:
        fail("Boot Catalog section entry bootable indicator is 0x%02X, expected 0x88" % ent[0])
        return
    if ent[1] != 0xEF:
        fail("Boot Catalog section entry media type is 0x%02X, expected 0xEF (EFI)" % ent[1])
        return
    boot_image_lba = struct.unpack_from("<I", ent, 8)[0]
    boot_image_sectors = struct.unpack_from("<I", ent, 12)[0]
    ok("Boot Catalog EFI section entry: boot image at LBA %d, %d sectors" % (boot_image_lba, boot_image_sectors))

    # Boot image present within ISO bounds
    boot_end_lba = boot_image_lba + boot_image_sectors
    if boot_end_lba > total_sectors:
        fail("El Torito boot image extends beyond ISO (LBA %d + %d > %d)"
             % (boot_image_lba, boot_image_sectors, total_sectors))
        return
    ok("El Torito boot image within ISO bounds (LBA %d-%d of %d)"
       % (boot_image_lba, boot_end_lba - 1, total_sectors))

    print("\n== ISO RESULT: %s" % ("FAILURES: %d" % len(failures) if failures else "ALL CHECKS PASSED"))


if __name__ == "__main__":
    sys.exit(main())
