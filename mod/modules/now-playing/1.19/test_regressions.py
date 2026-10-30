#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Static guards for the Now playing module, all of them measured facts."""

from pathlib import Path
import json

ROOT = Path(__file__).resolve().parent
MODULE = (ROOT / "module.sh").read_text()
MANIFEST = json.loads((ROOT / "manifest.json").read_text())
FEATURE = (ROOT / "rx3_now_playing_feature.h").read_text()
DECL = (ROOT / "rx3_now_playing_decl.h").read_text()
TAGS = (ROOT / "rx3_now_playing_tags.h").read_text()
HOOK = (ROOT.parents[1] / "core/1.19/rx3_core_hook.c").read_text()
SYMBOLS = (ROOT.parents[3] / "tests/test_hook_symbols.py").read_text()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require(
    "module_begin now-playing now_playing" in MODULE
    and MANIFEST["runtime_directory"] == "now-playing",
    "the orchestrator loads this module under the id and namespace it declares",
)
require(
    '[ -r "$CORE_OBJECT" ]' in MODULE and 'getenv("RX3_NOW_PLAYING")' in FEATURE,
    "the module declines without the performance core and gates on its flag",
)
require(
    '#include "../../now-playing/1.19/rx3_now_playing_feature.h"' in HOOK
    and '.name = "now-playing",' in HOOK,
    "the core must compile and register the feature",
)
require(
    "#define NOW_PLAYING_MUSIC_TITLE    0x28u" in DECL
    and "#define NOW_PLAYING_MUSIC_PATH     0x128u" in DECL,
    "the title and path offsets are the ones the player's own TrkInfoDataSet copies",
)
require(
    "now_playing_load_tags(index, deck.path" in FEATURE and '\\"artist\\":' in FEATURE
    and '\\"comment\\":' in FEATURE,
    "the worker must read the tags and carry both new fields in the datagram",
)
require(
    "\"poll\"" in SYMBOLS and "\"socketpair\"" in SYMBOLS
    and "\"pthread_join\"" in SYMBOLS,
    "the networking and join imports are confirmed by the hook symbol test",
)
require(
    "#include <" not in TAGS and "#include <" not in FEATURE,
    "the hook links no libc header; every declaration comes from the core",
)

print("Now playing regression guards: OK")
