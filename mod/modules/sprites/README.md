<!-- SPDX-License-Identifier: MPL-2.0 -->
# Animated sprites

Firmware 1.19 only. The module is off unless `RX3_SPRITES` is on the USB stick, and it can be switched off on the Modules screen like any other.

The computer writes `*.rx3spr` and `catalog.txt` into `RX3_SPRITES` (the
host tool is `python3 -m tools.rx3_sprites`). The player loads one pack per
deck when a track loads. It draws one sprite through an overlay window above
the waveform, centred on the whole waveform view (both deck strips), scaled
at runtime with nearest-neighbour to about 0.9× the height of one waveform
strip (200 px on screen). Anchor 1 centres it on the view; anchor 2 rests it
on the bottom edge. Source art is scaled. Index 0 is transparent. The sprite
follows the deck that is playing; when both play, the one that moved last
wins.

BEAT JUMP page 2 is the sprite menu. Page 1 still beat-jumps. Pads 1–8 are the
first eight catalog packs: press a pad and both decks show that pack; press
the lit pad again and the overlay turns off. The choice is kept in
`RX3_RUNTIME/sprite-choice`. The pad faces keep the stock glyphs in this
port — relabelling them needs the stock glyph records' measured sizes, which
only a player or an imagedata dump can give — so the lit pad is the selection
marker until then.

While sprites run, UTILITY > ON AIR DISPLAY becomes SPRITE PULSE. With it ON,
the sprite grows by up to 30% on each kick drum (the master mix through a
~150 Hz low-pass). The choice is kept in `RX3_RUNTIME/sprite-pulse`. The
stock ON AIR DISPLAY setting is held ON the whole time.
`RX3_RUNTIME/sprite-status.log` notes "sprite window open" or
"sprite window could not be created". Fixed-rate packs play from wall time.
Beat-mode packs follow the player's beat grid, walked from the deck's own
reader, so beat mode draws nothing until a track with a grid is playing. A
bad pack turns that deck's overlay off. Stock drawing and stems keep running.

Nothing here has run on hardware. The overlay window's creation, its
compositing order against rbp's own windows, the beat jump page 2 pads and
the relabelled UTILITY row are all still to be checked on a player.
