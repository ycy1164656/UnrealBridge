#!/usr/bin/env python3
"""Golden-image comparison used by the UnrealBridge MCP artifact workflow."""

from __future__ import annotations

import io
from pathlib import Path
from typing import Any, Dict, Optional, Tuple

import numpy as np
from PIL import Image


def compare_images(
    expected_path: Path,
    actual_path: Path,
    *,
    pixel_delta: int = 8,
    max_mae: float = 0.01,
    max_changed_ratio: float = 0.01,
    make_diff: bool = True,
) -> Tuple[Dict[str, Any], Optional[bytes]]:
    if not 0 <= pixel_delta <= 255:
        raise ValueError("pixel_delta must be between 0 and 255")
    if max_mae < 0 or max_changed_ratio < 0:
        raise ValueError("golden-image tolerances cannot be negative")

    with Image.open(expected_path) as expected_source:
        expected_image = expected_source.convert("RGBA")
    with Image.open(actual_path) as actual_source:
        actual_image = actual_source.convert("RGBA")
    result: Dict[str, Any] = {
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
        return result, None

    expected = np.asarray(expected_image, dtype=np.int16)
    actual = np.asarray(actual_image, dtype=np.int16)
    absolute = np.abs(expected - actual)
    rgb_absolute = absolute[:, :, :3]
    per_pixel_max = rgb_absolute.max(axis=2)
    changed = per_pixel_max > pixel_delta
    mae = float(rgb_absolute.mean() / 255.0)
    rmse = float(np.sqrt(np.square(rgb_absolute.astype(np.float64)).mean()) / 255.0)
    changed_ratio = float(changed.mean())
    passed = mae <= max_mae and changed_ratio <= max_changed_ratio
    result.update(
        passed=passed,
        error="" if passed else "GOLDEN_IMAGE_MISMATCH",
        mae=mae,
        rmse=rmse,
        changed_pixel_ratio=changed_ratio,
        changed_pixel_count=int(changed.sum()),
        total_pixel_count=int(changed.size),
        max_channel_delta=int(rgb_absolute.max()),
        message=(
            "Golden image comparison passed."
            if passed
            else "Golden image comparison exceeded tolerance."
        ),
    )
    if not make_diff:
        return result, None

    heat = np.zeros((*per_pixel_max.shape, 4), dtype=np.uint8)
    heat[:, :, 0] = np.clip(per_pixel_max * 4, 0, 255).astype(np.uint8)
    heat[:, :, 1] = np.clip(per_pixel_max, 0, 255).astype(np.uint8)
    heat[:, :, 3] = np.where(changed, 255, 72).astype(np.uint8)
    output = io.BytesIO()
    Image.fromarray(heat, mode="RGBA").save(output, format="PNG")
    return result, output.getvalue()
