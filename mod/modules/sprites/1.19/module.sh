#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Point the performance core at RX3_SPRITES. Absent packs leave the player stock.

module_begin sprites sprites

SPRITES_LINK=/tmp/rx3-sprites

sprites_prepare()
{
    [ -r "$CORE_OBJECT" ] || {
        say "Sprites disabled: the performance core is not selected"
        return 1
    }
    rm -f "$SPRITES_LINK"
    if [ -d "$USB/RX3_SPRITES" ]; then
        ln -s "$USB/RX3_SPRITES" "$SPRITES_LINK" || return 1
        export RX3_SPRITES_DIR="$SPRITES_LINK"
    fi
    # the running player keeps drawing with the binary it was started with.
    # a new overlay has to replace that process or the sprite never appears.
    if [ -d "$USB/RX3_SPRITES" ]; then
        status_path="$USB/RX3_RUNTIME/sprite-status.log"
        mkdir -p "$USB/RX3_RUNTIME" || status_path=""
        if [ -n "$status_path" ]; then
            export RX3_SPRITE_STATUS_PATH="$status_path"
            export RX3_SPRITE_PULSE_PATH="$USB/RX3_RUNTIME/sprite-pulse"
            export RX3_SPRITE_CHOICE_PATH="$USB/RX3_RUNTIME/sprite-choice"
            echo "boot ${BOOT_NUMBER:-0} id ${BOOT_ID:-0}" >> "$status_path"
            running_status=$(rbp_environment_value RX3_SPRITE_STATUS_PATH)
            if [ "$running_status" != "$status_path" ]; then
                say "Sprites need a restart so the overlay can draw"
                request_rbp_restart
            fi
        fi
    fi
    # one nonce per boot, so the same track can pick a different pack next time
    export RX3_SPRITE_NONCE="$(( ($(date +%s) ^ $$) & 4294967295 ))"
    return 0
}

register_prepare_hook sprites_prepare
