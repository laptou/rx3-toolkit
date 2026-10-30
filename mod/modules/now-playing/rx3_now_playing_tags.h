/* SPDX-License-Identifier: MPL-2.0
 * The artist and the comment for the file a deck loaded.
 *
 * DBIF_MusicInfo carries the id, the title and the path only: the player's
 * own play screen never shows the artist on a standalone USB deck, so the DB
 * record has no artist or comment to copy. The worker reads them from the
 * file's tags instead, on its own thread, so a slow USB read cannot hold up
 * the player. The formats are the ones firmware 1.19 accepts: MP3/AIFF/WAV
 * (ID3v2, or the native AIFF/WAV fields) and MP4/AAC (the ilst atoms).
 *
 * Everything here is byte-bounded and reads only the bytes a field needs.
 */

#ifndef RX3_NOW_PLAYING_TAGS_H
#define RX3_NOW_PLAYING_TAGS_H

#define NP_SEEK_SET 0
#define NP_SEEK_END 2
#define NP_TAG_FIELD_BYTES 4096u

struct now_playing_tags {
    uint16_t artist[NOW_PLAYING_ARTIST_UNITS];
    uint16_t comment[NOW_PLAYING_COMMENT_UNITS];
};

static unsigned long np_be32(const uint8_t *p)
{
    return ((unsigned long)p[0] << 24u) | ((unsigned long)p[1] << 16u) |
           ((unsigned long)p[2] << 8u) | p[3];
}

static unsigned long np_be24(const uint8_t *p)
{
    return ((unsigned long)p[0] << 16u) | ((unsigned long)p[1] << 8u) | p[2];
}

static unsigned long np_le32(const uint8_t *p)
{
    return ((unsigned long)p[3] << 24u) | ((unsigned long)p[2] << 16u) |
           ((unsigned long)p[1] << 8u) | p[0];
}

static unsigned long np_syncsafe(const uint8_t *p)
{
    return ((unsigned long)(p[0] & 0x7fu) << 21u) |
           ((unsigned long)(p[1] & 0x7fu) << 14u) |
           ((unsigned long)(p[2] & 0x7fu) << 7u) | (p[3] & 0x7fu);
}

static int np_read_at(int fd, unsigned long offset, void *buffer,
                      unsigned int length)
{
    unsigned int done = 0;
    if (lseek(fd, (off_t)offset, NP_SEEK_SET) < 0)
        return 0;
    while (done < length) {
        ssize_t got = read(fd, (uint8_t *)buffer + done, length - done);
        if (got <= 0)
            break;
        done += (unsigned int)got;
    }
    return done == length;
}

static unsigned long np_file_size(int fd)
{
    off_t end = lseek(fd, 0, NP_SEEK_END);
    return end < 0 ? 0u : (unsigned long)end;
}

/* Text output ---------------------------------------------------------------- */

static void np_trim(uint16_t *text, unsigned int units)
{
    unsigned int i = 0;
    while (i < units && text[i])
        i++;
    while (i && (text[i - 1u] == 0x20u || text[i - 1u] == 0x09u ||
                 text[i - 1u] == 0x0au || text[i - 1u] == 0x0du))
        i--;
    if (i < units)
        text[i] = 0;
}

static unsigned int np_utf16_copy(uint16_t *out, unsigned int units,
                                  const uint8_t *bytes, unsigned int length,
                                  int big_endian, int has_bom)
{
    unsigned int i = 0;
    if (has_bom && length >= 2u) {
        if (bytes[0] == 0xffu && bytes[1] == 0xfeu) {
            big_endian = 0;
            bytes += 2;
            length -= 2u;
        } else if (bytes[0] == 0xfeu && bytes[1] == 0xffu) {
            big_endian = 1;
            bytes += 2;
            length -= 2u;
        }
    }
    while (i + 1u < units && (i + 1u) * 2u <= length) {
        uint16_t unit = big_endian
            ? (uint16_t)(((uint16_t)bytes[i * 2u] << 8u) | bytes[i * 2u + 1u])
            : (uint16_t)(((uint16_t)bytes[i * 2u + 1u] << 8u) | bytes[i * 2u]);
        if (!unit)
            break;
        out[i++] = unit;
    }
    out[i] = 0;
    return i;
}

static unsigned int np_latin1_copy(uint16_t *out, unsigned int units,
                                   const uint8_t *bytes, unsigned int length)
{
    unsigned int i = 0;
    while (i + 1u < units && i < length && bytes[i]) {
        out[i] = bytes[i];
        i++;
    }
    out[i] = 0;
    return i;
}

static unsigned int np_utf8_copy(uint16_t *out, unsigned int units,
                                 const uint8_t *bytes, unsigned int length)
{
    unsigned int i = 0, at = 0;
    while (i + 2u < units && at < length) {
        uint32_t point = bytes[at++];
        if (point < 0x80u) {
            if (!point)
                break;
        } else if (point >= 0xc2u && point <= 0xdfu && at < length) {
            point = ((point & 0x1fu) << 6u) | (bytes[at++] & 0x3fu);
        } else if (point >= 0xe0u && point <= 0xefu && at + 1u < length) {
            point = ((point & 0x0fu) << 12u) | ((bytes[at] & 0x3fu) << 6u) |
                    (bytes[at + 1u] & 0x3fu);
            at += 2u;
        } else if (point >= 0xf0u && point <= 0xf4u && at + 2u < length) {
            point = ((point & 0x07u) << 18u) | ((bytes[at] & 0x3fu) << 12u) |
                    ((bytes[at + 1u] & 0x3fu) << 6u) | (bytes[at + 2u] & 0x3fu);
            at += 3u;
        } else {
            point = 0xfffdu;
        }
        if (point >= 0x10000u) {
            point -= 0x10000u;
            out[i++] = (uint16_t)(0xd800u | (point >> 10u));
            out[i++] = (uint16_t)(0xdc00u | (point & 0x3ffu));
        } else {
            out[i++] = (uint16_t)point;
        }
    }
    out[i] = 0;
    return i;
}

/* Decode a tagged string that starts with its ID3 encoding byte. `skip` is the
   number of bytes between that byte and the first character. */
static void np_id3_text(uint16_t *out, unsigned int units, const uint8_t *data,
                        unsigned int length, unsigned int skip)
{
    uint8_t encoding;
    if (length <= skip)
        return;
    encoding = data[0];
    data += 1u + skip;
    length -= 1u + skip;
    if (encoding == 1u || encoding == 2u)
        (void)np_utf16_copy(out, units, data, length, encoding == 2u, encoding == 1u);
    else if (encoding == 3u)
        (void)np_utf8_copy(out, units, data, length);
    else
        (void)np_latin1_copy(out, units, data, length);
    np_trim(out, units);
}

/* The comment frame has a three-byte language and a NUL-terminated descriptor
   before the text. Skip the descriptor, whichever width the encoding uses. */
static unsigned int np_id3_descriptor(const uint8_t *data, unsigned int length,
                                      unsigned int at, uint8_t encoding)
{
    if (encoding == 1u || encoding == 2u) {
        while (at + 1u < length) {
            if (!data[at] && !data[at + 1u])
                return at + 2u;
            at += 2u;
        }
    } else {
        while (at < length)
            if (!data[at++])
                return at;
    }
    return length;
}

/* ID3v2 at `base` ------------------------------------------------------------ */

static void np_id3v2(int fd, unsigned long base, struct now_playing_tags *tags)
{
    uint8_t header[10];
    unsigned long end, at;
    unsigned int version, flags;

    if (!np_read_at(fd, base, header, sizeof(header)))
        return;
    if (header[0] != 'I' || header[1] != 'D' || header[2] != '3')
        return;
    version = header[3];
    if (version < 2u || version > 4u)
        return;
    flags = header[5];
    end = base + 10u + np_syncsafe(header + 6);
    at = base + 10u;
    if (flags & 0x40u) {
        uint8_t extended[4];
        unsigned long size;
        if (!np_read_at(fd, at, extended, sizeof(extended)))
            return;
        size = version >= 4u ? np_syncsafe(extended) : np_be32(extended);
        at += version >= 4u ? size : 4u + size;
    }
    if (end < at || end - at > 0x400000u)
        return;

    while (at + (version == 2u ? 6u : 10u) <= end) {
        uint8_t frame[10];
        unsigned long size;
        unsigned int header_size = version == 2u ? 6u : 10u;

        if (!np_read_at(fd, at, frame, header_size))
            break;
        if (!frame[0])
            break;
        if (version == 2u) {
            size = np_be24(frame + 3);
        } else {
            size = version == 4u ? np_syncsafe(frame + 4) : np_be32(frame + 4);
        }
        if (!size)
            break;
        /* Advance by the frame's real length; only the read is clamped, so a
            large frame cannot make the next header land in its middle. */
        unsigned long advance = size;
        unsigned int read_size;
        if (at + header_size + advance > end)
            advance = end - at - header_size;
        read_size = advance > NP_TAG_FIELD_BYTES
            ? (unsigned int)NP_TAG_FIELD_BYTES : (unsigned int)advance;
        if (!read_size)
            break;

        int wanted_artist = version == 2u
            ? (frame[0] == 'T' && frame[1] == 'P' && frame[2] == '1')
            : (memcmp(frame, "TPE1", 4) == 0);
        int wanted_comment = version == 2u
            ? (frame[0] == 'C' && frame[1] == 'O' && frame[2] == 'M')
            : (memcmp(frame, "COMM", 4) == 0);
        if (wanted_artist || wanted_comment) {
            uint8_t buffer[NP_TAG_FIELD_BYTES];
            unsigned int length = read_size;
            if (np_read_at(fd, at + header_size, buffer, length)) {
                if (flags & 0x80u) {          /* unsynchronisation */
                    unsigned int in = 0, out = 0;
                    while (in < length) {
                        buffer[out++] = buffer[in++];
                        if (buffer[out - 1u] == 0xffu && in < length &&
                            buffer[in] == 0x00u)
                            in++;
                    }
                    length = out;
                }
                if (wanted_artist) {
                    np_id3_text(tags->artist, NOW_PLAYING_ARTIST_UNITS,
                                buffer, length, 0u);
                } else {
                    unsigned int skip = 3u;   /* language */
                    skip = np_id3_descriptor(buffer, length, 1u + skip,
                                             buffer[0]);
                    np_id3_text(tags->comment, NOW_PLAYING_COMMENT_UNITS,
                                buffer, length, skip - 1u);
                }
            }
            at += header_size + advance;
        } else {
            at += header_size + advance;
        }
    }
}

static int np_id3v1(int fd, unsigned long size, struct now_playing_tags *tags)
{
    uint8_t tag[128];
    if (size < sizeof(tag) ||
        !np_read_at(fd, size - sizeof(tag), tag, sizeof(tag)))
        return 0;
    if (tag[0] != 'T' || tag[1] != 'A' || tag[2] != 'G')
        return 0;
    if (!tags->artist[0])
        (void)np_latin1_copy(tags->artist, NOW_PLAYING_ARTIST_UNITS,
                             tag + 33, 30u);
    if (!tags->comment[0])
        (void)np_latin1_copy(tags->comment, NOW_PLAYING_COMMENT_UNITS,
                             tag + 97, 30u);
    np_trim(tags->artist, NOW_PLAYING_ARTIST_UNITS);
    np_trim(tags->comment, NOW_PLAYING_COMMENT_UNITS);
    return 1;
}

/* WAV and AIFF ---------------------------------------------------------------- */

static void np_riff_chunks(int fd, unsigned long start, unsigned long end,
                           int little, struct now_playing_tags *tags)
{
    uint8_t header[8];
    unsigned long at = start;
    while (at + 8u <= end && np_read_at(fd, at, header, sizeof(header))) {
        unsigned long size = little ? np_le32(header + 4) : np_be32(header + 4);
        unsigned long data = at + 8u;
        if (size > end - data)
            size = end - data;

        if (header[0] == 'I' && header[1] == 'D' && header[2] == '3' && size) {
            np_id3v2(fd, data, tags);
        } else if (header[0] == 'L' && header[1] == 'I' && header[2] == 'S' &&
                   header[3] == 'T' && size >= 4u) {
            uint8_t form[4];
            if (np_read_at(fd, data, form, sizeof(form)) &&
                form[0] == 'I' && form[1] == 'N' && form[2] == 'F' &&
                form[3] == 'O')
                np_riff_chunks(fd, data + 4u, data + size, 1, tags);
        } else if (little &&
                   ((header[0] == 'I' && header[1] == 'A' && header[2] == 'R' &&
                     header[3] == 'T') ||
                    (header[0] == 'I' && header[1] == 'C' && header[2] == 'M' &&
                     header[3] == 'T'))) {
            uint8_t text[NP_TAG_FIELD_BYTES];
            unsigned int length = size > sizeof(text) ? (unsigned int)sizeof(text)
                                                      : (unsigned int)size;
            if (np_read_at(fd, data, text, length)) {
                uint16_t *field = header[1] == 'A' ? tags->artist : tags->comment;
                unsigned int units = header[1] == 'A'
                    ? NOW_PLAYING_ARTIST_UNITS : NOW_PLAYING_COMMENT_UNITS;
                (void)np_utf8_copy(field, units, text, length);
                np_trim(field, units);
            }
        } else if (!little &&
                   ((header[0] == 'A' && header[1] == 'U' && header[2] == 'T' &&
                     header[3] == 'H') ||
                    (header[0] == 'A' && header[1] == 'N' && header[2] == 'N' &&
                     header[3] == 'O'))) {
            uint8_t text[NP_TAG_FIELD_BYTES];
            unsigned int length = size > sizeof(text) ? (unsigned int)sizeof(text)
                                                      : (unsigned int)size;
            if (np_read_at(fd, data, text, length)) {
                uint16_t *field = header[1] == 'U' ? tags->artist : tags->comment;
                unsigned int units = header[1] == 'U'
                    ? NOW_PLAYING_ARTIST_UNITS : NOW_PLAYING_COMMENT_UNITS;
                (void)np_utf8_copy(field, units, text, length);
                np_trim(field, units);
            }
        }
        at = data + size + (size & 1u);
    }
}

/* MP4 ilst atoms -------------------------------------------------------------- */

static int np_mp4_data(int fd, unsigned long at, unsigned long end,
                       uint16_t *out, unsigned int units)
{
    uint8_t header[16];
    unsigned long size;
    if (at + 8u > end || !np_read_at(fd, at, header, sizeof(header)))
        return 0;
    size = np_be32(header);
    if (size < 16u || size > end - at)
        size = end - at;
    if (!(header[4] == 'd' && header[5] == 'a' && header[6] == 't' &&
          header[7] == 'a'))
        return 0;

    uint32_t kind = (uint32_t)np_be32(header + 8);
    uint8_t text[NP_TAG_FIELD_BYTES];
    unsigned int length = size > 16u + sizeof(text) ? (unsigned int)sizeof(text)
                                                    : (unsigned int)(size - 16u);
    if (!length || !np_read_at(fd, at + 16u, text, length))
        return 0;
    if (kind == 2u)
        (void)np_utf16_copy(out, units, text, length, 1, 0);
    else
        (void)np_utf8_copy(out, units, text, length);
    np_trim(out, units);
    return 1;
}

/* The ilst entry is a container: its children are the data atom. */
static int np_mp4_field(int fd, unsigned long start, unsigned long end,
                        uint32_t marker, uint16_t *out, unsigned int units,
                        unsigned int depth)
{
    uint8_t header[8];
    unsigned long at = start;
    if (depth > 5u)
        return 0;
    while (at + 8u <= end && np_read_at(fd, at, header, sizeof(header))) {
        unsigned long size = np_be32(header);
        unsigned long data = at + 8u;
        if (size == 1u) {
            uint8_t wide[8];
            if (!np_read_at(fd, data, wide, sizeof(wide)) || np_be32(wide))
                break;                      /* a >4 GiB ilst is not this */
            size = np_be32(wide + 4u);
            data = at + 16u;
        } else if (size == 0u) {
            size = end - at;
        }
        if (size < 8u || size > end - at)
            break;
        if (np_be32(header + 4u) == marker)
            return np_mp4_data(fd, data, at + size, out, units);
        int container =
            (header[4] == 'm' && header[5] == 'o' && header[6] == 'o' &&
             header[7] == 'v') ||
            (header[4] == 'u' && header[5] == 'd' && header[6] == 't' &&
             header[7] == 'a') ||
            (header[4] == 'i' && header[5] == 'l' && header[6] == 's' &&
             header[7] == 't');
        if (container) {
            if (np_mp4_field(fd, data, at + size, marker, out, units, depth + 1u))
                return 1;
        } else if (header[4] == 'm' && header[5] == 'e' && header[6] == 't' &&
                   header[7] == 'a') {
            /* meta has a four-byte version and flags before its children. */
            if (np_mp4_field(fd, data + 4u, at + size, marker, out, units,
                             depth + 1u))
                return 1;
        }
        at += size;
    }
    return 0;
}

static void np_mp4(int fd, struct now_playing_tags *tags)
{
    uint8_t header[12];
    unsigned long size = np_file_size(fd);
    if (size < 12u || !np_read_at(fd, 0, header, sizeof(header)))
        return;
    (void)np_mp4_field(fd, 0, size, 0xa9415254u, tags->artist,
                       NOW_PLAYING_ARTIST_UNITS, 0u);
    (void)np_mp4_field(fd, 0, size, 0xa9636d74u, tags->comment,
                       NOW_PLAYING_COMMENT_UNITS, 0u);
}

/* Entry point ----------------------------------------------------------------- */

static int np_read_tags(const char *path, struct now_playing_tags *tags)
{
    uint8_t header[12];
    unsigned long size;
    int fd;

    if (!path || !path[0])
        return 0;
    memset(tags, 0, sizeof(*tags));
    fd = open(path, 0 /* read only */);
    if (fd < 0)
        return 0;
    size = np_file_size(fd);
    if (size < 12u || !np_read_at(fd, 0, header, sizeof(header))) {
        close(fd);
        return 0;
    }

    if (header[0] == 'I' && header[1] == 'D' && header[2] == '3') {
        np_id3v2(fd, 0, tags);
        if (!tags->artist[0] || !tags->comment[0])
            (void)np_id3v1(fd, size, tags);
    } else if (header[0] == 'R' && header[1] == 'I' && header[2] == 'F' &&
               header[3] == 'F' && header[8] == 'W' && header[9] == 'A' &&
               header[10] == 'V' && header[11] == 'E') {
        np_riff_chunks(fd, 12u, size, 1, tags);
    } else if (header[0] == 'F' && header[1] == 'O' && header[2] == 'R' &&
               header[3] == 'M' && header[8] == 'A' && header[9] == 'I' &&
               header[10] == 'F') {
        np_riff_chunks(fd, 12u, size, 0, tags);
    } else if (header[4] == 'f' && header[5] == 't' && header[6] == 'y' &&
               header[7] == 'p') {
        np_mp4(fd, tags);
    } else {
        (void)np_id3v1(fd, size, tags);
    }
    close(fd);
    return 1;
}

#endif /* RX3_NOW_PLAYING_TAGS_H */
