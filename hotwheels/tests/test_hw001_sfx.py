"""Fixture libre de assets para el formato del SFX Advanced Installer."""

import struct
import tempfile
import unittest
from pathlib import Path

from hotwheels.tools.extract_advanced_installer import extract


class SfxExtractionTest(unittest.TestCase):
    def test_directory_and_xor_boundary(self):
        first = bytes(range(256)) * 3
        second = b"\xff\xfeI\x00N\x00I\x00"
        stub = b"MZ" + b"\0" * 30
        encoded_first = bytes(byte ^ 0xFF for byte in first[:0x200]) + first[0x200:]
        start_first = len(stub)
        start_second = start_first + len(first)
        info = bytearray()
        for name, flag, payload, offset in (
            ("sample.msi", 2, first, start_first),
            ("sample.ini", 0, second, start_second),
        ):
            info += struct.pack("<6I", 1, 0, flag, len(payload), offset, len(name))
            info += name.encode("utf-16le")
        info_offset = start_second + len(second)
        footer_offset = info_offset + len(info)
        footer = bytearray(0x4A)
        struct.pack_into("<7I", footer, 0, 0, footer_offset, 2, 100,
                         footer_offset, info_offset, start_first)
        footer[0x40:] = b"ADVINSTSFX"
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "installer.exe"
            src.write_bytes(stub + encoded_first + second + info + footer)
            out = Path(tmp) / "out"
            extract(src, out)
            self.assertEqual((out / "sample.msi").read_bytes(), first)
            self.assertEqual((out / "sample.ini").read_bytes(), second)


if __name__ == "__main__":
    unittest.main()
