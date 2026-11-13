/* SPDX-License-Identifier: MPL-2.0 */
/* The DUB ECHO sound color fx as a swept, resonant band-pass filter. */
#include "../core/api/rx3_module_api.h"
static const struct rx3_services *framework;
#include "rx3_bandpass_feature.h"

static int bandpass_configured(void)
{
    return bandpass_feature_configured();
}

static int bandpass_start(const struct rx3_services *services)
{
    framework = services;
    if (!services->install_hook || !services->detach_hook ||
        !services->release_hook)
        return 0;
    if (!bandpass_feature_install()) {
        framework->log_line("bandpass refused: the color fx hook was rejected");
        return 0;
    }
    return 1;
}

static void bandpass_stop(void)
{
    if (framework)
        bandpass_feature_remove();
}

const struct rx3_module rx3_bandpass_module = {
    .version = RX3_MODULE_API_VERSION, .size = sizeof(struct rx3_module),
    .name = "bandpass", .configured = bandpass_configured,
    .start = bandpass_start, .stop = bandpass_stop
};
