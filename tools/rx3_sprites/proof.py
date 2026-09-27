# SPDX-License-Identifier: MPL-2.0
"""Operator proof files. Missing or ambiguous proof keeps sprites off."""

from __future__ import annotations

import logging
from dataclasses import dataclass


log = logging.getLogger("rx3.sprites")


class ProofError(ValueError):
    """The proof file is absent, malformed, or contradicts itself."""


@dataclass(frozen=True)
class FormatProof:
    format_byte: int
    palette: int
    bpp: int
    transparent: int
    entry_size: int

    def text(self) -> str:
        return (
            "RX3FMT1\n"
            f"format {self.format_byte}\n"
            f"palette {self.palette}\n"
            f"bpp {self.bpp}\n"
            f"transparent {self.transparent}\n"
            f"entry {self.entry_size}\n"
        )


def proven_format() -> FormatProof:
    """The pack encoding. The player expands it to RGB565 when it loads a pack.

    format 2, a palette, 8 bits per pixel, transparent index 0,
    16-bit colour entries.
    """
    return FormatProof(format_byte=2, palette=1, bpp=8, transparent=0, entry_size=2)


@dataclass(frozen=True)
class Anchor:
    layer: int
    image_id: int
    deck: int


def parse_format(text: str) -> FormatProof:
    lines = _lines(text)
    if not lines or lines[0] != "RX3FMT1":
        raise ProofError("format proof must start with RX3FMT1")
    fields = _fields(lines[1:], ("format", "palette", "bpp", "transparent", "entry"))
    proof = FormatProof(
        format_byte=fields["format"],
        palette=fields["palette"],
        bpp=fields["bpp"],
        transparent=fields["transparent"],
        entry_size=fields["entry"],
    )
    if proof != proven_format():
        raise ProofError(
            "format proof must be the pack encoding: format 2, palette 1, "
            "8 bpp, transparent 0, entry 2"
        )
    return proof


def parse_anchors(text: str) -> tuple[Anchor, ...]:
    lines = _lines(text)
    if not lines or lines[0] != "RX3ANC1":
        raise ProofError("anchor proof must start with RX3ANC1")
    if len(lines) > 17:
        raise ProofError("too many anchor records")
    found: list[Anchor] = []
    seen: set[tuple[int, int]] = set()
    for line in lines[1:]:
        parts = line.split()
        if len(parts) != 6 or parts[0] != "layer" or parts[2] != "id" or parts[4] != "deck":
            raise ProofError("anchor line must be: layer N id N deck N")
        layer, image_id, deck = (int(parts[1]), int(parts[3]), int(parts[5]))
        if layer < 0 or layer > 0xFFFF or image_id < 0 or image_id > 0xFFFFFFFF:
            raise ProofError("anchor value is out of range")
        if deck not in (0, 1):
            raise ProofError("anchor deck must be 0 or 1")
        key = (layer, image_id)
        if key in seen:
            raise ProofError("anchor layer and image id are ambiguous")
        seen.add(key)
        found.append(Anchor(layer, image_id, deck))
    if not found:
        raise ProofError("anchor proof has no records")
    return tuple(found)


def anchors_text(records: tuple[Anchor, ...]) -> str:
    body = "".join(
        f"layer {item.layer} id {item.image_id} deck {item.deck}\n" for item in records
    )
    return "RX3ANC1\n" + body


# the player screen is 1280x720. the key badge uses the same split.
# upper left is deck 0 in both layouts. lower right is deck 1 in both.
# the other two corners swap decks when the layout changes, so they are unused.
SCREEN_WIDTH = 1280
SCREEN_HEIGHT = 720
WAVEFORM_MIN_WIDTH = 200
WAVEFORM_MIN_HEIGHT = 40


def anchors_from_trace(text: str) -> tuple[Anchor, ...]:
    """Pick one waveform image per deck from a sprite-trace.log.

    A layer and image is kept only when every sample is at least 200 by 40
    and every sample sits in the same stable corner. The largest image on
    a deck wins. Two images of the same size leave that deck blank.
    """
    grouped: dict[tuple[int, int], list[tuple[int, int, int, int]]] = {}
    for line in text.replace("\r", "").split("\n"):
        parts = line.split()
        if len(parts) != 6:
            continue
        try:
            layer, image_id, x1, y1, x2, y2 = (int(part) for part in parts)
        except ValueError:
            continue
        if (
            layer < 0
            or layer > 0xFFFF
            or image_id <= 0
            or image_id >= 0xFFFFFFFF
            or x2 <= x1
            or y2 <= y1
        ):
            continue
        width = x2 - x1
        height = y2 - y1
        if width < WAVEFORM_MIN_WIDTH or height < WAVEFORM_MIN_HEIGHT:
            continue
        grouped.setdefault((layer, image_id), []).append((x1, y1, x2, y2))
    candidates: dict[int, list[tuple[int, Anchor]]] = {0: [], 1: []}
    for (layer, image_id), boxes in grouped.items():
        corners = {_corner(*box) for box in boxes}
        if corners == {0}:
            deck = 0
        elif corners == {3}:
            deck = 1
        else:
            continue
        area = max((x2 - x1) * (y2 - y1) for x1, y1, x2, y2 in boxes)
        candidates[deck].append((area, Anchor(layer, image_id, deck)))
    chosen: list[Anchor] = []
    for deck in (0, 1):
        rows = candidates[deck]
        if not rows:
            continue
        top = max(area for area, _item in rows)
        winners = [item for area, item in rows if area == top]
        if len(winners) != 1:
            log.warning(
                "sprite trace deck %s has two waveforms of the same size; that deck draws nothing",
                deck,
            )
            continue
        chosen.append(winners[0])
    return tuple(chosen)


def trace_fit_edge(text: str) -> int | None:
    """Shortest side of the waveforms anchors_from_trace kept."""
    chosen = {(item.layer, item.image_id) for item in anchors_from_trace(text)}
    if not chosen:
        return None
    edge: int | None = None
    for line in text.replace("\r", "").split("\n"):
        parts = line.split()
        if len(parts) != 6:
            continue
        try:
            layer, image_id, x1, y1, x2, y2 = (int(part) for part in parts)
        except ValueError:
            continue
        if (layer, image_id) not in chosen or x2 <= x1 or y2 <= y1:
            continue
        span = min(x2 - x1, y2 - y1)
        edge = span if edge is None else min(edge, span)
    return edge


def _corner(x1: int, y1: int, x2: int, y2: int) -> int:
    center_x = (x1 + x2) // 2
    center_y = (y1 + y2) // 2
    right = 1 if center_x >= SCREEN_WIDTH // 2 else 0
    bottom = 1 if center_y >= SCREEN_HEIGHT // 2 else 0
    return (bottom << 1) | right


def _lines(text: str) -> list[str]:
    if "\r" in text:
        raise ProofError("proof files use unix line endings")
    return [line.strip() for line in text.split("\n") if line.strip()]


def _fields(lines: list[str], names: tuple[str, ...]) -> dict[str, int]:
    if len(lines) != len(names):
        raise ProofError("format proof has the wrong number of fields")
    values: dict[str, int] = {}
    for line, name in zip(lines, names):
        parts = line.split()
        if len(parts) != 2 or parts[0] != name:
            raise ProofError(f"format proof is missing {name}")
        try:
            values[name] = int(parts[1])
        except ValueError as error:
            raise ProofError(f"{name} is not an integer") from error
    return values
