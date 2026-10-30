/* SPDX-License-Identifier: MPL-2.0
 * sidechain on the HELIX beat fx, firmware 1.19.
 *
 * rbp sums the mix in software. every block, MixerChannel::updateFilter
 * leaves each deck's post-fader output for the master to sum, and the audio
 * service's channel stage hands that block to sc_channel below. while HELIX
 * is the beat fx and
 * it is ON, the beat fx channel (CH1 or CH2) is the trigger and the other
 * deck is ducked by its level. that level is taken through a 150 hz
 * low-pass, so the duck follows the kick and bass.
 *
 * the stock helix never runs: from ON through OFF the beat fx service's
 * rx3_beatfx_owned holds, the operate claim returns at once, and the
 * trigger channel plays dry. ON is the requested type being helix, 14; OFF
 * requests type 0. the controls are read from the BeatEffectManager
 * (MixerChannel +0x218):
 *   +0x00 beat fx channel (0 CH1, 1 CH2, others are mic, master, aux)
 *   +0x08 active effect: +0x24 TIME in ms
 *   +0xe8 DEPTH 0..1, as the dial left it; operateBeatEffect is what copies
 *         it onto the effect, and it no longer runs
 * the BEAT arrows step the ratio: MasterInnards::onEv_BeatFxBeat(this,
 * delta) is taken over while its type (+0x3c, ui numbering) is helix, 4.
 *
 * on screen, the play screen's beat fx block names its beat number by a
 * label index (+0x10); each ratio writes its own label there, so rbp
 * redraws when the ratio changes. the caption images and the beat number's
 * lettering keep their stock artwork in this port: relabelling them needs
 * the stock records' measured sizes, which only a player or an imagedata
 * dump can give.
 *
 * CH1 runs before CH2, so a CH2 trigger keys CH1 one block late.
 */

#ifndef RX3_SIDECHAIN_FEATURE_H
#define RX3_SIDECHAIN_FEATURE_H

#include "rx3_sidechain_dsp.h"

#define BEAT_FX_HELIX 14
#define BEAT_FX_UI_HELIX 4
/* the play screen's display numbering (CSWTCH.55 of the ui type). */
#define BEAT_FX_DISPLAY_HELIX 13
static const unsigned char sc_owner;

struct sc_deck {
    volatile float level_l2;
    float gain;
};

/* the beat labels the ratios borrow: 1/16 1/8 1/4 1/2 3/4 16 32 64. */
/* stock label indices the ratios borrow for the beat number: 1/16 1/8 1/4
   1/2 3/4 16 32 64. */
static const uint32_t sc_ratio_beat_labels[SC_RATIO_COUNT] = {
    3u, 4u, 5u, 6u, 7u, 12u, 13u, 14u,
};

static unsigned sc_now_ms(void)
{
    struct sc_time { long seconds, microseconds; } stamp;
    if (gettimeofday(&stamp, 0))
        return 0;
    return (unsigned)(((uint64_t)(unsigned long)stamp.seconds * 1000000u +
                      (uint64_t)(unsigned long)stamp.microseconds) / 1000u);
}

static struct sc_deck sc_decks[2] = { { -40.0f, 1.0f }, { -40.0f, 1.0f } };
/* each deck's key filter, left and right. */
static struct sc_key_filter sc_keys[2][2];
static volatile unsigned sc_ratio_index = SC_RATIO_DEFAULT;
static int sc_logged;

#include "rx3_sidechain_meter.h"

/* the block's peak through the key filter. this level is what the meter
   draws and what the other deck ducks by. */
static float sc_block_peak_l2(struct sc_key_filter key[2],
                              const struct rx3_stereo *buf, int frames)
{
    float peak = 0.0f;
    int index;
    for (index = 0; index < frames; index++) {
        float left = sc_key_step(&key[0], buf[index].left);
        float right = sc_key_step(&key[1], buf[index].right);
        if (left < 0.0f)
            left = -left;
        if (right < 0.0f)
            right = -right;
        if (left > peak)
            peak = left;
        if (right > peak)
            peak = right;
    }
    sc_key_settle(&key[0]);
    sc_key_settle(&key[1]);
    return sc_log2(peak);
}

/* moves the deck's gain toward target and ramps it across the block so a
   gain change never clicks. */
static void sc_apply_gain(struct sc_deck *self, float target, float release,
                          struct rx3_stereo *buf, int frames)
{
    float start = self->gain;
    float step;
    int index;
    self->gain = target +
                 (start - target) *
                     sc_block_keep(target < start ? sc_attack_ms(release)
                                                  : release,
                                   frames);
    if (self->gain > 0.9999f)
        self->gain = 1.0f;
    if (start == 1.0f && self->gain == 1.0f)
        return;
    step = (self->gain - start) / (float)frames;
    for (index = 0; index < frames; index++) {
        float gain = start + step * (float)(index + 1);
        buf[index].left *= gain;
        buf[index].right *= gain;
    }
}

static void sc_channel(unsigned int deck, void *channel,
                        struct rx3_stereo *output, unsigned int frames)
{
    struct sc_deck *self = &sc_decks[deck];
    const uint8_t *manager =
        *(const uint8_t *const *)((const uint8_t *)channel + 0x218u);
    const uint8_t *effect;
    unsigned trigger;
    float threshold;
    float release;
    self->level_l2 = sc_block_peak_l2(sc_keys[deck], output, frames);
    effect = manager ? *(const uint8_t *const *)(manager + 0x08u) : 0;
    trigger = manager ? *(const uint32_t *)manager : 2u;
    if (!effect || trigger >= 2u || !rx3_beatfx_on(manager, BEAT_FX_HELIX)) {
        sc_apply_gain(self, 1.0f, 50.0f, output, (int)frames);
        return;
    }
    if (!sc_logged) {
        sc_logged = 1;
        framework->log_line("sidechain: HELIX is now the sidechain compressor");
    }
    threshold = sc_threshold_l2(*(const float *)(manager + 0xe8u));
    release = sc_release_ms(*(const uint32_t *)(effect + 0x24u));
    sc_meter_note_block();
    if (deck == trigger) {
        sc_meter_note_source(self->level_l2, threshold);
        sc_apply_gain(self, 1.0f, release, output, (int)frames);
        return;
    }
    sc_apply_gain(self,
                  sc_exp2(sc_gain_l2(sc_decks[trigger].level_l2, threshold,
                                     sc_ratios[sc_ratio_index])),
                  release, output, (int)frames);
    sc_meter_note_cut(self->gain);
}

/* helix stays dry: its wet path is skipped while sidechain owns the block.
   a switch to or from helix still runs stock, so rbp's crossfade happens. */
static int sc_operate(void *manager, struct rx3_stereo *buffer,
                       unsigned int frames)
{
    (void)buffer;
    (void)frames;
    return rx3_beatfx_owned(manager, BEAT_FX_HELIX);
}

/* the BEAT arrows pick the ratio while helix is the selected beat fx. */
static int sc_beat(void *innards, long delta)
{
    long index;
    if (*(const int *)((const uint8_t *)innards + 0x3cu) != BEAT_FX_UI_HELIX)
        return 0;
    index = (long)sc_ratio_index + delta;
    if (index < 0)
        index = 0;
    if (index >= (long)SC_RATIO_COUNT)
        index = (long)SC_RATIO_COUNT - 1;
    sc_ratio_index = (unsigned)index;
    return 1;
}

static void sc_info(uint8_t *block)
{
    if (*(const int *)block == BEAT_FX_DISPLAY_HELIX)
        *(uint32_t *)(block + 0x10u) = sc_ratio_beat_labels[sc_ratio_index];
}

/* the draw watcher: the core calls this after the player drew an image,
   on its ui thread, and the meter keeps step from there. */
static void sc_drawn(unsigned image)
{
    (void)image;
    sc_meter_tick();
}

static int sidechain_feature_configured(void)
{
    const char *flag = getenv("RX3_SIDECHAIN");
    return flag && flag[0] == '1';
}

/* Returns 0 on any refusal; the core then calls remove(), which takes out
   whatever part of this had already been put in. Without the channel stage
   and the operate and beat claims, helix would play wet and the arrows
   would change its beat, so the module does not start. */
static int sidechain_feature_install(void)
{
    if (!framework->audio->claim_channel(&sc_owner, sc_channel)) {
        framework->log_line("sidechain refused: the channel stage is taken");
        return 0;
    }
    if (!framework->beatfx->claim_operate(&sc_owner, 10u, sc_operate)) {
        framework->log_line("sidechain refused: the operate claim failed");
        return 0;
    }
    if (!framework->beatfx->claim_beat(&sc_owner, 10u, sc_beat)) {
        framework->log_line("sidechain refused: the beat claim failed");
        return 0;
    }
    if (!framework->beatfx->claim_info(&sc_owner, sc_info))
        framework->log_line("sidechain: the play screen block stays stock");
    if (!framework->overlays->watch_draw(&sc_owner, sc_drawn)) {
        framework->log_line("sidechain refused: no draw watcher slot");
        return 0;
    }
    return 1;
}

static void sidechain_feature_remove(void)
{
    framework->beatfx->unregister_owner(&sc_owner);
    framework->overlays->unwatch_draw(&sc_owner);
    framework->overlays->release(&sc_owner);
    memset(&sc_meter_overlay, 0, sizeof(sc_meter_overlay));
}

#endif /* RX3_SIDECHAIN_FEATURE_H */
