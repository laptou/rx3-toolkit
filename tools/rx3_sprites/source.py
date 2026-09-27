# SPDX-License-Identifier: MPL-2.0
"""Validate authored PNG frames and compile them into one pack."""

from __future__ import annotations

import json
import logging
from dataclasses import dataclass
from pathlib import Path

from PIL import Image

from tools.rx3_sprites.limits import (
    ANCHOR_BOTTOM,
    ANCHOR_CENTER,
    MAX_EDGE,
    MAX_FRAMES,
    MODE_BEAT,
    MODE_FPS,
    SCHEMA,
)
from tools.rx3_sprites.pack import Pack, PackError
from tools.rx3_sprites.proof import FormatProof
from tools.rx3_sprites.quantize import QuantizeError, quantize

log = logging.getLogger("rx3.sprites")


class SourceError(ValueError):
    """The manifest or the frames cannot be compiled."""


@dataclass(frozen=True)
class Source:
    name: str
    mode: int
    fps_num: int
    fps_den: int
    frames_per_beat: int
    beats_per_loop: int
    anchor: int
    frames: tuple[Image.Image, ...]


def load_source(directory: Path) -> Source:
    manifest_path = directory / "manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SourceError(f"cannot read {manifest_path.name}") from error
    if not isinstance(manifest, dict):
        raise SourceError("manifest must be an object")
    _require_keys(manifest)
    if manifest["schema"] != SCHEMA:
        raise SourceError("unsupported manifest schema")
    name = manifest["name"]
    if not isinstance(name, str) or not name or "/" in name or name.startswith("."):
        raise SourceError("animation name is empty or unsafe")
    mode_name = manifest["mode"]
    if mode_name == "fps":
        mode = MODE_FPS
    elif mode_name == "beat":
        mode = MODE_BEAT
    else:
        raise SourceError("mode must be fps or beat")
    anchor_name = manifest["anchor"]
    if anchor_name == "center":
        anchor = ANCHOR_CENTER
    elif anchor_name == "bottom-center":
        anchor = ANCHOR_BOTTOM
    else:
        raise SourceError("anchor must be center or bottom-center")
    width = _positive(manifest["width"], "width")
    height = _positive(manifest["height"], "height")
    frame_count = _positive(manifest["frame_count"], "frame_count")
    if width > MAX_EDGE or height > MAX_EDGE or frame_count > MAX_FRAMES:
        raise SourceError("frames exceed 64x64 or 16 frames")
    fps_num = _whole(manifest["fps_num"], "fps_num")
    fps_den = _whole(manifest["fps_den"], "fps_den")
    frames_per_beat = _whole(manifest["frames_per_beat"], "frames_per_beat")
    beats_per_loop = _whole(manifest["beats_per_loop"], "beats_per_loop")
    if mode == MODE_FPS and (fps_num <= 0 or fps_den <= 0):
        raise SourceError("fps mode needs a positive rate")
    if mode == MODE_BEAT and frames_per_beat * beats_per_loop != frame_count:
        raise SourceError("beat loop must cover every frame once")
    frames = _frames(directory, manifest, width, height, frame_count)
    log.info("%s %s %sx%s %s frames", name, mode_name, width, height, frame_count)
    return Source(
        name=name, mode=mode, fps_num=fps_num, fps_den=fps_den,
        frames_per_beat=frames_per_beat, beats_per_loop=beats_per_loop,
        anchor=anchor, frames=frames,
    )


def compile_source(source: Source, proof: FormatProof) -> Pack:
    raw = [_rgba(frame) for frame in source.frames]
    try:
        palette, packed = quantize(raw, proof)
    except QuantizeError as error:
        raise SourceError(str(error)) from error
    try:
        return Pack(
            name=source.name,
            width=source.frames[0].width,
            height=source.frames[0].height,
            mode=source.mode,
            fps_num=source.fps_num if source.mode == MODE_FPS else 0,
            fps_den=source.fps_den if source.mode == MODE_FPS else 0,
            frames_per_beat=source.frames_per_beat if source.mode == MODE_BEAT else 0,
            beats_per_loop=source.beats_per_loop if source.mode == MODE_BEAT else 0,
            anchor=source.anchor,
            proof=proof,
            palette=palette,
            frames=tuple(packed),
        )
    except PackError as error:
        raise SourceError(str(error)) from error


def _require_keys(manifest: dict) -> None:
    required = {
        "schema", "name", "layout", "width", "height", "frame_count", "mode",
        "fps_num", "fps_den", "frames_per_beat", "beats_per_loop", "anchor",
    }
    missing = required.difference(manifest)
    if missing:
        raise SourceError(f"manifest is missing {', '.join(sorted(missing))}")
    extra = set(manifest).difference(required | {"sheet", "columns", "rows"})
    if extra:
        raise SourceError(f"manifest has unknown fields: {', '.join(sorted(extra))}")


def _positive(value: object, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise SourceError(f"{label} must be a positive integer")
    return value


def _whole(value: object, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise SourceError(f"{label} must be a non-negative integer")
    return value


def _frames(directory: Path, manifest: dict, width: int, height: int, count: int) -> tuple[Image.Image, ...]:
    layout = manifest["layout"]
    if layout == "sequence":
        paths = sorted(
            path for path in directory.glob("*.png") if not path.name.startswith(".")
        )
        if len(paths) != count:
            raise SourceError("png count does not match frame_count")
        for path in paths:
            log.debug("%s", path.name)
        images = tuple(_open(path, width, height) for path in paths)
    elif layout == "sheet":
        columns = manifest.get("columns")
        rows = manifest.get("rows")
        if isinstance(columns, bool) or isinstance(rows, bool):
            raise SourceError("a sheet needs integer columns and rows")
        if not isinstance(columns, int) or not isinstance(rows, int) or columns < 1 or rows < 1:
            raise SourceError("a sheet needs integer columns and rows")
        sheet_path = directory / str(manifest.get("sheet", "sheet.png"))
        log.debug("%s", sheet_path.name)
        sheet = _open_unbounded(sheet_path)
        if columns * rows != count or sheet.width != columns * width or sheet.height != rows * height:
            raise SourceError("sheet geometry does not match the frames")
        images_list = []
        for row in range(rows):
            for column in range(columns):
                box = (column * width, row * height, (column + 1) * width, (row + 1) * height)
                images_list.append(sheet.crop(box))
        images = tuple(images_list)
    else:
        raise SourceError("layout must be sequence or sheet")
    return images


def _open(path: Path, width: int, height: int) -> Image.Image:
    image = _open_unbounded(path)
    if image.width != width or image.height != height:
        raise SourceError(f"{path.name} is not {width}x{height}")
    return image


def _open_unbounded(path: Path) -> Image.Image:
    try:
        return Image.open(path).convert("RGBA")
    except (OSError, ValueError) as error:
        raise SourceError(f"cannot read {path.name}") from error


def _rgba(image: Image.Image) -> list[tuple[int, int, int, int]]:
    raw = image.convert("RGBA").tobytes()
    return [
        (raw[offset], raw[offset + 1], raw[offset + 2], raw[offset + 3])
        for offset in range(0, len(raw), 4)
    ]
