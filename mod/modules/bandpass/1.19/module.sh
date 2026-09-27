#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Band-pass: the DUB ECHO color fx becomes a sweepable band-pass filter.
# The code lives in the performance core's shared object; this module decides
# whether it runs, and owns its documentation and tests.

module_begin bandpass bandpass

BANDPASS_READY=0

bandpass_prepare()
{
    [ -r "$CORE_OBJECT" ] || {
        say "Band-pass disabled: the performance core is not selected"
        return 1
    }
    export RX3_BANDPASS=1
    BANDPASS_READY=1
    running=$(rbp_environment_value RX3_BANDPASS)
    if [ "$running" != "1" ]; then
        say "Band-pass needs a restart: running rbp carries RX3_BANDPASS=[${running:-none}]"
        request_rbp_restart
    fi
    say "Band-pass prepared: DUB ECHO is the band-pass filter"
}

bandpass_after_launch()
{
    [ "$BANDPASS_READY" = "1" ] || return 0
    say "DUB ECHO: COLOR left = bass band, COLOR right = treble band, PARAMETER = resonance"
}

register_prepare_hook bandpass_prepare
register_after_launch_hook bandpass_after_launch
