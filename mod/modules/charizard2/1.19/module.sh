#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Charizard: the TRANS beat fx becomes the charizard sprite.
# The code lives in the performance core's shared object; this module decides
# whether it runs. Without the pack, TRANS stays stock.

module_begin charizard2 charizard2

charizard2_prepare()
{
    [ -r "$CORE_OBJECT" ] || {
        say "Charizard disabled: the performance core is not selected"
        return 1
    }
    if [ ! -r "$USB/RX3_SPRITES/charizard.rx3spr" ]; then
        say "Charizard disabled: RX3_SPRITES/charizard.rx3spr is missing"
        return 0
    fi
    export RX3_CHARIZARD2=1
    export RX3_CHARIZARD_PACK="$USB/RX3_SPRITES/charizard.rx3spr"
    running=$(rbp_environment_value RX3_CHARIZARD2)
    if [ "$running" != "1" ]; then
        say "Charizard needs a restart so TRANS can become CHARIZARD"
        request_rbp_restart
    fi
    say "Charizard prepared: TRANS is CHARIZARD"
}

register_prepare_hook charizard2_prepare
