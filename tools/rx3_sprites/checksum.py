# SPDX-License-Identifier: MPL-2.0
"""32-bit ISO-HDLC checksum. The player implements the same polynomial."""

from __future__ import annotations

import zlib


def checksum(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF
