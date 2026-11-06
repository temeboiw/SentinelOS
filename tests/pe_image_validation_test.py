#!/usr/bin/env python3
"""Validate that all PE images in build/rootfs/magic/System32/ are valid PE32+ x86-64 executables.

Checks:
- File exists and is non-empty
- Starts with MZ signature (0x5A4D)
- Contains PE\0\0 signature at the e_lfanew offset
- Is a PE32+ (magic 0x20b) x86-64 (machine 0x8664) executable
- Reports file size
"""

import os
import struct
import sys

SYSTEM32 = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                        "build", "rootfs", "magic", "System32")

failures = 0
checked = 0


def fail(msg):
    global failures
    failures += 1
    print("FAIL: " + msg)


def check_pe(path):
    global checked
    checked += 1
    name = os.path.basename(path)
    try:
        size = os.path.getsize(path)
    except OSError as e:
        fail(f"{name}: cannot stat file: {e}")
        return
    if size == 0:
        fail(f"{name}: file is empty (0 bytes)")
        return

    with open(path, "rb") as f:
        header = f.read(64)

    if len(header) < 64:
        fail(f"{name}: file too small ({size} bytes) to contain a DOS header")
        return

    # MZ signature
    if header[0:2] != b"MZ":
        fail(f"{name}: missing MZ signature (got {header[0:2]!r})")
        return

    # e_lfanew at offset 0x3c
    e_lfanew = struct.unpack_from("<I", header, 0x3c)[0]
    if e_lfanew + 24 > size:
        fail(f"{name}: e_lfanew={e_lfanew} points beyond file size {size}")
        return

    with open(path, "rb") as f:
        f.seek(e_lfanew)
        pe_sig = f.read(4)
    if pe_sig != b"PE\x00\x00":
        fail(f"{name}: missing PE\\0\\0 signature at offset {e_lfanew} (got {pe_sig!r})")
        return

    # COFF header follows immediately: machine (2), number_of_sections (2), ...
    with open(path, "rb") as f:
        f.seek(e_lfanew + 4)
        coff = f.read(20)
    machine = struct.unpack_from("<H", coff, 0)[0]
    num_sections = struct.unpack_from("<H", coff, 2)[0]
    size_of_optional_header = struct.unpack_from("<H", coff, 16)[0]
    characteristics = struct.unpack_from("<H", coff, 18)[0]

    if machine != 0x8664:
        fail(f"{name}: not x86-64 (machine=0x{machine:04x})")
        return

    # Optional header magic: PE32+ = 0x20b
    # Optional header starts right after COFF header (20 bytes)
    with open(path, "rb") as f:
        f.seek(e_lfanew + 24)
        opt = f.read(2)
    if len(opt) < 2:
        fail(f"{name}: optional header truncated")
        return
    opt_magic = struct.unpack_from("<H", opt, 0)[0]
    if opt_magic != 0x20b:
        fail(f"{name}: not PE32+ (optional header magic=0x{opt_magic:04x}, expected 0x20b)")
        return

    print(f"PASS: {name} ({size} bytes, PE32+ x86-64, {num_sections} sections)")


def main():
    if not os.path.isdir(SYSTEM32):
        print(f"FAIL: System32 directory not found: {SYSTEM32}")
        sys.exit(1)

    entries = sorted(os.listdir(SYSTEM32))
    if not entries:
        print("FAIL: System32 directory is empty")
        sys.exit(1)

    pe_candidates = []
    for entry in entries:
        path = os.path.join(SYSTEM32, entry)
        if not os.path.isfile(path):
            continue
        # Only validate actual PE images (.dll and .exe); skip .lib, .ttf, icons, etc.
        if entry.lower().endswith((".dll", ".exe")):
            pe_candidates.append(path)

    if not pe_candidates:
        print("FAIL: No PE image candidates (.dll/.exe) found in System32")
        sys.exit(1)

    for path in pe_candidates:
        check_pe(path)

    print(f"\nChecked {checked} PE images; {failures} failure(s)")
    if failures:
        sys.exit(1)
    print("All System32 PE images are valid PE32+ x86-64 executables: PASS")
    sys.exit(0)


if __name__ == "__main__":
    main()
