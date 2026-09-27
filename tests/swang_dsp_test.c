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
    free(buf);
    puts("swang dsp: ok");
    return 0;
}
