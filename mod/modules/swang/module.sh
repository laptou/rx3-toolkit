#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Swang: the SPIRAL beat fx becomes a swing, locked to the tempo master.
# The code lives in the performance core's shared object; this module decides
# whether it runs, and owns its documentation and tests.

module_begin swang swang

SWANG_READY=0

swang_prepare()
{
    [ -r "$CORE_OBJECT" ] || {
        say "Swang disabled: the performance core is not selected"
        return 1
    }
    module_disabled_by_switch swang && return 0
    export RX3_SWANG=1
    SWANG_READY=1
    running=$(rbp_environment_value RX3_SWANG)
    if [ "$running" != "1" ]; then
        say "Swang needs a restart: running rbp carries RX3_SWANG=[${running:-none}]"
        request_rbp_restart
    fi
    say "Swang prepared: SPIRAL is the swing"
}

swang_after_launch()
{
    [ "$SWANG_READY" = "1" ] || return 0
    say "SWANG: DEPTH = strength, BEAT arrows = swung note, TIME = phase against the MASTER deck's grid"
}

register_prepare_hook swang_prepare
register_after_launch_hook swang_after_launch
