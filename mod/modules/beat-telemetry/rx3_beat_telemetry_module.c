/* SPDX-License-Identifier: MPL-2.0 */
#include "../core/api/rx3_module_api.h"
#include "rx3_beat_telemetry_decl.h"
#include "rx3_beat_telemetry_feature.h"

static int rx3_bt_start(const struct rx3_services *services)
{
    rx3_bt_framework = services;
    return rx3_bt_feature_install();
}

const struct rx3_module rx3_beat_telemetry_module = {
    RX3_MODULE_API_VERSION, sizeof(struct rx3_module), "beat-telemetry",
    rx3_bt_feature_configured, rx3_bt_start, rx3_bt_feature_remove,
    rx3_bt_track_did_load, 0, 0, 0, rx3_bt_track_will_load
};
