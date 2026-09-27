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
    "#define COLOR_FX_OPERATE ((unsigned long)0x000bbc68)" in FEATURE
    and "0xf8, 0x45, 0x2d, 0xe9, 0x02, 0x8b, 0x2d, 0xed" in FEATURE,
    "the hook sits on SoundColorFxManager::operate, guarded by its stmdb/vpush prologue",
)
require(
    "#define COLOR_FX_DUB_ECHO 4" in FEATURE
    and "*(const int *)(fields + 0x24u) != COLOR_FX_DUB_ECHO" in FEATURE,
    "only the desired DUB ECHO type (manager +0x24 == 4) is replaced; every "
    "other color fx must run stock",
)
require(
    "(fields + 0x84u)" in FEATURE and "(channel + 0x14u)" in FEATURE,
    "the channel comes from the manager's MixerChannel back-pointer, not a guess",
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
    "sidechain_feature_after_image\n" in HOOK
    and "sc_meter_tick();" in FEATURE,
    "the meter is drawn from rbp's ui thread through the after_image slot, "
    "never from the audio thread",
)
require(
    "rx3_gr_" not in FEATURE.split("static void sc_process")[1].split(
        "static int sidechain_feature_configured")[0],
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
