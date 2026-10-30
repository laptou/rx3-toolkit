# SPDX-License-Identifier: MPL-2.0
"""Summarise an operator-local image table. The table itself is never written back."""

from __future__ import annotations

from collections import Counter
from dataclasses import dataclass
from pathlib import Path


RECORD = 44


@dataclass(frozen=True)
class ImageRecord:
    index: int
    width: int
    height: int
    format_byte: int
    palette: int


class CensusError(ValueError):
    """The file is not a table of 44-byte image records."""


def summarize(path: Path) -> tuple[ImageRecord, ...]:
    data = path.read_bytes()
    if not data or len(data) % RECORD:
        raise CensusError("image table length is not a multiple of 44")
    records: list[ImageRecord] = []
    for index in range(len(data) // RECORD):
        record = data[index * RECORD:(index + 1) * RECORD]
        width = int.from_bytes(record[4:6], "little")
        height = int.from_bytes(record[6:8], "little")
        records.append(ImageRecord(index, width, height, record[0x18], record[0x19]))
    return tuple(records)


def histogram(records: tuple[ImageRecord, ...]) -> list[tuple[int, int, int]]:
    """`(format, palette flag, count)` sorted by format then flag."""
    counts: Counter[tuple[int, int]] = Counter(
        (record.format_byte, record.palette) for record in records
    )
    return [(fmt, flag, counts[(fmt, flag)]) for fmt, flag in sorted(counts)]
