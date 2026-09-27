/* SPDX-License-Identifier: MPL-2.0 */
/* host check for the sidebar key spelling. */

#include "mod/modules/keyshift/1.19/rx3_key_names.h"

#include <stdio.h>
#include <string.h>

static int expect(const char *base, int semitones, const char *want)
{
    char out[16];
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
    char out[16];
    if (expect("Am", 2, "Am Bm"))
        return 1;
    if (expect("C", 2, "C D"))
        return 2;
    if (expect("F#", 1, "F# G"))
        return 3;
    if (expect("C#", 1, "C# D"))
        return 4;
    if (expect("Db", 1, "Db D"))
        return 5;
    if (expect("Am", -1, "Am Abm"))
        return 6;
    if (expect("Am", 12, "Am"))
        return 7;
    if (expect("8A", 2, "8A 10A"))
        return 8;
    if (expect("8A", 7, "8A 9A"))
        return 9;
    if (expect("8A", -1, "8A 1A"))
        return 10;
    if (expect("8B", 2, "8B 10B"))
        return 11;
    if (expect("8d", 2, "8d 10d"))
        return 12;
    if (expect("8m", 2, "8m 10m"))
        return 13;
    if (rx3_key_pair("Hello", 1, out, (int)sizeof(out)) >= 0)
        return 14;
    puts("key names: OK");
    return 0;
}
