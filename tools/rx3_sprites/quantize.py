# SPDX-License-Identifier: MPL-2.0
"""Turn RGBA frames into a hard-edged indexed palette.

Index 0 is transparent. Every opaque pixel shares the other 255 slots of the
measured 8-bit palette. Colours past that limit are reduced to one palette
for the whole sequence, with no dither.
"""

from __future__ import annotations

import logging

from PIL import Image

from tools.rx3_sprites.limits import ALPHA_CUTOFF
from tools.rx3_sprites.proof import FormatProof

log = logging.getLogger("rx3.sprites")


class QuantizeError(ValueError):
    """The frames do not fit the proven palette."""


def rgb565(red: int, green: int, blue: int) -> int:
    return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)


def quantize(frames: list[list[tuple[int, int, int, int]]], proof: FormatProof) -> tuple[bytes, list[bytes]]:
    """Return palette bytes and one packed index blob per frame.

    Alpha below 128 is transparent. Anything else is opaque. There is no soft edge.
    """
    if proof.bpp != 8:
        raise QuantizeError("the measured format is 8 bits per pixel")
    max_opaque = 255
    masked: list[list[tuple[int, int, int] | None]] = []
    opaque: list[tuple[int, int, int]] = []
    for frame in frames:
        row: list[tuple[int, int, int] | None] = []
        for red, green, blue, alpha in frame:
            if alpha < ALPHA_CUTOFF:
                row.append(None)
                continue
            colour = (red, green, blue)
            row.append(colour)
            opaque.append(colour)
        masked.append(row)
    unique = {rgb565(*pixel) for pixel in opaque}
    if len(unique) > max_opaque:
        masked = _write_back(masked, _reduce(opaque, max_opaque))
    colours: list[int] = []
    known: dict[int, int] = {}
    indexed: list[list[int]] = []
    for frame in masked:
        pixels: list[int] = []
        for pixel in frame:
            if pixel is None:
                pixels.append(0)
                continue
            colour = rgb565(*pixel)
            index = known.get(colour)
            if index is None:
                if len(colours) == max_opaque:
                    raise QuantizeError("palette reduction still exceeds the player limit")
                colours.append(colour)
                index = len(colours)
                known[colour] = index
            pixels.append(index)
        indexed.append(pixels)
    if len(unique) > len(colours):
        log.info("reduced %s opaque colours to %s", len(unique), len(colours))
    else:
        log.debug("palette %s colours", len(colours))
    palette = bytearray()
    palette += (0).to_bytes(2, "little")
    for colour in colours:
        palette += colour.to_bytes(2, "little")
    packed = [bytes(pixels) for pixels in indexed]
    return bytes(palette), packed


def _reduce(pixels: list[tuple[int, int, int]], limit: int) -> list[tuple[int, int, int]]:
    """One median-cut palette for every opaque pixel. No dither."""
    image = Image.new("RGB", (len(pixels), 1))
    image.putdata(pixels)
    reduced = image.quantize(colors=limit, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    palette = reduced.getpalette() or []
    output: list[tuple[int, int, int]] = []
    for index in reduced.tobytes():
        slot = index * 3
        output.append((palette[slot], palette[slot + 1], palette[slot + 2]))
    return output


def _write_back(
    frames: list[list[tuple[int, int, int] | None]],
    reduced: list[tuple[int, int, int]],
) -> list[list[tuple[int, int, int] | None]]:
    cursor = 0
    rewritten: list[list[tuple[int, int, int] | None]] = []
    for frame in frames:
        row: list[tuple[int, int, int] | None] = []
        for pixel in frame:
            if pixel is None:
                row.append(None)
                continue
            row.append(reduced[cursor])
            cursor += 1
        rewritten.append(row)
    return rewritten


