/* SPDX-License-Identifier: MPL-2.0
 * band-pass on the DUB ECHO sound color fx, firmware 1.19.
 *
 * each mixer channel runs SoundColorFxManager::operate(manager, buf, frames)
 * once per block on the audio thread. while DUB ECHO is the selected type
 * (manager +0x24 == 4) the stock echo is skipped and the block goes through
 * the band-pass instead. because the manager never switches to its echo
 * slot, the knobs keep landing on the active effect (*manager): +0x1c
 * color, +0x20 parameter. manager +0x84 is the MixerChannel, whose +0x14 is
 * the input (0 or 1).
 */

#ifndef RX3_BANDPASS_FEATURE_H
#define RX3_BANDPASS_FEATURE_H

#include "rx3_bandpass_dsp.h"

#define COLOR_FX_OPERATE ((unsigned long)0x000bbc68)
#define COLOR_FX_DUB_ECHO 4

typedef void (*color_fx_operate_fn)(void *, Float2 *, int);

/* stmdb sp!,{r3-r8,r10,lr} ; vpush {d8} */
static const uint8_t color_fx_operate_guard[8] = {
    0xf8, 0x45, 0x2d, 0xe9, 0x02, 0x8b, 0x2d, 0xed
};

/* per input: high-pass then low-pass, each for left and right. running is
   cleared whenever the channel leaves DUB ECHO, so the filters start from
   silence rather than from a stale block. */
struct bp_channel {
    struct bp_state high[2];
    struct bp_state low[2];
    int running;
};

static struct installed_hook color_fx_operate_hook;
static color_fx_operate_fn original_color_fx_operate;
static struct bp_channel bp_channels[2];
static int bp_logged;

static void bp_process(struct bp_channel *self, const uint8_t *effect,
                       Float2 *buf, int frames)
{
    struct bp_band band = bp_band_for(*(const float *)(effect + 0x1cu),
                                      *(const float *)(effect + 0x20u));
    struct bp_svf high = bp_svf_for(band.low_edge_hz, band.q);
    struct bp_svf low = bp_svf_for(band.high_edge_hz, band.q);
    int index;
    if (!self->running) {
        memset(self, 0, sizeof(*self));
        self->running = 1;
    }
    for (index = 0; index < frames; index++) {
        buf[index].left =
            band.gain *
            bp_svf_step(&low, &self->low[0],
                        bp_svf_step(&high, &self->high[0], buf[index].left, 1),
                        0);
        buf[index].right =
            band.gain *
            bp_svf_step(&low, &self->low[1],
                        bp_svf_step(&high, &self->high[1], buf[index].right, 1),
                        0);
    }
}

static void hooked_color_fx_operate(void *manager, Float2 *buf, int frames)
{
    const uint8_t *fields = (const uint8_t *)manager;
    const uint8_t *channel = *(const uint8_t *const *)(fields + 0x84u);
    const uint8_t *effect = *(const uint8_t *const *)fields;
    unsigned input = channel ? *(const uint32_t *)(channel + 0x14u) : 2u;
    if (input >= 2u || !effect || !buf || frames <= 0 ||
        *(const int *)(fields + 0x24u) != COLOR_FX_DUB_ECHO) {
        if (input < 2u)
            bp_channels[input].running = 0;
        original_color_fx_operate(manager, buf, frames);
        return;
    }
    if (!bp_logged) {
        bp_logged = 1;
        log_line("bandpass: DUB ECHO is now the band-pass filter");
    }
    bp_process(&bp_channels[input], effect, buf, frames);
}

static int bandpass_feature_configured(void)
{
    const char *flag = getenv("RX3_BANDPASS");
    return flag && flag[0] == '1';
}

static int bandpass_feature_install(void)
{
    original_color_fx_operate = (color_fx_operate_fn)install_hook(
        &color_fx_operate_hook, COLOR_FX_OPERATE, color_fx_operate_guard,
        (void *)hooked_color_fx_operate);
    return original_color_fx_operate != 0;
}

static void bandpass_feature_remove(void)
{
    uninstall_hook(&color_fx_operate_hook);
    original_color_fx_operate = 0;
}

#endif /* RX3_BANDPASS_FEATURE_H */
