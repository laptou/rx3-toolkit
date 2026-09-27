<!-- SPDX-License-Identifier: MPL-2.0 -->
# Animated sprites

Firmware 1.19 only. The module is off unless `RX3_SPRITES` is on the USB stick.

The computer writes `*.rx3spr` and `catalog.txt`. The player loads one pack
per deck when a track loads. Each deck gets one overlay window, centred on
that deck's waveform strip (deck 0 top strip, deck 1 bottom). Anchor 2 rests
the sprite on the strip's bottom edge. Index 0 is transparent.
`RX3_RUNTIME/sprite-status.log` notes "sprite window open deck N" or
"sprite window could not be created". Fixed-rate packs play from wall time.
Beat-mode packs follow the player's beat clock. That clock address is recorded
only after a firmware 1.19 proof, and until then beat mode draws nothing. A
bad pack turns that deck's overlay off. Stock drawing and stems keep running.

See `docs/sprites.md` for the file layout and `docs/hardware-validation.md` for
the checks that have to be done on a player.
