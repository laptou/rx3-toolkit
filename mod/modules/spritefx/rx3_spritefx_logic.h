/* SPDX-License-Identifier: MPL-2.0
 * pure sprite decisions shared by the player and the host test.
 * no allocation and no libc, so the arm hook can include it.
 */

#ifndef RX3_SPRITEFX_LOGIC_H
#define RX3_SPRITEFX_LOGIC_H

/* the arm build meets the platform's own fixed-width types first; a bare
   host compile brings stdint itself. */
#ifndef RX3_PLATFORM_H
#include <stddef.h>
#include <stdint.h>
#endif


static uint32_t rx3_sprite_checksum(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xffffffffu;
    size_t index;
    for (index = 0; index < length; index++) {
        int bit;
        crc ^= data[index];
        for (bit = 0; bit < 8; bit++) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}



static unsigned rx3_sprite_fps_frame(uint32_t elapsed_ms, unsigned fps_num,
                                    unsigned fps_den, unsigned frame_count)
{
    if (!fps_num || !fps_den || !frame_count)
        return 0;
    return (unsigned)(((uint64_t)elapsed_ms * fps_num / fps_den / 1000u) %
                      frame_count);
}


#endif /* RX3_SPRITEFX_LOGIC_H */
