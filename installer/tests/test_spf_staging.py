"""Regression tests for SPF staging and package-integrity validation."""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

MODULE_PATH = Path(__file__).resolve().parents[1] / "make_spf.py"
SPEC = importlib.util.spec_from_file_location("make_spf", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
make_spf = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(make_spf)


class SpfStagingAndIntegrityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.source.mkdir()
        (self.source / "EFI" / "BOOT").mkdir(parents=True)
        (self.source / "EFI" / "BOOT" / "BOOTX64.EFI").write_bytes(b"uefi-loader-test")
        (self.source / "magic" / "System32").mkdir(parents=True)
        (self.source / "magic" / "System32" / "hal.dll").write_bytes(b"hal-test-payload")
        (self.source / "empty-dir").mkdir()
        self.package = self.root / "esp.spf"

    def tearDown(self):
        self.temp.cleanup()

    def build_package(self):
        make_spf.build(self.source, self.package, "esp")

    def test_stages_file_tree_and_manifest_hashes(self):
        self.build_package()
        manifest, raw_tar = make_spf.read_package(self.package)
        self.assertEqual(manifest["kind"], "esp")
        self.assertEqual(manifest["file_count"], 2)
        self.assertEqual(
            {entry["path"] for entry in manifest["files"]},
            {"EFI/BOOT/BOOTX64.EFI", "magic/System32/hal.dll"},
        )
        self.assertEqual(len(raw_tar), manifest["uncompressed_bytes"])
        make_spf.verify(self.package)

    def test_extract_restores_staged_files_byte_for_byte(self):
        self.build_package()
        target = self.root / "mounted-esp"
        make_spf.extract(self.package, target)
        self.assertEqual(
            (target / "EFI" / "BOOT" / "BOOTX64.EFI").read_bytes(),
            b"uefi-loader-test",
        )
        self.assertEqual(
            (target / "magic" / "System32" / "hal.dll").read_bytes(),
            b"hal-test-payload",
        )

    def test_corrupt_compressed_payload_is_rejected(self):
        self.build_package()
        data = self.package.read_bytes()
        magic, manifest_len, packed_len = make_spf.HEADER.unpack_from(data)
        manifest_start = make_spf.HEADER.size
        manifest_end = manifest_start + manifest_len
        manifest = json.loads(data[manifest_start:manifest_end].decode("utf-8"))
        manifest["payload_sha256"] = "0" * 64
        changed_manifest = json.dumps(
            manifest, sort_keys=True, separators=(",", ":")
        ).encode("utf-8")
        self.assertEqual(len(changed_manifest), manifest_len)
        self.package.write_bytes(
            make_spf.HEADER.pack(magic, manifest_len, packed_len)
            + changed_manifest
            + data[manifest_end:]
        )
        with self.assertRaisesRegex(ValueError, "payload checksum mismatch"):
            make_spf.read_package(self.package)

    def test_invalid_package_header_is_rejected(self):
        self.build_package()
        data = bytearray(self.package.read_bytes())
        data[0] ^= 0x01
        self.package.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "not a Sentinel SPF"):
            make_spf.read_package(self.package)

    def test_manifest_path_traversal_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "unsafe package path"):
            make_spf.safe_rel("../escape.txt")


if __name__ == "__main__":
    unittest.main()
