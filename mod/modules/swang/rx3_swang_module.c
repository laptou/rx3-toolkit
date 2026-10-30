/* SPDX-License-Identifier: MPL-2.0 */
/* The SPIRAL beat fx as a swing locked to the tempo master's grid. The
 * module owns the warp maths and the tick scan; the core owns the hooks it
 * listens through. */
#include "../core/api/rx3_module_api.h"
static const struct rx3_services *framework;
#include "rx3_swang_feature.h"

static int swang_configured(void)
{
    return swang_feature_configured();
}

static int swang_start(const struct rx3_services *services)
{
    framework = services;
    if (!services->beatfx || !services->beatfx->claim_operate ||
        !services->audio || !services->audio->claim_deck_stream)
        return 0;
    if (!swang_feature_install()) {
        swang_feature_remove();
        return 0;
    }
    return 1;
}

static void swang_stop(void)
{
    if (framework)
        swang_feature_remove();
}

const struct rx3_module rx3_swang_module = {
    .version = RX3_MODULE_API_VERSION, .size = sizeof(struct rx3_module),
    .name = "swang", .configured = swang_configured,
    .start = swang_start, .stop = swang_stop
};
