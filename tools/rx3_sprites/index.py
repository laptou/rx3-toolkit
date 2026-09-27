# SPDX-License-Identifier: MPL-2.0
"""`tracks.rx3idx`: one beat grid per exported track name."""

from __future__ import annotations

import struct

from tools.rx3_sprites.beatgrid import TrackGrid
from tools.rx3_sprites.checksum import checksum
from tools.rx3_sprites.limits import INDEX_HEADER_SIZE, INDEX_VERSION, MAX_NAME
from tools.rx3_sprites.timing import Segment


MAGIC = b"RX3IDX1\0"
HEADER = struct.Struct("<8sHHII12s")


class IndexError(ValueError):
    """The beat index is truncated, colliding, or internally inconsistent."""


def encode_index(grids: tuple[TrackGrid, ...] | list[TrackGrid]) -> bytes:
    seen: set[str] = set()
    body = bytearray()
    for grid in grids:
        identity = grid.identity.encode("utf-8")
        if not identity or len(identity) > MAX_NAME:
            raise IndexError("track identity is empty or too long")
        if grid.identity in seen:
            raise IndexError(f"duplicate track identity {grid.identity}")
        seen.add(grid.identity)
        if not grid.segments:
            raise IndexError("track beat grid is empty")
        _check_segments(grid.segments)
        body += struct.pack("<H", len(identity))
        body += identity
        body += struct.pack("<iH", grid.origin_samples, len(grid.segments))
        for segment in grid.segments:
            body += struct.pack("<iI", segment.start_samples, segment.bpm_x1000)
    header = HEADER.pack(MAGIC, INDEX_VERSION, INDEX_HEADER_SIZE, len(grids), 0, bytes(12))
    digest = checksum(header + body)
    header = HEADER.pack(
        MAGIC, INDEX_VERSION, INDEX_HEADER_SIZE, len(grids), digest, bytes(12)
    )
    return header + body


def decode_index(data: bytes) -> tuple[TrackGrid, ...]:
    if len(data) < INDEX_HEADER_SIZE:
        raise IndexError("beat index is shorter than its header")
    magic, version, header_size, count, digest, _pad = HEADER.unpack_from(data)
    if magic != MAGIC or version != INDEX_VERSION or header_size != INDEX_HEADER_SIZE:
        raise IndexError("unsupported beat index header")
    zeroed = bytearray(data[:INDEX_HEADER_SIZE])
    HEADER.pack_into(zeroed, 0, magic, version, header_size, count, 0, bytes(12))
    if checksum(bytes(zeroed) + data[INDEX_HEADER_SIZE:]) != digest:
        raise IndexError("beat index checksum does not match")
    cursor = INDEX_HEADER_SIZE
    grids: list[TrackGrid] = []
    seen: set[str] = set()
    for _ in range(count):
        if cursor + 2 > len(data):
            raise IndexError("beat index ended inside an entry")
        (length,) = struct.unpack_from("<H", data, cursor)
        cursor += 2
        if length < 1 or length > MAX_NAME or cursor + length + 6 > len(data):
            raise IndexError("beat index identity is invalid")
        try:
            identity = data[cursor:cursor + length].decode("utf-8")
        except UnicodeError as error:
            raise IndexError("beat index identity is not utf-8") from error
        cursor += length
        origin, segments_count = struct.unpack_from("<iH", data, cursor)
        cursor += 6
        if segments_count < 1 or cursor + segments_count * 8 > len(data):
            raise IndexError("beat index segments do not fit")
        segments: list[Segment] = []
        for _segment in range(segments_count):
            start, bpm = struct.unpack_from("<iI", data, cursor)
            cursor += 8
            segments.append(Segment(start, bpm))
        if identity in seen:
            raise IndexError(f"duplicate track identity {identity}")
        seen.add(identity)
        if origin != segments[0].start_samples:
            raise IndexError("beat index origin is not the first segment")
        _check_segments(tuple(segments))
        grids.append(TrackGrid(identity, tuple(segments)))
    if cursor != len(data):
        raise IndexError("beat index has trailing bytes")
    return tuple(grids)


def _check_segments(segments: tuple[Segment, ...]) -> None:
    previous = -1
    for segment in segments:
        if segment.start_samples < 0 or segment.start_samples <= previous:
            raise IndexError("beat segments must advance")
        if not 10_000 <= segment.bpm_x1000 <= 400_000:
            raise IndexError("beat tempo is outside 10..400")
        previous = segment.start_samples
