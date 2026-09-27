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
    "values[2] != 8u" in FEATURE
    and "format 2, palette, 8 bpp" in FEATURE,
    "the player must accept only the measured 8-bit hard-mask",
)
require(
    "sprites disabled: hard-mask format is not proven" in FEATURE
    and "sprites disabled: waveform anchor is not proven" in FEATURE
    and "sprites disabled: pack failed validation" in FEATURE,
    "an unproven format, anchor, or pack must disable the overlay",
)
