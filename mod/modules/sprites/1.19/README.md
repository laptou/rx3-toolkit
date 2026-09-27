<!-- SPDX-License-Identifier: MPL-2.0 -->
# Animated sprites

Firmware 1.19 only. The module is off unless `RX3_SPRITES` is on the USB stick.

The computer writes `*.rx3spr` and `catalog.txt`. The player loads one pack
per deck when a track loads. It draws one sprite centred over the whole
waveform view (both deck strips), scaled at runtime with nearest-neighbour
to about 0.9× the height of one waveform strip (200 px on screen). Anchor 1
centres it on the view; anchor 2 rests it on the bottom edge. Source art is
scaled. Index 0 is transparent. The sprite follows the deck that is playing;
when both play, it switches to one that is more than twice as loud.

BEAT JUMP page 2 is the sprite menu. Page 1 still beat-jumps. Pads 1–8 are the
first eight catalog packs. Press a pad and both decks show that pack. Press
the lit pad again and the overlay turns off. The choice is kept in
`RX3_RUNTIME/sprite-choice`. This mod is not for use with CHARIZARD2.

While sprites run, UTILITY > ON AIR DISPLAY becomes SPRITE PULSE. With it ON,
the sprite grows by up to 30% on each kick drum (the deck's audio through a
~150 Hz low-pass). The choice is kept in `RX3_RUNTIME/sprite-pulse`. The
stock ON AIR DISPLAY setting is held ON the whole time.
`RX3_RUNTIME/sprite-status.log` notes "sprite window open deck N" or
"sprite window could not be created". Fixed-rate packs play from wall time.
Beat-mode packs follow the player's beat clock. That clock address is recorded
only after a firmware 1.19 proof, and until then beat mode draws nothing. A
bad pack turns that deck's overlay off. Stock drawing and stems keep running.

See `docs/sprites.md` for the file layout and `docs/hardware-validation.md` for
the checks that have to be done on a player.
