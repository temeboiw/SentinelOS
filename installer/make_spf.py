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


def read_package(path: Path):
    with path.open("rb") as f:
        head = f.read(HEADER.size)
        if len(head) != HEADER.size:
            raise ValueError("truncated SPF header")
        magic, meta_len, packed_len = HEADER.unpack(head)
        if magic != MAGIC:
            raise ValueError("not a Sentinel SPF v1 package")
        if meta_len > 64 * 1024 * 1024 or packed_len > path.stat().st_size:
            raise ValueError("invalid SPF lengths")
        meta_raw = f.read(meta_len)
        packed = f.read(packed_len)
        if len(meta_raw) != meta_len or len(packed) != packed_len or f.read(1):
            raise ValueError("truncated package or unexpected trailing bytes")
    meta = json.loads(meta_raw.decode("utf-8"))
    raw = zlib.decompress(packed)
    if hashlib.sha256(raw).hexdigest() != meta.get("payload_sha256"):
        raise ValueError("payload checksum mismatch")
    if len(raw) != meta.get("uncompressed_bytes"):
        raise ValueError("payload size mismatch")
    return meta, raw


def verify(path: Path) -> None:
    meta, raw = read_package(path)
    expected = {safe_rel(x["path"]): x for x in meta.get("files", [])}
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


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="command", required=True)
    b = sub.add_parser("build", help="create esp.spf and system.spf")
    b.add_argument("--esp-root", type=Path, required=True, help="directory tree for the FAT32 ESP")
    b.add_argument("--system-root", type=Path, required=True, help="directory tree for the ext4 system partition")
    b.add_argument("--output-dir", type=Path, default=Path("installer"))
    v = sub.add_parser("verify", help="verify package header, payload and per-file hashes")
    v.add_argument("package", type=Path)
    i = sub.add_parser("info", help="print package manifest")
    i.add_argument("package", type=Path)
    args = ap.parse_args()
    try:
        if args.command == "build":
            build(args.esp_root, args.output_dir / "esp.spf", "esp")
            build(args.system_root, args.output_dir / "system.spf", "system")
        elif args.command == "verify":
            verify(args.package)
        else:
            meta, _ = read_package(args.package)
            print(json.dumps(meta, indent=2))
    except (OSError, ValueError, tarfile.TarError, zlib.error, json.JSONDecodeError) as exc:
        print(f"spf: error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
