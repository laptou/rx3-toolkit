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
    and 'status_path="$USB/RX3_RUNTIME/sprite-status.log"' in MODULE
    and "BOOT_TAG" not in MODULE
    and "RX3_SPRITE_TRACE" not in MODULE
    and "anchors.proof" not in MODULE
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
    and "sprite_overlay_draw" not in HOOK,
    "the performance core must own the sprite callbacks",
)
require(
    "WAVE_WINDOW_ID ((unsigned long)0x02498d44)" in FEATURE
    and "DS_GR_CREATE_WINDOW ((unsigned long)0x001a2634)" in FEATURE
    and "DS_GR_LOCK_WINDOW ((unsigned long)0x001a1c48)" in FEATURE
    and "original_draw_image" not in FEATURE
    and "IMAGE_TABLE_POINTER" not in FEATURE
    and "DirectFB" not in FEATURE
    and "dlsym" not in FEATURE,
    "rbp's waveform window covers every ns window; the sprite must draw into "
    "its own ds_gr window, not through pal_draw_image",
)
require(
    "static const uint32_t sprite_z_slots[] = { 0x3eu, 0x3du, 0x3cu, 0x3bu };"
    in FEATURE
    and "result == DS_GR_Z_TAKEN" in FEATURE,
    "the overlay z must sit above the waveform (0x4f) and skip taken slots",
)
require(
    "screen_up = *(void *volatile *)WAVE_WINDOW_ID != 0;" in FEATURE
    and "!sprite_deck_has_wave(deck)" in FEATURE
    and "sprite_window_close(window);" in FEATURE,
    "the overlay must close whenever the waveform screen or the deck's "
    "waveform is gone, or it floats over the browse page",
)
require(
    "SPRITE_KEY_ARGB 0xffff00ffu" in FEATURE
    and "SPRITE_KEY_ARGB);" in FEATURE,
    "index 0 must be keyed out by the window's colour key",
)
require(
    "header->format_byte != 2u" in FEATURE
    and "header->bpp != 8u" in FEATURE
    and "format.proof" not in FEATURE
    and "sprites disabled: pack failed validation" in FEATURE,
    "the player must accept only the 8-bit indexed pack encoding",
)
