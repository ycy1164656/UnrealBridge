from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from PIL import Image

from golden_image_compare import compare_images


class GoldenImageCompareTests(unittest.TestCase):
    def test_identical_images_pass(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            expected = root / "expected.png"
            actual = root / "actual.png"
            Image.new("RGBA", (8, 8), (10, 20, 30, 255)).save(expected)
            Image.new("RGBA", (8, 8), (10, 20, 30, 255)).save(actual)
            result = compare_images(expected, actual)
            self.assertTrue(result["passed"])
            self.assertEqual(result["changed_pixel_count"], 0)

    def test_large_change_fails_and_writes_diff(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            expected = root / "expected.png"
            actual = root / "actual.png"
            diff = root / "diff.png"
            Image.new("RGB", (8, 8), (0, 0, 0)).save(expected)
            Image.new("RGB", (8, 8), (255, 255, 255)).save(actual)
            result = compare_images(
                expected,
                actual,
                max_mae=0.0,
                max_changed_ratio=0.0,
                diff_output=diff,
            )
            self.assertFalse(result["passed"])
            self.assertEqual(result["changed_pixel_ratio"], 1.0)
            self.assertTrue(diff.exists())

    def test_size_mismatch_is_structured(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            expected = root / "expected.png"
            actual = root / "actual.png"
            Image.new("RGB", (8, 8)).save(expected)
            Image.new("RGB", (9, 8)).save(actual)
            result = compare_images(expected, actual)
            self.assertFalse(result["passed"])
            self.assertEqual(result["error"], "IMAGE_SIZE_MISMATCH")


if __name__ == "__main__":
    unittest.main(verbosity=2)
