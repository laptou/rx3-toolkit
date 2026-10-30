<!-- SPDX-License-Identifier: MPL-2.0 -->
# Sidechain compressor

Firmware 1.19 only. The HELIX beat fx becomes a sidechain compressor: while HELIX is the selected beat fx, the beat fx channel (CH1 or CH2) is the trigger and the other deck ducks by its level. The trigger's level is taken through a 150 Hz low-pass, so the duck follows the kick and bass. The stock HELIX never runs — the module owns the block from ON through OFF and hands it back to stock during a fade tail, so a switch to or from HELIX still crossfades in stock.

The controls keep their knobs: DEPTH is the threshold, the BEAT arrows step the ratio (2:1 to inf:1), TIME is the release. The play screen's beat fx block shows the ratio in the beat-number field. A compressor meter scrolls across the seam between the two waveforms while HELIX runs: the trigger's level rises from the bottom in grey, the threshold is a dotted line, and the gain reduction on the ducked channel hangs from the top in orange.

The caption images and the beat number's lettering keep their stock artwork in this port — relabelling them needs the stock records' measured sizes, which only a player or an imagedata dump can give. On screen the effect still reads HELIX everywhere except the beat number, which reads the ratio's stock label.

Nothing here has run on hardware. The channel stage's block, the operate skip, the BEAT arrow takeover and the meter's compositing all still need the player.
