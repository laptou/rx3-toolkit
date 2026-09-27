# SPDX-License-Identifier: MPL-2.0
"""Synthetic frames for tests. Geometric only; nothing from a device."""

from __future__ import annotations

from tools.rx3_sprites.proof import FormatProof
from tools.rx3_sprites.quantize import quantize


def proof_8() -> FormatProof:
    return FormatProof(format_byte=2, palette=1, bpp=8, transparent=0, entry_size=2)


def checker(width: int, height: int, frame: int) -> list[tuple[int, int, int, int]]:
    """Opaque squares on a transparent field. `frame` shifts the squares."""
    pixels = []
    for y in range(height):
        for x in range(width):
            on = ((x + frame) // 4 + y // 4) % 2 == 0
            pixels.append((220, 40, 40, 255) if on else (0, 0, 0, 0))
    return pixels


def probe_blob() -> bytes:
    """8x8 hard mask: transparent corners, one opaque centre pixel."""
    pixels = []
    for y in range(8):
        for x in range(8):
            corner = x in (0, 7) and y in (0, 7)
            centre = x == 3 and y == 3
            if corner or not centre:
                pixels.append((0, 0, 0, 0))
            else:
                pixels.append((255, 255, 255, 255))
    palette, frames = quantize([pixels], proof_8())
    return palette + frames[0]
