# SPDX-License-Identifier: MPL-2.0
"""`.rx3spr` container. The player rejects a file that does not round-trip exactly."""

from __future__ import annotations

import struct
from dataclasses import dataclass

from tools.rx3_sprites.checksum import checksum
from tools.rx3_sprites.limits import (
    ANCHOR_BOTTOM,
    ANCHOR_CENTER,
    HEADER_SIZE,
    MAX_EDGE,
    MAX_FRAMES,
    MAX_NAME,
    MEMORY_CEILING,
    MODE_BEAT,
    MODE_FPS,
    PACK_VERSION,
)
from tools.rx3_sprites.proof import FormatProof


MAGIC = b"RX3SPR1\0"
STRUCT = struct.Struct("<8s11H4B2H2I2H")
PAD = HEADER_SIZE - STRUCT.size


class PackError(ValueError):
    """The pack is truncated, inconsistent, or over the memory ceiling."""


@dataclass(frozen=True)
class Pack:
    name: str
    width: int
    height: int
    mode: int
    fps_num: int
    fps_den: int
    frames_per_beat: int
    beats_per_loop: int
    anchor: int
    proof: FormatProof
    palette: bytes
    frames: tuple[bytes, ...]

    @property
    def frame_count(self) -> int:
        return len(self.frames)


def encode(pack: Pack) -> bytes:
    _validate(pack)
    name = pack.name.encode("utf-8")
    body = name + pack.palette + b"".join(pack.frames)
    if len(body) > MEMORY_CEILING:
        raise PackError("sprite pack exceeds the memory ceiling")
    header = STRUCT.pack(
        MAGIC,
        PACK_VERSION,
        HEADER_SIZE,
        pack.width,
        pack.height,
        pack.frame_count,
        pack.mode,
        pack.fps_num,
        pack.fps_den,
        pack.frames_per_beat,
        pack.beats_per_loop,
        pack.anchor,
        pack.proof.format_byte,
        pack.proof.palette,
        pack.proof.transparent,
        pack.proof.bpp,
        len(pack.palette) // pack.proof.entry_size,
        pack.proof.entry_size,
        len(body),
        0,
        len(name),
        0,
    ) + bytes(PAD)
    digest = checksum(header + body)
    header = STRUCT.pack(
        MAGIC,
        PACK_VERSION,
        HEADER_SIZE,
        pack.width,
        pack.height,
        pack.frame_count,
        pack.mode,
        pack.fps_num,
        pack.fps_den,
        pack.frames_per_beat,
        pack.beats_per_loop,
        pack.anchor,
        pack.proof.format_byte,
        pack.proof.palette,
        pack.proof.transparent,
        pack.proof.bpp,
        len(pack.palette) // pack.proof.entry_size,
        pack.proof.entry_size,
        len(body),
        digest,
        len(name),
        0,
    ) + bytes(PAD)
    return header + body


def decode(data: bytes) -> Pack:
    if len(data) < HEADER_SIZE:
        raise PackError("sprite pack is shorter than its header")
    fields = STRUCT.unpack_from(data)
    (
        magic, version, header_size, width, height, frame_count, mode,
        fps_num, fps_den, frames_per_beat, beats_per_loop, anchor,
        format_byte, palette_flag, transparent, bpp, palette_count, entry_size,
        payload_length, digest, name_length, _reserved,
    ) = fields
    if magic != MAGIC or version != PACK_VERSION or header_size != HEADER_SIZE:
        raise PackError("unsupported sprite pack header")
    if len(data) != HEADER_SIZE + payload_length:
        raise PackError("sprite pack length does not match its header")
    if data[STRUCT.size:HEADER_SIZE] != bytes(PAD):
        raise PackError("sprite pack header padding is not zero")
    zeroed = bytearray(data[:HEADER_SIZE])
    STRUCT.pack_into(
        zeroed, 0, magic, version, header_size, width, height, frame_count, mode,
        fps_num, fps_den, frames_per_beat, beats_per_loop, anchor, format_byte,
        palette_flag, transparent, bpp, palette_count, entry_size, payload_length,
        0, name_length, 0,
    )
    if checksum(bytes(zeroed) + data[HEADER_SIZE:]) != digest:
        raise PackError("sprite pack checksum does not match")
    body = data[HEADER_SIZE:]
    if name_length > len(body) or name_length > MAX_NAME:
        raise PackError("sprite name does not fit")
    name = body[:name_length]
    try:
        text = name.decode("utf-8")
    except UnicodeError as error:
        raise PackError("sprite name is not utf-8") from error
    palette_bytes = palette_count * entry_size
    if name_length + palette_bytes > len(body):
        raise PackError("sprite palette does not fit")
    palette = body[name_length:name_length + palette_bytes]
    pixels = body[name_length + palette_bytes:]
    frame_bytes = _frame_bytes(width, height, bpp)
    if frame_count < 1 or frame_bytes < 1 or len(pixels) != frame_count * frame_bytes:
        raise PackError("sprite pixels do not match the frame geometry")
    frames = tuple(
        pixels[index * frame_bytes:(index + 1) * frame_bytes]
        for index in range(frame_count)
    )
    proof = FormatProof(format_byte, palette_flag, bpp, transparent, entry_size)
    pack = Pack(
        name=text, width=width, height=height, mode=mode, fps_num=fps_num,
        fps_den=fps_den, frames_per_beat=frames_per_beat,
        beats_per_loop=beats_per_loop, anchor=anchor, proof=proof,
        palette=palette, frames=frames,
    )
    _validate(pack)
    _check_indexes(pack)
    return pack


def _frame_bytes(width: int, height: int, bpp: int) -> int:
    count = width * height
    if bpp == 8:
        return count
    return (count + 1) // 2


def _check_indexes(pack: Pack) -> None:
    limit = len(pack.palette) // pack.proof.entry_size
    for frame in pack.frames:
        if pack.proof.bpp == 8:
            indexes = frame
        else:
            indexes = []
            for byte in frame:
                indexes.append(byte >> 4)
                indexes.append(byte & 0x0F)
            indexes = indexes[:pack.width * pack.height]
        if any(index >= limit for index in indexes):
            raise PackError("sprite pixel index is outside the palette")


def _validate(pack: Pack) -> None:
    if not pack.name or len(pack.name.encode("utf-8")) > MAX_NAME:
        raise PackError("sprite name is empty or too long")
    if not 1 <= pack.width <= MAX_EDGE or not 1 <= pack.height <= MAX_EDGE:
        raise PackError("sprite dimensions exceed 64")
    if not 1 <= pack.frame_count <= MAX_FRAMES:
        raise PackError("sprite frame count exceeds 16")
    if pack.anchor not in (ANCHOR_CENTER, ANCHOR_BOTTOM):
        raise PackError("unknown sprite anchor")
    if pack.proof.palette != 1 or pack.proof.entry_size != 2 or pack.proof.transparent != 0:
        raise PackError("sprite palette does not match the hard-mask rules")
    if pack.proof.bpp not in (4, 8):
        raise PackError("sprite bpp must be 4 or 8")
    if len(pack.palette) % 2 or len(pack.palette) < 2:
        raise PackError("sprite palette is incomplete")
    expected = _frame_bytes(pack.width, pack.height, pack.proof.bpp)
    if any(len(frame) != expected for frame in pack.frames):
        raise PackError("sprite frames differ in size")
    if pack.mode == MODE_FPS:
        if pack.fps_num <= 0 or pack.fps_den <= 0:
            raise PackError("fixed-rate sprites need a positive rate")
        if pack.frames_per_beat or pack.beats_per_loop:
            raise PackError("fixed-rate sprites do not carry beat timing")
    elif pack.mode == MODE_BEAT:
        if pack.fps_num or pack.fps_den:
            raise PackError("beat sprites do not carry a fixed rate")
        if pack.frames_per_beat <= 0 or pack.beats_per_loop <= 0:
            raise PackError("beat sprites need frames per beat and beats per loop")
        if pack.frames_per_beat * pack.beats_per_loop != pack.frame_count:
            raise PackError("beat loop does not cover every frame once")
    else:
        raise PackError("unknown sprite mode")
