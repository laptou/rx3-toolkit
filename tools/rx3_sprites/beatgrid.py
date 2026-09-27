# SPDX-License-Identifier: MPL-2.0
"""Read Rekordbox beat grids with pyrekordbox and reduce them to tempo segments."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from pyrekordbox.anlz import AnlzFile

from tools.rx3_sprites.limits import (
    MAX_BPM_X1000,
    MIN_BPM_X1000,
    SAMPLE_RATE,
)
from tools.rx3_sprites.timing import Segment
from tools.rx3_stems.rekordbox import export_stem


class BeatGridError(ValueError):
    """The analysis file has no usable beat grid."""


@dataclass(frozen=True)
class TrackGrid:
    identity: str
    segments: tuple[Segment, ...]

    @property
    def origin_samples(self) -> int:
        return self.segments[0].start_samples


def grid_from_anlz(path: Path) -> TrackGrid:
    """One analysis file. The path tag supplies the track identity."""
    try:
        parsed = AnlzFile.parse_file(path)
    except (OSError, ValueError) as error:
        raise BeatGridError(f"cannot read {path.name}") from error
    if "PPTH" not in parsed or "PQTZ" not in parsed:
        raise BeatGridError(f"{path.name} has no path and beat grid")
    identity = export_stem(Path(str(parsed.get("path"))).stem)
    beats, bpms, times = parsed.get("beat_grid")
    if len(times) == 0:
        raise BeatGridError(f"{path.name} beat grid is empty")
    segments: list[Segment] = []
    previous = -1
    for bpm, time in zip(bpms, times):
        start = int(round(float(time) * SAMPLE_RATE))
        if start < 0 or start <= previous:
            raise BeatGridError(f"{path.name} beat times do not advance")
        bpm_x1000 = int(round(float(bpm) * 1000))
        if not MIN_BPM_X1000 <= bpm_x1000 <= MAX_BPM_X1000:
            raise BeatGridError(f"{path.name} tempo is outside 10..400")
        segments.append(Segment(start, bpm_x1000))
        previous = start
    return TrackGrid(identity, tuple(segments))


def grids_from_tree(root: Path) -> tuple[TrackGrid, ...]:
    """Every ANLZ file under a Rekordbox USB tree. Colliding identities are refused."""
    found: dict[str, TrackGrid] = {}
    paths = sorted(
        path for path in root.rglob("*")
        if path.is_file() and path.suffix.upper() in {".DAT", ".EXT", ".2EX"}
        and path.name.upper().startswith("ANLZ")
    )
    for path in paths:
        try:
            grid = grid_from_anlz(path)
        except BeatGridError:
            continue
        if grid.identity in found:
            raise BeatGridError(f"two analysis files share {grid.identity}")
        found[grid.identity] = grid
    return tuple(found[key] for key in sorted(found))
