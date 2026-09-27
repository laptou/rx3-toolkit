# SPDX-License-Identifier: MPL-2.0
"""Pack encoding. The player expands 8-bit indices to rgb565."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class FormatProof:
    format_byte: int
    palette: int
    bpp: int
    transparent: int
    entry_size: int


def proven_format() -> FormatProof:
    """format 2, a palette, 8 bits per pixel, transparent index 0, 16-bit entries."""
    return FormatProof(format_byte=2, palette=1, bpp=8, transparent=0, entry_size=2)
