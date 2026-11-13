/* SPDX-License-Identifier: MPL-2.0 */
#ifndef RX3_BEAT_TELEMETRY_PACKET_H
#define RX3_BEAT_TELEMETRY_PACKET_H
#include <stdint.h>

#define RX3_BT_PACKET_LEN 48u
#define RX3_BT_VERSION 1u
#define RX3_BT_FLAG_LOADED 0x01u
#define RX3_BT_FLAG_PLAYING 0x02u
#define RX3_BT_FLAG_GRID_VALID 0x04u
#define RX3_BT_FLAG_ON_AIR 0x08u
#define RX3_BT_FLAG_TEMPO_MASTER_VALID 0x10u
#define RX3_BT_FLAG_TEMPO_MASTER 0x20u

struct rx3_bt_packet {
    uint8_t deck;              /* 1 or 2 */
    uint8_t flags;
    uint64_t session;
    uint32_t generation;
    uint32_t sequence;
    uint64_t track_identity;   /* 0 when unavailable */
    uint32_t bpm_milli;
    uint32_t phase_milli;      /* 0..999 */
    uint8_t beat_in_bar;       /* 1..4, or 0 when invalid */
};

static void rx3_bt_put_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8u);
    out[2] = (uint8_t)(value >> 16u);
    out[3] = (uint8_t)(value >> 24u);
}

static void rx3_bt_put_u64(uint8_t *out, uint64_t value)
{
    rx3_bt_put_u32(out, (uint32_t)value);
    rx3_bt_put_u32(out + 4u, (uint32_t)(value >> 32u));
}

/* Fixed-width little-endian v1 datagram. Invalid fields must already be zero. */
static void rx3_bt_encode(const struct rx3_bt_packet *packet,
                          uint8_t out[RX3_BT_PACKET_LEN])
{
    for (unsigned int i = 0; i < RX3_BT_PACKET_LEN; i++)
        out[i] = 0;
    out[0] = 'B'; out[1] = 'C'; out[2] = 'B'; out[3] = '1';
    out[4] = RX3_BT_VERSION;
    out[5] = packet->deck;
    out[6] = packet->flags;
    rx3_bt_put_u64(out + 8u, packet->session);
    rx3_bt_put_u32(out + 16u, packet->generation);
    rx3_bt_put_u32(out + 20u, packet->sequence);
    rx3_bt_put_u64(out + 24u, packet->track_identity);
    rx3_bt_put_u32(out + 32u, packet->bpm_milli);
    rx3_bt_put_u32(out + 36u, packet->phase_milli);
    out[40] = packet->beat_in_bar;
}

/* Split a positive IEEE-754 beat count without a target double-to-64-bit
   conversion helper, which may not be exported by the player's runtime. */
static inline void rx3_bt_phase_and_bar(double beat, uint32_t *phase_milli,
                                 uint8_t *beat_in_bar)
{
    union { double value; uint64_t bits; } encoded;
    encoded.value = beat;
    unsigned int exponent_field = (unsigned int)((encoded.bits >> 52u) & 0x7ffu);
    int exponent = (int)exponent_field - 1023;
    uint64_t significand = encoded.bits & UINT64_C(0x000fffffffffffff);
    if (!exponent_field) {
        *phase_milli = 0;
        *beat_in_bar = 1;
        return;
    }
    significand |= UINT64_C(1) << 52u;
    uint64_t remainder = 0;
    unsigned int integer_mod4 = 0;
    if (exponent < 0) {
        unsigned int shift = (unsigned int)(52 - exponent);
        if (shift < 64u)
            remainder = significand;
    } else if (exponent < 52) {
        unsigned int shift = (unsigned int)(52 - exponent);
        uint64_t mask = (UINT64_C(1) << shift) - 1u;
        remainder = significand & mask;
        integer_mod4 = (unsigned int)((significand >> shift) & 3u);
    } else {
        unsigned int shift = (unsigned int)(exponent - 52);
        integer_mod4 = shift >= 2u ? 0u :
            (unsigned int)((significand << shift) & 3u);
    }
    if (exponent < 0) {
        unsigned int shift = (unsigned int)(52 - exponent);
        *phase_milli = shift < 64u
            ? (uint32_t)((remainder * 1000u) >> shift) : 0u;
    } else if (exponent < 52) {
        unsigned int shift = (unsigned int)(52 - exponent);
        *phase_milli = (uint32_t)((remainder * 1000u) >> shift);
    } else {
        *phase_milli = 0;
    }
    if (*phase_milli > 999u)
        *phase_milli = 999u;
    *beat_in_bar = (uint8_t)(integer_mod4 + 1u);
}
#endif
