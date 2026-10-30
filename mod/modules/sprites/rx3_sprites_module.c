/* SPDX-License-Identifier: MPL-2.0 */
/* One sprite over the waveform view, from packs on the USB stick. The
 * module owns the packs, the choice and the drawing; the core owns the
 * overlay window it draws into and the hooks that tell it when. */
#include "../core/api/rx3_module_api.h"
static const struct rx3_services *framework;
#include "rx3_sprites_feature.h"

static int sprites_configured(void)
{
    return sprites_feature_configured();
}

static int sprites_start(const struct rx3_services *services)
{
    framework = services;
    if (!services->overlays || !services->overlays->claim ||
        !services->audio || !services->input)
        return 0;
    if (!sprites_feature_install()) {
        sprites_feature_remove();
        return 0;
    }
    return 1;
}

static void sprites_stop(void)
{
    if (framework)
        sprites_feature_remove();
}

const struct rx3_module rx3_sprites_module = {
    .version = RX3_MODULE_API_VERSION, .size = sizeof(struct rx3_module),
    .name = "sprites", .configured = sprites_configured,
    .start = sprites_start, .stop = sprites_stop,
    .track_did_load = sprites_feature_track_did_load,
    .track_will_load = sprites_feature_track_will_load
};
