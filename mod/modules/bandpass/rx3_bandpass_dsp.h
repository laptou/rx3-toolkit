/* SPDX-License-Identifier: MPL-2.0
 * band-pass maths shared by the player and the host test. no libc and no
 * libm: the arm hook links neither, so tan is a short series.
 *
 * the band is a high-pass into a low-pass, each a trapezoidal state
 * variable filter (simper), which stays stable while it is swept. the
 * color knob moves and narrows the band: at the centre it spans the whole
 * audible range and the sound is dry, left slides it down to the bass,
 * right slides it up to the treble. the parameter dial sets the resonance
 * at both edges, and the resonance fades in with the knob so the centre
 * stays flat. the output drops as the resonance rises so a narrow,
 * resonant band never plays louder than the dry signal.
 */

#ifndef RX3_BANDPASS_DSP_H
#define RX3_BANDPASS_DSP_H

/* the arm build meets the platform's own fixed-width types first; a bare
   host compile brings stdint itself. */
#ifndef RX3_PLATFORM_H
#include <stdint.h>
#endif

#define BP_SAMPLE_RATE 44100.0f
#define BP_DEAD_ZONE 0.04f
/* the band's centre with the knob at the centre, and at either end. */
#define BP_CENTRE_HZ 632.0f
#define BP_LOW_HZ 100.0f
#define BP_HIGH_HZ 6000.0f
/* the band's width in octaves, wide open and fully turned. */
#define BP_WIDE_OCTAVES 10.0f
#define BP_NARROW_OCTAVES 1.0f
#define BP_EDGE_MIN_HZ 16.0f
#define BP_EDGE_MAX_HZ 18000.0f
#define BP_Q_FLAT 0.7071f
#define BP_Q_MAX 2.0f
#define BP_PI 3.14159265f

struct bp_band {
    float low_edge_hz;
    float high_edge_hz;
    float q;
    /* the edges' resonant peaks overlap in a narrow band; this undoes them. */
    float gain;
};

/* one filter's coefficients. */
struct bp_svf {
    float a1;
    float a2;
    float a3;
    float k;
};

/* one filter's state for one side. */
struct bp_state {
    float ic1;
    float ic2;
};

static float bp_exp2(float value)
{
    union { float f; uint32_t i; } bits;
    int whole;
    float part;
    if (value < -30.0f)
        value = -30.0f;
    if (value > 30.0f)
        value = 30.0f;
    whole = (int)value;
    if ((float)whole > value)
        whole--;
    part = value - (float)whole;
    bits.f = 1.0f + part * (0.69314718f + part * (0.24022650f +
                                                  part * (0.05550411f +
                                                          part * 0.00961813f)));
    bits.i += (uint32_t)whole << 23;
    return bits.f;
}

/* tan on [0, 1.3], as sin over cos from their series. */
static float bp_tan(float x)
{
    float x2 = x * x;
    float sine = x * (1.0f - x2 / 6.0f * (1.0f - x2 / 20.0f *
                                          (1.0f - x2 / 42.0f *
                                           (1.0f - x2 / 72.0f *
                                            (1.0f - x2 / 110.0f)))));
    float cosine = 1.0f - x2 / 2.0f * (1.0f - x2 / 12.0f *
                                       (1.0f - x2 / 30.0f *
                                        (1.0f - x2 / 56.0f *
                                         (1.0f - x2 / 90.0f *
                                          (1.0f - x2 / 132.0f)))));
    return sine / cosine;
}

/* knob 0..1 and parameter 0..1 to the band's edges and resonance. */
static struct bp_band bp_band_for(float knob, float parameter)
{
    struct bp_band band;
    float span = 0.5f - BP_DEAD_ZONE;
    float turn = 0.0f;
    float centre_l2 = 0.0f;
    float half_width;
    float centre;
    if (knob < 0.5f - BP_DEAD_ZONE) {
        turn = (0.5f - BP_DEAD_ZONE - knob) / span;
        if (turn > 1.0f)
            turn = 1.0f;
        centre_l2 = -turn * 2.6599f; /* log2(632 / 100) */
    } else if (knob > 0.5f + BP_DEAD_ZONE) {
        turn = (knob - 0.5f - BP_DEAD_ZONE) / span;
        if (turn > 1.0f)
            turn = 1.0f;
        centre_l2 = turn * 3.2470f; /* log2(6000 / 632) */
    }
    if (parameter < 0.0f)
        parameter = 0.0f;
    if (parameter > 1.0f)
        parameter = 1.0f;
    centre = BP_CENTRE_HZ * bp_exp2(centre_l2);
    half_width =
        0.5f * (BP_WIDE_OCTAVES + (BP_NARROW_OCTAVES - BP_WIDE_OCTAVES) * turn);
    band.low_edge_hz = centre * bp_exp2(-half_width);
    band.high_edge_hz = centre * bp_exp2(half_width);
    if (band.low_edge_hz < BP_EDGE_MIN_HZ)
        band.low_edge_hz = BP_EDGE_MIN_HZ;
    if (band.high_edge_hz > BP_EDGE_MAX_HZ)
        band.high_edge_hz = BP_EDGE_MAX_HZ;
    band.q = BP_Q_FLAT + (BP_Q_MAX - BP_Q_FLAT) * parameter * turn;
    band.gain = BP_Q_FLAT / band.q;
    return band;
}

static struct bp_svf bp_svf_for(float cutoff_hz, float q)
{
    struct bp_svf svf;
    float g = bp_tan(BP_PI * cutoff_hz / BP_SAMPLE_RATE);
    svf.k = 1.0f / q;
    svf.a1 = 1.0f / (1.0f + g * (g + svf.k));
    svf.a2 = g * svf.a1;
    svf.a3 = g * svf.a2;
    return svf;
}

/* one sample through one filter; high selects the high-pass output. */
static float bp_svf_step(const struct bp_svf *svf, struct bp_state *state,
                         float in, int high)
{
    float v3 = in - state->ic2;
    float v1 = svf->a1 * state->ic1 + svf->a2 * v3;
    float v2 = state->ic2 + svf->a2 * state->ic1 + svf->a3 * v3;
    state->ic1 = 2.0f * v1 - state->ic1;
    state->ic2 = 2.0f * v2 - state->ic2;
    return high ? in - svf->k * v1 - v2 : v2;
}

#endif /* RX3_BANDPASS_DSP_H */
