#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Charizard: TRANS becomes the charizard sprite. The drawing lives in
# spritefx. This module names the pack and the beat-fx label. Without the
# pack, TRANS stays stock.

module_begin charizard2 charizard2

charizard2_prepare()
{
    [ -r "$CORE_OBJECT" ] || {
        say "Charizard disabled: the performance core is not selected"
        return 1
    }
    module_disabled_by_switch charizard2 && return 0
    if [ ! -r "$USB/RX3_SPRITES/charizard.rx3spr" ]; then
        say "Charizard disabled: RX3_SPRITES/charizard.rx3spr is missing"
        return 0
    fi
    export RX3_SPRITE_FX=1
    export RX3_SPRITE_FX_PACK="$USB/RX3_SPRITES/charizard.rx3spr"
    export RX3_SPRITE_FX_NAME=CHARIZARD
    running=$(rbp_environment_value RX3_SPRITE_FX)
    running_name=$(rbp_environment_value RX3_SPRITE_FX_NAME)
    running_pack=$(rbp_environment_value RX3_SPRITE_FX_PACK)
    if [ "$running" != "1" ] || [ "$running_name" != "CHARIZARD" ] \
        || [ "$running_pack" != "$USB/RX3_SPRITES/charizard.rx3spr" ]; then
        say "Charizard needs a restart so TRANS can become CHARIZARD"
        request_rbp_restart
    fi
    say "Charizard prepared: TRANS is CHARIZARD"
}

register_prepare_hook charizard2_prepare
