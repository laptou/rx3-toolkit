#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Static guards for the shared TRANS sprite."""

from pathlib import Path
import json

ROOT = Path(__file__).resolve().parent
MODULE = (ROOT / "module.sh").read_text()
MANIFEST = json.loads((ROOT / "manifest.json").read_text())
FEATURE = (ROOT / "rx3_spritefx_feature.h").read_text()
HOOK = (ROOT.parents[1] / "core/1.19/rx3_core_hook.c").read_text()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require(
    "module_begin spritefx spritefx" in MODULE
    and MANIFEST["id"] == "spritefx"
    and MANIFEST["selectable"] is False
    and MANIFEST["order"] == 108
    and MANIFEST["requires"] == ["core"]
    and MANIFEST["conflicts"] == [],
    "sprite fx is the shared TRANS sprite, pulled in by a named module",
)
require(
    'getenv("RX3_SPRITE_FX")' in FEATURE
    and 'getenv("RX3_SPRITE_FX_PACK")' in FEATURE
    and 'getenv("RX3_SPRITE_FX_NAME")' in FEATURE
    and "sfx_compose_label(sfx_name);" in FEATURE,
    "the pack path and the beat-fx name come from the selected module",
)
require(
    "#define BEAT_FX_TRANS 2" in FEATURE
    and "#define BEAT_FX_UI_TRANS 9" in FEATURE
    and "#define BEAT_FX_DISPLAY_TRANS 3" in FEATURE
    and "rx3_beat_fx_owned(fields, BEAT_FX_TRANS)" in FEATURE
    and "rx3_beat_fx_on(fields, BEAT_FX_TRANS)" in FEATURE
    and "(fields + 0xe8u)" in FEATURE
    and "(fields + 0x00u)" in FEATURE,
    "TRANS is engine type 2, ui type 9, display 3; depth is +0xe8 and "
    "channel is +0x00",
)
require(
    "TRANS_CAPTION_FIRST 0x1469u" in FEATURE
    and "TRANS_CAPTION_LAST 0x146cu" in FEATURE
    and "TRANS_SELECT_FIRST 0x15c2u" in FEATURE
    and "TRANS_SELECT_LAST 0x15c4u" in FEATURE
    and "rx3_relabel_image(image_id, &sfx_label)" in FEATURE
    and "SFX_LABEL_CHARS 16u" in FEATURE,
    "the seven TRANS images take the composed name",
)
require(
    "*y = WAVE_TOP + (WAVE_STRIP - height) / 2;" in FEATURE
    and "*y = WAVE_TOP + WAVE_STRIP + (WAVE_STRIP - height) / 2;" in FEATURE
    and "*y = WAVE_TOP + WAVE_STRIP - height / 2;" in FEATURE
    and "SFX_BASE_HEIGHT ((WAVE_STRIP * 9) / 10)" in FEATURE
    and "RX3_GR_ON_TOP" in FEATURE,
    "channel 0 is the top strip, channel 1 the bottom, anything else the seam",
)
require(
    "buffer[" not in FEATURE
    and "(void)buffer;" in FEATURE
    and "return 1;" in FEATURE,
    "while TRANS is owned the block is left untouched",
)
require(
    "rx3_sprite_fps_frame" in FEATURE
    and "rx3_sprite_checksum" in FEATURE
    and "header->format_byte != 2u" in FEATURE
    and "header->bpp != 8u" in FEATURE
    and "SFX_EDGE_LIMIT 256u" in FEATURE
    and "SFX_FILE_LIMIT (4u * 1024u * 1024u)" in FEATURE
    and "header->anchor != 1u && header->anchor != 2u" in FEATURE,
    "the pack is the sprites encoding, animated on its fps clock",
)
require(
    "#define RUNTIME_FEATURE_COUNT 7u" in HOOK
    and '#include "../../spritefx/1.19/rx3_spritefx_feature.h"' in HOOK
    and ".beat_fx_operate = spritefx_feature_beat_fx_operate," in HOOK
    and ".image_swap = spritefx_feature_image_swap," in HOOK
    and ".after_image = spritefx_feature_after_image," in HOOK
    and "spritefx_feature_beat_fx_beat" not in HOOK
    and "spritefx_feature_beat_fx_time" not in HOOK,
    "the performance core owns one TRANS sprite, and BEAT and TIME stay stock",
)
