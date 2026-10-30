#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The shared TRANS sprite. Charizard, Hector, and any later name are separate
# modules: each one exports RX3_SPRITE_FX, RX3_SPRITE_FX_PACK and
# RX3_SPRITE_FX_NAME. This module only checks that the performance core is
# present. Without those exports, TRANS stays stock.

module_begin spritefx spritefx

spritefx_prepare()
{
    [ -r "$CORE_OBJECT" ] || {
        say "Sprite fx disabled: the performance core is not selected"
        return 1
    }
    return 0
}

register_prepare_hook spritefx_prepare
