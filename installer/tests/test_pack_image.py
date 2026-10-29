"""Safety tests for the install-image block stream helper."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

MODULE_PATH = Path(__file__).resolve().parents[1] / "pack_image.py"
SPEC = importlib.util.spec_from_file_location("pack_image", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
pack_image = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(pack_image)


class PackImageTests(unittest.TestCase):
    def test_round_trip_preserves_zero_and_nonzero_blocks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = root / "source.img"
            expected = b"\0" * pack_image.BLOCK_SIZE + b"payload" + b"\0" * 20
            original.write_bytes(expected)
            packed = root / "source.zblk"
            restored = root / "restored.img"

            pack_image.pack(original, packed)
            pack_image.unpack(packed, restored)

            self.assertEqual(restored.read_bytes(), expected)

    def test_unpack_failure_keeps_previous_target(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            packed = root / "broken.zblk"
            target = root / "existing.img"
            packed.write_bytes(pack_image.HEADER.pack(
                pack_image.MAGIC, 8, pack_image.BLOCK_SIZE
            ) + b"\1short")
            target.write_bytes(b"previous image")

            with self.assertRaisesRegex(ValueError, "truncated raw block"):
                pack_image.unpack(packed, target)

            self.assertEqual(target.read_bytes(), b"previous image")
            self.assertEqual(list(root.glob("existing.img.*.tmp")), [])

    def test_pack_and_unpack_reject_same_source_and_target(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "image"
            source.write_bytes(b"keep me")

            with self.assertRaisesRegex(ValueError, "different files"):
                pack_image.pack(source, source)
            self.assertEqual(source.read_bytes(), b"keep me")

            with self.assertRaisesRegex(ValueError, "different files"):
                pack_image.unpack(source, source)
            self.assertEqual(source.read_bytes(), b"keep me")


if __name__ == "__main__":
    unittest.main()
