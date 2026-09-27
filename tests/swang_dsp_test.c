/* SPDX-License-Identifier: MPL-2.0 */
/* host check of the swing warp: its shape, a clean passthrough, and where a
   straight eighth-note click lands once it is swung. */

#include "mod/modules/swang/1.19/rx3_swang_dsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define PAIR 11025
#define PAIRS 8
#define TOTAL (PAIR * PAIRS)
#define BLOCK 256

static int near(float got, float want, float tolerance, const char *what)
{
    if (fabsf(got - want) > tolerance) {
        printf("%s: got %f want %f\n", what, got, want);
        return 0;
    }
    return 1;
}

/* the centroid of |output| in a window, in frames from its start. */
static float centroid(const struct sw_stereo *buf, int from, int to)
{
    double weight = 0.0;
    double sum = 0.0;
    int index;
    for (index = from; index < to; index++) {
        double level = fabs(buf[index].left);
        weight += level;
        sum += level * (index - from);
    }
    return weight > 0.0 ? (float)(sum / weight) : -1.0f;
}

static void run(struct sw_state *state, struct sw_stereo *buf, float swing)
{
    int start;
    sw_reset(state);
    for (start = 0; start < TOTAL; start += BLOCK) {
        int frames = TOTAL - start < BLOCK ? TOTAL - start : BLOCK;
        float at = (float)(start % PAIR) / (float)PAIR;
        sw_process(state, buf + start, frames, at, 1.0f / (float)PAIR, swing,
                   (float)PAIR);
    }
}

/* share of energy, after the first pair, that still sits within ±20 hz of
   440. a comb throws the tone into sidebands; a locked splice does not. */
static double purity(const struct sw_stereo *buf, int from, int to)
{
    int n = to - from;
    int k0 = (int)(420.0 * (double)n / 44100.0 + 0.5);
    int k1 = (int)(460.0 * (double)n / 44100.0 + 0.5);
    int k;
    int i;
    double energy = 0.0;
    double band = 0.0;
    for (i = 0; i < n; i++) {
        double sample = buf[from + i].left;
        energy += sample * sample;
    }
    for (k = k0; k <= k1; k++) {
        double angular = 2.0 * 3.141592653589793 * (double)k / (double)n;
        double coeff = 2.0 * cos(angular);
        double s1 = 0.0;
        double s2 = 0.0;
        double power;
        for (i = 0; i < n; i++) {
            double next = buf[from + i].left + coeff * s1 - s2;
            s2 = s1;
            s1 = next;
        }
        power = s2 * s2 + s1 * s1 - coeff * s1 * s2;
        /* the matching negative-frequency bin holds the other half. */
        band += 2.0 * power;
    }
    return energy > 0.0 ? band / ((double)n * energy) : 0.0;
}

int main(void)
{
    static struct sw_state state;
    struct sw_stereo *buf = calloc(TOTAL, sizeof(*buf));
    float swing = 2.0f / 3.0f;
    int pair;
    int index;

    if (!near(sw_delay_at(0.0f, swing), 0.0f, 1e-6f, "delay at start") ||
        !near(sw_delay_at(swing, swing), swing - 0.5f, 1e-6f,
              "delay at the swing point") ||
        !near(sw_delay_at(0.999999f, swing), 0.0f, 1e-4f, "delay at end") ||
        !near(sw_delay_at(0.3f, SW_STRAIGHT), 0.0f, 0.0f, "straight"))
        return 1;
    for (index = 0; index <= 1000; index++)
        if (sw_delay_at((float)index / 1000.0f, SW_HARDEST) < 0.0f) {
            printf("negative delay at %d\n", index);
            return 2;
        }
    if (!near(sw_swing_for(0.0f), 0.5f, 0.0f, "depth 0") ||
        !near(sw_swing_for(1.0f), 0.75f, 0.0f, "depth 1"))
        return 3;
    if (!near(sw_rate_at(0.3f, 2.0f / 3.0f), 0.75f, 1e-5f, "triplet slow") ||
        !near(sw_rate_at(0.8f, 2.0f / 3.0f), 1.5f, 1e-5f, "triplet fast") ||
        !near(sw_rate_at(0.3f, SW_HARDEST), 2.0f / 3.0f, 1e-5f, "hard slow") ||
        !near(sw_rate_at(0.9f, SW_HARDEST), 2.0f, 1e-5f, "hard fast") ||
        !near(sw_rate_at(0.3f, SW_STRAIGHT), 1.0f, 0.0f, "straight rate"))
        return 6;

    /* straight swing is the input, sample for sample. */
    for (index = 0; index < TOTAL; index++) {
        buf[index].left = sinf((float)index * 0.05f);
        buf[index].right = -buf[index].left;
    }
    run(&state, buf, SW_STRAIGHT);
    for (index = 0; index < TOTAL; index++)
        if (!near(buf[index].left, sinf((float)index * 0.05f), 1e-5f,
                  "passthrough"))
            return 4;

    /* a short click on every eighth: the downbeat stays, the off-beat moves
       from half the pair to the swing point. within 150 frames (3.4 ms). */
    for (index = 0; index < TOTAL; index++) {
        int in_half = index % (PAIR / 2);
        buf[index].left = buf[index].right = in_half < 32 ? 1.0f : 0.0f;
    }
    run(&state, buf, swing);
    for (pair = 1; pair < PAIRS; pair++) {
        int base = pair * PAIR;
        float on = centroid(buf, base - PAIR / 8, base + PAIR / 8) - PAIR / 8;
        float off = centroid(buf, base + PAIR / 4, base + 7 * PAIR / 8) + PAIR / 4;
        if (!near(on, 16.0f, 150.0f, "downbeat") ||
            !near(off, swing * PAIR + 16.0f, 150.0f, "off-beat"))
            return 5;
    }

    /* 440 hz through the warp. drop the first pair, while the grains fill. */
    {
        double triplet;
        double hard;
        double step = 2.0 * 3.141592653589793 * 440.0 / 44100.0;
        for (index = 0; index < TOTAL; index++) {
            buf[index].left = buf[index].right = sin(step * (double)index);
        }
        run(&state, buf, swing);
        triplet = purity(buf, PAIR, TOTAL);
        for (index = 0; index < TOTAL; index++) {
            buf[index].left = buf[index].right = sin(step * (double)index);
        }
        run(&state, buf, SW_HARDEST);
        hard = purity(buf, PAIR, TOTAL);
        if (triplet < 0.90 || hard < 0.90) {
            printf("purity %.3f %.3f, below 0.90\n", triplet, hard);
            free(buf);
            return 7;
        }
        printf("swang dsp: ok (purity %.3f %.3f)\n", triplet, hard);
    }
    free(buf);
    return 0;
}
