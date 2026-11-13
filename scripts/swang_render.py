#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Render a track through the player's SWANG dsp, to listen on the host.

The track is decoded by ffmpeg, swung by the same rx3_swang_dsp.h the player
runs, and encoded again. The grid comes from --bpm and --first-beat, the time
of any downbeat in the input.

    python3 scripts/swang_render.py in.mp3 out.wav --bpm 124 --first-beat 0.12
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "scripts" / "swang_render.c"
BINARY = ROOT / "build" / "swang_render"
RATE = 44100
# the BEAT arrows' notes, as the beats in one swung pair.
PAIR_BEATS = {"16th": 0.5, "8th": 1.0, "quarter": 2.0}
RAW = ["-f", "f32le", "-ac", "2", "-ar", str(RATE)]


def build() -> None:
    BINARY.parent.mkdir(exist_ok=True)
    subprocess.run(
        # the header also carries the waveform helpers the tool never calls.
        ["clang", "-O2", "-Wall", "-Wextra", "-Werror",
         "-Wno-unused-function", "-I", str(ROOT),
         "-o", str(BINARY), str(SOURCE)],
        check=True,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--bpm", type=float, required=True)
    parser.add_argument("--first-beat", type=float, default=0.0,
                        help="seconds into the input where a downbeat sits")
    parser.add_argument("--depth", type=float, default=1.0,
                        help="the DEPTH dial, 0 straight to 1 hardest")
    parser.add_argument("--note", choices=PAIR_BEATS, default="8th")
    args = parser.parse_args()

    build()
    swing = 0.5 + 0.25 * min(max(args.depth, 0.0), 1.0)
    decode = subprocess.Popen(
        ["ffmpeg", "-v", "error", "-i", str(args.input), *RAW, "-"],
        stdout=subprocess.PIPE,
    )
    render = subprocess.Popen(
        [str(BINARY), str(args.bpm), str(PAIR_BEATS[args.note]), str(swing),
         str(args.first_beat * RATE)],
        stdin=decode.stdout,
        stdout=subprocess.PIPE,
    )
    decode.stdout.close()
    encode = subprocess.run(
        ["ffmpeg", "-v", "error", "-y", *RAW, "-i", "-", str(args.output)],
        stdin=render.stdout,
        check=False,
    )
    render.stdout.close()
    failed = [decode.wait(), render.wait(), encode.returncode]
    if any(failed):
        print(f"render failed (ffmpeg, dsp, ffmpeg exit codes: {failed})",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
