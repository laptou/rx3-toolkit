#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Opt-in hard-mask probe. Normal playback does not select this module.

module_begin sprite-probe sprite_probe

sprite_probe_prepare()
{
    [ -r "$CORE_OBJECT" ] || {
        say "Sprite probe disabled: the performance core is not selected"
        return 1
    }
    export RX3_SPRITE_PROBE=1
    return 0
}

sprite_probe_rollback()
{
    unset RX3_SPRITE_PROBE
}

register_prepare_hook sprite_probe_prepare
register_rollback_hook sprite_probe_rollback
