#!/usr/bin/env python3
"""Safely stage verified SentinelOS SPF packages onto already-mounted filesystems.

This host-side tool does not discover disks, mount filesystems, partition, format,
or write raw devices. It defaults to a dry run. Applying requires an explicit
confirmation phrase and two existing, distinct mount directories.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

import make_spf


CONFIRMATION = "INSTALL SENTINELOS"


def checked_mount(path: Path, label: str) -> Path:
    if path.is_symlink():
        raise ValueError(f"{label} target must not be a symlink: {path}")
    target = path.resolve(strict=True)
    if not target.is_dir():
        raise ValueError(f"{label} target is not an existing directory: {target}")
    return target


def preflight(esp_package: Path, system_package: Path,
              esp_target: Path, system_target: Path,
              overwrite: bool) -> tuple[Path, Path]:
    esp_meta, _ = make_spf.read_package(esp_package)
    system_meta, _ = make_spf.read_package(system_package)
    if esp_meta.get("kind") != "esp":
        raise ValueError(f"ESP package has kind {esp_meta.get('kind')!r}, expected 'esp'")
    if system_meta.get("kind") != "system":
        raise ValueError(f"system package has kind {system_meta.get('kind')!r}, expected 'system'")

    esp = checked_mount(esp_target, "ESP")
    system = checked_mount(system_target, "system")
    if esp == system or esp in system.parents or system in esp.parents:
        raise ValueError("ESP and system targets must be distinct, non-overlapping directories")

    # Verify full package contents and hashes before any destination is changed.
    make_spf.verify(esp_package)
    make_spf.verify(system_package)

    for target, meta, label in ((esp, esp_meta, "ESP"), (system, system_meta, "system")):
        for row in meta["files"]:
            dest = target.joinpath(*make_spf.PurePosixPath(row["path"]).parts)
            parent = dest.parent
            while parent != target:
                if parent.is_symlink() or (parent.exists() and not parent.is_dir()):
                    raise ValueError(f"{label} has an unsafe destination parent: {parent}")
                parent = parent.parent
            if dest.is_symlink():
                raise ValueError(f"{label} destination is a symlink: {dest}")
            if dest.exists() and not overwrite:
                raise FileExistsError(f"{label} destination exists (pass --overwrite): {dest}")
            if dest.exists() and not dest.is_file():
                raise ValueError(f"{label} destination is not a regular file: {dest}")
    return esp, system


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--esp-package", type=Path, required=True)
    parser.add_argument("--system-package", type=Path, required=True)
    parser.add_argument("--esp-target", type=Path, required=True,
                        help="existing mounted ESP directory; not a block device")
    parser.add_argument("--system-target", type=Path, required=True,
                        help="existing mounted system directory; not a block device")
    parser.add_argument("--overwrite", action="store_true",
                        help="allow replacing existing regular files")
    parser.add_argument("--apply", action="store_true",
                        help="actually extract packages; otherwise perform a dry run")
    parser.add_argument("--confirm", default="",
                        help=f"required with --apply; type exactly: {CONFIRMATION}")
    args = parser.parse_args()

    try:
        esp, system = preflight(args.esp_package, args.system_package,
                                args.esp_target, args.system_target, args.overwrite)
        print(f"Preflight OK: ESP package -> {esp}")
        print(f"Preflight OK: system package -> {system}")
        if not args.apply:
            print("Dry run only. No destination files were changed. Pass --apply to install.")
            return 0
        if args.confirm != CONFIRMATION:
            raise ValueError(f"--apply requires --confirm '{CONFIRMATION}'")
        # Both packages and destinations were preflighted. Extraction remains two
        # separate filesystem operations, so power loss or I/O failure can leave a
        # partially installed system; this is not a transactional disk installer.
        make_spf.extract(args.esp_package, esp, args.overwrite)
        make_spf.extract(args.system_package, system, args.overwrite)
        print("Package extraction completed. Bootloader/partition setup is not performed.")
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f"installer: error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
