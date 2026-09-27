# SPDX-License-Identifier: MPL-2.0
"""Frame selection for fixed-rate and beat-grid playback.

Beat phase is absolute. A seek lands on the frame for that sample, because the
frame is a function of position rather than of frames already shown.
"""

from __future__ import annotations

from dataclasses import dataclass

from tools.rx3_sprites.limits import SAMPLE_RATE


@dataclass(frozen=True)
class Segment:
    start_samples: int
    bpm_x1000: int


def fps_frame(
    elapsed_ms: int, fps_num: int, fps_den: int, frame_count: int
) -> int:
    if elapsed_ms < 0 or fps_num <= 0 or fps_den <= 0 or frame_count <= 0:
        raise ValueError("fixed-rate timing is incomplete")
    return (elapsed_ms * fps_num // fps_den // 1000) % frame_count


def beat_frame(
    position: int,
    segments: tuple[Segment, ...] | list[Segment],
    frame_count: int,
    beats_per_loop: int,
) -> int:
    """Map a 44.1 kHz playhead through ordered beat-grid segments."""
    if frame_count <= 0 or beats_per_loop <= 0 or not segments:
        raise ValueError("beat timing is incomplete")
    if position < segments[0].start_samples:
        return 0
    index = 0
    for cursor, segment in enumerate(segments):
        if segment.start_samples <= position:
            index = cursor
        else:
            break
    current = segments[index]
    if index + 1 < len(segments):
        span = segments[index + 1].start_samples - current.start_samples
        if span <= 0:
            raise ValueError("beat grid segments must advance")
        into = position - current.start_samples
        if into > span:
            into = span
        beats_x1000 = index * 1000 + into * 1000 // span
    else:
        delta = position - current.start_samples
        beats_x1000 = index * 1000 + (
            delta * current.bpm_x1000 // (SAMPLE_RATE * 60)
        )
    ticks = beats_x1000 * frame_count // beats_per_loop // 1000
    return ticks % frame_count
