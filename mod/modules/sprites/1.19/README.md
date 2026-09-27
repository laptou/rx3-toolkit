<!-- SPDX-License-Identifier: MPL-2.0 -->
# Animated sprites

Firmware 1.19 only. The module is off unless `RX3_SPRITES` is on the USB stick.

The computer writes `*.rx3spr` and `catalog.txt`. The player loads one pack
per deck when a track loads. It draws one sprite centred over the whole
waveform view (both deck strips), scaled at runtime with nearest-neighbour
to about 0.9× the height of one waveform strip (200 px on screen). Anchor 1
centres it on the view; anchor 2 rests it on the bottom edge. Source art is
scaled. Index 0 is transparent.
`RX3_RUNTIME/sprite-status.log` notes "sprite window open deck N" or
"sprite window could not be created". Fixed-rate packs play from wall time.
Beat-mode packs follow the player's beat clock. That clock address is recorded
only after a firmware 1.19 proof, and until then beat mode draws nothing. A
bad pack turns that deck's overlay off. Stock drawing and stems keep running.

See `docs/sprites.md` for the file layout and `docs/hardware-validation.md` for
the checks that have to be done on a player.
