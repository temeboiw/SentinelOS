#!/usr/bin/env python3
"""Static contract tests tying the native installer components together.

These checks are deliberately cross-file: the UEFI loader, trusted kernel,
setup UI, and ISO builder must agree on image names, sizes, hashes, and LBAs.
They complement (but do not replace) building and booting the installer.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
LOADER = (ROOT / "source/boot/loadufi.c").read_text()
KERNEL = (ROOT / "source/kernel/user.h").read_text()
UI = (ROOT / "user/instlr.c").read_text()
NTDLL = (ROOT / "source/user/ntdll_user.h").read_text()
ISO = (ROOT / "make-iso.sh").read_text()
CHECK = (ROOT / "check-iso.sh").read_text()


class NativeInstallerContractTests(unittest.TestCase):
    def test_uefi_stages_exact_image_and_manifest_names(self):
        self.assertIn(
            '{ "esp.img", "system.img", "install.sha256" }',
            LOADER,
        )
        for name in ("esp.img", "system.img", "install.sha256"):
            self.assertIn(name, LOADER)

    def test_kernel_uses_raw_images_for_every_installer_operation(self):
        sections = (
            KERNEL[KERNEL.index("case SYS_NtVerifyInstallerPayload"):],
            KERNEL[KERNEL.index("case SYS_NtQueryInstallerPayload"):],
            KERNEL[KERNEL.index("case SYS_NtReadInstallerPayload"):],
            KERNEL[KERNEL.index("case SYS_NtInstallerInstallImage"):],
        )
        for section in sections:
            self.assertIn('"esp.img" : "system.img"', section)
        self.assertIn('PspFindModule(user_bootinfo, "install.sha256")', sections[0])

    def test_partition_offsets_match_image_copy_destinations(self):
        self.assertRegex(UI, r"esp_first\s*=\s*2048")
        self.assertRegex(UI, r"esp_count\s*=\s*262144")
        self.assertRegex(UI, r"sys_first\s*=\s*esp_first\s*\+\s*esp_count")
        image_syscall = KERNEL[KERNEL.index("case SYS_NtInstallerInstallImage"):]
        self.assertIn("f->rdi == 0 ? 2048ULL : 264192ULL", image_syscall)

    def test_image_sizes_and_manifest_format_agree(self):
        self.assertIn('ESP_IMAGE_SIZE_MIB="${INSTALL_ESP_SIZE_MIB:-128}"', ISO)
        self.assertIn('SYSTEM_IMAGE_SIZE_MIB="${INSTALL_SYSTEM_SIZE_MIB:-512}"', ISO)
        self.assertIn("printf '%s\\n%s\\n'", ISO)
        verify = KERNEL[KERNEL.index("case SYS_NtVerifyInstallerPayload"):]
        self.assertIn("manifest[64] != '\\n'", verify)
        self.assertIn("manifest[129] != '\\n'", verify)
        self.assertIn("payload_image_valid(0,esp_package_size)", UI)
        self.assertIn("total==128ULL*1024*1024", UI)
        self.assertIn("total>=512ULL*1024*1024", UI)

    def test_user_wrapper_exports_the_installer_image_syscall(self):
        self.assertIn("SYS_InstallerInstallImage = 45", NTDLL)
        self.assertIn("NtInstallerInstallImage(u64 image)", NTDLL)
        self.assertIn("NtSyscall6(SYS_InstallerInstallImage, image", NTDLL)

    def test_ui_verifies_before_install_and_deploys_both_images(self):
        ready = UI[UI.index("installer_payloads_ready=esp_query"):]
        self.assertIn("NtVerifyInstallerPayload(0)==0", ready)
        self.assertIn("NtVerifyInstallerPayload(1)==0", ready)
        self.assertIn("NtInstallerInstallImage(0)", ready)
        self.assertIn("NtInstallerInstallImage(1)", ready)

    def test_iso_builder_and_smoke_check_include_raw_images(self):
        for path in ("magic/System32/hal.dll", "magic/System32/krnl.exe",
                     "magic/System32/smss.exe", "magic/System32/instlr.exe"):
            self.assertIn(path, ISO)
        for name in ("esp.img", "system.img", "install.sha256"):
            self.assertIn(name, ISO)
            self.assertIn(name, CHECK)

    def test_native_install_path_does_not_call_host_shell_or_spf_extractor(self):
        self.assertNotIn("install-disk.sh", UI)
        self.assertNotIn("install_from_spf.py", UI)
        self.assertIn("NtInstallerInstallImage", UI)


if __name__ == "__main__":
    unittest.main()
