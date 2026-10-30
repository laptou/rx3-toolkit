/* SPDX-License-Identifier: MPL-2.0 */
/* host check that the player-side sprite decisions match the python tools. */

#include "mod/modules/sprites/rx3_sprites_logic.h"

#include <stdio.h>

int main(void)
{
    const uint8_t sample[] = "123456789";
    char stem[64];
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
    if (rx3_sprite_phase_frame(1, 0u, 1u, 1u, 4u, &frame) || frame != 0u)
        return 3;
    printf("phase-zero %u\n", frame);
    if (rx3_sprite_phase_frame(1, 2500u, 1u, 1u, 4u, &frame) || frame != 2u)
        return 4;
    printf("phase-seek %u\n", frame);
    /* 2 frames a beat over a 4-beat loop: beat 3.5 is frame 7. */
    if (rx3_sprite_phase_frame(1, 3500u, 2u, 4u, 8u, &frame) || frame != 7u)
        return 7;
    printf("phase-loop %u\n", frame);
    if (rx3_sprite_phase_frame(0, 2500u, 1u, 1u, 4u, &frame) == 0)
        return 5;
    printf("phase-missing 1\n");
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
