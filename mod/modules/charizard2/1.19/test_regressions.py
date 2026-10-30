#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Charizard is a name and a pack on the shared TRANS sprite."""

from pathlib import Path
import json

ROOT = Path(__file__).resolve().parent
MODULE = (ROOT / "module.sh").read_text()
MANIFEST = json.loads((ROOT / "manifest.json").read_text())
SPRITES = json.loads((ROOT.parents[1] / "sprites/1.19/manifest.json").read_text())


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require(
    "module_begin charizard2 charizard2" in MODULE
    and MANIFEST["id"] == "charizard2"
    and MANIFEST["order"] == 110
    and MANIFEST["requires"] == ["spritefx"]
    and MANIFEST["conflicts"] == ["sprites", "hector2"]
    and "charizard2" in SPRITES["conflicts"],
    "charizard2 uses sprite fx and conflicts with sprites and hector2",
)
require(
    'export RX3_SPRITE_FX_PACK="$USB/RX3_SPRITES/charizard.rx3spr"' in MODULE
    and "export RX3_SPRITE_FX=1" in MODULE
    and "export RX3_SPRITE_FX_NAME=CHARIZARD" in MODULE
    and MODULE.index("charizard.rx3spr is missing")
    < MODULE.index("export RX3_SPRITE_FX=1")
    and "request_rbp_restart" in MODULE,
    "without charizard.rx3spr the flag stays unset and TRANS stays stock",
)
