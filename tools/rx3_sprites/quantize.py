# SPDX-License-Identifier: MPL-2.0
"""Turn RGBA frames into a hard-edged indexed palette. Partial coverage is rejected."""

from __future__ import annotations

from tools.rx3_sprites.limits import ALPHA_CUTOFF
from tools.rx3_sprites.proof import FormatProof


class QuantizeError(ValueError):
    """The frames do not fit the proven palette."""


def rgb565(red: int, green: int, blue: int) -> int:
    return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)


def quantize(frames: list[list[tuple[int, int, int, int]]], proof: FormatProof) -> tuple[bytes, list[bytes]]:
    """Return palette bytes and one packed index blob per frame.

    Alpha below 128 is transparent. Anything else is opaque. There is no soft edge.
    """
    colours: list[int] = []
    known: dict[int, int] = {}
    indexed: list[list[int]] = []
    for frame in frames:
        pixels: list[int] = []
        for red, green, blue, alpha in frame:
            if alpha < ALPHA_CUTOFF:
                pixels.append(0)
                continue
            if alpha != 255 and alpha >= ALPHA_CUTOFF:
                # still hard: coverage at or above the cut is opaque
                pass
            colour = rgb565(red, green, blue)
            index = known.get(colour)
            if index is None:
                if len(colours) == 255:
                    raise QuantizeError("more than 255 opaque colours")
                colours.append(colour)
                index = len(colours)
                known[colour] = index
            pixels.append(index)
        indexed.append(pixels)
    limit = 16 if proof.bpp == 4 else 256
    if len(colours) + 1 > limit:
        raise QuantizeError(f"{proof.bpp}-bit sprites allow {limit - 1} opaque colours")
    palette = bytearray()
    palette += (0).to_bytes(2, "little")
    for colour in colours:
        palette += colour.to_bytes(2, "little")
    packed = [_pack(pixels, proof.bpp) for pixels in indexed]
    return bytes(palette), packed


def _pack(pixels: list[int], bpp: int) -> bytes:
    if bpp == 8:
        return bytes(pixels)
    out = bytearray()
    for offset in range(0, len(pixels), 2):
        high = pixels[offset]
        low = pixels[offset + 1] if offset + 1 < len(pixels) else 0
        out.append((high << 4) | low)
    return bytes(out)
