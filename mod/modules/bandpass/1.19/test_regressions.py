#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Static guards for the Band-pass module, all of them measured facts."""

from pathlib import Path
import json

ROOT = Path(__file__).resolve().parent
MODULE = (ROOT / "module.sh").read_text()
MANIFEST = json.loads((ROOT / "manifest.json").read_text())
FEATURE = (ROOT / "rx3_bandpass_feature.h").read_text()
DSP = (ROOT / "rx3_bandpass_dsp.h").read_text()
HOOK = (ROOT.parents[1] / "core/1.19/rx3_core_hook.c").read_text()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require(
    "module_begin bandpass bandpass" in MODULE
    and MANIFEST["runtime_directory"] == "bandpass",
    "the orchestrator loads this module under the id and namespace it declares",
)
require(
    '[ -r "$CORE_OBJECT" ]' in MODULE and 'getenv("RX3_BANDPASS")' in FEATURE,
    "the module declines without the performance core and gates on its flag",
)
require(
    "#define COLOR_FX_OPERATE ((unsigned long)0x000bbc68)" in FEATURE
    and "0xf8, 0x45, 0x2d, 0xe9, 0x02, 0x8b, 0x2d, 0xed" in FEATURE
    and "*(const int *)(fields + 0x24u) != COLOR_FX_DUB_ECHO" in FEATURE,
    "the hook sits on SoundColorFxManager::operate and replaces only the "
    "desired DUB ECHO type; every other color fx runs stock",
)
require(
    '#include "../../bandpass/1.19/rx3_bandpass_feature.h"' in HOOK
    and '.name = "bandpass",' in HOOK,
    "the core must compile and register the feature",
)
require(
    "#include <math.h>" not in FEATURE and "#include <math.h>" not in DSP,
    "the hook links no libm; tan must stay a series",
)
require(
    "band.gain *" in FEATURE and "band.gain = BP_Q_FLAT / band.q;" in DSP,
    "the resonant band must be turned down so it never plays louder than dry",
)

print("Band-pass regression guards: OK")
