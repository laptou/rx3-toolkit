/* SPDX-License-Identifier: MPL-2.0 */
/* runs the player's swang dsp over raw audio on the host, so the effect can
   be heard without the player. stdin and stdout are 44.1 khz stereo float32
   (f32le); scripts/swang_render.py does the decoding around it.

   usage: swang_render BPM PAIR_BEATS SWING FIRST_BEAT_FRAMES */

#include "mod/modules/swang/1.19/rx3_swang_dsp.h"

#include <stdio.h>
#include <stdlib.h>

#define BLOCK 256

int main(int argc, char **argv)
{
    static struct sw_state state;
    struct sw_stereo buf[BLOCK];
    double beats_per_frame;
    double pair_beats;
    double first_beat;
    float swing;
    long long frame = 0;
    size_t got;

    if (argc != 5) {
        fprintf(stderr,
                "usage: %s BPM PAIR_BEATS SWING FIRST_BEAT_FRAMES\n", argv[0]);
        return 2;
    }
    beats_per_frame = atof(argv[1]) / 60.0 / 44100.0;
    pair_beats = atof(argv[2]);
    swing = (float)atof(argv[3]);
    first_beat = atof(argv[4]);
    if (beats_per_frame <= 0.0 || pair_beats <= 0.0) {
        fprintf(stderr, "BPM and PAIR_BEATS must be positive\n");
        return 2;
    }

    sw_reset(&state);
    while ((got = fread(buf, sizeof(buf[0]), BLOCK, stdin)) > 0) {
        /* the player reads the grid once per block and steps from there. */
        double pairs = ((double)frame - first_beat) * beats_per_frame / pair_beats;
        double at = pairs - (double)(long long)pairs;
        if (at < 0.0)
            at += 1.0;
        sw_process(&state, buf, (int)got, (float)at,
                   (float)(beats_per_frame / pair_beats), swing,
                   (float)(pair_beats / beats_per_frame));
        if (fwrite(buf, sizeof(buf[0]), got, stdout) != got)
            return 1;
        frame += (long long)got;
    }
    return 0;
}
