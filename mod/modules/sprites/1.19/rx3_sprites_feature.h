/* SPDX-License-Identifier: MPL-2.0
 * usb sprite overlays for firmware 1.19.
 * draws only through pal_draw_image, and only after a proven hard-mask format
 * and waveform anchor. anything unproven leaves the stock image untouched.
 */

#ifndef RX3_SPRITES_FEATURE_H
#define RX3_SPRITES_FEATURE_H

#include "rx3_sprites_logic.h"

#define SPRITE_ID_FIRST 0x1604u
#define SPRITE_ID_COUNT 255u
#define SPRITE_PROBE_ID 0x1703u
#define SPRITE_PACK_LIMIT 64u
#define SPRITE_MEMORY_CEILING (2u * 1024u * 1024u)
#define SPRITE_FILE_LIMIT (1024u * 1024u)
#define SPRITE_TRACE_LIMIT 96u

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

struct rx3_format_proof {
    unsigned format_byte;
    unsigned palette;
    unsigned bpp;
    unsigned transparent;
    unsigned entry_size;
    int ok;
};

struct rx3_anchor {
    unsigned layer;
    unsigned image_id;
    unsigned deck;
};

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
    unsigned format_byte;
    unsigned id0;
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
    unsigned id0;
    unsigned epoch_ms;
    int have_phase;
    uint32_t phase_milli;
    char name[48];
    char previous[48];
};

static const char *sprites_dir;
static uint32_t sprites_nonce = 1u;
static int sprites_trace;
static int sprites_probe;
static int sprites_owns_timestretch;
static unsigned sprite_next_id = SPRITE_ID_FIRST;
static unsigned sprite_bytes_used;
static struct sprite_deck sprite_decks[2];
static struct sprite_job *volatile sprite_jobs[2];
static struct rx3_format_proof sprite_format;
static struct rx3_anchor sprite_anchors[16];
static unsigned sprite_anchor_count;
static int sprite_anchors_ok;
static unsigned sprite_logged;
static uint8_t *sprite_probe_pixels;
static uint8_t *sprite_probe_palette;
static void *sprite_probe_block;
static size_t sprite_probe_block_bytes;
static int sprite_probe_ready;
static unsigned sprite_probe_drawn_ms;
static void *sprite_kept[32];
static size_t sprite_kept_bytes[32];
static unsigned sprite_kept_count;

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

static int sprite_keep(void *block, size_t bytes)
{
    if (!block || !bytes)
        return -1;
    if (sprite_kept_count >= 32u ||
        sprite_bytes_used + bytes > SPRITE_MEMORY_CEILING) {
        sprite_log_once(1u, "sprites disabled: sprite memory ceiling");
        return -1;
    }
    sprite_kept[sprite_kept_count] = block;
    sprite_kept_bytes[sprite_kept_count] = bytes;
    sprite_kept_count++;
    sprite_bytes_used += (unsigned)bytes;
    return 0;
}

static void sprite_release_all(void)
{
    unsigned index;
    for (index = 0; index < sprite_kept_count; index++)
        munmap(sprite_kept[index], sprite_kept_bytes[index]);
    sprite_kept_count = 0u;
    sprite_bytes_used = 0u;
    if (sprite_probe_block && sprite_probe_block_bytes) {
        munmap(sprite_probe_block, sprite_probe_block_bytes);
        sprite_probe_block = 0;
        sprite_probe_block_bytes = 0u;
    }
}

static int sprite_parse_format(const char *text, size_t length,
                              struct rx3_format_proof *proof)
{
    unsigned values[5];
    unsigned count = 0;
    size_t cursor = 0;
    static const char *keys[5] = {
        "format ", "palette ", "bpp ", "transparent ", "entry "
    };
    if (length < 8 || memcmp(text, "RX3FMT1\n", 8))
        return -1;
    cursor = 8;
    while (cursor < length && count < 5u) {
        unsigned value = 0;
        size_t key_length = str_length(keys[count]);
        if (cursor + key_length >= length ||
            memcmp(text + cursor, keys[count], key_length))
            return -1;
        cursor += key_length;
        if (cursor >= length || text[cursor] < '0' || text[cursor] > '9')
            return -1;
        while (cursor < length && text[cursor] >= '0' && text[cursor] <= '9') {
            value = value * 10u + (unsigned)(text[cursor] - '0');
            cursor++;
        }
        if (cursor >= length || text[cursor] != '\n')
            return -1;
        values[count++] = value;
        cursor++;
    }
    if (count != 5u || cursor != length)
        return -1;
    /* firmware 1.19 hard-mask, copied from a passing format.proof:
       format 2, palette, 8 bpp, transparent index 0, 16-bit entries. */
    if (values[0] != 2u || values[1] != 1u || values[2] != 8u ||
        values[3] != 0u || values[4] != 2u)
        return -1;
    proof->format_byte = values[0];
    proof->palette = values[1];
    proof->bpp = values[2];
    proof->transparent = values[3];
    proof->entry_size = values[4];
    proof->ok = 1;
    return 0;
}

static int sprite_parse_anchors(const char *text, size_t length)
{
    size_t cursor = 0;
    sprite_anchor_count = 0u;
    sprite_anchors_ok = 0;
    if (length < 8 || memcmp(text, "RX3ANC1\n", 8))
        return -1;
    cursor = 8;
    while (cursor < length) {
        unsigned layer = 0;
        unsigned image_id = 0;
        unsigned deck = 0;
        unsigned index;
        if (sprite_anchor_count >= 16u)
            return -1;
        if (cursor + 6 >= length || memcmp(text + cursor, "layer ", 6))
            return -1;
        cursor += 6;
        while (cursor < length && text[cursor] >= '0' && text[cursor] <= '9') {
            layer = layer * 10u + (unsigned)(text[cursor] - '0');
            cursor++;
        }
        if (cursor + 4 >= length || memcmp(text + cursor, " id ", 4))
            return -1;
        cursor += 4;
        while (cursor < length && text[cursor] >= '0' && text[cursor] <= '9') {
            image_id = image_id * 10u + (unsigned)(text[cursor] - '0');
            cursor++;
        }
        if (cursor + 6 >= length || memcmp(text + cursor, " deck ", 6))
            return -1;
        cursor += 6;
        if (cursor >= length || (text[cursor] != '0' && text[cursor] != '1'))
            return -1;
        deck = (unsigned)(text[cursor] - '0');
        cursor++;
        if (cursor >= length || text[cursor] != '\n')
            return -1;
        cursor++;
        for (index = 0; index < sprite_anchor_count; index++)
            if (sprite_anchors[index].layer == layer &&
                sprite_anchors[index].image_id == image_id)
                return -1;
        sprite_anchors[sprite_anchor_count].layer = layer;
        sprite_anchors[sprite_anchor_count].image_id = image_id;
        sprite_anchors[sprite_anchor_count].deck = deck;
        sprite_anchor_count++;
    }
    if (!sprite_anchor_count)
        return -1;
    sprite_anchors_ok = 1;
    return 0;
}

static int sprite_header_ok(struct rx3_sprite_file_header *header, size_t size,
                           const struct rx3_format_proof *proof)
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
    if (!proof->ok || header->format_byte != proof->format_byte ||
        header->palette_flag != 1u || header->transparent != 0u ||
        header->bpp != proof->bpp || header->entry_size != 2u)
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

static int sprite_load_proofs(void)
{
    char path[512];
    size_t size = 0;
    void *block;
    if (sprite_format.ok && sprite_anchors_ok)
        return 0;
    if (sprite_join(path, sizeof(path), "format.proof"))
        return -1;
    block = sprite_slurp(path, &size, 256u);
    if (!block || sprite_parse_format(block, size, &sprite_format)) {
        if (block)
            munmap(block, size);
        sprite_log_once(2u, "sprites disabled: hard-mask format is not proven");
        return -1;
    }
    munmap(block, size);
    if (sprite_join(path, sizeof(path), "anchors.proof"))
        return -1;
    block = sprite_slurp(path, &size, 2048u);
    if (!block || sprite_parse_anchors(block, size)) {
        if (block)
            munmap(block, size);
        sprite_anchors_ok = 0;
        sprite_log_once(4u, "sprites disabled: waveform anchor is not proven");
        return -1;
    }
    munmap(block, size);
    log_line("sprite hard-mask proof ok");
    return 0;
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

static void *sprite_loader(void *opaque)
{
    struct sprite_request *request = opaque;
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

    if (sprite_load_proofs())
        goto done;
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
    frame_bytes = sprite_header_ok(header, pack_size, &sprite_format);
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
    job->format_byte = header->format_byte;
    job->pixel_bytes = (unsigned)frame_bytes;
    job->block = pack;
    job->block_bytes = pack_size;
    job->palette = pack + 64u + header->name_length;
    job->pixels = job->palette + header->palette_count * header->entry_size;
    if (header->name_length < sizeof(job->name))
        memcpy(job->name, pack + 64u, header->name_length);
    if (sprite_publish(job))
        goto release_catalog;
release_catalog:
    if (catalog)
        munmap(catalog, catalog_size);
done:
    munmap(request, 4096u);
    return 0;
}

static int sprite_install_job(struct sprite_job *job)
{
    uint8_t *table;
    unsigned frame;
    unsigned long available;
    if (!tab_assets_ready)
        return -1;
    if (sprite_next_id + job->frame_count > SPRITE_ID_FIRST + SPRITE_ID_COUNT)
        return -1;
    available = memory_available_kb();
    if (available && job->block_bytes > available * 1024u / 5u)
        return -1;
    table = *(uint8_t **)IMAGE_TABLE_POINTER;
    if (!table)
        return -1;
    if (sprite_keep(job->block, job->block_bytes))
        return -1;
    job->block = 0;
    job->id0 = sprite_next_id;
    for (frame = 0; frame < job->frame_count; frame++) {
        uint8_t *record = table + (job->id0 + frame) * 44u;
        uint16_t width = (uint16_t)job->width;
        uint16_t height = (uint16_t)job->height;
        uint8_t *pixels = job->pixels + frame * job->pixel_bytes;
        uint32_t pixel_off = (uint32_t)(unsigned long)pixels -
                             (uint32_t)(unsigned long)table;
        uint32_t palette_off = (uint32_t)(unsigned long)job->palette -
                               (uint32_t)(unsigned long)table;
        memcpy(record, table + 0x1598u * 44u, 44u);
        memcpy(record + 4u, &width, sizeof(width));
        memcpy(record + 6u, &height, sizeof(height));
        record[0x18u] = (uint8_t)job->format_byte;
        record[0x19u] = 1u;
        memcpy(record + 0x20u, &pixel_off, sizeof(pixel_off));
        memcpy(record + 0x24u, &palette_off, sizeof(palette_off));
    }
    sprite_next_id = job->id0 + job->frame_count;
    return 0;
}

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
        if (job->generation != state->generation || sprite_install_job(job)) {
            sprite_log_once(32u, "sprites disabled: image install failed");
            if (job->block)
                munmap(job->block, job->block_bytes);
            munmap(job, 4096u);
            continue;
        }
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
        state->id0 = job->id0;
        state->epoch_ms = now_ms();
        state->have_phase = 0;
        state->phase_milli = 0;
        memcpy(state->previous, job->name, sizeof(state->previous));
        memcpy(state->name, job->name, sizeof(state->name));
        munmap(job, 4096u);
    }
}

static void sprite_draw_model(void *render, void *image, unsigned image_id,
                             int x1, int y1, int x2, int y2)
{
    uint8_t model[0x54];
    uint16_t box[4];
    if (x2 <= x1 || y2 <= y1)
        return;
    memcpy(model, image, sizeof(model));
    box[0] = (uint16_t)x1;
    box[1] = (uint16_t)y1;
    box[2] = (uint16_t)x2;
    box[3] = (uint16_t)y2;
    memcpy(model + 0x18u, box, sizeof(box));
    set_u32(model, 0x44u, image_id);
    sprite_overlay_draw = 1;
    original_draw_image(render, model);
    sprite_overlay_draw = 0;
}

static void sprite_trace(uint16_t layer, uint32_t image_id, uint16_t x1,
                        uint16_t y1, uint16_t x2, uint16_t y2)
{
    static uint16_t layers[SPRITE_TRACE_LIMIT];
    static uint32_t ids[SPRITE_TRACE_LIMIT];
    static unsigned seen;
    unsigned index;
    char line[96];
    unsigned length = 0;
    int fd;
    if (seen >= SPRITE_TRACE_LIMIT)
        return;
    for (index = 0; index < seen; index++)
        if (layers[index] == layer && ids[index] == image_id)
            return;
    layers[seen] = layer;
    ids[seen] = image_id;
    seen++;
    /* bounded decimal line. one record per distinct layer and image. */
    length = 0;
    {
        unsigned values[6];
        unsigned value_index;
        values[0] = layer;
        values[1] = image_id;
        values[2] = x1;
        values[3] = y1;
        values[4] = x2;
        values[5] = y2;
        for (value_index = 0; value_index < 6u; value_index++) {
            char digits[16];
            unsigned digit_count = 0;
            unsigned value = values[value_index];
            if (value_index && length + 1u < sizeof(line))
                line[length++] = ' ';
            if (!value)
                digits[digit_count++] = '0';
            while (value && digit_count < sizeof(digits)) {
                digits[digit_count++] = (char)('0' + rx3_umod(value, 10u));
                value /= 10u;
            }
            while (digit_count && length + 1u < sizeof(line))
                line[length++] = digits[--digit_count];
        }
    }
    if (length + 1u < sizeof(line))
        line[length++] = '\n';
    fd = open("/tmp/rx3-sprite-trace.log", O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0)
        return;
    (void)write(fd, line, length);
    close(fd);
}

static int sprite_prepare_probe(void)
{
    char path[512];
    size_t spec_size = 0;
    size_t bin_size = 0;
    void *spec;
    uint8_t *binary;
    struct rx3_format_proof proof;
    unsigned palette_bytes;
    if (sprite_probe_ready || !sprites_probe)
        return 0;
    if (sprite_join(path, sizeof(path), "probe.spec"))
        return -1;
    spec = sprite_slurp(path, &spec_size, 256u);
    if (!spec || sprite_parse_format(spec, spec_size, &proof)) {
        if (spec)
            munmap(spec, spec_size);
        sprite_log_once(64u, "sprite hard-mask probe FAILED");
        sprites_probe = 0;
        return -1;
    }
    munmap(spec, spec_size);
    if (sprite_join(path, sizeof(path), "probe.bin"))
        return -1;
    binary = sprite_slurp(path, &bin_size, 4096u);
    palette_bytes = 2u * proof.entry_size;
    if (!binary || bin_size < palette_bytes + 64u || proof.bpp != 8u) {
        if (binary)
            munmap(binary, bin_size);
        sprite_log_once(64u, "sprite hard-mask probe FAILED");
        sprites_probe = 0;
        return -1;
    }
    if (binary[0] != 0 || binary[palette_bytes] != 0 ||
        binary[palette_bytes + 7u] != 0 || binary[palette_bytes + 56u] != 0 ||
        binary[palette_bytes + 63u] != 0 || binary[palette_bytes + 27u] == 0) {
        munmap(binary, bin_size);
        sprite_log_once(64u, "sprite hard-mask probe FAILED");
        sprites_probe = 0;
        return -1;
    }
    sprite_probe_block = binary;
    sprite_probe_block_bytes = bin_size;
    sprite_probe_palette = binary;
    sprite_probe_pixels = binary + palette_bytes;
    sprite_probe_ready = 1;
    log_line("sprite hard-mask probe armed");
    return 0;
}

static int sprite_probe_record_ready;

static void sprite_draw_probe(void *render, void *image, uint16_t x1,
                             uint16_t y1, uint16_t x2, uint16_t y2)
{
    uint8_t *table;
    uint8_t *record;
    uint16_t eight = 8u;
    uint32_t pixel_off;
    uint32_t palette_off;
    int width;
    int height;
    unsigned now;
    if (!sprite_probe_ready && sprite_prepare_probe())
        return;
    if (!sprite_probe_ready || !tab_assets_ready)
        return;
    width = (int)x2 - (int)x1;
    height = (int)y2 - (int)y1;
    now = now_ms();
    if (width < 200 || height < 40 || now - sprite_probe_drawn_ms < 200u)
        return;
    table = *(uint8_t **)IMAGE_TABLE_POINTER;
    if (!table)
        return;
    if (!sprite_probe_record_ready) {
        uint8_t format_byte = (uint8_t)sprite_format.format_byte;
        record = table + SPRITE_PROBE_ID * 44u;
        if (!sprite_format.ok) {
            struct rx3_format_proof proof;
            char path[512];
            size_t spec_size = 0;
            void *spec;
            memset(&proof, 0, sizeof(proof));
            if (sprite_join(path, sizeof(path), "probe.spec"))
                return;
            spec = sprite_slurp(path, &spec_size, 256u);
            if (!spec || sprite_parse_format(spec, spec_size, &proof)) {
                if (spec)
                    munmap(spec, spec_size);
                return;
            }
            munmap(spec, spec_size);
            format_byte = (uint8_t)proof.format_byte;
        }
        memcpy(record, table + 0x1598u * 44u, 44u);
        memcpy(record + 4u, &eight, sizeof(eight));
        memcpy(record + 6u, &eight, sizeof(eight));
        record[0x18u] = format_byte;
        pixel_off = (uint32_t)(unsigned long)sprite_probe_pixels -
                    (uint32_t)(unsigned long)table;
        palette_off = (uint32_t)(unsigned long)sprite_probe_palette -
                      (uint32_t)(unsigned long)table;
        record[0x19u] = 1u;
        memcpy(record + 0x20u, &pixel_off, sizeof(pixel_off));
        memcpy(record + 0x24u, &palette_off, sizeof(palette_off));
        sprite_probe_record_ready = 1;
    }
    sprite_probe_drawn_ms = now;
    sprite_draw_model(render, image,
                      SPRITE_PROBE_ID,
                      x1 + width / 2 - 4, y1 + height / 2 - 4,
                      x1 + width / 2 + 4, y1 + height / 2 + 4);
}

static void sprites_feature_after_image(void *render, void *image,
                                        uint16_t window_layer,
                                        uint32_t image_id)
{
    uint16_t x1, y1, x2, y2;
    unsigned deck;
    struct sprite_deck *state;
    unsigned frame = 0;
    int left;
    int top;
    int width;
    int height;
    sprite_take_jobs();
    memcpy(&x1, (const uint8_t *)image + 0x18u, sizeof(x1));
    memcpy(&y1, (const uint8_t *)image + 0x1au, sizeof(y1));
    memcpy(&x2, (const uint8_t *)image + 0x1cu, sizeof(x2));
    memcpy(&y2, (const uint8_t *)image + 0x1eu, sizeof(y2));
    if (sprites_trace)
        sprite_trace(window_layer, image_id, x1, y1, x2, y2);
    if (sprites_probe)
        sprite_draw_probe(render, image, x1, y1, x2, y2);
    if (!sprite_format.ok || !sprite_anchors_ok)
        return;
    for (deck = 0; deck < sprite_anchor_count; deck++) {
        if (sprite_anchors[deck].layer == window_layer &&
            sprite_anchors[deck].image_id == image_id)
            break;
    }
    if (deck == sprite_anchor_count)
        return;
    state = &sprite_decks[sprite_anchors[deck].deck];
    if (!state->live || state->frame_count == 0u)
        return;
    if (state->mode == 1u)
        frame = rx3_sprite_fps_frame(now_ms() - state->epoch_ms, state->fps_num,
                                    state->fps_den, state->frame_count);
    else if (state->mode == 2u) {
        if (rx3_sprite_phase_frame(state->have_phase, state->phase_milli,
                                  state->frames_per_beat, state->beats_per_loop,
                                  state->frame_count, &frame)) {
            sprite_log_once(128u, "sprites: beat clock unavailable");
            return;
        }
    } else {
        return;
    }
    width = (int)state->width;
    height = (int)state->height;
    if ((int)x2 - (int)x1 < width || (int)y2 - (int)y1 < height)
        return;
    left = (int)x1 + (((int)x2 - (int)x1) - width) / 2;
    if (state->anchor == 2u)
        top = (int)y2 - height;
    else
        top = (int)y1 + (((int)y2 - (int)y1) - height) / 2;
    sprite_draw_model(render, image, state->id0 + frame, left, top,
                      left + width, top + height);
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
    const char *trace = getenv("RX3_SPRITE_TRACE");
    const char *probe = getenv("RX3_SPRITE_PROBE");
    const char *nonce = getenv("RX3_SPRITE_NONCE");
    sprites_trace = trace && trace[0] == '1';
    sprites_probe = probe && probe[0] == '1';
    if (nonce && nonce[0]) {
        uint32_t value = 0u;
        const char *cursor;
        for (cursor = nonce; *cursor >= '0' && *cursor <= '9'; cursor++)
            value = value * 10u + (uint32_t)(*cursor - '0');
        if (value)
            sprites_nonce = value;
    }
    return sprites_dir != 0 || sprites_trace || sprites_probe;
}

static int sprites_feature_install(void)
{
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
    sprite_release_all();
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
    struct sprite_request *request;
    pthread_t thread;
    (void)reader;
    if (deck >= 2u || !sprites_dir)
        return;
    request = mmap(0, 4096u, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
                   -1, 0);
    if (request == MAP_FAILED)
        return;
    memset(request, 0, sizeof(*request));
    request->deck = deck;
    request->generation = sprite_decks[deck].generation;
    if (rx3_sprite_stem((const char *)track_info, request->stem, sizeof(request->stem))) {
        munmap(request, 4096u);
        return;
    }
    if (!pthread_create(&thread, 0, sprite_loader, request))
        pthread_detach(thread);
    else
        munmap(request, 4096u);
}

static void sprites_feature_destroy_deck(unsigned int deck)
{
    if (deck < 2u)
        sprite_decks[deck].live = 0;
}

#endif /* RX3_SPRITES_FEATURE_H */
