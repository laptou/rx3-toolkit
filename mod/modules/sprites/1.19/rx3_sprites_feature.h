/* SPDX-License-Identifier: MPL-2.0
 * usb sprite overlays for firmware 1.19.
 * rbp draws the scrolling waveforms into its own ds_gr window, and its
 * compositor puts that window above every ns window, so an image drawn
 * through pal_draw_image can never show over the waveform. each deck gets a
 * small ds_gr window of its own instead, placed on the deck's waveform strip
 * with a z above rbp's waveform window. packs are 8-bit indexed; the player
 * expands each frame to rgb565, index 0 becomes magenta, and the window's
 * colour key lets the waveform show through those pixels.
 */

#ifndef RX3_SPRITES_FEATURE_H
#define RX3_SPRITES_FEATURE_H

#include "rx3_sprites_logic.h"
#include "../../core/1.19/rx3_ds_gr.h"

#define SPRITE_PACK_LIMIT 64u
#define SPRITE_FILE_LIMIT (4u * 1024u * 1024u)
#define SPRITE_EDGE_LIMIT 256u
#define SPRITE_FRAMES_LIMIT 256u
/* the sprite's height on screen, 0.9 of one deck's waveform strip. */
#define SPRITE_BASE_HEIGHT ((WAVE_STRIP * 9) / 10)
/* the loudest pulse grows the sprite by this many thousandths. */
#define SPRITE_PULSE_MILLI 300u
/* one-pole low-pass coefficient for about 150 hz at 44.1 khz, applied
   twice, so the pulse follows the kick drum. */
#define SPRITE_BASS_POLE 0.0211f
/* the envelope falls to 1/e over this many samples, about 120 ms. */
#define SPRITE_FALL_SAMPLES 5292.0f
/* bass envelope (linear) that starts and fills the pulse. */
#define SPRITE_PULSE_FLOOR 0.06f
#define SPRITE_PULSE_FULL 0.7f
#define SPRITE_FRAME_MS 33u
#define SPRITE_RETRY_MS 1000u

struct __attribute__((packed)) rx3_sprite_file_header {
    char magic[8];
    uint16_t version;
    uint16_t header_size;
    uint16_t width;
    uint16_t height;
    uint16_t frame_count;
    uint16_t mode;
    uint16_t fps_num;
    uint16_t fps_den;
    uint16_t frames_per_beat;
    uint16_t beats_per_loop;
    uint16_t anchor;
    uint8_t format_byte;
    uint8_t palette_flag;
    uint8_t transparent;
    uint8_t bpp;
    uint16_t palette_count;
    uint16_t entry_size;
    uint32_t payload_length;
    uint32_t checksum;
    uint16_t name_length;
    uint16_t reserved;
};

_Static_assert(sizeof(struct rx3_sprite_file_header) == 50,
               "sprite header drifted from the host compiler");

struct sprite_job {
    unsigned deck;
    unsigned generation;
    unsigned frame_count;
    unsigned width;
    unsigned height;
    unsigned mode;
    unsigned fps_num;
    unsigned fps_den;
    unsigned frames_per_beat;
    unsigned beats_per_loop;
    unsigned anchor;
    unsigned palette_count;
    unsigned pixel_bytes;
    uint8_t *palette;
    uint8_t *pixels;
    void *block;
    size_t block_bytes;
    char name[48];
};

struct sprite_request {
    unsigned deck;
    unsigned generation;
    char stem[48];
};

struct sprite_deck {
    unsigned generation;
    int live;
    unsigned frame_count;
    unsigned width;
    unsigned height;
    unsigned mode;
    unsigned fps_num;
    unsigned fps_den;
    unsigned frames_per_beat;
    unsigned beats_per_loop;
    unsigned anchor;
    /* the pack file itself; frames stay 8-bit and are scaled as drawn. */
    void *block;
    size_t block_bytes;
    const uint8_t *pixels;
    unsigned pixel_bytes;
    uint16_t palette[256];
    unsigned epoch_ms;
    int have_phase;
    uint32_t phase_milli;
    /* written by the audio thread; a change between ui ticks means playing. */
    volatile long position;
    long seen_position;
    int playing;
    char name[48];
    char previous[48];
};

/* the one overlay window. shown is what was last drawn: deck, frame and
   size, or -1 after the window opens. */
struct sprite_window {
    struct rx3_gr_window gr;
    int shown;
    unsigned shown_deck;
    unsigned shown_height;
    unsigned failed_ms;
};

static const char *sprites_dir;
static uint32_t sprites_nonce = 1u;
static struct sprite_deck sprite_decks[2];
static struct sprite_job *volatile sprite_jobs[2];
static struct sprite_window sprite_window = { { 0, 0, 0, 0, 0 }, -1, 0, 0, 0 };
static unsigned sprite_lead;
static int sprite_pulse_on;

/* each deck's bass envelope, written by the audio thread. */
struct sprite_bass {
    float first;
    float second;
    volatile float envelope;
};

static struct sprite_bass sprite_bass[2];
static unsigned sprite_tick_ms;
static unsigned sprite_logged;

#include "rx3_sprites_menu.h"

static void *sprite_slurp(const char *path, size_t *size_out, size_t limit)
{
    int fd;
    off_t end;
    void *block;
    if (!path || !path[0])
        return 0;
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    end = lseek(fd, 0, SEEK_END);
    if (end <= 0 || (size_t)end > limit) {
        close(fd);
        return 0;
    }
    if (lseek(fd, 0, SEEK_SET) < 0) {
        close(fd);
        return 0;
    }
    block = mmap(0, (size_t)end, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (block == MAP_FAILED || read_exactly(fd, block, (size_t)end)) {
        close(fd);
        if (block != MAP_FAILED)
            munmap(block, (size_t)end);
        return 0;
    }
    close(fd);
    *size_out = (size_t)end;
    return block;
}

/* read the first bytes of a larger file. sprite_slurp refuses anything past its limit. */
static void *sprite_prefix(const char *path, size_t bytes)
{
    int fd;
    void *block;
    if (!path || !path[0] || !bytes)
        return 0;
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    block = mmap(0, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (block == MAP_FAILED || read_exactly(fd, block, bytes)) {
        close(fd);
        if (block != MAP_FAILED)
            munmap(block, bytes);
        return 0;
    }
    close(fd);
    return block;
}

static int sprite_join(char *output, size_t capacity, const char *name)
{
    size_t length = 0;
    size_t name_length;
    if (!sprites_dir)
        return -1;
    while (sprites_dir[length] && length + 1u < capacity) {
        output[length] = sprites_dir[length];
        length++;
    }
    if (sprites_dir[length])
        return -1;
    name_length = str_length(name);
    if (length && output[length - 1u] != '/') {
        if (length + 1u >= capacity)
            return -1;
        output[length++] = '/';
    }
    if (length + name_length + 1u > capacity)
        return -1;
    memcpy(output + length, name, name_length + 1u);
    return 0;
}

static void sprite_log_once(unsigned bit, const char *message)
{
    if (sprite_logged & bit)
        return;
    sprite_logged |= bit;
    log_line(message);
}


static int sprite_header_ok(struct rx3_sprite_file_header *header, size_t size)
{
    uint32_t digest;
    uint32_t stored;
    unsigned frame_bytes;
    if (size < 64u || header->header_size != 64u || header->version != 1u)
        return -1;
    {
        const uint8_t *pad = (const uint8_t *)header +
                             sizeof(struct rx3_sprite_file_header);
        unsigned index;
        for (index = 0; index < 14u; index++)
            if (pad[index])
                return -1;
    }
    if (memcmp(header->magic, "RX3SPR1", 7) || header->magic[7] != 0)
        return -1;
    if (size != 64u + header->payload_length)
        return -1;
    if (header->width < 1u || header->width > SPRITE_EDGE_LIMIT ||
        header->height < 1u || header->height > SPRITE_EDGE_LIMIT ||
        header->frame_count < 1u || header->frame_count > SPRITE_FRAMES_LIMIT)
        return -1;
    /* the one pack encoding: 8-bit indices into rgb565 entries, index 0
       transparent. */
    if (header->format_byte != 2u || header->palette_flag != 1u ||
        header->transparent != 0u || header->bpp != 8u ||
        header->entry_size != 2u)
        return -1;
    stored = header->checksum;
    header->checksum = 0u;
    digest = rx3_sprite_checksum((const uint8_t *)header, size);
    header->checksum = stored;
    if (digest != stored)
        return -1;
    frame_bytes = header->bpp == 8u
        ? (unsigned)header->width * header->height
        : ((unsigned)header->width * header->height + 1u) / 2u;
    if (header->name_length > 44u)
        return -1;
    if ((size_t)header->name_length +
            (size_t)header->palette_count * header->entry_size +
            (size_t)frame_bytes * header->frame_count !=
        header->payload_length)
        return -1;
    if (header->mode == 1u) {
        if (!header->fps_num || !header->fps_den ||
            header->frames_per_beat || header->beats_per_loop)
            return -1;
    } else if (header->mode == 2u) {
        if (header->fps_num || header->fps_den ||
            !header->frames_per_beat || !header->beats_per_loop ||
            header->frames_per_beat * header->beats_per_loop !=
                header->frame_count)
            return -1;
    } else {
        return -1;
    }
    if (header->anchor != 1u && header->anchor != 2u)
        return -1;
    return (int)frame_bytes;
}


static int sprite_publish(struct sprite_job *job)
{
    struct sprite_job *expected = 0;
    if (job->generation != sprite_decks[job->deck].generation) {
        munmap(job->block, job->block_bytes);
        munmap(job, 4096u);
        return -1;
    }
    if (!__sync_bool_compare_and_swap(&sprite_jobs[job->deck], expected, job)) {
        munmap(job->block, job->block_bytes);
        munmap(job, 4096u);
        return -1;
    }
    return 0;
}

static void sprite_note(unsigned bit, const char *message);

static void *sprite_loader(void *opaque)
{
    struct sprite_request *request = opaque;
    int failed = 1;
    char path[512];
    size_t catalog_size = 0;
    char *catalog = 0;
    const char *names[SPRITE_PACK_LIMIT];
    unsigned modes[SPRITE_PACK_LIMIT];
    unsigned count = 0;
    unsigned eligible[SPRITE_PACK_LIMIT];
    unsigned eligible_count = 0;
    unsigned chosen;
    unsigned index;
    size_t pack_size = 0;
    uint8_t *pack = 0;
    struct rx3_sprite_file_header *header;
    int frame_bytes;
    struct sprite_job *job;

    if (sprite_join(path, sizeof(path), "catalog.txt"))
        goto done;
    catalog = sprite_slurp(path, &catalog_size, 8192u);
    if (!catalog)
        goto done;
    {
        size_t cursor = 0;
        while (cursor < catalog_size && count < SPRITE_PACK_LIMIT) {
            size_t end = cursor;
            while (end < catalog_size && catalog[end] != '\n') {
                if (catalog[end] == '\r') {
                    munmap(catalog, catalog_size);
                    goto done;
                }
                end++;
            }
            if (end == catalog_size) {
                munmap(catalog, catalog_size);
                goto done;
            }
            catalog[end] = 0;
            if (catalog[cursor] && catalog[cursor] != '.') {
                names[count] = catalog + cursor;
                modes[count] = 0u;
                count++;
            }
            cursor = end + 1u;
        }
    }
    for (index = 0; index < count; index++) {
        struct rx3_sprite_file_header *peek_header;
        if (sprite_join(path, sizeof(path), names[index]))
            continue;
        peek_header = sprite_prefix(path, 64u);
        if (!peek_header)
            continue;
        modes[index] = peek_header->mode;
        munmap(peek_header, 64u);
        if (modes[index] == 1u || modes[index] == 2u)
            eligible[eligible_count++] = index;
    }
    if (!eligible_count) {
        sprite_log_once(8u, "sprites disabled: no eligible pack for this track");
        goto release_catalog;
    }
    {
        const char *eligible_names[SPRITE_PACK_LIMIT];
        unsigned pick;
        for (index = 0; index < eligible_count; index++)
            eligible_names[index] = names[eligible[index]];
        pick = rx3_sprite_pick(request->stem, request->deck, sprites_nonce,
                              eligible_names, eligible_count,
                              sprite_decks[request->deck].previous[0]
                                  ? sprite_decks[request->deck].previous : 0);
        chosen = eligible[pick];
    }
    if (sprite_join(path, sizeof(path), names[chosen]))
        goto release_catalog;
    pack = sprite_slurp(path, &pack_size, SPRITE_FILE_LIMIT);
    if (!pack)
        goto release_catalog;
    header = (struct rx3_sprite_file_header *)pack;
    frame_bytes = sprite_header_ok(header, pack_size);
    if (frame_bytes < 0) {
        sprite_log_once(16u, "sprites disabled: pack failed validation");
        munmap(pack, pack_size);
        goto release_catalog;
    }
    job = mmap(0, 4096u, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
               -1, 0);
    if (job == MAP_FAILED) {
        munmap(pack, pack_size);
        goto release_catalog;
    }
    memset(job, 0, sizeof(*job));
    job->deck = request->deck;
    job->generation = request->generation;
    job->frame_count = header->frame_count;
    job->width = header->width;
    job->height = header->height;
    job->mode = header->mode;
    job->fps_num = header->fps_num;
    job->fps_den = header->fps_den;
    job->frames_per_beat = header->frames_per_beat;
    job->beats_per_loop = header->beats_per_loop;
    job->anchor = header->anchor;
    job->palette_count = header->palette_count;
    job->pixel_bytes = (unsigned)frame_bytes;
    job->block = pack;
    job->block_bytes = pack_size;
    job->palette = pack + 64u + header->name_length;
    job->pixels = job->palette + header->palette_count * header->entry_size;
    if (header->name_length < sizeof(job->name))
        memcpy(job->name, pack + 64u, header->name_length);
    if (sprite_publish(job))
        goto release_catalog;
    failed = 0;
    sprite_note(128u, "sprite pack loaded");
release_catalog:
    if (catalog)
        munmap(catalog, catalog_size);
done:
    if (failed)
        sprite_note(32u, "sprite pack was not loaded");
    munmap(request, 4096u);
    return 0;
}

/* the pack's palette as rgb565. index 0, and an index past the palette,
   become the key colour. an opaque colour that equals the key moves one
   blue step so it still draws. */
static void sprite_palette(const struct sprite_job *job, uint16_t *palette)
{
    unsigned index;
    for (index = 0; index < 256u; index++) {
        uint16_t colour = DS_GR_KEY_RGB565;
        if (index && index < job->palette_count) {
            memcpy(&colour, job->palette + index * 2u, sizeof(colour));
            if (colour == DS_GR_KEY_RGB565)
                colour = DS_GR_KEY_RGB565 - 1u;
        }
        palette[index] = colour;
    }
}

static void sprite_drop_frames(struct sprite_deck *state)
{
    if (state->block)
        munmap(state->block, state->block_bytes);
    state->block = 0;
    state->block_bytes = 0;
    state->pixels = 0;
}

/* runs on rbp's ui thread, the only thread that reads the frames. */
static void sprite_take_jobs(void)
{
    unsigned deck;
    for (deck = 0; deck < 2u; deck++) {
        struct sprite_job *job = sprite_jobs[deck];
        struct sprite_deck *state;
        if (!job)
            continue;
        if (!__sync_bool_compare_and_swap(&sprite_jobs[deck], job, 0))
            continue;
        state = &sprite_decks[deck];
        if (job->generation != state->generation) {
            munmap(job->block, job->block_bytes);
            munmap(job, 4096u);
            continue;
        }
        sprite_drop_frames(state);
        state->live = 1;
        state->frame_count = job->frame_count;
        state->width = job->width;
        state->height = job->height;
        state->mode = job->mode;
        state->fps_num = job->fps_num;
        state->fps_den = job->fps_den;
        state->frames_per_beat = job->frames_per_beat;
        state->beats_per_loop = job->beats_per_loop;
        state->anchor = job->anchor;
        state->block = job->block;
        state->block_bytes = job->block_bytes;
        state->pixels = job->pixels;
        state->pixel_bytes = job->pixel_bytes;
        sprite_palette(job, state->palette);
        state->epoch_ms = now_ms();
        state->have_phase = 0;
        state->phase_milli = 0;
        memcpy(state->previous, job->name, sizeof(state->previous));
        memcpy(state->name, job->name, sizeof(state->name));
        munmap(job, 4096u);
    }
}

static unsigned sprite_notes;

static void sprite_note(unsigned bit, const char *message)
{
    const char *path;
    unsigned length = 0;
    int fd;
    if (sprite_notes & bit)
        return;
    sprite_notes |= bit;
    path = getenv("RX3_SPRITE_STATUS_PATH");
    if (!path || !path[0])
        return;
    while (message[length])
        length++;
    fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
        return;
    (void)write(fd, message, length);
    (void)write(fd, "\n", 1);
    close(fd);
}


static int sprite_frame_now(const struct sprite_deck *state, unsigned *frame)
{
    if (state->mode == 1u) {
        *frame = rx3_sprite_fps_frame(now_ms() - state->epoch_ms, state->fps_num,
                                     state->fps_den, state->frame_count);
        return 0;
    }
    if (state->mode == 2u &&
        !rx3_sprite_phase_frame(state->have_phase, state->phase_milli,
                                state->frames_per_beat, state->beats_per_loop,
                                state->frame_count, frame))
        return 0;
    sprite_log_once(128u, "sprites: beat clock unavailable");
    return -1;
}

/* the sprite's on-screen size at a pulse, scaled from its height to
   SPRITE_BASE_HEIGHT and kept inside the waveform view. */
static void sprite_size(const struct sprite_deck *state, unsigned pulse_milli,
                        unsigned *width, unsigned *height)
{
    unsigned tall = SPRITE_BASE_HEIGHT * (1000u + pulse_milli) / 1000u;
    unsigned wide = state->width * tall / state->height;
    if (wide > WAVE_WIDTH) {
        tall = tall * WAVE_WIDTH / wide;
        wide = WAVE_WIDTH;
    }
    *width = wide ? wide : 1u;
    *height = tall ? tall : 1u;
}

/* the window holds the largest pulse, centred on the whole waveform view,
   or resting on its bottom edge for anchor 2. */
static void sprite_frame_box(const struct sprite_deck *state, int *x, int *y,
                             unsigned *width, unsigned *height)
{
    sprite_size(state, sprite_pulse_on ? SPRITE_PULSE_MILLI : 0u, width, height);
    *x = WAVE_LEFT + (WAVE_WIDTH - (int)*width) / 2;
    if (state->anchor == 2u)
        *y = WAVE_TOP + 2 * WAVE_STRIP - (int)*height;
    else
        *y = WAVE_TOP + WAVE_STRIP - (int)*height / 2;
}

static void sprite_window_close(struct sprite_window *window)
{
    rx3_gr_close(&window->gr);
    window->shown = -1;
}

/* nearest-neighbour scale of one frame into the window, centred across and
   centred or bottom-aligned down, keyed out everywhere else. */
static void sprite_window_draw(struct sprite_window *window,
                               const struct sprite_deck *state, unsigned frame,
                               unsigned wide, unsigned tall)
{
    const uint8_t *source = state->pixels + (size_t)frame * state->pixel_bytes;
    unsigned left = (window->gr.width - wide) / 2u;
    unsigned top = state->anchor == 2u ? window->gr.height - tall
                                       : (window->gr.height - tall) / 2u;
    uint32_t step_x = (state->width << 16) / wide;
    uint32_t step_y = (state->height << 16) / tall;
    int pitch;
    uint8_t *pixels = rx3_gr_lock(&window->gr, &pitch);
    unsigned row;
    if (!pixels)
        return;
    for (row = 0; row < window->gr.height; row++) {
        uint16_t *line = (uint16_t *)(pixels + (size_t)row * (unsigned)pitch);
        unsigned column;
        if (row < top || row >= top + tall) {
            for (column = 0; column < window->gr.width; column++)
                line[column] = DS_GR_KEY_RGB565;
            continue;
        }
        {
            const uint8_t *source_row =
                source + (size_t)(((row - top) * step_y) >> 16) * state->width;
            uint32_t along = 0;
            for (column = 0; column < left; column++)
                line[column] = DS_GR_KEY_RGB565;
            for (column = 0; column < wide; column++) {
                line[left + column] = state->palette[source_row[along >> 16]];
                along += step_x;
            }
            for (column = left + wide; column < window->gr.width; column++)
                line[column] = DS_GR_KEY_RGB565;
        }
    }
    rx3_gr_present(&window->gr);
}

static int sprite_deck_showable(unsigned deck)
{
    const struct sprite_deck *state = &sprite_decks[deck];
    return state->live && state->pixels && rx3_wave_deck_loaded(deck);
}

/* 0..SPRITE_PULSE_MILLI from both decks' bass, squared so kicks jump out. */
static unsigned sprite_pulse_now(void)
{
    float level = sprite_bass[0].envelope + sprite_bass[1].envelope;
    float share = (level - SPRITE_PULSE_FLOOR) /
                  (SPRITE_PULSE_FULL - SPRITE_PULSE_FLOOR);
    if (!(share > 0.0f))
        return 0u;
    if (share > 1.0f)
        share = 1.0f;
    return (unsigned)(share * share * (float)SPRITE_PULSE_MILLI);
}

/* the deck the one sprite follows: a playing deck over a stopped one, the
   louder of two playing decks once it is twice as loud, and otherwise the
   current lead, so it does not flicker. */
static int sprite_pick_lead(void)
{
    unsigned deck;
    int best = -1;
    for (deck = 0; deck < 2u; deck++) {
        struct sprite_deck *state = &sprite_decks[deck];
        long position = state->position;
        state->playing = position != state->seen_position;
        state->seen_position = position;
    }
    for (deck = 0; deck < 2u; deck++) {
        unsigned candidate = deck ? sprite_lead ^ 1u : sprite_lead;
        if (!sprite_deck_showable(candidate))
            continue;
        if (best < 0 || (sprite_decks[candidate].playing &&
                         (!sprite_decks[best].playing ||
                          sprite_bass[candidate].envelope >
                              2.0f * sprite_bass[best].envelope)))
            best = (int)candidate;
    }
    return best;
}

/* keeps the window in step with the waveform screen: open while the screen
   is up and some deck has a sprite and a waveform, closed otherwise. */
static void sprite_update(int screen_up, unsigned now)
{
    struct sprite_window *window = &sprite_window;
    const struct sprite_deck *state;
    unsigned frame = 0;
    unsigned wide;
    unsigned tall;
    unsigned box_wide;
    unsigned box_tall;
    int lead = screen_up ? sprite_pick_lead() : -1;
    int x;
    int y;
    if (lead < 0 || sprite_frame_now(&sprite_decks[lead], &frame)) {
        sprite_window_close(window);
        return;
    }
    sprite_lead = (unsigned)lead;
    state = &sprite_decks[lead];
    sprite_frame_box(state, &x, &y, &box_wide, &box_tall);
    if (window->gr.handle &&
        (window->gr.x != x || window->gr.y != y ||
         window->gr.width != box_wide || window->gr.height != box_tall))
        sprite_window_close(window);
    if (!window->gr.handle) {
        if (window->failed_ms && now - window->failed_ms < SPRITE_RETRY_MS)
            return;
        if (rx3_gr_open(&window->gr, x, y, box_wide, box_tall,
                        RX3_GR_ON_TOP)) {
            window->failed_ms = now ? now : 1u;
            sprite_note(256u, "sprite window could not be created");
            return;
        }
        window->failed_ms = 0;
        window->shown = -1;
        sprite_note(4u, "sprite window open");
    }
    sprite_size(state, sprite_pulse_on ? sprite_pulse_now() : 0u, &wide, &tall);
    if (wide > box_wide)
        wide = box_wide;
    if (tall > box_tall)
        tall = box_tall;
    if (window->shown == (int)frame && window->shown_deck == sprite_lead &&
        window->shown_height == tall)
        return;
    sprite_window_draw(window, state, frame, wide, tall);
    window->shown = (int)frame;
    window->shown_deck = sprite_lead;
    window->shown_height = tall;
}

/* driven from rbp's image draws, which run on its ui thread. */
static void sprites_feature_after_image(void *render, void *image,
                                        uint16_t window_layer,
                                        uint32_t image_id)
{
    unsigned now;
    int screen_up;
    (void)render;
    (void)image;
    (void)window_layer;
    (void)image_id;
    sprite_take_jobs();
    now = now_ms();
    sprite_on_air_hold(now);
    if (now - sprite_tick_ms < SPRITE_FRAME_MS)
        return;
    sprite_tick_ms = now;
    screen_up = rx3_wave_screen_up();
    sprite_update(screen_up, now);
}

static void sprites_feature_playhead(unsigned int deck, long position)
{
    double beat;
    if (deck >= 2u)
        return;
    sprite_decks[deck].position = position;
    if (rx3_deck_beat(deck, position, &beat)) {
        sprite_decks[deck].have_phase = 0;
        return;
    }
    sprite_decks[deck].phase_milli = (uint32_t)(beat * 1000.0);
    sprite_decks[deck].have_phase = 1;
}

/* audio thread: the deck's post-fader block through a two-pole bass
   filter into an envelope that jumps up at once and falls over ~120 ms. */
static void sprites_feature_channel_block(unsigned int deck, void *channel,
                                          Float2 *output, int frames)
{
    struct sprite_bass *bass = &sprite_bass[deck];
    float first = bass->first;
    float second = bass->second;
    float peak = 0.0f;
    float envelope = bass->envelope;
    float fall;
    int index;
    (void)channel;
    for (index = 0; index < frames; index++) {
        float mono = 0.5f * (output[index].left + output[index].right);
        first += SPRITE_BASS_POLE * (mono - first);
        second += SPRITE_BASS_POLE * (first - second);
        if (second > peak)
            peak = second;
        else if (-second > peak)
            peak = -second;
    }
    bass->first = first;
    bass->second = second;
    /* 1 - frames / fall samples, close enough to exp for blocks of a few
       hundred frames. */
    fall = 1.0f - (float)frames / SPRITE_FALL_SAMPLES;
    envelope = fall > 0.0f ? envelope * fall : 0.0f;
    bass->envelope = peak > envelope ? peak : envelope;
}

static int sprites_feature_configured(void)
{
    const char *nonce = getenv("RX3_SPRITE_NONCE");
    if (nonce && nonce[0]) {
        uint32_t value = 0u;
        const char *cursor;
        for (cursor = nonce; *cursor >= '0' && *cursor <= '9'; cursor++)
            value = value * 10u + (uint32_t)(*cursor - '0');
        if (value)
            sprites_nonce = value;
    }
    return sprites_dir != 0;
}

static int sprite_start_load(unsigned deck, const char *stem)
{
    struct sprite_request *request;
    pthread_t thread;
    unsigned index;
    if (deck >= 2u || !sprites_dir || !stem)
        return -1;
    request = mmap(0, 4096u, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
                   -1, 0);
    if (request == MAP_FAILED)
        return -1;
    memset(request, 0, sizeof(*request));
    request->deck = deck;
    request->generation = sprite_decks[deck].generation;
    for (index = 0; stem[index] && index + 1u < sizeof(request->stem); index++)
        request->stem[index] = stem[index];
    request->stem[index] = 0;
    if (!pthread_create(&thread, 0, sprite_loader, request)) {
        pthread_detach(thread);
        return 0;
    }
    munmap(request, 4096u);
    return -1;
}

static int sprites_feature_install(void)
{
    /* a track load used to be the only way in. after a restart the decks
       are empty, so the pack has to be ready before the next track. */
    sprite_note(16u, "sprite feature active");
    if (!sprites_dir)
        sprite_note(64u, "sprite directory is missing");
    else {
        sprite_start_load(0u, "boot");
        sprite_start_load(1u, "boot");
    }
    sprite_menu_install();
    if (!rx3_deck_tap_ready())
        log_line("sprites: playback position unavailable, beat sync stays off");
    return 1;
}

static void sprites_feature_remove(void)
{
    unsigned deck;
    sprite_menu_remove();
    for (deck = 0; deck < 2u; deck++) {
        struct sprite_job *job = sprite_jobs[deck];
        sprite_jobs[deck] = 0;
        sprite_decks[deck].live = 0;
        if (job) {
            if (job->block)
                munmap(job->block, job->block_bytes);
            munmap(job, 4096u);
        }
    }
    /* teardown runs as rbp exits and the ui thread may still be blitting, so
       the frames and windows are left to the process exit. */
}

static void sprites_feature_track_will_load(unsigned int deck, void *reader,
                                            const void *track_info)
{
    (void)reader;
    (void)track_info;
    if (deck >= 2u)
        return;
    sprite_decks[deck].generation++;
    sprite_decks[deck].live = 0;
    sprite_decks[deck].have_phase = 0;
}

static void sprites_feature_track_did_load(unsigned int deck, void *reader,
                                           const void *track_info)
{
    char stem[RX3_SPRITE_NAME_LIMIT + 1u];
    (void)reader;
    if (deck >= 2u || !sprites_dir)
        return;
    if (rx3_sprite_stem((const char *)track_info, stem, sizeof(stem)))
        return;
    sprite_start_load(deck, stem);
}

static void sprites_feature_destroy_deck(unsigned int deck)
{
    if (deck < 2u)
        sprite_decks[deck].live = 0;
}

#endif /* RX3_SPRITES_FEATURE_H */
