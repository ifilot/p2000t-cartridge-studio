import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))

from application_image import MANIFEST_ADDRESS, stamp, validate  # noqa: E402
from hex_image import read_hex, write_hex  # noqa: E402


class ApplicationImageTest(unittest.TestCase):
    def test_stamp_and_validate(self):
        with tempfile.TemporaryDirectory() as directory:
            raw = Path(directory) / "raw.hex"
            stamped = Path(directory) / "stamped.hex"
            write_hex({0: 0x0C, 1: 0x94, 127: 0x42}, raw)
            stamp(raw, stamped)
            memory = read_hex(stamped)
            length, _ = validate(memory)
            self.assertEqual(length, 128)
            self.assertEqual(max(memory), MANIFEST_ADDRESS + 15)

            memory[64] = 0
            with self.assertRaisesRegex(ValueError, "CRC32 mismatch"):
                validate(memory)

    def test_rejects_blank_vector_and_manifest_overlap(self):
        with tempfile.TemporaryDirectory() as directory:
            raw = Path(directory) / "raw.hex"
            output = Path(directory) / "output.hex"
            write_hex({0: 0xFF, 1: 0xFF}, raw)
            with self.assertRaisesRegex(ValueError, "reset vector is blank"):
                stamp(raw, output)

            write_hex({0: 0x0C, 1: 0x94, MANIFEST_ADDRESS: 0}, raw)
            with self.assertRaisesRegex(ValueError, "overlaps the manifest"):
                stamp(raw, output)

    def test_rejects_unprotected_trailing_data(self):
        with tempfile.TemporaryDirectory() as directory:
            raw = Path(directory) / "raw.hex"
            stamped = Path(directory) / "stamped.hex"
            write_hex({0: 0x0C, 1: 0x94}, raw)
            stamp(raw, stamped)
            memory = read_hex(stamped)
            memory[0x100] = 0x42
            with self.assertRaisesRegex(ValueError, "outside its CRC-protected length"):
                validate(memory)


if __name__ == "__main__":
    unittest.main()
