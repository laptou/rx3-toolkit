<!-- SPDX-License-Identifier: MPL-2.0 -->
# Animated sprites

Firmware 1.19 only. The module is off unless `RX3_SPRITES` is on the USB stick.

The computer writes `*.rx3spr`, `catalog.txt`, and `tracks.rx3idx`. The player
loads one eligible pack per deck when a track loads. A missing proof file, a
bad pack, or an unknown waveform layout turns that deck's overlay off. Stock
drawing and stems keep running.

See `docs/sprites.md` for the file layout and `docs/hardware-validation.md` for
the checks that have to be done on a player.
