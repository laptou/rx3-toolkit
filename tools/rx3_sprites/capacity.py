# SPDX-License-Identifier: MPL-2.0
"""Host-side memory report. The player still refuses a pack that does not fit."""

from __future__ import annotations

from pathlib import Path

from tools.rx3_sprites.limits import MEMORY_CEILING


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
    if sprite_bytes > MEMORY_CEILING:
        return (
            f"{message}; sprite packs exceed the {MEMORY_CEILING} byte player ceiling"
        )
    return message
