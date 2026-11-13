/* SPDX-License-Identifier: MPL-2.0 */
/* host check of the band-pass maths against libm and a sine sweep. */

#include "mod/modules/bandpass/rx3_bandpass_dsp.h"

#include <math.h>
#include <stdio.h>

static int near(float got, float want, float tolerance, const char *what)
{
    if (fabsf(got - want) > tolerance) {
        printf("%s: got %f want %f\n", what, got, want);
        return 0;
    }
    return 1;
}

/* steady-state gain in db of a sine through the high-pass then low-pass. */
static float band_gain_db(struct bp_band band, float hz)
{
    struct bp_svf high = bp_svf_for(band.low_edge_hz, band.q);
    struct bp_svf low = bp_svf_for(band.high_edge_hz, band.q);
    struct bp_state high_state = { 0.0f, 0.0f };
    struct bp_state low_state = { 0.0f, 0.0f };
    float peak = 0.0f;
    int index;
    for (index = 0; index < 88200; index++) {
        float in = sinf(2.0f * 3.14159265f * hz * (float)index / 44100.0f);
        float out = bp_svf_step(&low, &low_state,
                                bp_svf_step(&high, &high_state, in, 1), 0);
        if (index > 44100 && fabsf(out) > peak)
            peak = fabsf(out);
    }
    return 20.0f * log10f(peak * band.gain);
}

int main(void)
{
    float value;
    struct bp_band band;
    for (value = 0.0f; value < 1.3f; value += 0.01f)
        if (!near(bp_tan(value), tanf(value), tanf(value) * 0.0005f + 1e-6f,
                  "tan"))
            return 1;
    band = bp_band_for(0.5f, 1.0f);
    if (!near(band.q, BP_Q_FLAT, 0.001f, "centre q") ||
        !near(band.low_edge_hz, 19.75f, 0.2f, "centre low edge") ||
        !near(band.high_edge_hz, 18000.0f, 0.1f, "centre high edge"))
        return 2;
    band = bp_band_for(0.0f, 0.0f);
    if (!near(band.low_edge_hz, 70.7f, 0.5f, "left low edge") ||
        !near(band.high_edge_hz, 141.4f, 1.0f, "left high edge"))
        return 3;
    band = bp_band_for(1.0f, 1.0f);
    if (!near(band.low_edge_hz, 4243.0f, 20.0f, "right low edge") ||
        !near(band.high_edge_hz, 8485.0f, 40.0f, "right high edge") ||
        !near(band.q, BP_Q_MAX, 0.001f, "right q"))
        return 4;
    /* full resonance never plays louder than dry */
    for (value = 0.0f; value <= 1.0f; value += 0.125f) {
        float hz;
        band = bp_band_for(value, 1.0f);
        for (hz = 40.0f; hz < 16000.0f; hz *= 1.15f)
            if (band_gain_db(band, hz) > 3.0f) {
                printf("knob %.3f %.0f hz: %.1f db\n", value, hz,
                       band_gain_db(band, hz));
                return 8;
            }
    }
    /* the centre is flat across the audible range */
    band = bp_band_for(0.5f, 0.5f);
    for (value = 60.0f; value < 12000.0f; value *= 2.0f)
        if (!near(band_gain_db(band, value), 0.0f, 0.5f, "centre flat"))
            return 5;
    /* fully left passes the bass and removes the mids */
    band = bp_band_for(0.0f, 0.0f);
    if (band_gain_db(band, 100.0f) < -4.0f || band_gain_db(band, 2000.0f) > -40.0f)
        return 6;
    /* fully right passes the treble and removes the bass */
    band = bp_band_for(1.0f, 0.0f);
    if (band_gain_db(band, 6000.0f) < -4.0f || band_gain_db(band, 200.0f) > -45.0f)
        return 7;
    puts("bandpass dsp: OK");
    return 0;
}
