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

#define SPRITE_PACK_LIMIT 64u
#define SPRITE_FILE_LIMIT (1024u * 1024u)
#define SPRITE_KEY_RGB565 0xf81fu
#define SPRITE_KEY_ARGB 0xffff00ffu
#define SPRITE_FRAME_MS 33u
#define SPRITE_RETRY_MS 1000u

/* ds_gr window api, see ui_PLAYMODE_Set for rbp's own use. */
#define DS_GR_CREATE_WINDOW ((unsigned long)0x001a2634)
#define DS_GR_DESTROY_WINDOW ((unsigned long)0x001a2990)
#define DS_GR_LOCK_WINDOW ((unsigned long)0x001a1c48)
#define DS_GR_UNLOCK_WINDOW ((unsigned long)0x001a1cb0)
#define DS_GR_SET_WINDOW_OPTION ((unsigned long)0x001a061c)
#define DS_GR_SET_WINDOW_COLOR ((unsigned long)0x001a0680)
#define DS_GR_FORMAT_RGB565 9u
#define DS_GR_OPTION_SET 1u
#define DS_GR_OPTION_VISIBLE 8u
#define DS_GR_COLOR_KEY 2u
#define DS_GR_Z_TAKEN 4

/* rbp's scrolling waveform window: both decks in one 892x444 window at
   (190, 47), deck 0 in the top 222 rows. the handle is 0 whenever the
   waveform screen is not built. the per-deck words are non-zero while that
   deck has waveform data. */
#define WAVE_WINDOW_ID ((unsigned long)0x02498d44)
#define WAVE_DECK0_DATA ((unsigned long)0x05902198)
#define WAVE_DECK1_DATA ((unsigned long)0x05930fb4)
#define WAVE_LEFT 190
#define WAVE_TOP 47
#define WAVE_WIDTH 892
#define WAVE_STRIP 222

typedef int (*ds_gr_create_window_fn)(void **window, uint32_t *info);
typedef int (*ds_gr_window_fn)(void *window);
typedef int (*ds_gr_lock_window_fn)(void *window, void **pixels, int *pitch);
typedef int (*ds_gr_set_option_fn)(void *window, uint32_t mode, uint32_t bits);
typedef int (*ds_gr_set_color_fn)(void *window, uint32_t kind, uint32_t colour);

/* free z slots under rbp's waveform (0x4f) and counter windows (0x41-0x4e).
   a lower z composites later, so on top. */
static const uint32_t sprite_z_slots[] = { 0x3eu, 0x3du, 0x3cu, 0x3bu };
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
    uint16_t *frames;
    size_t frames_bytes;
    unsigned epoch_ms;
    int have_phase;
    uint32_t phase_milli;
    char name[48];
    char previous[48];
};

/* one overlay window per deck. shown is the frame last blitted, or -1. */
struct sprite_window {
    void *handle;
    int x;
    int y;
    unsigned width;
    unsigned height;
    int shown;
    unsigned failed_ms;
};

static const char *sprites_dir;
static uint32_t sprites_nonce = 1u;
static int sprites_owns_timestretch;
static struct sprite_deck sprite_decks[2];
static struct sprite_job *volatile sprite_jobs[2];
static struct sprite_window sprite_windows[2];
static unsigned sprite_tick_ms;
static unsigned sprite_logged;

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
    if (header->width < 1u || header->width > 64u ||
        header->height < 1u || header->height > 64u ||
        header->frame_count < 1u || header->frame_count > 16u)
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


/* the player beat clock is not wired until a firmware-1.19 log proves the field.
 * do not guess an address. a failed read leaves beat-mode packs undrawn. */
static int sprite_read_beat_phase(unsigned deck, uint32_t *phase_milli)
{
    (void)deck;
    (void)phase_milli;
    return -1;
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

/* expand the indexed frames of a pack into rgb565, one frame after another.
   index 0, and an index past the palette, become the key colour. an opaque
   colour that equals the key moves one blue step so it still draws. */
static uint16_t *sprite_expand(const struct sprite_job *job, size_t *bytes_out)
{
    size_t pixels_per_frame = (size_t)job->width * job->height;
    size_t count = pixels_per_frame * job->frame_count;
    size_t bytes = (count * 2u + 4095u) & ~(size_t)4095u;
    uint16_t *output;
    unsigned frame;
    size_t offset;
    output = mmap(0, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
                  -1, 0);
    if (output == MAP_FAILED)
        return 0;
    for (frame = 0; frame < job->frame_count; frame++) {
        const uint8_t *source = job->pixels + (size_t)frame * job->pixel_bytes;
        uint16_t *target = output + (size_t)frame * pixels_per_frame;
        for (offset = 0; offset < pixels_per_frame; offset++) {
            unsigned colour_index = source[offset];
            uint16_t colour = SPRITE_KEY_RGB565;
            if (colour_index && colour_index < job->palette_count) {
                memcpy(&colour, job->palette + colour_index * 2u, sizeof(colour));
                if (colour == SPRITE_KEY_RGB565)
                    colour = SPRITE_KEY_RGB565 - 1u;
            }
            target[offset] = colour;
        }
    }
    *bytes_out = bytes;
    return output;
}

static void sprite_drop_frames(struct sprite_deck *state)
{
    if (state->frames)
        munmap(state->frames, state->frames_bytes);
    state->frames = 0;
    state->frames_bytes = 0;
}

/* runs on rbp's ui thread, the only thread that reads the frames. */
static void sprite_take_jobs(void)
{
    unsigned deck;
    for (deck = 0; deck < 2u; deck++) {
        struct sprite_job *job = sprite_jobs[deck];
        struct sprite_deck *state;
        uint16_t *frames;
        size_t frames_bytes = 0;
        if (!job)
            continue;
        if (!__sync_bool_compare_and_swap(&sprite_jobs[deck], job, 0))
            continue;
        state = &sprite_decks[deck];
        frames = job->generation == state->generation
                     ? sprite_expand(job, &frames_bytes)
                     : 0;
        munmap(job->block, job->block_bytes);
        if (!frames) {
            if (job->generation == state->generation)
                sprite_log_once(32u, "sprites disabled: frame expansion failed");
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
        state->frames = frames;
        state->frames_bytes = frames_bytes;
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

/* centred on the deck's strip, or resting on its bottom edge for anchor 2. */
static void sprite_place(unsigned deck, const struct sprite_deck *state,
                         int *x, int *y)
{
    int strip_top = WAVE_TOP + (int)deck * WAVE_STRIP;
    *x = WAVE_LEFT + (WAVE_WIDTH - (int)state->width) / 2;
    if (state->anchor == 2u)
        *y = strip_top + WAVE_STRIP - (int)state->height;
    else
        *y = strip_top + (WAVE_STRIP - (int)state->height) / 2;
}

static void sprite_window_close(struct sprite_window *window)
{
    if (window->handle)
        ((ds_gr_window_fn)DS_GR_DESTROY_WINDOW)(window->handle);
    window->handle = 0;
    window->shown = -1;
}

/* same window setup as rbp's waveform: rgb565 on plane 0, visible, with
   magenta keyed out. tries each free z until one is not taken. */
static int sprite_window_open(struct sprite_window *window, int x, int y,
                              unsigned width, unsigned height)
{
    unsigned slot;
    for (slot = 0; slot < sizeof(sprite_z_slots) / sizeof(sprite_z_slots[0]);
         slot++) {
        uint32_t info[13];
        void *handle = 0;
        int result;
        memset(info, 0, sizeof(info));
        info[2] = (uint32_t)width | ((uint32_t)height << 16);
        info[3] = DS_GR_FORMAT_RGB565;
        info[4] = 0u;
        info[5] = sprite_z_slots[slot];
        info[7] = (uint32_t)x;
        info[8] = (uint32_t)y;
        result = ((ds_gr_create_window_fn)DS_GR_CREATE_WINDOW)(&handle, info);
        if (result == DS_GR_Z_TAKEN)
            continue;
        if (result || !handle)
            return -1;
        ((ds_gr_set_option_fn)DS_GR_SET_WINDOW_OPTION)(handle, DS_GR_OPTION_SET,
                                                       DS_GR_OPTION_VISIBLE);
        ((ds_gr_set_option_fn)DS_GR_SET_WINDOW_OPTION)(handle, DS_GR_OPTION_SET,
                                                       1u);
        ((ds_gr_set_color_fn)DS_GR_SET_WINDOW_COLOR)(handle, DS_GR_COLOR_KEY,
                                                     SPRITE_KEY_ARGB);
        window->handle = handle;
        window->x = x;
        window->y = y;
        window->width = width;
        window->height = height;
        window->shown = -1;
        return 0;
    }
    return -1;
}

/* copies one frame into the window. setting the visible option again is what
   marks the window dirty for rbp's compositor. */
static void sprite_window_blit(struct sprite_window *window,
                               const struct sprite_deck *state, unsigned frame)
{
    const uint16_t *source =
        state->frames + (size_t)frame * state->width * state->height;
    uint8_t *pixels = 0;
    int pitch = 0;
    unsigned row;
    if (((ds_gr_lock_window_fn)DS_GR_LOCK_WINDOW)(window->handle,
                                                  (void **)&pixels, &pitch))
        return;
    if (pixels && pitch >= (int)(state->width * 2u)) {
        for (row = 0; row < state->height; row++)
            memcpy(pixels + (size_t)row * (unsigned)pitch,
                   source + (size_t)row * state->width, state->width * 2u);
        ((ds_gr_set_option_fn)DS_GR_SET_WINDOW_OPTION)(
            window->handle, DS_GR_OPTION_SET, DS_GR_OPTION_VISIBLE);
        window->shown = (int)frame;
    }
    ((ds_gr_window_fn)DS_GR_UNLOCK_WINDOW)(window->handle);
}

static int sprite_deck_has_wave(unsigned deck)
{
    return *(volatile int *)(deck ? WAVE_DECK1_DATA : WAVE_DECK0_DATA) != 0;
}

/* keeps each deck's window in step with the waveform screen: open while the
   screen is up and the deck has a sprite and a waveform, closed otherwise. */
static void sprite_update_deck(unsigned deck, int screen_up, unsigned now)
{
    struct sprite_deck *state = &sprite_decks[deck];
    struct sprite_window *window = &sprite_windows[deck];
    unsigned frame = 0;
    int x;
    int y;
    if (!screen_up || !state->live || !state->frames ||
        !sprite_deck_has_wave(deck) || sprite_frame_now(state, &frame)) {
        sprite_window_close(window);
        return;
    }
    sprite_place(deck, state, &x, &y);
    if (window->handle && (window->x != x || window->y != y ||
                           window->width != state->width ||
                           window->height != state->height))
        sprite_window_close(window);
    if (!window->handle) {
        if (window->failed_ms && now - window->failed_ms < SPRITE_RETRY_MS)
            return;
        if (sprite_window_open(window, x, y, state->width, state->height)) {
            window->failed_ms = now ? now : 1u;
            sprite_note(256u, "sprite window could not be created");
            return;
        }
        window->failed_ms = 0;
        sprite_note(deck ? 8u : 4u,
                    deck ? "sprite window open deck 1" : "sprite window open deck 0");
    }
    if (window->shown != (int)frame)
        sprite_window_blit(window, state, frame);
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
    if (now - sprite_tick_ms < SPRITE_FRAME_MS)
        return;
    sprite_tick_ms = now;
    screen_up = *(void *volatile *)WAVE_WINDOW_ID != 0;
    sprite_update_deck(0u, screen_up, now);
    sprite_update_deck(1u, screen_up, now);
}

static void sprites_feature_playhead(unsigned int deck, long position)
{
    uint32_t phase = 0;
    (void)position;
    if (deck >= 2u)
        return;
    if (sprite_read_beat_phase(deck, &phase)) {
        sprite_decks[deck].have_phase = 0;
        return;
    }
    sprite_decks[deck].phase_milli = phase;
    sprite_decks[deck].have_phase = 1;
}

static long sprites_timestretch_operate(void *stretch, long position,
                                       Float2 *output, unsigned int frames)
{
    long result = original_timestretch_operate(stretch, position, output, frames);
    int deck = deck_index_for_reader(
        *(void *const *)((const uint8_t *)stretch + 4u));
    if (deck >= 0)
        rx3_notify_playhead((unsigned int)deck, position);
    return result;
}

static long sprites_timestretch_fgpr(void *stretch, long position,
                                    Float2 *output, unsigned int frames)
{
    long result = original_timestretch_fgpr(stretch, position, output, frames);
    int deck = deck_index_for_reader(
        *(void *const *)((const uint8_t *)stretch + 4u));
    if (deck >= 0)
        rx3_notify_playhead((unsigned int)deck, position);
    return result;
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
    if (original_timestretch_operate)
        return 1;
    original_timestretch_operate = (timestretch_operate_fn)install_hook(
        &timestretch_operate_hook, TIMESTRETCH_OPERATE, timestretch_operate_guard,
        (void *)sprites_timestretch_operate);
    original_timestretch_fgpr = (timestretch_operate_fn)install_hook(
        &timestretch_fgpr_hook, TIMESTRETCH_FGPR_OPERATE, timestretch_fgpr_guard,
        (void *)sprites_timestretch_fgpr);
    if (!original_timestretch_operate || !original_timestretch_fgpr) {
        log_line("sprites: playback position hook unavailable, beat sync stays off");
        return 1;
    }
    sprites_owns_timestretch = 1;
    return 1;
}

static void sprites_feature_remove(void)
{
    unsigned deck;
    if (sprites_owns_timestretch) {
        uninstall_hook(&timestretch_fgpr_hook);
        uninstall_hook(&timestretch_operate_hook);
        original_timestretch_fgpr = 0;
        original_timestretch_operate = 0;
        sprites_owns_timestretch = 0;
    }
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
