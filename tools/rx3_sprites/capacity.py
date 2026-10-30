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


def report(pack_bytes: list[int], stem_bytes: int = 0) -> str:
    """Summarize every pack. The limit applies to each pack, not the total."""
    total = sum(pack_bytes)
    message = f"sprites {total} bytes, stems {stem_bytes} bytes"
    if any(size > MAX_PACK_BYTES for size in pack_bytes):
        return (
            f"{message}; sprite pack exceeds the {MAX_PACK_BYTES} byte per-pack limit"
        )
    return message
