/* SPDX-License-Identifier: MPL-2.0 */
/* host check that the player-side sprite decisions match the python tools. */

#include "mod/modules/sprites/1.19/rx3_sprites_logic.h"

#include <stdio.h>

int main(void)
{
    const uint8_t sample[] = "123456789";
    char stem[64];
    struct rx3_tempo_segment one = {0, 120000};
    struct rx3_tempo_segment two[2] = {{0, 120000}, {22050, 60000}};
    unsigned frame = 99u;
    const char *names[] = {"a.rx3spr", "b.rx3spr"};
    unsigned pick;
    if (rx3_sprite_checksum(sample, 9) != 0xcbf43926u)
        return 1;
    if (rx3_sprite_stem("/usb/Artist - Title.aiff", stem, sizeof(stem)) ||
        stem[0] != 'A')
        return 2;
    printf("stem %s\n", stem);
    printf("fps %u\n", rx3_sprite_fps_frame(500u, 10u, 1u, 4u));
    if (rx3_sprite_beat_frame(11025, &one, 1u, 4u, 1u, &frame))
        return 3;
    printf("beat-one %u\n", frame);
    if (rx3_sprite_beat_frame(44100, two, 2u, 4u, 1u, &frame))
        return 4;
    printf("beat-two %u\n", frame);
    if (rx3_sprite_beat_frame(-1, &one, 1u, 4u, 1u, &frame) || frame != 0u)
        return 5;
    pick = rx3_sprite_pick("demo", 0u, 1u, names, 2u, 0);
    printf("pick %u\n", pick);
    pick = rx3_sprite_pick("demo", 0u, 1u, names, 2u, names[pick]);
    printf("pick-next %u\n", pick);
    printf("long-stem ");
    if (rx3_sprite_stem("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.wav",
                        stem, sizeof(stem)))
        return 6;
    printf("%s\n", stem);
    puts("sprite logic: OK");
    return 0;
}
