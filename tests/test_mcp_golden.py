from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image


REPO = Path(__file__).resolve().parents[1]
MODULE_PATH = (
    REPO
    / ".claude"
    / "skills"
    / "unreal-bridge"
    / "scripts"
    / "unreal_bridge_golden.py"
)
spec = importlib.util.spec_from_file_location("unreal_bridge_golden", MODULE_PATH)
golden = importlib.util.module_from_spec(spec)
assert spec and spec.loader
sys.modules[spec.name] = golden
spec.loader.exec_module(golden)


class McpGoldenTests(unittest.TestCase):
    def test_metrics_and_diff_are_returned_without_a_temporary_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            expected = root / "expected.png"
            actual = root / "actual.png"
            Image.new("RGB", (8, 8), (0, 0, 0)).save(expected)
            Image.new("RGB", (8, 8), (255, 255, 255)).save(actual)
            result, diff_png = golden.compare_images(
                expected,
                actual,
                pixel_delta=0,
                max_mae=0.0,
                max_changed_ratio=0.0,
            )
            self.assertFalse(result["passed"])
            self.assertEqual(result["changed_pixel_ratio"], 1.0)
            self.assertIsNotNone(diff_png)
            self.assertTrue(diff_png.startswith(b"\x89PNG\r\n\x1a\n"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
