/* SPDX-License-Identifier: MPL-2.0
 * musical keys as rbp spells them.
 * alphabet, camelot, and open key. a pitch shift keeps the notation
 * and the major or minor mode.
 */

#ifndef RX3_KEY_NAMES_H
#define RX3_KEY_NAMES_H

#define RX3_KEY_ALPHABET 0
#define RX3_KEY_CAMELOT 1
#define RX3_KEY_OPEN 2

struct rx3_key_name {
    int pitch;
    int minor;
    int notation;
    int sharp;
    char suffix;
};

static int rx3_key_length(const char *text)
{
    int length = 0;
    if (!text)
        return 0;
    while (text[length])
        length++;
    return length;
}

static int rx3_key_equal(const char *text, const char *name)
{
    int index = 0;
    while (name[index]) {
        if (text[index] != name[index])
            return 0;
        index++;
    }
    return text[index] == 0;
}

static const char *rx3_alpha_flat[12] = {
    "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"
};

static const char *rx3_alpha_sharp[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static int rx3_key_number(const char *text, int *used)
{
    int value;
    int count = 1;
    if (text[0] < '1' || text[0] > '9')
        return -1;
    value = text[0] - '0';
    if (text[1] >= '0' && text[1] <= '9') {
        value = value * 10 + (text[1] - '0');
        count = 2;
    }
    if (value < 1 || value > 12)
        return -1;
    *used = count;
    return value;
}

/* camelot 8b is c. number = (7 + 7 * pitch) mod 12 + 1. */
static int rx3_camelot_pitch(int number)
{
    return (7 * (number - 1) - 1) % 12;
}

static int rx3_camelot_number(int pitch)
{
    return (7 + 7 * pitch) % 12 + 1;
}

static int rx3_key_parse(const char *text, struct rx3_key_name *key)
{
    int used = 0;
    int number;
    int pitch;
    char suffix;
    const char *stem;
    char stem_buf[4];
    int minor;
    int index;
    if (!text || !text[0] || rx3_key_length(text) > 4)
        return -1;
    number = rx3_key_number(text, &used);
    if (number > 0) {
        suffix = text[used];
        if (!suffix || text[used + 1])
            return -1;
        key->notation = 0;
        key->sharp = 0;
        key->suffix = suffix;
        if (suffix == 'A' || suffix == 'a') {
            key->notation = RX3_KEY_CAMELOT;
            key->minor = 1;
        } else if (suffix == 'B' || suffix == 'b') {
            key->notation = RX3_KEY_CAMELOT;
            key->minor = 0;
        } else if (suffix == 'd' || suffix == 'D') {
            key->notation = RX3_KEY_OPEN;
            key->minor = 0;
        } else if (suffix == 'm' || suffix == 'M') {
            key->notation = RX3_KEY_OPEN;
            key->minor = 1;
        } else {
            return -1;
        }
        pitch = rx3_camelot_pitch(number);
        if (pitch < 0)
            pitch += 12;
        /* a minor key shares its number with the major a minor third above. */
        if (key->minor)
            pitch = (pitch + 9) % 12;
        key->pitch = pitch;
        return 0;
    }
    minor = 0;
    stem = text;
    index = rx3_key_length(text);
    if (index > 1 && text[index - 1] == 'm') {
        minor = 1;
        if (index - 1 > 3)
            return -1;
        stem_buf[0] = text[0];
        stem_buf[1] = index > 2 ? text[1] : 0;
        stem_buf[2] = index > 3 ? text[2] : 0;
        stem_buf[3] = 0;
        if (index == 2)
            stem_buf[1] = 0;
        stem = stem_buf;
    }
    for (pitch = 0; pitch < 12; pitch++) {
        if (rx3_key_equal(stem, rx3_alpha_sharp[pitch]) ||
            rx3_key_equal(stem, rx3_alpha_flat[pitch])) {
            key->pitch = pitch;
            key->minor = minor;
            key->notation = RX3_KEY_ALPHABET;
            key->sharp = stem[1] == '#' && pitch != 6;
            key->suffix = 0;
            return 0;
        }
    }
    return -1;
}

static int rx3_key_spell(const struct rx3_key_name *key, char *out, int cap)
{
    const char *name;
    int number;
    int pitch;
    int length;
    int index;
    if (cap < 2)
        return -1;
    if (key->notation == RX3_KEY_ALPHABET) {
        name = (key->sharp ? rx3_alpha_sharp : rx3_alpha_flat)[key->pitch];
        length = rx3_key_length(name);
        if (length + key->minor + 1 > cap)
            return -1;
        for (index = 0; index < length; index++)
            out[index] = name[index];
        if (key->minor)
            out[length++] = 'm';
        out[length] = 0;
        return length;
    }
    pitch = key->minor ? (key->pitch + 3) % 12 : key->pitch;
    number = rx3_camelot_number(pitch);
    if (number > 9) {
        if (cap < 4)
            return -1;
        out[0] = '1';
        out[1] = (char)('0' + number - 10);
        out[2] = key->suffix;
        out[3] = 0;
        return 3;
    }
    if (cap < 3)
        return -1;
    out[0] = (char)('0' + number);
    out[1] = key->suffix;
    out[2] = 0;
    return 2;
}

/* "Bm (Am)", the heard key then the analysed one, when the pitch differs.
   a multiple of 12 writes the base alone. -1 when `base` is not a key. */
static int rx3_key_pair(const char *base, int semitones, char *out, int cap)
{
    struct rx3_key_name key;
    struct rx3_key_name shifted;
    char heard[8];
    int base_len;
    int heard_len;
    int steps;
    int index;
    int at;
    if (rx3_key_parse(base, &key))
        return -1;
    base_len = rx3_key_length(base);
    steps = semitones % 12;
    if (steps < 0)
        steps += 12;
    if (!steps) {
        if (base_len + 1 > cap)
            return -1;
        for (index = 0; index < base_len; index++)
            out[index] = base[index];
        out[base_len] = 0;
        return base_len;
    }
    shifted = key;
    shifted.pitch = (key.pitch + steps) % 12;
    heard_len = rx3_key_spell(&shifted, heard, (int)sizeof(heard));
    if (heard_len < 0 || heard_len + 2 + base_len + 1 + 1 > cap)
        return -1;
    at = 0;
    for (index = 0; index < heard_len; index++)
        out[at++] = heard[index];
    out[at++] = ' ';
    out[at++] = '(';
    for (index = 0; index < base_len; index++)
        out[at++] = base[index];
    out[at++] = ')';
    out[at] = 0;
    return at;
}

#endif /* RX3_KEY_NAMES_H */
