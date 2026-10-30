/* SPDX-License-Identifier: MPL-2.0
 * sidechain meter: a scrolling strip across the seam between the two
 * waveforms, drawn like a compressor's history view. the trigger's level
 * rises from the bottom in grey, its threshold is a dotted line, and the
 * gain reduction on the ducked channel hangs from the top in orange.
 *
 * the audio thread only folds each block into a peak and a deepest cut.
 * rbp's ui thread takes those every tick, pushes one column, and redraws.
 */

#ifndef RX3_SIDECHAIN_METER_H
#define RX3_SIDECHAIN_METER_H

#define SC_METER_WIDTH RX3_OVERLAY_WAVE_WIDTH
#define SC_METER_HEIGHT 44
#define SC_METER_X RX3_OVERLAY_WAVE_X
#define SC_METER_Y (RX3_OVERLAY_WAVE_Y + (int)RX3_OVERLAY_WAVE_STRIDE - SC_METER_HEIGHT / 2)
#define SC_METER_COLUMN 2
#define SC_METER_COLUMNS (SC_METER_WIDTH / SC_METER_COLUMN)
#define SC_METER_TICK_MS 33u
/* the meter closes once helix has been off for this long. */
#define SC_METER_IDLE_MS 500u
#define SC_METER_FLOOR_DB 48.0f
#define SC_METER_CUT_DB 24.0f

#define SC_COLOUR_BACK 0x0841u
#define SC_COLOUR_LEVEL 0x6b4du
#define SC_COLOUR_THRESHOLD 0xbdf7u
#define SC_COLOUR_CUT_FILL 0x7a00u
#define SC_COLOUR_CUT_LINE 0xfd20u

/* threshold 1.0 (above full scale) means no channel is the trigger. */
/* written by the audio thread, taken by the ui thread. a lost update only
   costs one column, so plain volatile stores are enough. */
static volatile float sc_meter_level_l2 = -40.0f;
static volatile float sc_meter_cut_l2;
static volatile float sc_meter_threshold_l2 = 1.0f;
static volatile unsigned sc_meter_blocks;

static struct rx3_overlay sc_meter_overlay;
static uint8_t sc_meter_level[SC_METER_COLUMNS];
static uint8_t sc_meter_cut[SC_METER_COLUMNS];
static float sc_meter_shown_threshold_l2 = 1.0f;
static unsigned sc_meter_head;
static unsigned sc_meter_tick_ms;
static unsigned sc_meter_seen_blocks;
static unsigned sc_meter_active_ms;
static unsigned sc_meter_failed_ms;

static void sc_meter_note_source(float level_l2, float threshold_l2)
{
    if (level_l2 > sc_meter_level_l2)
        sc_meter_level_l2 = level_l2;
    sc_meter_threshold_l2 = threshold_l2;
}

static void sc_meter_note_cut(float gain)
{
    float cut = sc_log2(gain);
    if (cut < sc_meter_cut_l2)
        sc_meter_cut_l2 = cut;
}

static void sc_meter_note_block(void)
{
    sc_meter_blocks++;
}

/* db below full scale, or of cut, to rows. */
static unsigned sc_meter_rows(float db, float range)
{
    float share = db / range;
    if (share < 0.0f)
        share = 0.0f;
    if (share > 1.0f)
        share = 1.0f;
    return (unsigned)(share * (float)SC_METER_HEIGHT + 0.5f);
}

static void sc_meter_push(void)
{
    float level_db = sc_meter_level_l2 * SC_DB_PER_L2;
    float cut_db = -sc_meter_cut_l2 * SC_DB_PER_L2;
    sc_meter_shown_threshold_l2 = sc_meter_threshold_l2;
    sc_meter_level_l2 = -40.0f;
    sc_meter_cut_l2 = 0.0f;
    sc_meter_threshold_l2 = 1.0f;
    sc_meter_level[sc_meter_head] =
        (uint8_t)sc_meter_rows(level_db + SC_METER_FLOOR_DB, SC_METER_FLOOR_DB);
    sc_meter_cut[sc_meter_head] = (uint8_t)sc_meter_rows(cut_db, SC_METER_CUT_DB);
    sc_meter_head = sc_meter_head + 1u < SC_METER_COLUMNS ? sc_meter_head + 1u : 0u;
}

/* oldest column on the left, newest on the right. */
static void sc_meter_draw(void)
{
    uint16_t *pixels = 0;
    unsigned int pitch = 0;
    unsigned threshold_row;
    unsigned column;
    unsigned row;
    if (!framework->overlays->lock(&sc_meter_overlay, &pixels, &pitch))
        return;
    threshold_row = SC_METER_HEIGHT -
                    sc_meter_rows(sc_meter_shown_threshold_l2 * SC_DB_PER_L2 +
                                      SC_METER_FLOOR_DB,
                                  SC_METER_FLOOR_DB);
    for (row = 0; row < SC_METER_HEIGHT; row++) {
        uint16_t *line = pixels + (size_t)row * pitch;
        unsigned from_bottom = SC_METER_HEIGHT - row;
        for (column = 0; column < SC_METER_COLUMNS; column++) {
            unsigned slot = sc_meter_head + column;
            unsigned level;
            unsigned cut;
            uint16_t colour = SC_COLOUR_BACK;
            unsigned x = column * SC_METER_COLUMN;
            if (slot >= SC_METER_COLUMNS)
                slot -= SC_METER_COLUMNS;
            level = sc_meter_level[slot];
            cut = sc_meter_cut[slot];
            if (from_bottom <= level)
                colour = SC_COLOUR_LEVEL;
            if (sc_meter_shown_threshold_l2 <= 0.0f && row == threshold_row &&
                !(column & 1u))
                colour = SC_COLOUR_THRESHOLD;
            if (cut && row < cut)
                colour = SC_COLOUR_CUT_FILL;
            if (cut && (row + 1u == cut || row + 2u == cut))
                colour = SC_COLOUR_CUT_LINE;
            line[x] = colour;
            line[x + 1u] = colour;
        }
    }
    framework->overlays->present(&sc_meter_overlay);
}

/* ui thread, from the draw watcher. open while helix is running. */
static void sc_meter_tick(void)
{
    unsigned now = sc_now_ms();
    unsigned blocks;
    if (now - sc_meter_tick_ms < SC_METER_TICK_MS)
        return;
    sc_meter_tick_ms = now;
    blocks = sc_meter_blocks;
    if (blocks != sc_meter_seen_blocks) {
        sc_meter_seen_blocks = blocks;
        sc_meter_active_ms = now ? now : 1u;
    }
    if (!framework->overlays->wave_up() || !sc_meter_active_ms ||
        now - sc_meter_active_ms > SC_METER_IDLE_MS) {
        framework->overlays->release(&sc_owner);
        memset(&sc_meter_overlay, 0, sizeof(sc_meter_overlay));
        return;
    }
    if (!sc_meter_overlay.handle) {
        struct rx3_overlay_rect rect = { SC_METER_X, SC_METER_Y,
                                         SC_METER_WIDTH, SC_METER_HEIGHT };
        if (sc_meter_failed_ms && now - sc_meter_failed_ms < 1000u)
            return;
        if (!framework->overlays->claim(&sc_owner, &rect,
                                        RX3_OVERLAY_BELOW, &sc_meter_overlay)) {
            sc_meter_failed_ms = now ? now : 1u;
            framework->log_line("sidechain: meter window could not be created");
            return;
        }
        sc_meter_failed_ms = 0;
        memset(sc_meter_level, 0, sizeof(sc_meter_level));
        memset(sc_meter_cut, 0, sizeof(sc_meter_cut));
    }
    sc_meter_push();
    sc_meter_draw();
}

#endif /* RX3_SIDECHAIN_METER_H */
