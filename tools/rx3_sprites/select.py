# SPDX-License-Identifier: MPL-2.0
"""Deterministic per-deck pack choice. Catalog order is sorted, so the result is testable."""

from __future__ import annotations

from tools.rx3_sprites.checksum import checksum


def mix(value: int) -> int:
    value &= 0xFFFFFFFF
    value ^= (value << 13) & 0xFFFFFFFF
    value ^= value >> 17
    value ^= (value << 5) & 0xFFFFFFFF
    return value


def choose(
    names: list[str],
    identity: str,
    deck: int,
    nonce: int,
    previous: str | None,
) -> str:
    if not names:
        raise ValueError("no sprite pack is eligible")
    if deck not in (0, 1):
        raise ValueError("deck must be 0 or 1")
    mixed = mix(checksum(identity.encode("utf-8")) ^ (deck * 0x9E3779B9) ^ nonce)
    index = mixed % len(names)
    if len(names) > 1 and names[index] == previous:
        index = (index + 1) % len(names)
    return names[index]
