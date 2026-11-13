#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Beat-grid timing for TouchDesigner and other visual tools.

module_begin beat-telemetry beat_telemetry

BEAT_TELEMETRY_READY=0

beat_telemetry_prepare()
{
    [ -r "$CORE_OBJECT" ] || {
        say "Beat telemetry disabled: the performance core is not selected"
        return 0
    }
    module_disabled_by_switch beat-telemetry && return 0
    BEAT_TELEMETRY_READY=1
    module_export RX3_BEAT_TELEMETRY 1 "Beat telemetry"
    say "Beat telemetry prepared: beat-grid data goes out on UDP 50124 over USB-B"
}

beat_telemetry_after_launch()
{
    [ "$BEAT_TELEMETRY_READY" = "1" ] || return 0
    say "Beat telemetry: receive UDP 50124 on the computer plugged into rear USB-B"
}

register_prepare_hook beat_telemetry_prepare
register_after_launch_hook beat_telemetry_after_launch
