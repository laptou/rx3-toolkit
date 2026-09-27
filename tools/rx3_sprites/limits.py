# SPDX-License-Identifier: MPL-2.0
"""Bounds shared by the host compilers and the firmware-1.19 player."""

MAX_EDGE = 64
MAX_FRAMES = 16
MAX_PACKS = 64
MAX_NAME = 44
# two resident packs, plus a few reloads, stay far under the player's stem budget
MEMORY_CEILING = 2 * 1024 * 1024
HEADER_SIZE = 64
ALPHA_CUTOFF = 128
SCHEMA = 1
PACK_VERSION = 1
MODE_FPS = 1
MODE_BEAT = 2
ANCHOR_CENTER = 1
ANCHOR_BOTTOM = 2
