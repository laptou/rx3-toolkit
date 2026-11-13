/* SPDX-License-Identifier: MPL-2.0 */
/* One named sprite on the TRANS beat fx. The module owns the pack and the
 * drawing; the core owns the beat fx operate slot it claims and the overlay
 * window it draws into. */
#include "../core/api/rx3_module_api.h"
static const struct rx3_services *framework;
#include "rx3_spritefx_feature.h"

static int spritefx_configured(void)
{
    return spritefx_feature_configured();
}

static int spritefx_start(const struct rx3_services *services)
{
    framework = services;
    if (!services->beatfx || !services->overlays)
        return 0;
    if (!spritefx_feature_install()) {
        spritefx_feature_remove();
        return 0;
    }
    return 1;
}

static void spritefx_stop(void)
{
    if (framework)
        spritefx_feature_remove();
}

const struct rx3_module rx3_spritefx_module = {
    .version = RX3_MODULE_API_VERSION, .size = sizeof(struct rx3_module),
    .name = "spritefx", .configured = spritefx_configured,
    .start = spritefx_start, .stop = spritefx_stop
};
