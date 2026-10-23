import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).parents[1] / "tools/torture/tcache_proto.py"
SPEC = importlib.util.spec_from_file_location("tcache_proto", SCRIPT)
cache = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(cache)


class PersistentCacheTest(unittest.TestCase):
    def test_roundtrip_and_identity_invalidations(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            exe = root / "game.exe"
            exe.write_bytes(b"exe-v1")
            path = root / "translation.cache"
            blocks = [{"guest_eip": 0x401000, "source_sha256": "aa" * 32,
                       "arm_code_hex": "0000a0e1", "relocations": [{"offset": 0, "kind": "helper", "symbol": 3}]}]
            cache.write_cache(path, exe, "wx86-abc", "armv7-neon-le", blocks)
            self.assertEqual(cache.load_cache(path, exe, "wx86-abc", "armv7-neon-le")["blocks"], blocks)
            self.assertEqual(cache.load_cache(path, exe, "wx86-abc", "armv7-neon-le",
                                              {0x401000: "cc" * 32})["blocks"], [])
            self.assertEqual(cache.load_cache(path, exe, "wx86-abc", "armv7-neon-le", {})["blocks"], [])
            self.assertIsNone(cache.load_cache(path, exe, "wx86-def", "armv7-neon-le"))
            self.assertIsNone(cache.load_cache(path, exe, "wx86-abc", "armv7-vfp-le"))
            exe.write_bytes(b"exe-v2")
            self.assertIsNone(cache.load_cache(path, exe, "wx86-abc", "armv7-neon-le"))

    def test_corruption_and_bad_relocation_are_misses(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            exe = root / "game.exe"
            exe.write_bytes(b"exe")
            path = root / "translation.cache"
            base = {"guest_eip": 0x401000, "source_sha256": "bb" * 32,
                    "arm_code_hex": "0000a0e1", "relocations": [{"offset": 4, "kind": "helper", "symbol": 3}]}
            cache.write_cache(path, exe, "wx86-1", "armv7-neon-le", [base])
            self.assertIsNone(cache.load_cache(path, exe, "wx86-1", "armv7-neon-le"))
            path.write_bytes(path.read_bytes()[:-1] + b"x")
            self.assertIsNone(cache.load_cache(path, exe, "wx86-1", "armv7-neon-le"))


if __name__ == "__main__":
    unittest.main()
