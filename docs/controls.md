<!-- SPDX-License-Identifier: MPL-2.0 -->
# Deck controls: where each mod lives

A mod that adds a control puts it on a surface the player already has. Nothing
here needs a menu of our own. That also means a control is easy to forget, so
this page names every one.

## Performance tab strip

Three tabs sit above the pads. Two of them belong to a module when it is
loaded, and the third is the player's own STATUS / BEAT FX row.

| Tab | Module | Controls |
|---|---|---|
| **KEY SHIFT** (left slot) | `keyshift` | `-1` / centre / `+1` per deck, one semitone per tap. The centre returns to neutral. Loading a track also returns to neutral. |
| **STEMS** (right slot) | `stems` | One control per available stem: INST, VOCAL, DRUMS. Tap toggles, press and hold then drag sets the level. A deck with nothing prepared says why instead. |
| **SAMPLES / BEAT FX** | the player | Stock. |

In SLIP LOOP mode the same stems are pads 5–8: INST red, VOCAL green, DRUMS
blue; pad 8 keeps its native loop.

## Beat FX

Selecting one of these effects runs a module instead of the stock effect.
The caption on the screen is the module's name.

| Effect | Module | Controls |
|---|---|---|
| **SIDECHAIN** (was HELIX) | `sidechain` | `DEPTH` is how hard the ducked channel folds. The meter strip rides the seam between the two waveforms: trigger level in grey, the threshold as a dotted line, gain reduction hanging from the top in orange. |
| **SWANG** (was SPIRAL) | `swang` | `ON` swings, `OFF` glides back to straight. `DEPTH` is the strength: straight at fully left, the off-beat at three quarters at fully right. The BEAT arrows pick the swung note (1/4, 1/2 or 1 beat) and the screen shows it. `TIME` shifts the swing against the grid, 32 steps per pair of notes, and the screen shows the shift in ms. |
| **CHARIZARD**, **HECTOR** | `charizard2` / `hector2` (on top of `spritefx`) | `TRANS` is renamed to the module and shows its sprite while it is ON. `LEVEL`/`DEPTH` is the size. CH1 puts the sprite on the top waveform strip, CH2 on the bottom, Master on the seam. `BEAT` and `TIME` still edit TRANS and do not move the picture. The audio stays dry. |

## Sound Color FX

| Effect | Module | Controls |
|---|---|---|
| **DUB ECHO** | `bandpass` | Becomes a band-pass filter. `COLOR` at centre is dry; left of centre narrows and slides the band down (fully left, roughly 70–140 Hz), right of centre narrows and slides it up (fully right, roughly 4–8.5 kHz). `PARAMETER` is the resonance at the edges. |

## Beat Jump

| Module | Controls |
|---|---|
| `beatjump-32bars` | Pads 7 and 8 become `-32` / `+32` beats instead of `-8` / `+8`. |
| `beatjump-no-quantize` | Beat jumps stop waiting for the beat. |
| `sprites` | **Page 2** of the pad row is the sprite menu. Pads 1–8 are the first eight packs in `RX3_SPRITES/catalog.txt`; press a pad and both decks show that pack, press the lit pad again and the overlay turns off. Page 1 still beat-jumps. |

Sprites only appear when `RX3_SPRITES` exists on the stick with at least one
pack in it. The choice is kept in `RX3_RUNTIME/sprite-choice`; `off` in that
file is why a later boot shows no sprite.

## UTILITY

| Module | Controls |
|---|---|
| `sprites` | **ON AIR DISPLAY** becomes **SPRITE PULSE**. With it ON, the sprite grows by up to 30% on each kick drum. The stock ON AIR setting is held ON the whole time. The choice is kept in `RX3_RUNTIME/sprite-pulse`. |

## No controls at all

`now-playing` reports the playing track over UDP from a fixed address; `theme-white`,
`logo`, `decoder-sleep`, `search-latin`, `browse-columns`, `asshole-mode` and
`logging` change behaviour without adding a control.
