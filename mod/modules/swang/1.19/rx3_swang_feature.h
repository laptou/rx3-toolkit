/* SPDX-License-Identifier: MPL-2.0
 * SWANG on the SPIRAL beat fx, firmware 1.19: swing, by warping time
 * against the tempo master's beat grid.
 *
 * while SPIRAL is the settled beat fx, the core's broker skips the stock
 * spiral and hands its block (the beat fx channel, or the master bus) to
 * rx3_swang_dsp.h instead. the controls:
 *   DEPTH (BeatEffectManager +0xe8, the pending value; the skipped
 *     operateBeatEffect is what would copy it onto the effect) is the
 *     strength: straight fully left, the off-beat at 3/4 fully right.
 *   the BEAT arrows pick the swung note: 1/4 beat (16ths, the start),
 *     1/2 beat (8ths) or 1 beat (quarters). the screen shows that beat.
 *   TIME shifts the swing against the grid, in 32 steps per pair of notes.
 *     the screen shows the shift in ms.
 *   ON swings; OFF glides the warp back to straight. rbp's ON and OFF are
 *     the requested type (+0x50) turning spiral or 0, and SWANG owns the
 *     block across both (the broker's rx3_beat_fx_owned).
 * while it is on, each deck's waveform gains a shorter dimmer tick in every
 * gap between the beat ticks rbp already draws. the tick sits where the
 * swung off-beat lands, so DEPTH slides it and TIME shifts it.
 * the grid is the tempo MASTER deck's (PlayEngine +0x5c SyncMaster: +0x8
 * the deck, or +0x20 while +0x1c overrides; -1 is none). with no master it
 * is the beat fx channel's deck, and with neither the audio stays straight.
 */

#ifndef RX3_SWANG_FEATURE_H
#define RX3_SWANG_FEATURE_H

#include "rx3_swang_dsp.h"
#include "rx3_swang_labels.h"

#define BEAT_FX_SPIRAL 7
#define BEAT_FX_UI_SPIRAL 3
#define BEAT_FX_DISPLAY_SPIRAL 5
/* spiral's name: four play-screen captions and three select-grid tiles. */
#define SPIRAL_CAPTION_FIRST 0x1465u
#define SPIRAL_CAPTION_LAST 0x1468u
#define SPIRAL_SELECT_FIRST 0x15bfu
#define SPIRAL_SELECT_LAST 0x15c1u
#define PLAY_ENGINE_INSTANCE ((const uint8_t *const volatile *)0x011497d0)
/* BpmManager::getInstance()'s static, +0x1c the beat fx bpm x100. */
#define BFX_BPM_X100 (*(const volatile int32_t *)(0x01149378u + 0x1cu))
#define SW_NOTE_COUNT 3u
#define SW_PHASE_STEPS 32
#define SW_SAMPLE_RATE 44100.0f
/* the swing follows DEPTH and ON over about this many frames, so a change
   bends time instead of skipping audio. */
#define SW_GLIDE_FRAMES 11025.0f

/* the swung note in beats, and the beat label that names it on screen. */
static const float sw_note_beats[SW_NOTE_COUNT] = { 0.25f, 0.5f, 1.0f };
static const uint32_t sw_note_labels[SW_NOTE_COUNT] = { 5u, 6u, 8u };

/* the audio thread's beat clock for the grid deck. */
struct sw_clock {
    int deck;
    double beat;
    int frames;
    float step;    /* beats per frame */
};

static volatile unsigned sw_note;
static volatile int sw_phase;
static volatile long sw_positions[2] = { -1, -1 };
static struct sw_state sw_state;
static struct sw_clock sw_clock = { -1, 0.0, 0, 0.0f };
static float sw_swing = SW_STRAIGHT;
/* the ui thread's copy of the glide, and whether the waveform ticks show. */
static volatile float sw_swing_ui = SW_STRAIGHT;
static volatile int sw_ticks_on;
static int sw_settled;
static int sw_logged;

/* ui_PlayMode_Wave_SetUpHalfBeatData. rbp draws one tick per beat into the
   waveform buffer: downbeats from row 5, other beats from row 10, both
   ending at row 22. the prologue is ldr r12,[pc] ; push {r4-r11}. */
#define SW_HALFBEAT ((unsigned long)0x0024b72c)
#define SW_WAVE_PTR (*(uint8_t *volatile *)0x02498cc8u)
#define SW_WAVE_STRIDE 0x6f8u
#define SW_WAVE_DECK 0x60b10u
#define SW_WAVE_WIDTH 892u
#define SW_TICK_TOP 16u
#define SW_TICK_BOTTOM 23u
#define SW_TICK_COLOUR 0x632cu
#define SW_BEAT_RED 0xf800u
#define SW_BEAT_MARK 0xce59u
#define SW_BEAT_DIM 0x2c3au
static const uint8_t sw_halfbeat_guard[8] = {
    0xc0, 0xc3, 0x9f, 0xe5, 0xf0, 0x0f, 0x2d, 0xe9
};
typedef void (*sw_halfbeat_fn)(int deck, unsigned pixel, unsigned from,
                               unsigned to, uint16_t *mark);
static struct installed_hook sw_halfbeat_hook;
static sw_halfbeat_fn original_sw_halfbeat;
/* the beat-tick scan in progress, ui thread only. */
static int sw_scan_deck = -2;
static unsigned sw_scan_x;
static int sw_scan_have;
static float sw_scan_beat;
static unsigned sw_scan_px;

static int sw_master_deck(void)
{
    const uint8_t *engine = *PLAY_ENGINE_INSTANCE;
    const uint8_t *sync;
    int deck;
    if (!engine)
        return -1;
    sync = *(const uint8_t *const volatile *)(engine + 0x5cu);
    if (!sync)
        return -1;
    deck = *(const volatile uint8_t *)(sync + 0x1cu)
               ? *(const volatile int32_t *)(sync + 0x20u)
               : *(const volatile int32_t *)(sync + 0x08u);
    return deck == 0 || deck == 1 ? deck : -1;
}

static float sw_bpm_step(void)
{
    int32_t bpm = BFX_BPM_X100;
    if (bpm <= 0)
        bpm = 12000;
    return (float)bpm / (100.0f * 60.0f * SW_SAMPLE_RATE);
}

static float sw_fraction(double value)
{
    double whole = (double)(int32_t)value;
    double part = value - whole;
    if (part < 0.0)
        part += 1.0;
    return part >= 1.0 ? 0.0f : (float)part;
}

/* where the grid deck is, in beats, and how fast it moves. no grid, no
   playhead or a paused deck is 0. */
static int sw_clock_read(int deck, int frames, double *beat, float *step)
{
    long position;
    double now;
    if (deck < 0)
        return 0;
    position = sw_positions[deck];
    if (position < 0 || rx3_deck_beat((unsigned)deck, position, &now))
        return 0;
    if (sw_clock.deck != deck || sw_clock.frames <= 0) {
        sw_clock.step = sw_bpm_step();
    } else {
        double moved = now - sw_clock.beat;
        float measured = (float)(moved / (double)sw_clock.frames);
        if (moved == 0.0) {
            sw_clock.frames = frames;
            return 0;
        }
        /* a seek or a loop jump keeps the tempo it had. */
        if (measured > 0.0f && measured < 4.0f * sw_clock.step)
            sw_clock.step += 0.2f * (measured - sw_clock.step);
    }
    sw_clock.deck = deck;
    sw_clock.beat = now;
    sw_clock.frames = frames;
    *beat = now;
    *step = sw_clock.step;
    return sw_clock.step > 0.0f;
}

static int swang_feature_beat_fx_operate(void *manager, Float2 *buffer,
                                         int frames)
{
    const uint8_t *fields = (const uint8_t *)manager;
    unsigned note = sw_note;
    float pair_beats = 2.0f * sw_note_beats[note < SW_NOTE_COUNT ? note : 0u];
    float swing = SW_STRAIGHT;
    float glide = (float)frames / SW_GLIDE_FRAMES;
    float pair_at = 0.0f;
    float pair_step = 0.0f;
    float pair_frames = 0.0f;
    double beat;
    float step;
    int deck;
    int clocked = 0;
    unsigned channel;
    if (!rx3_beat_fx_owned(fields, BEAT_FX_SPIRAL)) {
        sw_settled = 0;
        sw_ticks_on = 0;
        return 0;
    }
    if (!sw_settled) {
        sw_reset(&sw_state);
        sw_clock.deck = -1;
        sw_swing = SW_STRAIGHT;
        sw_settled = 1;
        if (!sw_logged) {
            sw_logged = 1;
            log_line("swang: SPIRAL is now SWANG");
        }
    }
    channel = *(const uint32_t *)fields;
    deck = sw_master_deck();
    if (deck < 0 && channel < 2u)
        deck = (int)channel;
    if (sw_clock_read(deck, frames, &beat, &step)) {
        double pairs = beat / (double)pair_beats -
                       (double)sw_phase / (double)SW_PHASE_STEPS;
        pair_at = sw_fraction(pairs);
        pair_step = step / pair_beats;
        pair_frames = pair_beats / step;
        clocked = 1;
        if (rx3_beat_fx_on(fields, BEAT_FX_SPIRAL))
            swing = sw_swing_for(*(const float *)(fields + 0xe8u));
    }
    if (glide > 1.0f)
        glide = 1.0f;
    sw_swing += (swing - sw_swing) * glide;
    sw_process(&sw_state, (struct sw_stereo *)buffer, frames, pair_at,
               pair_step, sw_swing, pair_frames,
               (int)(pair_beats * 2.0f + 0.5f));
    sw_swing_ui = sw_swing;
    sw_ticks_on = clocked && (rx3_beat_fx_on(fields, BEAT_FX_SPIRAL) ||
                              sw_swing > SW_STRAIGHT + 0.002f);
    return 1;
}

/* a column rbp just painted as a beat tick: the same colour on the rows
   every beat mark covers. */
static int swang_beat_painted(int deck, unsigned x)
{
    uint8_t *base = SW_WAVE_PTR;
    uint16_t sample[3];
    unsigned index;
    static const unsigned rows[3] = { 16u, 19u, 22u };
    if (!base || x >= SW_WAVE_WIDTH)
        return 0;
    for (index = 0; index < 3u; index++)
        sample[index] = *(const uint16_t *)(base + (unsigned)deck * SW_WAVE_DECK +
                                            rows[index] * SW_WAVE_STRIDE +
                                            x * 2u);
    if (sample[0] != sample[1] || sample[1] != sample[2])
        return 0;
    return sample[0] == SW_BEAT_RED || sample[0] == SW_BEAT_MARK ||
           sample[0] == SW_BEAT_DIM;
}

/* shorter and dimmer than the beat ticks, in the same gutter. */
static void swang_paint_tick(int deck, int x)
{
    uint8_t *base = SW_WAVE_PTR;
    unsigned row;
    if (!base || x < 0 || x >= (int)SW_WAVE_WIDTH)
        return;
    for (row = SW_TICK_TOP; row < SW_TICK_BOTTOM; row++)
        *(uint16_t *)(base + (unsigned)deck * SW_WAVE_DECK +
                      row * SW_WAVE_STRIDE + (unsigned)x * 2u) = SW_TICK_COLOUR;
}

/* called after rbp's own tick for this column. a gap is drawn once its
   right-hand beat has been painted, still inside this frame. noinline so
   the softfp float work cannot run before the hard-float original. */
static void __attribute__((noinline))
swang_note_beat_tick(int deck, unsigned pixel, uint16_t *mark)
{
    unsigned beat;
    unsigned note;
    int this_mod;
    int prev_mod;
    int delta;
    int xs[8];
    int count;
    int index;
    float right_beat;
    if (!sw_ticks_on || !mark || deck < 0 || deck > 1) {
        sw_scan_have = 0;
        sw_scan_deck = -2;
        return;
    }
    if (sw_scan_deck != deck || pixel < sw_scan_x) {
        sw_scan_have = 0;
        sw_scan_deck = deck;
    } else if (pixel == sw_scan_x) {
        return;
    }
    sw_scan_x = pixel;
    beat = *mark;
    if (beat < 1u || beat > 5u || !swang_beat_painted(deck, pixel))
        return;
    this_mod = (int)(beat == 5u ? 1u : beat) - 1;
    if (!sw_scan_have) {
        sw_scan_have = 1;
        sw_scan_beat = (float)this_mod;
        sw_scan_px = pixel;
        return;
    }
    /* a downbeat is painted on two columns. that is one beat, not a bar. */
    if (this_mod == ((int)sw_scan_beat & 3) && pixel - sw_scan_px <= 2u)
        return;
    prev_mod = (int)sw_scan_beat & 3;
    delta = this_mod - prev_mod;
    if (delta <= 0)
        delta += 4;
    right_beat = sw_scan_beat + (float)delta;
    note = sw_note < SW_NOTE_COUNT ? sw_note : 0u;
    count = sw_offbeat_xs(sw_scan_beat, right_beat, (int)sw_scan_px,
                          (int)pixel, 2.0f * sw_note_beats[note],
                          (float)sw_phase / (float)SW_PHASE_STEPS, sw_swing_ui,
                          xs, 8);
    for (index = 0; index < count; index++)
        swang_paint_tick(deck, xs[index]);
    sw_scan_beat = right_beat;
    sw_scan_px = pixel;
}

/* s0 and s1 are rbp's zoom, passed in the hard-float registers. this
   translation unit is softfp, so the wrapper takes only the integer args
   and calls the original before any of our own float work. */
static void swang_halfbeat(int deck, unsigned pixel, unsigned from,
                           unsigned to, uint16_t *mark)
{
    original_sw_halfbeat(deck, pixel, from, to, mark);
    (void)from;
    (void)to;
    swang_note_beat_tick(deck, pixel, mark);
}

static void swang_feature_playhead(unsigned int deck, long position)
{
    if (deck < 2u)
        sw_positions[deck] = position;
}

/* the BEAT arrows pick the swung note while spiral is selected. */
static int swang_feature_beat_fx_beat(void *innards, long delta)
{
    long note;
    if (*(const int *)((const uint8_t *)innards + 0x3cu) != BEAT_FX_UI_SPIRAL)
        return 0;
    note = (long)sw_note + delta;
    if (note < 0)
        note = 0;
    if (note >= (long)SW_NOTE_COUNT)
        note = (long)SW_NOTE_COUNT - 1;
    sw_note = (unsigned)note;
    return 1;
}

/* TIME turns the swing against the grid, wrapping at a whole pair. while
   +0xe9 is set the knob is adjusting the bpm, and that stays stock. */
static int swang_feature_beat_fx_time(void *innards, long delta)
{
    const uint8_t *fields = (const uint8_t *)innards;
    long phase;
    if (*(const int *)(fields + 0x3cu) != BEAT_FX_UI_SPIRAL || fields[0xe9u])
        return 0;
    phase = (long)sw_phase + delta;
    while (phase >= SW_PHASE_STEPS)
        phase -= SW_PHASE_STEPS;
    while (phase < 0)
        phase += SW_PHASE_STEPS;
    sw_phase = (int)phase;
    return 1;
}

/* the beat shows the swung note and the time shows the shift in ms. */
static void swang_feature_beat_fx_info(uint8_t *block)
{
    unsigned note = sw_note < SW_NOTE_COUNT ? sw_note : 0u;
    int32_t bpm = BFX_BPM_X100;
    float ms;
    if (*(const int *)block != BEAT_FX_DISPLAY_SPIRAL)
        return;
    if (bpm <= 0)
        bpm = 12000;
    ms = (float)sw_phase / (float)SW_PHASE_STEPS * 2.0f *
         sw_note_beats[note] * 6000000.0f / (float)bpm;
    *(uint32_t *)(block + 0x10u) = sw_note_labels[note];
    *(uint32_t *)(block + 0x14u) = (uint32_t)(ms + 0.5f);
}

static uint32_t swang_feature_image_swap(uint32_t image_id)
{
    if ((image_id >= SPIRAL_CAPTION_FIRST && image_id <= SPIRAL_CAPTION_LAST) ||
        (image_id >= SPIRAL_SELECT_FIRST && image_id <= SPIRAL_SELECT_LAST))
        return rx3_relabel_image(image_id, &sw_label_caption);
    return image_id;
}

static int swang_feature_configured(void)
{
    const char *flag = getenv("RX3_SWANG");
    return flag && flag[0] == '1';
}

/* without the broker's hooks spiral would play stock and the arrows would
   move its beat, and without the deck tap there is no grid. the waveform
   ticks are optional: a rejected guard leaves the beat marks stock. */
static int swang_feature_install(void)
{
    if (!original_operate_beat_effect || !original_beat_fx_beat_event ||
        !original_beat_fx_time_event || !rx3_deck_tap_ready())
        return 0;
    original_sw_halfbeat = (sw_halfbeat_fn)install_pc_ldr_hook(
        &sw_halfbeat_hook, SW_HALFBEAT, sw_halfbeat_guard,
        (void *)swang_halfbeat, 12u);
    if (!original_sw_halfbeat)
        log_line("swang: waveform ticks stay stock");
    return 1;
}

static void swang_feature_remove(void)
{
    uninstall_hook(&sw_halfbeat_hook);
    original_sw_halfbeat = 0;
    sw_ticks_on = 0;
}

#endif /* RX3_SWANG_FEATURE_H */
