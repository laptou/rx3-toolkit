# SPDX-License-Identifier: MPL-2.0
"""Synthetic frames for tests. Geometric only; nothing from a device."""

from __future__ import annotations

from tools.rx3_sprites.proof import FormatProof, proven_format


def proof_8() -> FormatProof:
    return proven_format()


def checker(width: int, height: int, frame: int) -> list[tuple[int, int, int, int]]:
    """Opaque squares on a transparent field. `frame` shifts the squares."""
    pixels = []
    for y in range(height):
        for x in range(width):
            on = ((x + frame) // 4 + y // 4) % 2 == 0
            pixels.append((220, 40, 40, 255) if on else (0, 0, 0, 0))
    return pixels
