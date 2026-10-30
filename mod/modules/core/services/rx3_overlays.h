/* SPDX-License-Identifier: MPL-2.0 */
#ifndef RX3_OVERLAYS_H
#define RX3_OVERLAYS_H
#include "../api/rx3_overlay_api.h"
/* Render thread, after the player drew `image`. The one place an overlay
   watcher is dispatched from. */
void rx3_overlays_drawn(unsigned int image);
/* Render thread count of live windows, for the render probe. */
unsigned int rx3_overlay_count(void);
#endif
