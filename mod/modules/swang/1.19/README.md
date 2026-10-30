<!-- SPDX-License-Identifier: MPL-2.0 -->
# Swang

With this module on, the SPIRAL Beat FX is renamed SWANG. It swings the music:
the off-beat of each pair of notes lands late. Nothing is sped up or slowed
down. Each sixteenth note is moved as a slice: the sixteenths in the first
note spread out, and the ones in the second note close up. Every hit plays
untouched and lands within 2 ms of its swung place. To fill the extra time,
the tail of each first-note sixteenth is stretched by repeating short bits of
it. To save time, the tail of each second-note sixteenth is cut. Every splice
is made in phase with the audio already playing, and none cuts through a hit.
With DEPTH fully left, the output is the input exactly.

To hear it without the player, render a track through the same code:

```sh
python3 scripts/swang_render.py track.mp3 swung.wav --bpm 124 --first-beat 0.12 --depth 0.67 --note 8th
```

`--first-beat` is the time, in seconds, of any downbeat in the track. The
script needs `clang` and `ffmpeg`.

The swing locks to the beat grid of the tempo MASTER deck. With no MASTER deck
it uses the deck on the Beat FX channel. With neither, or while that deck is
paused, the audio plays straight.

Select SWANG, pick a channel (CH1, CH2 or MASTER) and press ON:

- **DEPTH** sets the strength. Fully left is straight. At about two thirds the
  swing is a triplet feel, and fully right the off-beat lands at 3/4 of the pair.
- **The BEAT arrows** pick the swung note: 1/4 beat (sixteenths, the start),
  1/2 beat (eighths) or 1 beat (quarters). The screen shows that beat.
- **TIME** moves the swing against the grid, in 32 steps per pair of notes.
  The screen shows the shift in ms. When TIME adjusts the BPM (the Beat FX
  BPM mode), it does that as before.

When you press ON or OFF, the swing glides in or out instead of jumping.

While SWANG is on, each deck's waveform draws an extra tick in every gap
between the beat marks. It is shorter and dimmer than those marks, and it
sits where the swung off-beat lands: DEPTH slides it later, TIME shifts it
with the grid, and the BEAT arrows decide how the gaps are divided. Sixteenth
notes put two ticks in a beat, eighths one, and quarter notes one every two
beats. As the effect glides off, the ticks slide back and then go away.

Not demonstrated on hardware yet: the SWANG labels on the screen, the swing
ticks on the waveform, and how tight the swing sits against the grid while
the master deck is pitch-bent.
