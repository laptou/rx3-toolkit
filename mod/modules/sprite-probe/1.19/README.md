<!-- SPDX-License-Identifier: MPL-2.0 -->
# Sprite hard-mask probe

Select this only while confirming that transparent corners show the picture
underneath. Put `probe.spec` and `probe.bin` in `RX3_SPRITES`. The probe draws
one 8×8 fixture, at most every 200 ms, on images at least 200×40. It does not
trace every frame.

After the corners are confirmed, deselect this module. Deck sprites still need
`format.proof` and `anchors.proof`, which this module does not invent.
