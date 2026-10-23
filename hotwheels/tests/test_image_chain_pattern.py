import importlib.util
from pathlib import Path
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("image_chain_pattern", ROOT / "tools/image_chain_pattern.py")
pattern = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(pattern)


class ImageChainPatternTests(unittest.TestCase):
    def test_fit_viewport_and_native_ui(self):
        self.assertEqual(pattern.viewport(), (117, 0, 725, 544))
        image = pattern.render("point")
        self.assertEqual(image.size, (960, 544))
        self.assertEqual(image.getpixel((0, 0)), (0, 0, 0))
        self.assertEqual(image.getpixel((8, 8)), (0, 255, 255))

    def test_filter_changes_high_frequency_capture(self):
        point = pattern.render("point")
        linear = pattern.render("linear")
        self.assertNotEqual(point.tobytes(), linear.tobytes())
        self.assertEqual(point.getpixel((117, 0)), (255, 255, 255))


if __name__ == "__main__":
    unittest.main()
