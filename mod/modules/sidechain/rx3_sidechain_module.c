/* SPDX-License-Identifier: MPL-2.0 */
/* The HELIX beat fx as a sidechain compressor. The module owns the
 * compression maths and the meter; the core owns the hooks it listens
 * through and the window the meter draws into. */
#include "../core/api/rx3_module_api.h"
static const struct rx3_services *framework;
#include "rx3_sidechain_feature.h"

static int sidechain_configured(void)
{
    return sidechain_feature_configured();
}

static int sidechain_start(const struct rx3_services *services)
{
    framework = services;
    if (!services->audio || !services->audio->claim_channel ||
        !services->beatfx || !services->overlays)
        return 0;
    if (!sidechain_feature_install()) {
        sidechain_feature_remove();
        return 0;
    }
    return 1;
}

static void sidechain_stop(void)
{
    if (framework)
        sidechain_feature_remove();
}

const struct rx3_module rx3_sidechain_module = {
    .version = RX3_MODULE_API_VERSION, .size = sizeof(struct rx3_module),
    .name = "sidechain", .configured = sidechain_configured,
    .start = sidechain_start, .stop = sidechain_stop
};
