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
    # one nonce per boot, so the same track can pick a different pack next time
    export RX3_SPRITE_NONCE="$(( ($(date +%s) ^ $$) & 4294967295 ))"
    return 0
}

sprites_rollback()
{
    rm -f "$SPRITES_LINK"
    unset RX3_SPRITES_DIR
    unset RX3_SPRITE_NONCE
}

register_prepare_hook sprites_prepare
register_rollback_hook sprites_rollback
