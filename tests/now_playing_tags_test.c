/* SPDX-License-Identifier: MPL-2.0 */
/* host check of the now-playing tag reader against files built here. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#define NOW_PLAYING_ARTIST_UNITS 128u
#define NOW_PLAYING_COMMENT_UNITS 256u
#include "mod/modules/now-playing/rx3_now_playing_tags.h"

static void put_be32(uint8_t *p, unsigned long value)
{
    p[0] = (uint8_t)(value >> 24u);
    p[1] = (uint8_t)(value >> 16u);
    p[2] = (uint8_t)(value >> 8u);
    p[3] = (uint8_t)value;
}

static void put_le32(uint8_t *p, unsigned long value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
    p[2] = (uint8_t)(value >> 16u);
    p[3] = (uint8_t)(value >> 24u);
}

static FILE *create(const char *path)
{
    FILE *file = fopen(path, "wb");
    if (!file) {
        perror(path);
        exit(2);
    }
    return file;
}

static void utf8(uint16_t *text)
{
    for (; *text; text++) {
        uint32_t point = *text;
        unsigned char bytes[4];
        unsigned int count = 0;
        if (point < 0x80u) {
            bytes[count++] = (unsigned char)point;
        } else if (point < 0x800u) {
            bytes[count++] = (unsigned char)(0xc0u | (point >> 6u));
            bytes[count++] = (unsigned char)(0x80u | (point & 0x3fu));
        } else {
            bytes[count++] = (unsigned char)(0xe0u | (point >> 12u));
            bytes[count++] = (unsigned char)(0x80u | ((point >> 6u) & 0x3fu));
            bytes[count++] = (unsigned char)(0x80u | (point & 0x3fu));
        }
        fwrite(bytes, 1, count, stdout);
    }
}

static int check(const char *path, const char *artist, const char *comment)
{
    struct now_playing_tags tags;
    uint8_t got[512], want[512];
    unsigned int at = 0, expected = 0;
    if (!np_read_tags(path, &tags)) {
        printf("%s: no tags read\n", path);
        return 1;
    }
    if (!tags.artist[0] || !tags.comment[0]) {
        printf("%s: artist=[%s] comment=[%s]\n", path, (const char *)got,
               (const char *)got);
        return 1;
    }
    while (tags.artist[at] && at + 1u < sizeof(got)) {
        got[at] = (uint8_t)tags.artist[at];
        at++;
    }
    got[at] = 0;
    while (*artist && expected + 1u < sizeof(want))
        want[expected++] = (uint8_t)*artist++;
    want[expected] = 0;
    if (strcmp((const char *)got, (const char *)want) != 0) {
        printf("%s: artist ", path);
        utf8(tags.artist);
        printf(" want %s\n", (const char *)want);
        return 1;
    }
    at = expected = 0;
    while (tags.comment[at] && at + 1u < sizeof(got)) {
        got[at] = (uint8_t)tags.comment[at];
        at++;
    }
    got[at] = 0;
    while (*comment && expected + 1u < sizeof(want))
        want[expected++] = (uint8_t)*comment++;
    want[expected] = 0;
    if (strcmp((const char *)got, (const char *)want) != 0) {
        printf("%s: comment ", path);
        utf8(tags.comment);
        printf(" want %s\n", (const char *)want);
        return 1;
    }
    remove(path);
    return 0;
}

/* ID3v2.3, UTF-8 text. */
static void write_mp3(const char *path, int version1)
{
    FILE *file = create(path);
    if (version1) {
        uint8_t tag[128];
        memset(tag, 0, sizeof(tag));
        memcpy(tag, "TAG", 3);
        memcpy(tag + 33, "Id3 Artist", 10);
        memcpy(tag + 97, "Id3 Comment", 11);
        fwrite("MPEGDATA", 1, 8, file);
        fwrite(tag, 1, sizeof(tag), file);
        fclose(file);
        return;
    }
    {
        uint8_t tpe[64], comm[64], tag[256];
        unsigned int at = 0;
        tpe[0] = 3;                       /* UTF-8 */
        memcpy(tpe + 1, "Tag Artist", 10);
        comm[0] = 3;
        memcpy(comm + 1, "eng", 3);
        comm[4] = 0;                      /* empty descriptor */
        memcpy(comm + 5, "Tag Comment", 11);
        memcpy(tag, "ID3", 3);
        tag[3] = 3; tag[4] = 0; tag[5] = 0;
        at = 10;
        memcpy(tag + at, "TPE1", 4); put_be32(tag + at + 4, 11);
        tag[at + 8] = 0; tag[at + 9] = 0;
        memcpy(tag + at + 10, tpe, 11); at += 21;
        memcpy(tag + at, "COMM", 4); put_be32(tag + at + 4, 16);
        tag[at + 8] = 0; tag[at + 9] = 0;
        memcpy(tag + at + 10, comm, 16); at += 26;
        put_be32(tag + 6, 0);
        {
            unsigned long size = at - 10;
            tag[6] = (uint8_t)((size >> 21u) & 0x7fu);
            tag[7] = (uint8_t)((size >> 14u) & 0x7fu);
            tag[8] = (uint8_t)((size >> 7u) & 0x7fu);
            tag[9] = (uint8_t)(size & 0x7fu);
        }
        fwrite(tag, 1, at, file);
        fwrite("MPEGDATA", 1, 8, file);
        fclose(file);
    }
}

static void chunk_le(FILE *file, const char *id, const char *text)
{
    uint8_t header[8];
    unsigned int length = (unsigned int)strlen(text) + 1u;
    memcpy(header, id, 4);
    put_le32(header + 4, length);
    fwrite(header, 1, 8, file);
    fwrite(text, 1, length, file);
    if (length & 1u)
        fputc(0, file);
}

static void write_wav(const char *path)
{
    FILE *file = create(path);
    uint8_t header[12] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E'};
    uint8_t list[12] = {'L', 'I', 'S', 'T', 0, 0, 0, 0, 'I', 'N', 'F', 'O'};
    fwrite(header, 1, 12, file);
    fwrite(list, 1, 12, file);
    chunk_le(file, "IART", "Wav Artist");
    chunk_le(file, "ICMT", "Wav Comment");
    {
        long end = ftell(file);
        uint8_t size[4];
        put_le32(size, (unsigned long)(end - 8));
        fseek(file, 4, SEEK_SET);
        fwrite(size, 1, 4, file);
        put_le32(size, (unsigned long)(end - 20));
        fseek(file, 16, SEEK_SET);
        fwrite(size, 1, 4, file);
    }
    fclose(file);
}

static void chunk_be(FILE *file, const char *id, const char *text)
{
    uint8_t header[8];
    unsigned int length = (unsigned int)strlen(text);
    memcpy(header, id, 4);
    put_be32(header + 4, length);
    fwrite(header, 1, 8, file);
    fwrite(text, 1, length, file);
    if (length & 1u)
        fputc(0, file);
}

static void write_aiff(const char *path)
{
    FILE *file = create(path);
    uint8_t header[12] = {'F', 'O', 'R', 'M', 0, 0, 0, 0, 'A', 'I', 'F', 'F'};
    fwrite(header, 1, 12, file);
    chunk_be(file, "AUTH", "Aiff Artist");
    chunk_be(file, "ANNO", "Aiff Comment");
    {
        long end = ftell(file);
        uint8_t size[4];
        put_be32(size, (unsigned long)(end - 8));
        fseek(file, 4, SEEK_SET);
        fwrite(size, 1, 4, file);
    }
    fclose(file);
}

static void ilst_entry(FILE *file, const char *name, const char *text)
{
    uint8_t data[512], entry[512];
    unsigned int length = (unsigned int)strlen(text);
    unsigned int at = 0;
    put_be32(data, length + 16u);
    memcpy(data + 4, "data", 4);
    put_be32(data + 8, 1);               /* UTF-8 */
    put_be32(data + 12, 0);
    memcpy(data + 16, text, length);
    put_be32(entry, length + 24u);
    memcpy(entry + 4, name, 4);
    memcpy(entry + 8, data, length + 16u);
    at = length + 24u;
    fwrite(entry, 1, at, file);
}

static void write_mp4(const char *path)
{
    FILE *file = create(path);
    uint8_t ftyp[16] = {0, 0, 0, 16, 'f', 't', 'y', 'p', 'M', '4', 'A', ' ',
                        0, 0, 0, 0};
    uint8_t ilst[512], meta[512], udta[512], moov[512];
    unsigned long ilst_size = 0, meta_size = 0, udta_size = 0, moov_size = 0;
    FILE *scratch;

    fwrite(ftyp, 1, sizeof(ftyp), file);
    scratch = tmpfile();
    if (!scratch) return;
    ilst_entry(scratch, "\xa9""ART", "Mp4 Artist");
    ilst_entry(scratch, "\xa9""cmt", "Mp4 Comment");
    ilst_size = (unsigned long)ftell(scratch);
    rewind(scratch);
    {
        unsigned long got = fread(ilst + 8, 1, sizeof(ilst) - 8, scratch);
        put_be32(ilst, got + 8u);
        memcpy(ilst + 4, "ilst", 4);
        ilst_size = got + 8u;
    }
    fclose(scratch);

    put_be32(meta, ilst_size + 12u);
    memcpy(meta + 4, "meta", 4);
    put_be32(meta + 8, 0);
    memcpy(meta + 12, ilst, ilst_size);
    meta_size = ilst_size + 12u;

    put_be32(udta, meta_size + 8u);
    memcpy(udta + 4, "udta", 4);
    memcpy(udta + 8, meta, meta_size);
    udta_size = meta_size + 8u;

    put_be32(moov, udta_size + 8u);
    memcpy(moov + 4, "moov", 4);
    memcpy(moov + 8, udta, udta_size);
    moov_size = udta_size + 8u;

    fwrite(moov, 1, moov_size, file);
    fclose(file);
}

int main(void)
{
    write_mp3("/tmp/rx3-tags-v23.mp3", 0);
    if (check("/tmp/rx3-tags-v23.mp3", "Tag Artist", "Tag Comment"))
        return 1;
    write_mp3("/tmp/rx3-tags-v1.mp3", 1);
    if (check("/tmp/rx3-tags-v1.mp3", "Id3 Artist", "Id3 Comment"))
        return 2;
    write_wav("/tmp/rx3-tags.wav");
    if (check("/tmp/rx3-tags.wav", "Wav Artist", "Wav Comment"))
        return 3;
    write_aiff("/tmp/rx3-tags.aiff");
    if (check("/tmp/rx3-tags.aiff", "Aiff Artist", "Aiff Comment"))
        return 4;
    write_mp4("/tmp/rx3-tags.m4a");
    if (check("/tmp/rx3-tags.m4a", "Mp4 Artist", "Mp4 Comment"))
        return 5;
    printf("now-playing tags: OK\n");
    return 0;
}
