#!/usr/bin/env python3
"""Regression tests for the Sentinel Package File builder/extractor."""
from __future__ import annotations

import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import tarfile
import unittest
import zlib

MODULE_PATH = Path(__file__).resolve().parents[1] / "make_spf.py"
SPEC = importlib.util.spec_from_file_location("make_spf", MODULE_PATH)
spf = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
SPEC.loader.exec_module(spf)


class SPFTests(unittest.TestCase):
    def test_build_verify_extract_round_trip(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td)
            source = base / "partition"
            source.mkdir()
            (source / "System32").mkdir()
            (source / "System32" / "krnl.exe").write_bytes(b"sentinel-kernel\x00\x01")
            (source / "boot.cfg").write_text("mode=uefi\n", encoding="utf-8")
            package = base / "system.spf"
            spf.build(source, package, "system")
            spf.verify(package)
            target = base / "mounted-system"
            spf.extract(package, target)
            self.assertEqual((target / "System32" / "krnl.exe").read_bytes(), b"sentinel-kernel\x00\x01")
            self.assertEqual((target / "boot.cfg").read_text(encoding="utf-8"), "mode=uefi\n")

    def test_existing_file_requires_overwrite(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td)
            source = base / "source"
            source.mkdir()
            (source / "config.ini").write_text("new", encoding="utf-8")
            package = base / "system.spf"
            spf.build(source, package, "system")
            target = base / "target"
            target.mkdir()
            (target / "config.ini").write_text("old", encoding="utf-8")
            with self.assertRaises(FileExistsError):
                spf.extract(package, target)
            spf.extract(package, target, overwrite=True)
            self.assertEqual((target / "config.ini").read_text(encoding="utf-8"), "new")

    def test_rejects_corrupt_package(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td)
            source = base / "source"
            source.mkdir()
            (source / "file").write_bytes(b"content")
            package = base / "data.spf"
            spf.build(source, package, "system")
            data = bytearray(package.read_bytes())
            data[-1] ^= 0x40
            package.write_bytes(data)
            with self.assertRaises((ValueError, zlib.error)):
                spf.verify(package)

    def test_rejects_inconsistent_manifest_file_count(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td)
            source = base / "source"
            source.mkdir()
            (source / "file").write_bytes(b"content")
            package = base / "data.spf"
            spf.build(source, package, "system")
            data = bytearray(package.read_bytes())
            _, meta_len, packed_len = spf.HEADER.unpack(data[:spf.HEADER.size])
            start = spf.HEADER.size
            manifest = json.loads(data[start:start + meta_len])
            manifest["file_count"] += 1
            meta = json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()
            packed = bytes(data[start + meta_len:start + meta_len + packed_len])
            package.write_bytes(spf.HEADER.pack(spf.MAGIC, len(meta), len(packed)) + meta + packed)
            with self.assertRaisesRegex(ValueError, "file_count mismatch"):
                spf.verify(package)

    def test_rejects_traversal_path_before_writing(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td)
            raw_buffer = io.BytesIO()
            with tarfile.open(fileobj=raw_buffer, mode="w") as tf:
                payload = b"outside"
                info = tarfile.TarInfo("../escape.txt")
                info.size = len(payload)
                tf.addfile(info, io.BytesIO(payload))
            raw = raw_buffer.getvalue()
            manifest = {
                "format": "Sentinel Package File", "version": 1, "kind": "system",
                "compression": "zlib", "archive": "tar", "file_count": 1,
                "uncompressed_bytes": len(raw),
                "payload_sha256": hashlib.sha256(raw).hexdigest(),
                "files": [{"path": "../escape.txt", "size": len(payload),
                           "sha256": hashlib.sha256(payload).hexdigest()}],
            }
            meta = json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()
            packed = zlib.compress(raw)
            package = base / "malicious.spf"
            package.write_bytes(spf.HEADER.pack(spf.MAGIC, len(meta), len(packed)) + meta + packed)
            target = base / "target"
            target.mkdir()
            with self.assertRaises(ValueError):
                spf.extract(package, target)
            self.assertFalse((base / "escape.txt").exists())

    def test_rejects_decompression_bomb_above_declared_size(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td)
            raw = b"A" * (1024 * 1024)
            manifest = {
                "format": "Sentinel Package File", "version": 1, "kind": "system",
                "compression": "zlib", "archive": "tar", "file_count": 0,
                "uncompressed_bytes": 8,
                "payload_sha256": hashlib.sha256(raw).hexdigest(),
                "files": [],
            }
            meta = json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()
            packed = zlib.compress(raw)
            package = base / "oversized.spf"
            package.write_bytes(spf.HEADER.pack(spf.MAGIC, len(meta), len(packed)) + meta + packed)
            with self.assertRaisesRegex(ValueError, "exceeds declared size"):
                spf.read_package(package)

    def test_rejects_package_with_trailing_bytes(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td)
            source = base / "source"
            source.mkdir()
            (source / "file").write_bytes(b"content")
            package = base / "data.spf"
            spf.build(source, package, "system")
            with package.open("ab") as f:
                f.write(b"trailing")
            with self.assertRaisesRegex(ValueError, "invalid SPF lengths"):
                spf.read_package(package)


if __name__ == "__main__":
    unittest.main(verbosity=2)
