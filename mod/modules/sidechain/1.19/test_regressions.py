#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Static guards for the Sidechain module, all of them measured facts."""

from pathlib import Path
import json

ROOT = Path(__file__).resolve().parent
MODULE = (ROOT / "module.sh").read_text()
MANIFEST = json.loads((ROOT / "manifest.json").read_text())
FEATURE = (ROOT / "rx3_sidechain_feature.h").read_text()
METER = (ROOT / "rx3_sidechain_meter.h").read_text()
HOOK = (ROOT.parents[1] / "core/1.19/rx3_core_hook.c").read_text()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require(
    "module_begin sidechain sidechain" in MODULE,
    "the orchestrator loads this module under the id and namespace it declares",
)
require(
    MANIFEST["runtime_directory"] == "sidechain",
    "the directory written into the image is the one module.sh is read from",
)
require(
    '[ -r "$CORE_OBJECT" ]' in MODULE,
    "the module must decline when the performance core is not selected",
)
require(
    'getenv("RX3_SIDECHAIN")' in FEATURE,
    "the feature must gate on its own environment flag",
)

require(
    "#define OPERATE_BEAT_EFFECT ((unsigned long)0x0008a52c)" in FEATURE
    and "0xf8, 0x45, 0x2d, 0xe9, 0x01, 0x60, 0xa0, 0xe1" in FEATURE
    and "#define BEAT_FX_BEAT_EVENT ((unsigned long)0x0034553c)" in FEATURE
    and "0xe9, 0x30, 0xd0, 0xe5, 0xf0, 0x41, 0x2d, 0xe9" in FEATURE,
    "the hooks sit on BeatEffectManager::operateBeatEffect and "
    "MasterInnards::onEv_BeatFxBeat, guarded by their prologues",
)
require(
    "#define BEAT_FX_HELIX 14" in FEATURE
    and "#define BEAT_FX_UI_HELIX 4" in FEATURE
    and "*(const int *)(manager + 0x4cu) == BEAT_FX_HELIX" in FEATURE
    and "*(const int *)(manager + 0x50u) == BEAT_FX_HELIX" in FEATURE,
    "helix is engine type 14 and ui type 4; only a settled helix is replaced, "
    "so a switch to or from it keeps rbp's crossfade",
)
require(
    "(manager + 0xe8u)" in FEATURE and "(effect + 0x24u)" in FEATURE
    and "(effect + 0x3cu)" in FEATURE,
    "DEPTH comes from the manager's pending value, because the skipped "
    "operateBeatEffect is what copies it onto the effect; TIME and ON are "
    "the effect's own",
)
require(
    "color_fx" not in FEATURE.lower() and "COLOR_FX" not in FEATURE,
    "DUB ECHO belongs to the band-pass module now",
)
require(
    "#include <math.h>" not in FEATURE
    and "#include <math.h>" not in (ROOT / "rx3_sidechain_dsp.h").read_text(),
    "the hook links no libm; log2 and exp2 must stay polynomials",
)

require(
    "rx3_sidechain_meter.h" in MANIFEST["build_files"]
    and '#include "rx3_sidechain_meter.h"' in FEATURE,
    "the meter header must be part of the module's build inputs",
)
require(
    "sidechain_feature_after_image, sidechain_feature_channel_block" in HOOK
    and "sc_meter_tick();" in FEATURE,
    "the meter is drawn from rbp's ui thread through the after_image slot, "
    "never from the audio thread",
)
require(
    "rx3_gr_" not in FEATURE.split("static float sc_block_peak_l2")[1].split(
        "static void sidechain_feature_after_image")[0],
    "the audio path may only fold levels into the meter; ds_gr calls lock "
    "the compositor and belong on the ui thread",
)
require(
    "rx3_wave_screen_up()" in METER
    and "SC_METER_IDLE_MS" in METER
    and "rx3_gr_close(&sc_meter_window);" in FEATURE,
    "the meter must close off the waveform screen, when DUB ECHO stops, and "
    "when the feature is removed",
)

print("Sidechain regression guards: OK")
