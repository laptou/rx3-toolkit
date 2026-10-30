#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Hector is a name and a pack on the shared TRANS sprite."""

from pathlib import Path
import json

ROOT = Path(__file__).resolve().parent
MODULE = (ROOT / "module.sh").read_text()
MANIFEST = json.loads((ROOT / "manifest.json").read_text())
SPRITES = json.loads((ROOT.parents[1] / "sprites/1.19/manifest.json").read_text())
CHARIZARD = json.loads((ROOT.parents[1] / "charizard2/1.19/manifest.json").read_text())


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require(
    "module_begin hector2 hector2" in MODULE
    and MANIFEST["id"] == "hector2"
    and MANIFEST["order"] == 112
    and MANIFEST["requires"] == ["spritefx"]
    and MANIFEST["conflicts"] == ["sprites", "charizard2"]
    and "hector2" in SPRITES["conflicts"]
    and "hector2" in CHARIZARD["conflicts"],
    "hector2 uses sprite fx and conflicts with sprites and charizard2",
)
require(
    'export RX3_SPRITE_FX_PACK="$USB/RX3_SPRITES/hector.rx3spr"' in MODULE
    and "export RX3_SPRITE_FX=1" in MODULE
    and "export RX3_SPRITE_FX_NAME=HECTOR" in MODULE
    and MODULE.index("hector.rx3spr is missing")
    < MODULE.index("export RX3_SPRITE_FX=1")
    and "request_rbp_restart" in MODULE,
    "without hector.rx3spr the flag stays unset and TRANS stays stock",
)
