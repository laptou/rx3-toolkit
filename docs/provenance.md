<!-- SPDX-License-Identifier: MPL-2.0 -->
# Provenance

This tree starts from [Tratosca/rx3-toolkit](https://github.com/Tratosca/rx3-toolkit) at commit `0cb402e03d0c32d9bcc6c6fb8b8b4f40728e816a`. That history and the MPL-2.0 licence stay in place. Later commits on this branch add the sprite compilers and the firmware 1.19 sprite module.

The player target is XDJ-RX3 firmware 1.19 only. The existing `rbp` hash check still refuses any other build. Nothing here is a firmware image, and nothing is written to the player's internal flash. Pull the USB stick out and power-cycle to return to stock.
