/* SPDX-License-Identifier: MPL-2.0
 * one named sprite on the TRANS beat fx, firmware 1.19.
 *
 * trans is engine type 2, ui type 9. while trans is the settled effect the
 * beat fx service skips its dsp -- this module's operate claim returns
 * nonzero -- so the audio stays dry, and the fade tail still rings out in
 * stock. beat and time keep editing trans and do not move the picture.
 *
 * a module chooses the picture:
 *   RX3_SPRITE_FX=1
 *   RX3_SPRITE_FX_PACK  the .rx3spr path
 *   RX3_SPRITE_FX_NAME  the beat-fx name
 * the sprite shows only while trans is on, the waveform screen is up, and
 * depth is above zero. depth is the size. channel 0 sits on track 1, channel
 * 1 on track 2, and any other channel (master) on the seam between the
 * strips. the window is the full-depth box, so the knob scales the frame
 * inside it. the fps clock starts when it becomes visible.
 */
#ifndef RX3_SPRITEFX_FEATURE_H
#define RX3_SPRITEFX_FEATURE_H

#include "rx3_spritefx_logic.h"

#define BEAT_FX_TRANS 2
static const unsigned char sfx_owner;

#define SFX_FILE_LIMIT (4u * 1024u * 1024u)
#define SFX_EDGE_LIMIT 256u
#define SFX_FRAMES_LIMIT 256u
/* full depth is the sprites base height, 0.9 of one waveform strip. */
#define SFX_BASE_HEIGHT ((RX3_OVERLAY_WAVE_STRIDE * 9) / 10)
#define SFX_FRAME_MS 33u
#define SFX_RETRY_MS 1000u

struct __attribute__((packed)) sfx_pack_header {
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

_Static_assert(sizeof(struct sfx_pack_header) == 50,
               "sprite pack header drifted");

struct sfx_pack {
    unsigned width;
    unsigned height;
    unsigned frame_count;
    unsigned fps_num;
    unsigned fps_den;
    unsigned anchor;
    unsigned pixel_bytes;
    const uint8_t *pixels;
    uint16_t palette[256];
    void *block;
    size_t block_bytes;
};

struct sfx_window {
    struct rx3_overlay overlay;
    unsigned channel;
    int shown;
    unsigned shown_frame;
    unsigned shown_tall;
    unsigned failed_ms;
};

static const char *sfx_path;
static const char *sfx_name;
static struct sfx_pack sfx_pack;
static volatile int sfx_loaded;
static volatile int sfx_on;
static volatile unsigned sfx_channel;
static volatile float sfx_depth;
static struct sfx_window sfx_window;
static int sfx_showing;
static unsigned sfx_epoch;
static unsigned sfx_tick;
static unsigned sfx_logged;

static void sfx_log_once(unsigned bit, const char *message)
{
    if (sfx_logged & bit)
        return;
    sfx_logged |= bit;
    framework->log_line(message);
}

static int read_exactly(int fd, void *destination, size_t length)
{
    uint8_t *cursor = destination;
    while (length) {
        ssize_t got = read(fd, cursor, length > 0x100000u ? 0x100000u : length);
        if (got <= 0)
            return -1;
        cursor += (size_t)got;
        length -= (size_t)got;
    }
    return 0;
}

static unsigned sfx_now_ms(void)
{
    struct sfx_time { long seconds, microseconds; } stamp;
    if (gettimeofday(&stamp, 0))
        return 0;
    return (unsigned)(((uint64_t)(unsigned long)stamp.seconds * 1000000u +
                      (uint64_t)(unsigned long)stamp.microseconds) / 1000u);
}

/* the sprite pack rules: magic, version, 8 bpp, format byte 2, anchor 1 or
   2, edges at most 256, file at most 4 MiB. */
static int sfx_header_ok(struct sfx_pack_header *header, size_t size)
{
    uint32_t digest;
    uint32_t stored;
    unsigned frame_bytes;
    if (size < 64u || size > SFX_FILE_LIMIT || header->header_size != 64u ||
        header->version != 1u)
        return -1;
    {
        const uint8_t *pad = (const uint8_t *)header + sizeof(*header);
        unsigned index;
        for (index = 0; index < 14u; index++)
            if (pad[index])
                return -1;
    }
    if (memcmp(header->magic, "RX3SPR1", 7) || header->magic[7] != 0)
        return -1;
    if (size != 64u + header->payload_length)
        return -1;
    if (header->width < 1u || header->width > SFX_EDGE_LIMIT ||
        header->height < 1u || header->height > SFX_EDGE_LIMIT ||
        header->frame_count < 1u || header->frame_count > SFX_FRAMES_LIMIT)
        return -1;
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
    frame_bytes = (unsigned)header->width * header->height;
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

static void sfx_palette(const struct sfx_pack_header *header,
                       const uint8_t *entries, uint16_t *palette)
{
    unsigned index;
    for (index = 0; index < 256u; index++) {
        uint16_t colour = RX3_OVERLAY_KEY;
        if (index && index < header->palette_count) {
            memcpy(&colour, entries + index * 2u, sizeof(colour));
            if (colour == RX3_OVERLAY_KEY)
                colour = (uint16_t)(RX3_OVERLAY_KEY - 1u);
        }
        palette[index] = colour;
    }
}

static void *sfx_loader(void *opaque)
{
    int fd = -1;
    off_t end;
    size_t size = 0;
    uint8_t *pack = 0;
    struct sfx_pack_header *header;
    int frame_bytes;
    const uint8_t *palette;
    (void)opaque;
    fd = open(sfx_path, O_RDONLY);
    if (fd < 0)
        goto fail;
    end = lseek(fd, 0, SEEK_END);
    if (end <= 0 || (size_t)end > SFX_FILE_LIMIT || lseek(fd, 0, SEEK_SET) < 0)
        goto fail;
    size = (size_t)end;
    pack = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
                -1, 0);
    if (pack == MAP_FAILED) {
        pack = 0;
        goto fail;
    }
    if (read_exactly(fd, pack, size))
        goto fail;
    close(fd);
    fd = -1;
    header = (struct sfx_pack_header *)pack;
    frame_bytes = sfx_header_ok(header, size);
    if (frame_bytes < 0)
        goto fail;
    palette = pack + 64u + header->name_length;
    sfx_pack.width = header->width;
    sfx_pack.height = header->height;
    sfx_pack.frame_count = header->frame_count;
    sfx_pack.fps_num = header->fps_num;
    sfx_pack.fps_den = header->fps_den;
    sfx_pack.anchor = header->anchor;
    sfx_pack.pixel_bytes = (unsigned)frame_bytes;
    sfx_pack.pixels = palette + (size_t)header->palette_count * header->entry_size;
    sfx_pack.block = pack;
    sfx_pack.block_bytes = size;
    sfx_palette(header, palette, sfx_pack.palette);
    __sync_synchronize();
    sfx_loaded = 1;
    return 0;
fail:
    if (fd >= 0)
        close(fd);
    if (pack)
        munmap(pack, size);
    sfx_log_once(1u, "sprite fx: pack failed validation");
    return 0;
}

/* the full-depth box. depth scales the frame inside it, so the knob does
   not recreate the window. a channel change does, because the box moves. */
static void sfx_full_size(unsigned *width, unsigned *height)
{
    unsigned tall = SFX_BASE_HEIGHT;
    unsigned wide = sfx_pack.width * tall / sfx_pack.height;
    if (wide > RX3_OVERLAY_WAVE_WIDTH) {
        tall = tall * RX3_OVERLAY_WAVE_WIDTH / wide;
        wide = RX3_OVERLAY_WAVE_WIDTH;
    }
    *width = wide ? wide : 1u;
    *height = tall ? tall : 1u;
}

static void sfx_place(unsigned channel, int width, int height, int *x, int *y)
{
    *x = RX3_OVERLAY_WAVE_X + (RX3_OVERLAY_WAVE_WIDTH - width) / 2;
    if (channel == 0u)
        *y = RX3_OVERLAY_WAVE_Y + ((int)RX3_OVERLAY_WAVE_STRIDE - height) / 2;
    else if (channel == 1u)
        *y = RX3_OVERLAY_WAVE_Y + (int)RX3_OVERLAY_WAVE_STRIDE +
             ((int)RX3_OVERLAY_WAVE_STRIDE - height) / 2;
    else
        *y = RX3_OVERLAY_WAVE_Y + (int)RX3_OVERLAY_WAVE_STRIDE - height / 2;
}

static void sfx_close(void)
{
    framework->overlays->release(&sfx_owner);
    memset(&sfx_window.overlay, 0, sizeof(sfx_window.overlay));
    sfx_window.shown = -1;
}

/* nearest-neighbour scale of one frame into the window. anchor 2 rests on
   the bottom of the box; anchor 1 is centred. the rest of the box is keyed. */
static void sfx_draw(unsigned frame, unsigned wide, unsigned tall)
{
    const uint8_t *source = sfx_pack.pixels + (size_t)frame * sfx_pack.pixel_bytes;
    unsigned left = (sfx_window.overlay.rect.width - wide) / 2u;
    unsigned top = sfx_pack.anchor == 2u
                       ? sfx_window.overlay.rect.height - tall
                       : (sfx_window.overlay.rect.height - tall) / 2u;
    uint32_t step_x = (sfx_pack.width << 16) / wide;
    uint32_t step_y = (sfx_pack.height << 16) / tall;
    uint16_t *pixels = 0;
    unsigned int pitch = 0;
    unsigned row;
    if (!wide || !tall ||
        !framework->overlays->lock(&sfx_window.overlay, &pixels, &pitch))
        return;
    for (row = 0; row < sfx_window.overlay.rect.height; row++) {
        uint16_t *line = pixels + (size_t)row * pitch;
        unsigned column;
        if (row < top || row >= top + tall) {
            for (column = 0; column < sfx_window.overlay.rect.width; column++)
                line[column] = RX3_OVERLAY_KEY;
            continue;
        }
        {
            const uint8_t *source_row =
                source + (size_t)(((row - top) * step_y) >> 16) * sfx_pack.width;
            uint32_t along = 0;
            for (column = 0; column < left; column++)
                line[column] = RX3_OVERLAY_KEY;
            for (column = 0; column < wide; column++) {
                line[left + column] = sfx_pack.palette[source_row[along >> 16]];
                along += step_x;
            }
            for (column = left + wide; column < sfx_window.overlay.rect.width; column++)
                line[column] = RX3_OVERLAY_KEY;
        }
    }
    framework->overlays->present(&sfx_window.overlay);
}

static void sfx_hide(void)
{
    sfx_showing = 0;
    sfx_close();
}

/* trans owned: skip the stock dsp and publish what the picture needs.
   the fade tail is not owned, so stock rings out. the picture reads the
   published fields and never the manager. */
static int sfx_operate(void *manager, struct rx3_stereo *buffer,
                       unsigned int frames)
{
    const uint8_t *fields = manager;
    (void)buffer;
    (void)frames;
    if (!fields || !rx3_beatfx_owned(fields, BEAT_FX_TRANS)) {
        sfx_on = 0;
        return 0;
    }
    sfx_channel = *(const uint32_t *)(fields + 0x00u);
    sfx_depth = *(const float *)(fields + 0xe8u);
    sfx_on = rx3_beatfx_on(fields, BEAT_FX_TRANS);
    return 1;
}

static void sfx_drawn(unsigned image)
{
    unsigned now = sfx_now_ms();
    unsigned channel;
    float depth;
    unsigned full_w;
    unsigned full_h;
    unsigned wide;
    unsigned tall;
    unsigned frame;
    int x;
    int y;
    (void)image;
    if (sfx_tick && now - sfx_tick < SFX_FRAME_MS)
        return;
    sfx_tick = now ? now : 1u;
    if (!sfx_loaded || !sfx_on || !framework->overlays->wave_up()) {
        sfx_hide();
        return;
    }
    __sync_synchronize();
    channel = sfx_channel;
    depth = sfx_depth;
    if (!(depth > 0.0f)) {
        sfx_hide();
        return;
    }
    sfx_full_size(&full_w, &full_h);
    tall = (unsigned)((float)full_h * depth + 0.5f);
    if (!tall) {
        sfx_hide();
        return;
    }
    if (tall > full_h)
        tall = full_h;
    wide = full_w * tall / full_h;
    if (!wide)
        wide = 1u;
    if (wide > full_w)
        wide = full_w;
    sfx_place(channel, (int)full_w, (int)full_h, &x, &y);
    if (sfx_window.overlay.handle &&
        (sfx_window.overlay.rect.x != x || sfx_window.overlay.rect.y != y ||
         sfx_window.overlay.rect.width != full_w ||
         sfx_window.overlay.rect.height != full_h ||
         sfx_window.channel != channel))
        sfx_close();
    if (!sfx_window.overlay.handle) {
        struct rx3_overlay_rect rect = { x, y, full_w, full_h };
        if (sfx_window.failed_ms && now - sfx_window.failed_ms < SFX_RETRY_MS)
            return;
        if (!framework->overlays->claim(&sfx_owner, &rect, RX3_OVERLAY_ON_TOP,
                                        &sfx_window.overlay)) {
            sfx_window.failed_ms = now ? now : 1u;
            sfx_log_once(2u, "sprite fx: window could not be created");
            return;
        }
        sfx_window.failed_ms = 0;
        sfx_window.shown = -1;
        sfx_window.channel = channel;
    }
    if (!sfx_showing) {
        sfx_showing = 1;
        sfx_epoch = now;
    }
    frame = rx3_sprite_fps_frame(now - sfx_epoch, sfx_pack.fps_num, sfx_pack.fps_den,
                                 sfx_pack.frame_count);
    if (sfx_window.shown == (int)frame && sfx_window.shown_tall == tall)
        return;
    sfx_draw(frame, wide, tall);
    sfx_window.shown = (int)frame;
    sfx_window.shown_frame = frame;
    sfx_window.shown_tall = tall;
}

static int spritefx_feature_configured(void)
{
    const char *flag = getenv("RX3_SPRITE_FX");
    const char *path = getenv("RX3_SPRITE_FX_PACK");
    const char *name = getenv("RX3_SPRITE_FX_NAME");
    if (!flag || flag[0] != '1' || !path || !path[0] || !name || !name[0])
        return 0;
    sfx_path = path;
    sfx_name = name;
    return 1;
}

/* Returns 0 on any refusal; the core then calls remove(), which takes out
   whatever part of this had already been put in. The operate claim has to
   hold, or trans would keep its dsp. */
static int spritefx_feature_install(void)
{
    pthread_t thread;
    if (!sfx_path)
        return 0;
    if (!framework->beatfx->claim_operate(&sfx_owner, 20u, sfx_operate)) {
        framework->log_line("sprite fx refused: the operate claim failed");
        return 0;
    }
    if (!framework->overlays->watch_draw(&sfx_owner, sfx_drawn)) {
        framework->log_line("sprite fx refused: no draw watcher slot");
        return 0;
    }
    if (pthread_create(&thread, 0, sfx_loader, 0))
        return 0;
    pthread_detach(thread);
    return 1;
}

static void spritefx_feature_remove(void)
{
    sfx_on = 0;
    sfx_hide();
    framework->beatfx->unregister_owner(&sfx_owner);
    framework->overlays->unwatch_draw(&sfx_owner);
    framework->overlays->release(&sfx_owner);
    memset(&sfx_window.overlay, 0, sizeof(sfx_window.overlay));
}

#endif /* RX3_SPRITEFX_FEATURE_H */
