<!-- SPDX-License-Identifier: MPL-2.0 -->
# Sidechain compressor

Firmware 1.19 only. The HELIX beat fx becomes a sidechain compressor: while HELIX is the selected beat fx, the beat fx channel (CH1 or CH2) is the trigger and the other deck ducks by its level. The trigger's level is taken through a 150 Hz low-pass, so the duck follows the kick and bass. The stock HELIX never runs — the module owns the block from ON through OFF and hands it back to stock during a fade tail, so a switch to or from HELIX still crossfades in stock.

The controls keep their knobs: DEPTH is the threshold, the BEAT arrows step the ratio (2:1 to inf:1), TIME is the release. The play screen's beat fx block shows the ratio in the beat-number field. A compressor meter scrolls across the seam between the two waveforms while HELIX runs: the trigger's level rises from the bottom in grey, the threshold is a dotted line, and the gain reduction on the ducked channel hangs from the top in orange.

The four caption images read SIDECHAIN. The core paints them while it builds the image tables, which is the only moment a stock record can be read: the module starts before the player has built its table, so it cannot measure a record itself. `replace_masked` hands over the mask and nothing else. The record gives the size, its first pixel the ground it was drawn on, and the pixel furthest from that ground the ink, so the dim and white variants and the black and blue grounds all keep their own colouring. No size or colour is hardcoded, and the mask is box-averaged to whatever size the record turns out to be.

The select grid's three tiles take the same mask, averaged down to their size, as the old module did. The beat number's lettering stays stock: it shows the selected ratio, not the effect. The sizes the paint actually used are written to the module log (`masked image painted =`, `masked image width =`, `masked image height =`), and a record that cannot be painted -- paletted, no bitmap of its own, or already spent -- is logged as `masked image refused =` instead of being painted over with nonsense.

Nothing here has run on hardware. The channel stage's block, the operate skip, the BEAT arrow takeover and the meter's compositing all still need the player.
