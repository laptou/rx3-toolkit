/* SPDX-License-Identifier: MPL-2.0 */
/* host check for the play-screen key spelling. */

#include "mod/modules/keyshift/1.19/rx3_key_names.h"

#include <stdio.h>
#include <string.h>

static int expect(const char *base, int semitones, const char *want)
{
    char out[24];
    if (rx3_key_pair(base, semitones, out, (int)sizeof(out)) < 0)
        return 1;
    if (strcmp(out, want)) {
        printf("got %s + %d = %s, want %s\n", base, semitones, out, want);
        return 1;
    }
    printf("%s %+d %s\n", base, semitones, out);
    return 0;
}

int main(void)
{
    char out[24];
    if (expect("Am", 2, "Bm (Am)"))
        return 1;
    if (expect("C", 2, "D (C)"))
        return 2;
    if (expect("F#", 1, "G (F#)"))
        return 3;
    if (expect("C#", 1, "D (C#)"))
        return 4;
    if (expect("Db", 1, "D (Db)"))
        return 5;
    if (expect("Am", -1, "Abm (Am)"))
        return 6;
    if (expect("Am", 12, "Am"))
        return 7;
    if (expect("8A", 2, "10A (8A)"))
        return 8;
    if (expect("8A", 7, "9A (8A)"))
        return 9;
    if (expect("8A", -1, "1A (8A)"))
        return 10;
    if (expect("8B", 2, "10B (8B)"))
        return 11;
    if (expect("8d", 2, "10d (8d)"))
        return 12;
    if (expect("8m", 2, "10m (8m)"))
        return 13;
    if (rx3_key_pair("Hello", 1, out, (int)sizeof(out)) >= 0)
        return 14;
    puts("key names: OK");
    return 0;
}
