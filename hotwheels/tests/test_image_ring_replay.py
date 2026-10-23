import os
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

# El binario de replay es un insumo externo: lo construye el build de CMake
# (target image_ring_replay) y lo inyecta ctest en HW_IMAGE_RING_REPLAY.
# Sin ese insumo los casos se SALTAN, no fallan: run_all corre esta suite sin
# argumentos (es un glob) y un rojo de infraestructura no puede tapar un rojo
# de imagen (BUG-049; precedente: HW_DS_REPLAY en test_dsound_oracle.py).
REPLAY = os.environ.get("HW_IMAGE_RING_REPLAY")
needs_replay = unittest.skipUnless(
    REPLAY, "requiere HW_IMAGE_RING_REPLAY (binario image_ring_replay)")

if not REPLAY:
    print("RUN_MODE: skip (sin HW_IMAGE_RING_REPLAY; el binario lo inyecta "
          "ctest en image_ring_replay_input)")


def command(op, *payload):
    return [((len(payload) << 16) | op), *payload]


def capture(words, *, magic=b"HWRING01", target=0):
    data = bytearray(magic + struct.pack("<II", 1, target))
    data += struct.pack("<I", len(words))
    data += struct.pack("<" + "I" * len(words), *words)
    return data


@needs_replay
class ImageRingReplayInputTests(unittest.TestCase):
    binary = REPLAY

    def run_capture(self, data):
        with tempfile.TemporaryDirectory(prefix="image-ring-input-") as td:
            source = Path(td) / "capture.hwr"
            source.write_bytes(data)
            return subprocess.run(
                [self.binary, source, Path(td) / "out"],
                capture_output=True,
                text=True,
            )

    def test_valid_small_frame_replays(self):
        words = command(1, 2, 2) + command(8) + command(9)
        result = self.run_capture(capture(words))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_accepts_vita_maximum_frame_dimension(self):
        words = command(1, 2048, 1) + command(8) + command(9)
        result = self.run_capture(capture(words))
        self.assertEqual(result.returncode, 0, result.stderr)

    def assert_rejected(self, data):
        result = self.run_capture(data)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("captura HWRING01 inválida", result.stderr)

    def test_rejects_bad_header_and_truncation(self):
        self.assert_rejected(capture([], magic=b"BADMAGIC"))
        self.assert_rejected(b"HWRING01\x01")
        self.assert_rejected(capture([0x00020001, 2]))  # Begin says 2 payload words, only one follows.

    def test_rejects_incoherent_draw_count_without_reading_past_ring(self):
        # 6*715827883 wraps to 2 in 32-bit arithmetic; four payload words
        # pass the old unchecked expression but cannot contain that vertex list.
        words = command(1, 2, 2) + command(7, 4, 715827883, 0, 0)
        self.assert_rejected(capture(words))

    def test_rejects_impossible_frame_dimensions(self):
        self.assert_rejected(capture(command(1, 2049, 1) + command(8) + command(9)))
        self.assert_rejected(capture(command(1, 0, 1) + command(8) + command(9)))

    def test_rejects_clear_coordinate_overflow(self):
        words = command(1, 2, 2) + command(3, 0x7FFFFFFF, 0, 1, 1, 0) + command(8) + command(9)
        self.assert_rejected(capture(words))

    def test_rejects_target_out_of_range(self):
        self.assert_rejected(capture(command(1, 2, 2) + command(8) + command(9), target=1))

    def test_rejects_unknown_opcode(self):
        self.assert_rejected(capture(command(0x7FFF)))


if __name__ == "__main__":
    unittest.main()
