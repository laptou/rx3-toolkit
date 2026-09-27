#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Static guards for the sprite module."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[4]
MODULE = (Path(__file__).resolve().parent / "module.sh").read_text()
FEATURE = (Path(__file__).resolve().parent / "rx3_sprites_feature.h").read_text()
GR = (ROOT / "mod/modules/core/1.19/rx3_ds_gr.h").read_text()
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
    "WAVE_WINDOW_ID ((unsigned long)0x02498d44)" in GR
    and "DS_GR_CREATE_WINDOW ((unsigned long)0x001a2634)" in GR
    and "DS_GR_LOCK_WINDOW ((unsigned long)0x001a1c48)" in GR
    and '#include "../../core/1.19/rx3_ds_gr.h"' in FEATURE
    and "rx3_gr_open(&window->gr" in FEATURE
    and "original_draw_image" not in FEATURE
    and "IMAGE_TABLE_POINTER" not in FEATURE
    and "DirectFB" not in FEATURE
    and "dlsym" not in FEATURE,
    "rbp's waveform window covers every ns window; the sprite must draw into "
    "its own ds_gr window, not through pal_draw_image",
)
require(
    "static const uint32_t rx3_gr_z_slots[] = { 0x3eu, 0x3du, 0x3cu, 0x3bu };"
    in GR
    and "result == DS_GR_Z_TAKEN" in GR,
    "the overlay z must sit above the waveform (0x4f) and skip taken slots",
)
require(
    "screen_up = rx3_wave_screen_up();" in FEATURE
    and "state->live && state->pixels && rx3_wave_deck_loaded(deck)" in FEATURE
    and "int lead = screen_up ? sprite_pick_lead() : -1;" in FEATURE
    and "sprite_window_close(window);" in FEATURE,
    "the overlay must close whenever the waveform screen or every deck's "
    "waveform is gone, or it floats over the browse page",
)
require(
    "static struct sprite_window sprite_window =" in FEATURE
    and "sprite_windows[" not in FEATURE
    and "*y = WAVE_TOP + WAVE_STRIP - (int)*height / 2;" in FEATURE,
    "one sprite window, centred on the whole waveform view, not one per deck",
)
require(
    "#define SPRITE_EDGE_LIMIT 256u" in FEATURE
    and "#define SPRITE_FRAMES_LIMIT 256u" in FEATURE
    and "#define SPRITE_FILE_LIMIT (4u * 1024u * 1024u)" in FEATURE
    and "#define SPRITE_BASE_HEIGHT ((WAVE_STRIP * 9) / 10)" in FEATURE,
    "the player's pack limits must match the host tools, and the sprite is "
    "scaled to 0.9 of a waveform strip",
)
require(
    "sprite_expand" not in FEATURE
    and "state->palette[source_row[along >> 16]]" in FEATURE,
    "frames stay 8-bit and are scaled as drawn; expanding every frame of a "
    "256-frame 256x256 pack to rgb565 up front would cost 32 MiB",
)
require(
    "DS_GR_KEY_ARGB 0xffff00ffu" in GR and "DS_GR_KEY_ARGB);" in GR,
    "index 0 must be keyed out by the window's colour key",
)
require(
    "header->format_byte != 2u" in FEATURE
    and "header->bpp != 8u" in FEATURE
    and "format.proof" not in FEATURE
    and "sprites disabled: pack failed validation" in FEATURE,
    "the player must accept only the 8-bit indexed pack encoding",
)
