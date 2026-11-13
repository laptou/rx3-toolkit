<!-- SPDX-License-Identifier: MPL-2.0 -->
# Sprite beat fx

This module is not in the picker. Charizard and Hector use it.

TRANS is renamed to the module's label and shows that module's sprite while
it is ON. LEVEL/DEPTH is the size. CH1 sits on the top waveform strip, CH2
on the bottom, and Master on the seam between them. The audio stays dry.
BEAT and TIME still edit TRANS and do not move the picture.

A new name is a new module. Copy `hector2`, then change three things: the
module id, the pack path, and the label. The pack is
`RX3_SPRITES/<name>.rx3spr`. The label is `RX3_SPRITE_FX_NAME`. It is drawn
from the sprite glyph sheet (A–Z, 0–9, space, hyphen), uppercased, with any
other character dropped, and cut at 16 glyphs. The module requires
`spritefx`, and it conflicts with `sprites` and with every other name on
TRANS. If the pack file is missing, do not export the flag, so TRANS stays
stock.

The three exports are:

```sh
export RX3_SPRITE_FX=1
export RX3_SPRITE_FX_PACK="$USB/RX3_SPRITES/hector.rx3spr"
export RX3_SPRITE_FX_NAME=HECTOR
```
