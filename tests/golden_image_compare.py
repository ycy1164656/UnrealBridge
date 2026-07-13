#!/usr/bin/env python3
"""Compare a rendered image with a golden baseline and emit machine-readable metrics."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def compare_images(
    expected_path: Path,
    actual_path: Path,
    *,
    pixel_delta: int = 8,
    max_mae: float = 0.01,
    max_changed_ratio: float = 0.01,
    diff_output: Path | None = None,
) -> dict:
    expected_image = Image.open(expected_path).convert("RGBA")
    actual_image = Image.open(actual_path).convert("RGBA")
    result: dict = {
        "expected": str(expected_path.resolve()),
        "actual": str(actual_path.resolve()),
        "expected_size": list(expected_image.size),
        "actual_size": list(actual_image.size),
        "pixel_delta": pixel_delta,
        "max_mae": max_mae,
        "max_changed_ratio": max_changed_ratio,
    }
    if expected_image.size != actual_image.size:
        result.update(
            passed=False,
            error="IMAGE_SIZE_MISMATCH",
            message="Expected and actual image dimensions differ.",
        )
        return result

    expected = np.asarray(expected_image, dtype=np.int16)
    actual = np.asarray(actual_image, dtype=np.int16)
    absolute = np.abs(expected - actual)
    rgb_absolute = absolute[:, :, :3]
    per_pixel_max = rgb_absolute.max(axis=2)
    changed = per_pixel_max > pixel_delta
    mae = float(rgb_absolute.mean() / 255.0)
    rmse = float(np.sqrt(np.square(rgb_absolute.astype(np.float64)).mean()) / 255.0)
    changed_ratio = float(changed.mean())
    max_delta = int(rgb_absolute.max())
    passed = mae <= max_mae and changed_ratio <= max_changed_ratio

    result.update(
        passed=passed,
        error="" if passed else "GOLDEN_IMAGE_MISMATCH",
        mae=mae,
        rmse=rmse,
        changed_pixel_ratio=changed_ratio,
        changed_pixel_count=int(changed.sum()),
        total_pixel_count=int(changed.size),
        max_channel_delta=max_delta,
        message="Golden image comparison passed." if passed else "Golden image comparison exceeded tolerance.",
    )

    if diff_output is not None:
        diff_output.parent.mkdir(parents=True, exist_ok=True)
        heat = np.zeros((*per_pixel_max.shape, 4), dtype=np.uint8)
        heat[:, :, 0] = np.clip(per_pixel_max * 4, 0, 255).astype(np.uint8)
        heat[:, :, 1] = np.clip(per_pixel_max, 0, 255).astype(np.uint8)
        heat[:, :, 3] = np.where(changed, 255, 72).astype(np.uint8)
        Image.fromarray(heat, mode="RGBA").save(diff_output)
        result["diff_output"] = str(diff_output.resolve())
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("expected", type=Path)
    parser.add_argument("actual", type=Path)
    parser.add_argument("--pixel-delta", type=int, default=8)
    parser.add_argument("--max-mae", type=float, default=0.01)
    parser.add_argument("--max-changed-ratio", type=float, default=0.01)
    parser.add_argument("--diff-output", type=Path)
    parser.add_argument("--json-output", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not 0 <= args.pixel_delta <= 255:
        raise SystemExit("--pixel-delta must be between 0 and 255")
    try:
        result = compare_images(
            args.expected,
            args.actual,
            pixel_delta=args.pixel_delta,
            max_mae=args.max_mae,
            max_changed_ratio=args.max_changed_ratio,
            diff_output=args.diff_output,
        )
    except (OSError, ValueError) as error:
        result = {
            "passed": False,
            "error": "IMAGE_READ_FAILED",
            "message": str(error),
        }
    payload = json.dumps(result, ensure_ascii=False, indent=2)
    print(payload)
    if args.json_output:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(payload + "\n", encoding="utf-8")
    if result.get("error") == "IMAGE_READ_FAILED":
        return 2
    return 0 if result.get("passed") else 1


if __name__ == "__main__":
    sys.exit(main())
