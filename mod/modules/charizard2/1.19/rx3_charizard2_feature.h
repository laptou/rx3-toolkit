/* SPDX-License-Identifier: MPL-2.0
 * CHARIZARD on the TRANS beat fx, firmware 1.19.
 *
 * trans is engine type 2, ui type 9, display index 3. captions are
 * 0x1469-0x146c and the select tiles are 0x15c2-0x15c4. while trans is the
 * settled effect the broker skips its dsp, so the audio stays dry, and the
 * fade tail still rings out in stock. beat and time keep editing trans and
 * do not move the picture.
 *
 * the picture is charizard.rx3spr. it shows only while trans is on, the
 * waveform screen is up, and depth is above zero. depth is the size. channel
 * 0 sits on track 1, channel 1 on track 2, and any other channel (master)
 * on the seam between the strips. the window is the full-depth box, so the
 * knob scales the frame inside it. the fps clock starts when it becomes
 * visible.
 */
#ifndef RX3_CHARIZARD2_FEATURE_H
#define RX3_CHARIZARD2_FEATURE_H

#include "rx3_charizard2_labels.h"
#include "../../sprites/1.19/rx3_sprites_logic.h"
#include "../../core/1.19/rx3_ds_gr.h"

#define BEAT_FX_TRANS 2
#define BEAT_FX_UI_TRANS 9
#define BEAT_FX_DISPLAY_TRANS 3
#define TRANS_CAPTION_FIRST 0x1469u
#define TRANS_CAPTION_LAST 0x146cu
#define TRANS_SELECT_FIRST 0x15c2u
#define TRANS_SELECT_LAST 0x15c4u

#define CZ_FILE_LIMIT (4u * 1024u * 1024u)
#define CZ_EDGE_LIMIT 256u
#define CZ_FRAMES_LIMIT 256u
/* full depth is the sprites base height, 0.9 of one waveform strip. */
#define CZ_BASE_HEIGHT ((WAVE_STRIP * 9) / 10)
#define CZ_FRAME_MS 33u
#define CZ_RETRY_MS 1000u

struct __attribute__((packed)) cz_pack_header {
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

_Static_assert(sizeof(struct cz_pack_header) == 50,
               "charizard pack header drifted from the sprite pack");

struct cz_pack {
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

struct cz_window {
    struct rx3_gr_window gr;
    unsigned channel;
    int shown;
    unsigned shown_frame;
    unsigned shown_tall;
    unsigned failed_ms;
};

static const char *cz_path;
static struct cz_pack cz_pack;
static volatile int cz_loaded;
static volatile int cz_on;
static volatile unsigned cz_channel;
static volatile float cz_depth;
static struct cz_window cz_window;
static int cz_showing;
static unsigned cz_epoch;
static unsigned cz_tick;
static unsigned cz_logged;

static void cz_log_once(unsigned bit, const char *message)
{
    if (cz_logged & bit)
        return;
    cz_logged |= bit;
    log_line(message);
}

/* the sprite pack rules: magic, version, 8 bpp, format byte 2, anchor 1 or
   2, edges at most 256, file at most 4 MiB. */
static int cz_header_ok(struct cz_pack_header *header, size_t size)
{
    uint32_t digest;
    uint32_t stored;
    unsigned frame_bytes;
    if (size < 64u || size > CZ_FILE_LIMIT || header->header_size != 64u ||
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
    if (header->width < 1u || header->width > CZ_EDGE_LIMIT ||
        header->height < 1u || header->height > CZ_EDGE_LIMIT ||
        header->frame_count < 1u || header->frame_count > CZ_FRAMES_LIMIT)
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

static void cz_palette(const struct cz_pack_header *header,
                       const uint8_t *entries, uint16_t *palette)
{
    unsigned index;
    for (index = 0; index < 256u; index++) {
        uint16_t colour = DS_GR_KEY_RGB565;
        if (index && index < header->palette_count) {
            memcpy(&colour, entries + index * 2u, sizeof(colour));
            if (colour == DS_GR_KEY_RGB565)
                colour = (uint16_t)(DS_GR_KEY_RGB565 - 1u);
        }
        palette[index] = colour;
    }
}

static void *cz_loader(void *opaque)
{
    int fd = -1;
    off_t end;
    size_t size = 0;
    uint8_t *pack = 0;
    struct cz_pack_header *header;
    int frame_bytes;
    const uint8_t *palette;
    (void)opaque;
    fd = open(cz_path, O_RDONLY);
    if (fd < 0)
        goto fail;
    end = lseek(fd, 0, SEEK_END);
    if (end <= 0 || (size_t)end > CZ_FILE_LIMIT || lseek(fd, 0, SEEK_SET) < 0)
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
    header = (struct cz_pack_header *)pack;
    frame_bytes = cz_header_ok(header, size);
    if (frame_bytes < 0)
        goto fail;
    palette = pack + 64u + header->name_length;
    cz_pack.width = header->width;
    cz_pack.height = header->height;
    cz_pack.frame_count = header->frame_count;
    cz_pack.fps_num = header->fps_num;
    cz_pack.fps_den = header->fps_den;
    cz_pack.anchor = header->anchor;
    cz_pack.pixel_bytes = (unsigned)frame_bytes;
    cz_pack.pixels = palette + (size_t)header->palette_count * header->entry_size;
    cz_pack.block = pack;
    cz_pack.block_bytes = size;
    cz_palette(header, palette, cz_pack.palette);
    __sync_synchronize();
    cz_loaded = 1;
    return 0;
fail:
    if (fd >= 0)
        close(fd);
    if (pack)
        munmap(pack, size);
    cz_log_once(1u, "charizard: pack failed validation");
    return 0;
}

/* the full-depth box. depth scales the frame inside it, so the knob does
   not recreate the window. a channel change does, because the box moves. */
static void cz_full_size(unsigned *width, unsigned *height)
{
    unsigned tall = CZ_BASE_HEIGHT;
    unsigned wide = cz_pack.width * tall / cz_pack.height;
    if (wide > WAVE_WIDTH) {
        tall = tall * WAVE_WIDTH / wide;
        wide = WAVE_WIDTH;
    }
    *width = wide ? wide : 1u;
    *height = tall ? tall : 1u;
}

static void cz_place(unsigned channel, int width, int height, int *x, int *y)
{
    *x = WAVE_LEFT + (WAVE_WIDTH - width) / 2;
    if (channel == 0u)
        *y = WAVE_TOP + (WAVE_STRIP - height) / 2;
    else if (channel == 1u)
        *y = WAVE_TOP + WAVE_STRIP + (WAVE_STRIP - height) / 2;
    else
        *y = WAVE_TOP + WAVE_STRIP - height / 2;
}

static void cz_close(void)
{
    rx3_gr_close(&cz_window.gr);
    cz_window.shown = -1;
}

/* nearest-neighbour scale of one frame into the window. anchor 2 rests on
   the bottom of the box; anchor 1 is centred. the rest of the box is keyed. */
static void cz_draw(unsigned frame, unsigned wide, unsigned tall)
{
    const uint8_t *source = cz_pack.pixels + (size_t)frame * cz_pack.pixel_bytes;
    unsigned left = (cz_window.gr.width - wide) / 2u;
    unsigned top = cz_pack.anchor == 2u ? cz_window.gr.height - tall
                                        : (cz_window.gr.height - tall) / 2u;
    uint32_t step_x = (cz_pack.width << 16) / wide;
    uint32_t step_y = (cz_pack.height << 16) / tall;
    int pitch;
    uint8_t *pixels = rx3_gr_lock(&cz_window.gr, &pitch);
    unsigned row;
    if (!pixels || !wide || !tall)
        return;
    for (row = 0; row < cz_window.gr.height; row++) {
        uint16_t *line = (uint16_t *)(pixels + (size_t)row * (unsigned)pitch);
        unsigned column;
        if (row < top || row >= top + tall) {
            for (column = 0; column < cz_window.gr.width; column++)
                line[column] = DS_GR_KEY_RGB565;
            continue;
        }
        {
            const uint8_t *source_row =
                source + (size_t)(((row - top) * step_y) >> 16) * cz_pack.width;
            uint32_t along = 0;
            for (column = 0; column < left; column++)
                line[column] = DS_GR_KEY_RGB565;
            for (column = 0; column < wide; column++) {
                line[left + column] = cz_pack.palette[source_row[along >> 16]];
                along += step_x;
            }
            for (column = left + wide; column < cz_window.gr.width; column++)
                line[column] = DS_GR_KEY_RGB565;
        }
    }
    rx3_gr_present(&cz_window.gr);
}

static void cz_hide(void)
{
    cz_showing = 0;
    cz_close();
}

/* trans owned: skip the stock dsp and publish what the picture needs.
   the fade tail is not owned, so stock rings out. the picture reads the
   published fields and never the manager. */
static int charizard2_feature_beat_fx_operate(void *manager, Float2 *buffer,
                                              int frames)
{
    const uint8_t *fields = manager;
    (void)buffer;
    (void)frames;
    if (!fields || !rx3_beat_fx_owned(fields, BEAT_FX_TRANS)) {
        cz_on = 0;
        return 0;
    }
    cz_channel = *(const uint32_t *)(fields + 0x00u);
    cz_depth = *(const float *)(fields + 0xe8u);
    cz_on = rx3_beat_fx_on(fields, BEAT_FX_TRANS);
    return 1;
}

static uint32_t charizard2_feature_image_swap(uint32_t image_id)
{
    if ((image_id >= TRANS_CAPTION_FIRST && image_id <= TRANS_CAPTION_LAST) ||
        (image_id >= TRANS_SELECT_FIRST && image_id <= TRANS_SELECT_LAST))
        return rx3_relabel_image(image_id, &cz_label_caption);
    return image_id;
}

static void charizard2_feature_after_image(void *render, void *image,
                                           uint16_t window_layer,
                                           uint32_t image_id)
{
    unsigned now = now_ms();
    unsigned channel;
    float depth;
    unsigned full_w;
    unsigned full_h;
    unsigned wide;
    unsigned tall;
    unsigned frame;
    int x;
    int y;
    (void)render;
    (void)image;
    (void)window_layer;
    (void)image_id;
    if (cz_tick && now - cz_tick < CZ_FRAME_MS)
        return;
    cz_tick = now ? now : 1u;
    if (!cz_loaded || !cz_on || !rx3_wave_screen_up()) {
        cz_hide();
        return;
    }
    __sync_synchronize();
    channel = cz_channel;
    depth = cz_depth;
    if (!(depth > 0.0f)) {
        cz_hide();
        return;
    }
    cz_full_size(&full_w, &full_h);
    tall = (unsigned)((float)full_h * depth + 0.5f);
    if (!tall) {
        cz_hide();
        return;
    }
    if (tall > full_h)
        tall = full_h;
    wide = full_w * tall / full_h;
    if (!wide)
        wide = 1u;
    if (wide > full_w)
        wide = full_w;
    cz_place(channel, (int)full_w, (int)full_h, &x, &y);
    if (cz_window.gr.handle &&
        (cz_window.gr.x != x || cz_window.gr.y != y ||
         cz_window.gr.width != full_w || cz_window.gr.height != full_h ||
         cz_window.channel != channel))
        cz_close();
    if (!cz_window.gr.handle) {
        if (cz_window.failed_ms && now - cz_window.failed_ms < CZ_RETRY_MS)
            return;
        if (rx3_gr_open(&cz_window.gr, x, y, full_w, full_h, RX3_GR_ON_TOP)) {
            cz_window.failed_ms = now ? now : 1u;
            cz_log_once(2u, "charizard: window could not be created");
            return;
        }
        cz_window.failed_ms = 0;
        cz_window.shown = -1;
        cz_window.channel = channel;
    }
    if (!cz_showing) {
        cz_showing = 1;
        cz_epoch = now;
    }
    frame = rx3_sprite_fps_frame(now - cz_epoch, cz_pack.fps_num, cz_pack.fps_den,
                                 cz_pack.frame_count);
    if (cz_window.shown == (int)frame && cz_window.shown_tall == tall)
        return;
    cz_draw(frame, wide, tall);
    cz_window.shown = (int)frame;
    cz_window.shown_frame = frame;
    cz_window.shown_tall = tall;
}

static int charizard2_feature_configured(void)
{
    const char *flag = getenv("RX3_CHARIZARD2");
    const char *path = getenv("RX3_CHARIZARD_PACK");
    if (!flag || flag[0] != '1' || !path || !path[0])
        return 0;
    cz_path = path;
    return 1;
}

/* the broker has to own operateBeatEffect, or trans would keep its dsp. */
static int charizard2_feature_install(void)
{
    pthread_t thread;
    if (!original_operate_beat_effect || !cz_path)
        return 0;
    if (pthread_create(&thread, 0, cz_loader, 0))
        return 0;
    pthread_detach(thread);
    return 1;
}

static void charizard2_feature_remove(void)
{
    cz_on = 0;
    cz_hide();
}

#endif /* RX3_CHARIZARD2_FEATURE_H */
