# SPDX-License-Identifier: MPL-2.0
"""Frame selection for fixed-rate playback and a live beat phase.

Beat phase comes from the player. A missing clock produces no frame.
"""

from __future__ import annotations


def fps_frame(
    elapsed_ms: int, fps_num: int, fps_den: int, frame_count: int
) -> int:
    if elapsed_ms < 0 or fps_num <= 0 or fps_den <= 0 or frame_count <= 0:
        raise ValueError("fixed-rate timing is incomplete")
    return (elapsed_ms * fps_num // fps_den // 1000) % frame_count


def phase_frame(
    phase_milli: int | None,
    frames_per_beat: int,
    beats_per_loop: int,
    frame_count: int,
) -> int | None:
    """Map beats*1000 from the player clock onto one frame. None means no clock."""
    if (
        phase_milli is None
        or phase_milli < 0
        or frame_count <= 0
        or frames_per_beat <= 0
        or beats_per_loop <= 0
    ):
        return None
    ticks = phase_milli * frames_per_beat // beats_per_loop // 1000
    return ticks % frame_count
