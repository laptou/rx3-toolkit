# SPDX-License-Identifier: MPL-2.0
"""Operator proof files. Missing or ambiguous proof keeps sprites off."""

from __future__ import annotations

from dataclasses import dataclass


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
    if proof.palette != 1:
        raise ProofError("hard-edge sprites require a palette")
    if proof.bpp not in (4, 8):
        raise ProofError("bpp must be 4 or 8")
    if not 0 <= proof.format_byte <= 255:
        raise ProofError("format byte is out of range")
    if not 0 <= proof.transparent <= 255:
        raise ProofError("transparent index is out of range")
    if proof.entry_size != 2:
        raise ProofError("palette entries must be 16-bit colour")
    if proof.transparent != 0:
        raise ProofError("transparent index must be 0")
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
