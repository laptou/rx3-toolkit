#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Static guards for the sprite module."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[4]
MODULE = (Path(__file__).resolve().parent / "module.sh").read_text()
FEATURE = (Path(__file__).resolve().parent / "rx3_sprites_feature.h").read_text()
HOOK = (ROOT / "mod/modules/core/1.19/rx3_core_hook.c").read_text()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require(
    "register_prepare_hook sprites_prepare" in MODULE
    and 'export RX3_SPRITES_DIR="$SPRITES_LINK"' in MODULE
    and "RX3_SPRITE_NONCE" in MODULE
    and "export RX3_SPRITE_TRACE=1" in MODULE
    and 'trace_path="$USB/RX3_RUNTIME/sprite-trace.log"' in MODULE
    and 'status_path="$USB/RX3_RUNTIME/sprite-status.log"' in MODULE
    and "BOOT_TAG" not in MODULE
    and 'export RX3_SPRITE_TRACE_PATH="$trace_path"' in MODULE
    and "anchors.proof" in MODULE
    and "rbp_environment_value RX3_SPRITE_TRACE" in MODULE
    and "request_rbp_restart" in MODULE
    and 'export RX3_SPRITE_STATUS_PATH="$status_path"' in MODULE
    and "Sprites need a restart so the overlay can draw" in MODULE
    and "register_rollback_hook" not in MODULE,
    "the module must publish a stable sprite directory during prepare",
)
require(
    "return 0" in MODULE and "[ -d \"$USB/RX3_SPRITES\" ]" in MODULE,
    "a stick without RX3_SPRITES must still let the other modules run",
)
require(
    '#include "../../sprites/1.19/rx3_sprites_feature.h"' in HOOK
    and "sprites_feature_after_image" in HOOK
    and "EXTENDED_IMAGE_COUNT 0x1704u" in HOOK,
    "the performance core must own the sprite callbacks and the widened image ids",
)
require(
    "SPRITE_ID_FIRST 0x1604u" in FEATURE
    and "SPRITE_PROBE_ID 0x1703u" in FEATURE
    and "DirectFB" not in FEATURE
    and "dlsym" not in FEATURE,
    "sprite drawing must stay inside PAL_DRAW_IMAGE and the private id pool",
)
require(
    "RX3_SPRITE_TRACE_PATH" in FEATURE
    and "sprite_trace_file" in FEATURE
    and "SPRITE_WAVE_MIN_W 200" in FEATURE
    and "span_x < SPRITE_WAVE_MIN_W" in FEATURE
    and "SPRITE_WAVE_MIN_H" in FEATURE,
    "the draw trace must follow the path on the stick and keep waveform-sized boxes",
)
require(
    "a box under 40px on either edge is a glyph" in FEATURE
    and "(int)x2 - (int)x1 < SPRITE_WAVE_MIN_H" in FEATURE
    and "sprite_pick_state" in FEATURE
    and "sprite waiting for a track" in FEATURE
    and "RX3_SPRITE_STATUS_PATH" in FEATURE,
    "a loaded track on either deck must draw, and the strip can be shorter than the frame",
)
require(
    "sprite_consider" not in FEATURE
    and "sprite_hold" not in FEATURE
    and "file_deck = sprite_file_deck(window_layer, image_id);\n    if (file_deck < 0)\n        return;"
    in FEATURE,
    "the sprite must draw only on the anchor image's own draw; a box learned "
    "from the screen put it on the browse page",
)
require(
    "record[0x18u] = 1u; /* rgb565, as the custom tabs */" in FEATURE
    and "record[0x19u] = 0u;" in FEATURE
    and "sprite_expand(job, &expanded_bytes)" in FEATURE,
    "record format 2 reads index bytes as rgb565 pixels; the player must "
    "expand packs to the tab format",
)
require(
    "values[2] != 8u" in FEATURE
    and "format 2, palette, 8 bpp" in FEATURE,
    "the player must accept only the 8-bit indexed pack encoding",
)
require(
    "sprites disabled: hard-mask format is not proven" in FEATURE
    and "sprites disabled: waveform anchor is not proven" in FEATURE
    and "sprites disabled: pack failed validation" in FEATURE,
    "an unproven format, anchor, or pack must disable the overlay",
)
