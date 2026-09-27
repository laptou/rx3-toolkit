# SPDX-License-Identifier: MPL-2.0
"""Host-side capacity report. A pack over MAX_PACK_BYTES is rejected."""

from __future__ import annotations

from pathlib import Path

from tools.rx3_sprites.limits import MAX_PACK_BYTES


def directory_bytes(path: Path, suffix: str) -> int:
    if not path.is_dir():
        return 0
    return sum(
        child.stat().st_size
        for child in path.glob(f"*{suffix}")
        if child.is_file() and not child.name.startswith(".")
    )


def report(sprite_bytes: int, stem_bytes: int = 0) -> str:
    message = f"sprites {sprite_bytes} bytes, stems {stem_bytes} bytes"
    if sprite_bytes > MAX_PACK_BYTES:
        return (
            f"{message}; sprite pack exceeds the {MAX_PACK_BYTES} byte per-pack limit"
        )
    return message
