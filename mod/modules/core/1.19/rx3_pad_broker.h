/* SPDX-License-Identifier: MPL-2.0
 * the performance-pad broker, firmware 1.19.
 *
 * onKey_Pad can carry one hook. stems takes slip-loop pads 7 and 8, and
 * sprites takes beat jump page 2, so the core owns the site and asks each
 * active feature. a feature returns nonzero to swallow the event; otherwise
 * stock runs. the hook is installed only when a feature listens.
 */

#ifndef RX3_PAD_BROKER_H
#define RX3_PAD_BROKER_H

static int hooked_on_key_pad(void *player_innards, const void *key_input)
{
    unsigned int index;
    for (index = 0; index < RUNTIME_FEATURE_COUNT; index++)
        if (runtime_features[index].active &&
            runtime_features[index].on_pad &&
            runtime_features[index].on_pad(player_innards, key_input))
            return 1;
    return original_on_key_pad(player_innards, key_input);
}

static void install_pad_broker(void)
{
    unsigned int index;
    if (original_on_key_pad)
        return;
    for (index = 0; index < RUNTIME_FEATURE_COUNT; index++)
        if (runtime_features[index].active && runtime_features[index].on_pad)
            break;
    if (index == RUNTIME_FEATURE_COUNT)
        return;
    original_on_key_pad = (on_key_pad_fn)install_hook(
        &pad_hook, ON_KEY_PAD, pad_guard, (void *)hooked_on_key_pad);
    if (!original_on_key_pad)
        log_line("warning: onKey_Pad guard rejected");
}

static void remove_pad_broker(void)
{
    uninstall_hook(&pad_hook);
    original_on_key_pad = 0;
}

#endif /* RX3_PAD_BROKER_H */
