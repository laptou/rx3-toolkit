#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Hector: TRANS becomes the hector sprite. The drawing lives in spritefx.
# This module names the pack and the beat-fx label. Without the pack, TRANS
# stays stock.

module_begin hector2 hector2

hector2_prepare()
{
    [ -r "$CORE_OBJECT" ] || {
        say "Hector disabled: the performance core is not selected"
        return 1
    }
    module_disabled_by_switch hector2 && return 0
    if [ ! -r "$USB/RX3_SPRITES/hector.rx3spr" ]; then
        say "Hector disabled: RX3_SPRITES/hector.rx3spr is missing"
        return 0
    fi
    export RX3_SPRITE_FX=1
    export RX3_SPRITE_FX_PACK="$USB/RX3_SPRITES/hector.rx3spr"
    export RX3_SPRITE_FX_NAME=HECTOR
    running=$(rbp_environment_value RX3_SPRITE_FX)
    running_name=$(rbp_environment_value RX3_SPRITE_FX_NAME)
    running_pack=$(rbp_environment_value RX3_SPRITE_FX_PACK)
    if [ "$running" != "1" ] || [ "$running_name" != "HECTOR" ] \
        || [ "$running_pack" != "$USB/RX3_SPRITES/hector.rx3spr" ]; then
        say "Hector needs a restart so TRANS can become HECTOR"
        request_rbp_restart
    fi
    say "Hector prepared: TRANS is HECTOR"
}

register_prepare_hook hector2_prepare
