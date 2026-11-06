#!/usr/bin/env python3
# make_iso.py — Build a bootable ISO 9660 image with El Torito UEFI boot.
#
# The ISO contains:
#   * an ISO 9660 tree (Primary Volume Descriptor, directories, file data)
#   * an El Torito Boot Catalog whose EFI section entry (type 0xEF) points at
#     the ESP FAT image (the El Torito boot image) containing
#     \EFI\BOOT\BOOTX64.EFI — the UEFI fallback boot path.
#
# Uses only Python. No xorriso/mkisofs required. End-to-end boot is validated
# structurally (PVD, boot catalog, boot image present); it is not executed
# here because no OVMF UEFI firmware is present in this host.
#
# Usage:
#   python3 make_iso.py <iso> <esp_fat_image> <boot_dir> [volume_id]
#
#   esp_fat_image : the FAT32 ESP image used as the El Torito EFI boot image.
#   boot_dir     : directory tree to publish in the ISO (e.g. build/esp).

import os
import sys
import struct

ISO_SECTOR = 2048


def le16(v):
    return struct.pack("<H", v & 0xFFFF)


def le32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def be16(v):
    return struct.pack(">H", v & 0xFFFF)


def be32(v):
    return struct.pack(">I", v & 0xFFFFFFFF)


def dir_record(extent_lba, data_len, flags, name):
    """ISO 9660 directory record (name is bytes, no trailing version)."""
    name_len = len(name)
    rec_len = 33 + name_len
    if rec_len % 2:
        rec_len += 1
    rec = bytearray(rec_len)
    rec[0] = rec_len
    rec[1] = 0  # extended attribute record length
    struct.pack_into("<I", rec, 2, extent_lba)   # extent location LE
    struct.pack_into(">I", rec, 6, extent_lba)   # extent location BE
    struct.pack_into("<I", rec, 10, data_len)    # data length LE
    struct.pack_into(">I", rec, 14, data_len)    # data length BE
    rec[18:25] = b"\x00" * 7  # recording date (1970-01-01)
    rec[25] = flags  # 0x02 = directory
    rec[26] = 0  # file unit size
    rec[27] = 0  # interleave gap size
    struct.pack_into("<H", rec, 28, 1)  # volume sequence number LE
    struct.pack_into(">H", rec, 30, 1)  # volume sequence number BE
    rec[32] = name_len
    rec[33:33 + name_len] = name
    return bytes(rec)


def primary_volume_descriptor(vol_id, root_extent, root_size, vol_space_sectors):
    pvd = bytearray(ISO_SECTOR)
    pvd[0] = 1  # type: Primary Volume Descriptor
    pvd[1:6] = b"CD001"
    pvd[6] = 1  # version
    vid = vol_id.encode("ascii")[:32]
    pvd[40:40 + len(vid)] = vid  # volume identifier
    struct.pack_into("<I", pvd, 80, vol_space_sectors)  # volume space size LE
    struct.pack_into(">I", pvd, 84, vol_space_sectors)  # volume space size BE
    pvd[119] = 0  # escape sequences (none)
    root_rec = dir_record(root_extent, root_size, 0x02, b"\x00")
    pvd[156:156 + len(root_rec)] = root_rec  # root directory record
    return bytes(pvd)


def eltorito_boot_record(catalog_lba):
    """El Torito Boot Record Volume Descriptor (points to the boot catalog)."""
    brd = bytearray(ISO_SECTOR)
    brd[0] = 0  # type: Boot Record
    brd[1:6] = b"CD001"
    brd[6] = 1  # version
    brd[7:7 + len(b"EL TORITO SPECIFICATION")] = b"EL TORITO SPECIFICATION"
    struct.pack_into("<I", brd, 120, catalog_lba)  # boot catalog pointer LE
    struct.pack_into(">I", brd, 124, catalog_lba)  # boot catalog pointer BE
    return bytes(brd)


def eltorito_catalog(boot_image_lba, boot_image_sectors):
    """El Torito Boot Catalog (512-byte logical block).

    Validation entry + one EFI section entry (media type 0xEF) referencing the
    ESP FAT image as the El Torito boot image.
    """
    cat = bytearray(512)
    # Validation entry
    cat[0] = 0x01  # header ID
    cat[1] = 0x00  # platform ID (x86)
    idstr = b"EL TORITO SPEC"
    cat[4:4 + len(idstr)] = idstr
    # Section entry (Default Entry), EFI boot image
    ent = bytearray(32)
    ent[0] = 0x88  # bootable indicator
    ent[1] = 0xEF  # media type: EFI
    struct.pack_into("<I", ent, 8, boot_image_lba)       # LBA of boot image
    struct.pack_into("<I", ent, 12, boot_image_sectors)  # size (CD sectors)
    cat[32:32 + len(ent)] = ent
    # Checksum: make the sum of all bytes except the checksum field [28:30]
    # == 0 mod 65536. Computed AFTER all content is written so the section
    # entry bytes are included.
    s = sum(cat[:28]) + sum(cat[30:])
    struct.pack_into("<H", cat, 28, (-s) & 0xFFFF)
    return bytes(cat)


def build_iso(iso_path, esp_fat_path, boot_dir, volume_id="SENTINELOS"):
    with open(esp_fat_path, "rb") as f:
        esp_data = f.read()
    esp_sectors = (len(esp_data) + ISO_SECTOR - 1) // ISO_SECTOR

    boot_dir = os.path.abspath(boot_dir)
    tree_files = {}  # relpath (posix) -> data bytes
    for dirpath, dirnames, filenames in os.walk(boot_dir):
        for fn in filenames:
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, boot_dir).replace(os.sep, "/")
            with open(full, "rb") as f:
                tree_files[rel] = f.read()

    # Directory set (posix relpaths, "" = root).
    dirs = {""}
    for rel in tree_files:
        p = rel.rsplit("/", 1)[0] if "/" in rel else ""
        while p:
            dirs.add(p)
            p = p.rsplit("/", 1)[0] if "/" in p else ""
    dirs = sorted(dirs, key=lambda d: (d.count("/"), d))

    # ---- Pass 1: assign LBAs ----
    # LBA 0-15: system area; 16: PVD; 17: El Torito boot record; 18: catalog.
    next_lba = 19
    dir_lba = {}
    file_lba = {}
    file_size = {}
    dir_lba[""] = next_lba
    next_lba += 1
    for d in dirs:
        if d == "":
            continue
        dir_lba[d] = next_lba
        next_lba += 1
    for rel in sorted(tree_files):
        file_lba[rel] = next_lba
        file_size[rel] = len(tree_files[rel])
        next_lba += (len(tree_files[rel]) + ISO_SECTOR - 1) // ISO_SECTOR
    boot_image_lba = next_lba
    total_sectors = boot_image_lba + esp_sectors

    # ---- Pass 2: build directory records ----
    dir_data = {}
    dir_size = {}
    for d in [""] + list(dirs):
        if d == "":
            continue
        children = []
        for d2 in dirs:
            if d2 == d:
                continue
            parent = d2.rsplit("/", 1)[0] if "/" in d2 else ""
            if parent == d:
                children.append((d2.rsplit("/", 1)[-1], True, d2))
        for rel in sorted(tree_files):
            parent = rel.rsplit("/", 1)[0] if "/" in rel else ""
            if parent == d:
                children.append((rel.rsplit("/", 1)[-1], False, rel))
        entries = bytearray()
        entries += dir_record(dir_lba[d], dir_size.get(d, ISO_SECTOR), 0x02, b"\x00")
        parent_d = d.rsplit("/", 1)[0] if "/" in d else ""
        entries += dir_record(dir_lba.get(parent_d, dir_lba[""]), ISO_SECTOR, 0x02, b"\x00")
        for name, is_dir, rel in children:
            flags = 0x02 if is_dir else 0x00
            if is_dir:
                entries += dir_record(dir_lba[rel], dir_size.get(rel, ISO_SECTOR), flags,
                                      name.encode("ascii").upper())
            else:
                entries += dir_record(file_lba[rel], file_size[rel], flags,
                                      name.encode("ascii").upper())
        dir_data[d] = bytes(entries)
        dir_size[d] = len(dir_data[d])

    # Root directory (".") — dir_size[""] is referenced by its own "." and ".."
    # entries, so set a placeholder first; the record length does not depend on
    # data_len, so we can fix the actual size after building.
    dir_size[""] = ISO_SECTOR  # placeholder
    root_entries = bytearray()
    root_entries += dir_record(dir_lba[""], dir_size[""], 0x02, b"\x00")
    root_entries += dir_record(dir_lba[""], dir_size[""], 0x02, b"\x00")  # ".." == root
    for d in dirs:
        if d == "":
            continue
        root_entries += dir_record(dir_lba[d], dir_size[d], 0x02,
                                   d.split("/")[-1].encode("ascii").upper())
    for rel in sorted(tree_files):
        parent = rel.rsplit("/", 1)[0] if "/" in rel else ""
        if parent == "":
            root_entries += dir_record(file_lba[rel], file_size[rel], 0x00,
                                       rel.rsplit("/", 1)[-1].encode("ascii").upper())
    dir_data[""] = bytes(root_entries)
    dir_size[""] = len(dir_data[""])  # actual size

    # ---- Assemble image ----
    iso = bytearray(total_sectors * ISO_SECTOR)

    # LBA 16: Primary Volume Descriptor
    iso[16 * ISO_SECTOR:17 * ISO_SECTOR] = primary_volume_descriptor(
        volume_id, dir_lba[""], dir_size[""], total_sectors)
    # LBA 17: El Torito Boot Record Volume Descriptor (catalog at LBA 18)
    iso[17 * ISO_SECTOR:18 * ISO_SECTOR] = eltorito_boot_record(catalog_lba=18)
    # LBA 18: Boot Catalog
    cat = eltorito_catalog(boot_image_lba=boot_image_lba, boot_image_sectors=esp_sectors)
    iso[18 * ISO_SECTOR:18 * ISO_SECTOR + len(cat)] = cat

    # Directories
    for d in [""] + list(dirs):
        off = dir_lba[d] * ISO_SECTOR
        data = dir_data[d]
        iso[off:off + len(data)] = data
    # Files
    for rel in sorted(tree_files):
        off = file_lba[rel] * ISO_SECTOR
        iso[off:off + len(tree_files[rel])] = tree_files[rel]
    # El Torito boot image (ESP FAT)
    iso[boot_image_lba * ISO_SECTOR:(boot_image_lba + esp_sectors) * ISO_SECTOR] = esp_data

    tmp = iso_path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(bytes(iso[:total_sectors * ISO_SECTOR]))
    os.replace(tmp, iso_path)
    print("built %s (%d sectors = %d MiB)"
          % (iso_path, total_sectors, total_sectors * ISO_SECTOR // (1024 * 1024)))
    print("  El Torito EFI boot image at LBA %d (%d sectors)" % (boot_image_lba, esp_sectors))
    print("  ISO tree files: %d" % len(tree_files))
    return 0


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    iso_path = sys.argv[1]
    esp_fat_path = sys.argv[2]
    boot_dir = sys.argv[3]
    volume_id = sys.argv[4] if len(sys.argv) > 4 else "SENTINELOS"
    return build_iso(iso_path, esp_fat_path, boot_dir, volume_id)


if __name__ == "__main__":
    sys.exit(main())
