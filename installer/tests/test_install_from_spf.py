#!/usr/bin/env python3
"""Regression tests for the guarded host-side SPF installation coordinator."""
from __future__ import annotations

import contextlib
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

INSTALLER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(INSTALLER_DIR))
SPEC = importlib.util.spec_from_file_location(
    "install_from_spf", INSTALLER_DIR / "install_from_spf.py"
)
installer = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
SPEC.loader.exec_module(installer)
spf = installer.make_spf


class InstallCoordinatorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.esp_source = self.base / "esp-source"
        self.system_source = self.base / "system-source"
        self.esp_source.mkdir()
        self.system_source.mkdir()
        (self.esp_source / "EFI").mkdir()
        (self.esp_source / "EFI" / "BOOT").mkdir()
        (self.esp_source / "EFI" / "BOOT" / "BOOTX64.EFI").write_bytes(b"test-efi")
        (self.esp_source / "magic" / "System32").mkdir(parents=True)
        (self.esp_source / "magic" / "System32" / "hal.dll").write_bytes(b"test-hal")
        (self.esp_source / "magic" / "System32" / "krnl.exe").write_bytes(b"test-kernel")
        (self.system_source / "magic" / "System32").mkdir(parents=True)
        (self.system_source / "magic" / "System32" / "hal.dll").write_bytes(b"test-hal")
        (self.system_source / "magic" / "System32" / "krnl.exe").write_bytes(b"test-kernel")
        (self.system_source / "magic" / "System32" / "smss.exe").write_bytes(b"test-smss")
        self.esp_package = self.base / "esp.spf"
        self.system_package = self.base / "system.spf"
        spf.build(self.esp_source, self.esp_package, "esp")
        spf.build(self.system_source, self.system_package, "system")
        self.esp_target = self.base / "mounted-esp"
        self.system_target = self.base / "mounted-system"
        self.esp_target.mkdir()
        self.system_target.mkdir()

    def invoke(self, *args):
        argv = [
            "install_from_spf.py",
            "--esp-package", str(self.esp_package),
            "--system-package", str(self.system_package),
            "--esp-target", str(self.esp_target),
            "--system-target", str(self.system_target),
            *map(str, args),
        ]
        output = io.StringIO()
        with patch.object(sys, "argv", argv), contextlib.redirect_stdout(output):
            with contextlib.redirect_stderr(output):
                result = installer.main()
        return result, output.getvalue()

    def test_dry_run_does_not_write_files(self):
        result, output = self.invoke()
        self.assertEqual(result, 0, output)
        self.assertIn("Dry run only", output)
        self.assertFalse((self.esp_target / "EFI" / "BOOT" / "BOOTX64.EFI").exists())
        self.assertFalse((self.system_target / "System32" / "krnl.exe").exists())

    def test_apply_requires_exact_confirmation_and_writes_nothing(self):
        result, output = self.invoke("--apply", "--confirm", "INSTALL")
        self.assertEqual(result, 1)
        self.assertIn("requires --confirm", output)
        self.assertFalse((self.esp_target / "EFI" / "BOOT" / "BOOTX64.EFI").exists())
        self.assertFalse((self.system_target / "System32" / "krnl.exe").exists())

    def test_confirmed_apply_extracts_both_packages(self):
        result, output = self.invoke("--apply", "--confirm", installer.CONFIRMATION)
        self.assertEqual(result, 0, output)
        self.assertEqual(
            (self.esp_target / "EFI" / "BOOT" / "BOOTX64.EFI").read_bytes(),
            b"test-efi",
        )
        self.assertEqual(
            (self.system_target / "System32" / "krnl.exe").read_bytes(),
            b"test-kernel",
        )

    def test_existing_file_blocks_without_overwrite(self):
        dest = self.system_target / "System32" / "krnl.exe"
        dest.parent.mkdir()
        dest.write_bytes(b"keep-me")
        result, output = self.invoke("--apply", "--confirm", installer.CONFIRMATION)
        self.assertEqual(result, 1)
        self.assertIn("--overwrite", output)
        self.assertEqual(dest.read_bytes(), b"keep-me")
        self.assertFalse((self.esp_target / "EFI" / "BOOT" / "BOOTX64.EFI").exists())

    def test_overwrite_requires_explicit_flag(self):
        dest = self.system_target / "System32" / "krnl.exe"
        dest.parent.mkdir()
        dest.write_bytes(b"old")
        result, output = self.invoke(
            "--apply", "--confirm", installer.CONFIRMATION, "--overwrite"
        )
        self.assertEqual(result, 0, output)
        self.assertEqual(dest.read_bytes(), b"test-kernel")

    def test_overlapping_targets_rejected(self):
        nested = self.esp_target / "nested-system"
        nested.mkdir()
        self.system_target = nested
        result, output = self.invoke()
        self.assertEqual(result, 1)
        self.assertIn("non-overlapping", output)
        self.assertFalse((self.esp_target / "EFI").exists())

    def test_wrong_package_kind_rejected_before_writing(self):
        wrong_esp = self.base / "wrong-esp.spf"
        spf.build(self.system_source, wrong_esp, "system")
        self.esp_package = wrong_esp
        result, output = self.invoke("--apply", "--confirm", installer.CONFIRMATION)
        self.assertEqual(result, 1)
        self.assertIn("expected 'esp'", output)
        self.assertFalse((self.esp_target / "EFI").exists())
        self.assertFalse((self.system_target / "System32").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
