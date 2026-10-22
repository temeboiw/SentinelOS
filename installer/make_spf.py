#!/usr/bin/env python3
"""Build and inspect Sentinel Package Files (.spf) for SentinelOS.

SPF v1 layout:
  8 bytes  magic: SENTSPF1
  8 bytes  big-endian JSON manifest length
  8 bytes  big-endian compressed payload length
  N bytes  UTF-8 JSON manifest
  rest     zlib-compressed deterministic tar archive

This is a package container, not a disk image. The installer must separately
partition/format the target (ESP=FAT32, system=ext4) and safely extract each
package onto its matching filesystem.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import struct
import sys
import tarfile
import zlib

MAGIC = b"SENTSPF1"
HEADER = struct.Struct(">8sQQ")
CHUNK = 1024 * 1024
MAX_MANIFEST_BYTES = 16 * 1024 * 1024
MAX_COMPRESSED_BYTES = 1024 * 1024 * 1024
MAX_UNCOMPRESSED_BYTES = 2 * 1024 * 1024 * 1024


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(CHUNK), b""):
            h.update(block)
    return h.hexdigest()


def safe_rel(path: str) -> str:
    p = PurePosixPath(path)
    if p.is_absolute() or not p.parts or any(x in ("", ".", "..") for x in p.parts):
        raise ValueError(f"unsafe package path: {path!r}")
    return p.as_posix()


def collect(root: Path):
    root = root.resolve(strict=True)
    if not root.is_dir():
        raise ValueError(f"not a directory: {root}")
    rows = []
    for p in sorted(root.rglob("*"), key=lambda x: x.relative_to(root).as_posix()):
        rel = safe_rel(p.relative_to(root).as_posix())
        if p.is_symlink():
            raise ValueError(f"symbolic links are not allowed in packages: {rel}")
        if p.is_file():
            rows.append({"path": rel, "size": p.stat().st_size, "sha256": sha256_file(p)})
        elif not p.is_dir():
            raise ValueError(f"unsupported filesystem object: {rel}")
    return root, rows


def build(root: Path, output: Path, kind: str) -> None:
    root, files = collect(root)
    output.parent.mkdir(parents=True, exist_ok=True)
    tarbuf = io.BytesIO()
    with tarfile.open(fileobj=tarbuf, mode="w", format=tarfile.PAX_FORMAT) as tf:
        for item in files:
            p = root.joinpath(*PurePosixPath(item["path"]).parts)
            info = tf.gettarinfo(str(p), arcname=item["path"])
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            info.mtime = 0
            with p.open("rb") as src:
                tf.addfile(info, src)
    raw = tarbuf.getvalue()
    packed = zlib.compress(raw, level=9)
    manifest = {
        "format": "Sentinel Package File",
        "version": 1,
        "kind": kind,
        "compression": "zlib",
        "archive": "tar",
        "file_count": len(files),
        "uncompressed_bytes": len(raw),
        "payload_sha256": hashlib.sha256(raw).hexdigest(),
        "files": files,
    }
    meta = json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode("utf-8")
    tmp = output.with_suffix(output.suffix + ".tmp")
    with tmp.open("wb") as f:
        f.write(HEADER.pack(MAGIC, len(meta), len(packed)))
        f.write(meta)
        f.write(packed)
    os.replace(tmp, output)
    print(f"{output}: {len(files)} files, {len(raw)} -> {len(packed)} bytes")


def validate_manifest(meta) -> None:
    """Reject malformed or internally inconsistent SPF metadata before use."""
    if not isinstance(meta, dict):
        raise ValueError("SPF manifest must be a JSON object")
    if meta.get("format") != "Sentinel Package File" or meta.get("version") != 1:
        raise ValueError("unsupported SPF manifest format/version")
    if meta.get("kind") not in ("esp", "system"):
        raise ValueError("SPF kind must be 'esp' or 'system'")
    if meta.get("compression") != "zlib" or meta.get("archive") != "tar":
        raise ValueError("unsupported SPF compression or archive")
    files = meta.get("files")
    if not isinstance(files, list):
        raise ValueError("SPF manifest files must be a list")
    if type(meta.get("file_count")) is not int or meta["file_count"] != len(files):
        raise ValueError("SPF manifest file_count mismatch")
    if type(meta.get("uncompressed_bytes")) is not int or meta["uncompressed_bytes"] < 0:
        raise ValueError("invalid SPF uncompressed_bytes")
    if meta["uncompressed_bytes"] > MAX_UNCOMPRESSED_BYTES:
        raise ValueError("SPF uncompressed payload exceeds the configured safety limit")
    digest = meta.get("payload_sha256")
    if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
        raise ValueError("invalid SPF payload_sha256")
    seen = set()
    for row in files:
        if not isinstance(row, dict):
            raise ValueError("SPF file entry must be an object")
        name = safe_rel(row.get("path", ""))
        if name in seen:
            raise ValueError(f"duplicate manifest path: {name}")
        seen.add(name)
        if type(row.get("size")) is not int or row["size"] < 0:
            raise ValueError(f"invalid file size: {name}")
        digest = row.get("sha256")
        if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
            raise ValueError(f"invalid file SHA-256: {name}")


def read_package(path: Path):
    with path.open("rb") as f:
        head = f.read(HEADER.size)
        if len(head) != HEADER.size:
            raise ValueError("truncated SPF header")
        magic, meta_len, packed_len = HEADER.unpack(head)
        if magic != MAGIC:
            raise ValueError("not a Sentinel SPF v1 package")
        file_size = path.stat().st_size
        if (meta_len > MAX_MANIFEST_BYTES or packed_len > MAX_COMPRESSED_BYTES
                or meta_len + packed_len != file_size - HEADER.size):
            raise ValueError("invalid SPF lengths")
        meta_raw = f.read(meta_len)
        packed = f.read(packed_len)
        if len(meta_raw) != meta_len or len(packed) != packed_len:
            raise ValueError("truncated SPF package")
    meta = json.loads(meta_raw.decode("utf-8"))
    validate_manifest(meta)
    # Bound output to the manifest's declared size plus one byte. This prevents
    # a tiny compressed package from expanding without limit in memory.
    declared_size = meta["uncompressed_bytes"]
    decompressor = zlib.decompressobj()
    raw = decompressor.decompress(packed, declared_size + 1)
    if len(raw) > declared_size or decompressor.unconsumed_tail:
        raise ValueError("SPF decompressed payload exceeds declared size")
    if not decompressor.eof:
        raise ValueError("truncated or oversized SPF compressed payload")
    if decompressor.unused_data:
        raise ValueError("unexpected data after SPF compressed stream")
    if hashlib.sha256(raw).hexdigest() != meta.get("payload_sha256"):
        raise ValueError("payload checksum mismatch")
    if len(raw) != meta["uncompressed_bytes"]:
        raise ValueError("payload size mismatch")
    return meta, raw


def verify(path: Path, expected_kind: str | None = None, require_boot_layout: bool = False) -> None:
    meta, raw = read_package(path)
    if expected_kind is not None and meta.get("kind") != expected_kind:
        raise ValueError(f"SPF kind mismatch: expected {expected_kind!r}, got {meta.get('kind')!r}")
    expected = {safe_rel(x["path"]): x for x in meta.get("files", [])}
    if require_boot_layout:
        required_by_kind = {
            "esp": ("EFI/BOOT/BOOTX64.EFI", "magic/System32/hal.dll", "magic/System32/krnl.exe"),
            "system": ("magic/System32/hal.dll", "magic/System32/krnl.exe", "magic/System32/smss.exe"),
        }
        kind = meta.get("kind")
        if kind not in required_by_kind:
            raise ValueError("cannot validate boot layout for unknown package kind")
        missing = [name for name in required_by_kind[kind] if name not in expected]
        if missing:
            raise ValueError(f"{kind} package is missing boot-critical path(s): {chr(44).join(missing)}")
    seen = set()
    with tarfile.open(fileobj=io.BytesIO(raw), mode="r:") as tf:
        for member in tf:
            name = safe_rel(member.name)
            if name in seen:
                raise ValueError(f"duplicate archive path: {name}")
            seen.add(name)
            if not (member.isfile() or member.isdir()):
                raise ValueError(f"unsupported archive member: {name}")
            if member.isfile():
                if name not in expected:
                    raise ValueError(f"unlisted archive file: {name}")
                data = tf.extractfile(member).read()
                row = expected[name]
                if len(data) != row["size"] or hashlib.sha256(data).hexdigest() != row["sha256"]:
                    raise ValueError(f"file checksum mismatch: {name}")
    if seen.intersection(expected) != set(expected):
        raise ValueError("manifest/archive file list mismatch")
    print(f"OK: {path} ({meta['kind']}, {len(expected)} files, {meta['uncompressed_bytes']} bytes raw)")


def extract(path: Path, target: Path, overwrite: bool = False) -> None:
    """Extract a fully verified SPF to an existing or new directory tree."""
    # Verify every manifest entry and archive member before creating directories
    # or changing any destination file. Payload SHA-256 alone does not prove the
    # archive's file list agrees with the per-file manifest.
    verify(path)
    meta, raw = read_package(path)
    if meta.get("format") != "Sentinel Package File" or meta.get("version") != 1:
        raise ValueError("unsupported SPF manifest format/version")
    target.mkdir(parents=True, exist_ok=True)
    target = target.resolve(strict=True)
    if not target.is_dir():
        raise ValueError(f"target is not a directory: {target}")
    expected = {}
    for row in meta.get("files", []):
        name = safe_rel(row["path"])
        if name in expected:
            raise ValueError(f"duplicate manifest path: {name}")
        expected[name] = row
    with tarfile.open(fileobj=io.BytesIO(raw), mode="r:") as tf:
        members = {}
        for member in tf:
            name = safe_rel(member.name)
            if name in members:
                raise ValueError(f"duplicate archive path: {name}")
            if not (member.isfile() or member.isdir()):
                raise ValueError(f"unsupported archive member: {name}")
            members[name] = member
        if {n for n, m in members.items() if m.isfile()} != set(expected):
            raise ValueError("manifest/archive file list mismatch")
        # Preflight all paths/conflicts before writing any files.
        for name, member in members.items():
            dest = target.joinpath(*PurePosixPath(name).parts)
            parent = dest.parent
            while parent != target:
                if target not in parent.parents:
                    raise ValueError(f"destination escapes target: {name}")
                if parent.is_symlink() or (parent.exists() and not parent.is_dir()):
                    raise ValueError(f"unsafe destination parent: {parent}")
                parent = parent.parent
            if dest.is_symlink():
                raise ValueError(f"refusing symlink destination: {name}")
            if dest.exists():
                if member.isdir() and dest.is_dir():
                    continue
                if not overwrite:
                    raise FileExistsError(f"destination exists (use --overwrite): {dest}")
                if member.isdir() != dest.is_dir():
                    raise ValueError(f"destination type mismatch: {dest}")
        for name, member in members.items():
            dest = target.joinpath(*PurePosixPath(name).parts)
            if member.isdir():
                dest.mkdir(parents=True, exist_ok=True)
                continue
            src = tf.extractfile(member)
            if src is None:
                raise ValueError(f"cannot read archive member: {name}")
            data = src.read()
            row = expected[name]
            if len(data) != row.get("size") or hashlib.sha256(data).hexdigest() != row.get("sha256"):
                raise ValueError(f"file checksum mismatch: {name}")
            dest.parent.mkdir(parents=True, exist_ok=True)
            tmp = dest.with_name(dest.name + ".spf-tmp")
            try:
                with tmp.open("wb") as out:
                    out.write(data)
                    out.flush()
                    os.fsync(out.fileno())
                os.replace(tmp, dest)
            finally:
                if tmp.exists():
                    tmp.unlink()
    print(f"Extracted {path} ({meta.get('kind', 'unknown')}) to {target}: {len(expected)} files")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="command", required=True)
    b = sub.add_parser("build", help="create esp.spf and system.spf")
    b.add_argument("--esp-root", type=Path, required=True, help="directory tree for the FAT32 ESP")
    b.add_argument("--system-root", type=Path, required=True, help="directory tree for the ext4 system partition")
    b.add_argument("--output-dir", type=Path, default=Path("installer"))
    v = sub.add_parser("verify", help="verify package header, payload and per-file hashes")
    v.add_argument("package", type=Path)
    v.add_argument("--kind", choices=("esp", "system"), help="require this package kind")
    v.add_argument("--require-boot-layout", action="store_true", help="require loader and boot-critical files for this package kind")
    e = sub.add_parser("extract", help="extract an SPF to an already-mounted partition directory")
    e.add_argument("package", type=Path)
    e.add_argument("--target-root", type=Path, required=True, help="mounted ESP or system partition directory; raw devices are not supported")
    e.add_argument("--overwrite", action="store_true", help="replace existing files after validation")
    i = sub.add_parser("info", help="print package manifest")
    i.add_argument("package", type=Path)
    args = ap.parse_args()
    try:
        if args.command == "build":
            build(args.esp_root, args.output_dir / "esp.spf", "esp")
            build(args.system_root, args.output_dir / "system.spf", "system")
        elif args.command == "verify":
            verify(args.package, args.kind, args.require_boot_layout)
        elif args.command == "extract":
            extract(args.package, args.target_root, args.overwrite)
        else:
            meta, _ = read_package(args.package)
            print(json.dumps(meta, indent=2))
    except (OSError, ValueError, tarfile.TarError, zlib.error, json.JSONDecodeError) as exc:
        print(f"spf: error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
