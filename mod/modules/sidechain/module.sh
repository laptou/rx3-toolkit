#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Sidechain: the HELIX beat fx becomes a sidechain compressor.
# This module decides whether it runs, and owns its documentation.

module_begin sidechain sidechain

SIDECHAIN_READY=0

sidechain_prepare()
{
    [ -r "$CORE_OBJECT" ] || {
        say "Sidechain disabled: the performance core is not selected"
        return 1
    }
    module_disabled_by_switch sidechain && return 0
    export RX3_SIDECHAIN=1
    SIDECHAIN_READY=1
    running=$(rbp_environment_value RX3_SIDECHAIN)
    if [ "$running" != "1" ]; then
        say "Sidechain needs a restart: running rbp carries RX3_SIDECHAIN=[${running:-none}]"
        request_rbp_restart
    fi
    say "Sidechain prepared: HELIX is the sidechain compressor"
}

sidechain_after_launch()
{
    [ "$SIDECHAIN_READY" = "1" ] || return 0
    say "HELIX: beat fx channel = trigger, other deck ducks; DEPTH = threshold, BEAT arrows = ratio, TIME = release"
}

register_prepare_hook sidechain_prepare
register_after_launch_hook sidechain_after_launch
