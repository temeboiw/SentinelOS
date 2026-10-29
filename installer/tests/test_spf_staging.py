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


    def test_per_file_digest_mismatch_is_rejected(self):
        self.build_package()
        data = self.package.read_bytes()
        magic, manifest_len, packed_len = make_spf.HEADER.unpack_from(data)
        manifest_start = make_spf.HEADER.size
        manifest_end = manifest_start + manifest_len
        manifest = json.loads(data[manifest_start:manifest_end].decode("utf-8"))
        manifest["files"][0]["sha256"] = "0" * 64
        changed_manifest = json.dumps(
            manifest, sort_keys=True, separators=(",", ":")
        ).encode("utf-8")
        self.assertEqual(len(changed_manifest), manifest_len)
        self.package.write_bytes(
            make_spf.HEADER.pack(magic, manifest_len, packed_len)
            + changed_manifest
            + data[manifest_end:]
        )
        with self.assertRaisesRegex(ValueError, "file checksum mismatch"):
            make_spf.verify(self.package)

    def test_extract_rejects_manifest_file_hash_before_writing(self):
        self.build_package()
        data = self.package.read_bytes()
        magic, manifest_len, packed_len = make_spf.HEADER.unpack_from(data)
        manifest_start = make_spf.HEADER.size
        manifest_end = manifest_start + manifest_len
        manifest = json.loads(data[manifest_start:manifest_end].decode("utf-8"))
        manifest["files"][0]["sha256"] = "0" * 64
        changed_manifest = json.dumps(
            manifest, sort_keys=True, separators=(",", ":")
        ).encode("utf-8")
        self.assertEqual(len(changed_manifest), manifest_len)
        self.package.write_bytes(
            make_spf.HEADER.pack(magic, manifest_len, packed_len)
            + changed_manifest
            + data[manifest_end:]
        )
        target = self.root / "must-stay-absent"
        with self.assertRaisesRegex(ValueError, "file checksum mismatch"):
            make_spf.extract(self.package, target)
        self.assertFalse(target.exists(), "failed verification must not create destination")

    def test_source_symlinks_are_rejected(self):
        link = self.source / "linked-loader"
        try:
            link.symlink_to(self.source / "magic" / "System32" / "hal.dll")
        except (OSError, NotImplementedError) as exc:
            self.skipTest(f"symlink creation is not supported: {exc}")
        with self.assertRaisesRegex(ValueError, "symbolic links are not allowed"):
            make_spf.build(self.source, self.package, "esp")

    def test_boot_layout_check_accepts_complete_esp_and_system_packages(self):
        (self.source / "magic" / "System32" / "krnl.exe").write_bytes(b"kernel-test-payload")
        self.build_package()
        make_spf.verify(self.package, expected_kind="esp", require_boot_layout=True)

        system = self.root / "system"
        (system / "magic" / "System32").mkdir(parents=True)
        for name, data in (("hal.dll", b"hal"), ("krnl.exe", b"kernel"), ("smss.exe", b"session")):
            (system / "magic" / "System32" / name).write_bytes(data)
        system_package = self.root / "system.spf"
        make_spf.build(system, system_package, "system")
        make_spf.verify(system_package, expected_kind="system", require_boot_layout=True)

    def test_boot_layout_check_rejects_missing_kernel(self):
        self.build_package()
        with self.assertRaisesRegex(ValueError, "missing boot-critical path"):
            make_spf.verify(self.package, expected_kind="esp", require_boot_layout=True)

    def test_system_package_rejects_installer_only_executables(self):
        system = self.root / "system"
        sys32 = system / "magic" / "System32"
        sys32.mkdir(parents=True)
        for name, data in (("hal.dll", b"hal"), ("krnl.exe", b"kernel"),
                           ("smss.exe", b"main-session-manager"), ("instlr.exe", b"installer")):
            (sys32 / name).write_bytes(data)
        with self.assertRaisesRegex(ValueError, "installer-only executable"):
            make_spf.build(system, self.root / "system.spf", "system")

    def test_expected_package_kind_is_enforced(self):
        self.build_package()
        make_spf.verify(self.package, expected_kind="esp")
        with self.assertRaisesRegex(ValueError, "SPF kind mismatch"):
            make_spf.verify(self.package, expected_kind="system")

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
