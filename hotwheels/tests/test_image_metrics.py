#!/usr/bin/env python3
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from image_metrics import compare_images


class ImageMetricsTest(unittest.TestCase):
    def test_identical_and_blurred_images_have_expected_metrics(self):
        from PIL import Image, ImageFilter
        sharp = Image.new("RGB", (32, 32), "black")
        for x in range(16, 32):
            for y in range(32): sharp.putpixel((x, y), (255, 255, 255))
        blurred = sharp.filter(ImageFilter.GaussianBlur(2))
        same = compare_images(sharp, sharp)
        soft = compare_images(sharp, blurred)
        self.assertEqual(same["mae"], 0)
        self.assertGreater(soft["laplacian_variance_reference"], soft["laplacian_variance_candidate"])
        self.assertGreater(soft["gradient_mean_reference"], soft["gradient_mean_candidate"])
        self.assertGreater(soft["mae"], 0)

    def test_different_dimensions_are_not_silently_resized(self):
        from PIL import Image
        with self.assertRaises(ValueError):
            compare_images(Image.new("RGB", (16, 16)), Image.new("RGB", (8, 8)))


if __name__ == "__main__": unittest.main()
