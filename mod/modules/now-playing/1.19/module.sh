#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Now playing. The code lives in the performance core's shared object; this
# module only switches it on.

module_begin now-playing now_playing

NOW_PLAYING_READY=0

now_playing_prepare()
{
    [ -r "$CORE_OBJECT" ] || {
        say "Now playing disabled: the performance core is not selected"
        return 1
    }
    export RX3_NOW_PLAYING=1
    NOW_PLAYING_READY=1
    running=$(rbp_environment_value RX3_NOW_PLAYING)
    if [ "$running" != "1" ]; then
        say "Now playing needs a restart: running rbp carries RX3_NOW_PLAYING=[${running:-none}]"
        request_rbp_restart
    fi
    say "Now playing prepared: deck state goes out on UDP 50123 over the rear USB link"
}

now_playing_after_launch()
{
    [ "$NOW_PLAYING_READY" = "1" ] || return 0
    say "Now playing: listen on UDP 50123 on the computer plugged into the rear USB port"
}

register_prepare_hook now_playing_prepare
register_after_launch_hook now_playing_after_launch
