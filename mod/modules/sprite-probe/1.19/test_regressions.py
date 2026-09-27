#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The probe stays opt-in and does not invent a format."""

import json
from pathlib import Path


HERE = Path(__file__).resolve().parent
MODULE = (HERE / "module.sh").read_text()
MANIFEST = json.loads((HERE / "manifest.json").read_text())


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require(MANIFEST["default"] is False, "the probe must be off unless selected")
require(MANIFEST["requires"] == ["sprites"], "the probe needs the sprite module")
require(
    "export RX3_SPRITE_PROBE=1" in MODULE
    and "register_prepare_hook sprite_probe_prepare" in MODULE
    and "register_rollback_hook" not in MODULE,
    "the probe must be a single environment switch",
)
