/* SPDX-License-Identifier: MPL-2.0 */
#ifndef RX3_BEAT_TELEMETRY_DECL_H
#define RX3_BEAT_TELEMETRY_DECL_H
#include "../core/api/rx3_module_api.h"
#include "rx3_beat_telemetry_packet.h"

#define RX3_BT_PLAY_BPM ((unsigned long)0x000fd1fcu)
#define RX3_BT_ON_AIR   ((unsigned long)0x000fe34cu)
static const uint8_t rx3_bt_play_bpm_guard[8] = {
    0x08, 0x40, 0x2d, 0xe9, 0x00, 0x10, 0xa0, 0xe1
};
static const uint8_t rx3_bt_on_air_guard[8] = {
    0x01, 0x20, 0xa0, 0xe1, 0x30, 0x10, 0x9f, 0xe5
};
typedef int (*rx3_bt_deck_int_fn)(unsigned int);
typedef int (*rx3_bt_on_air_fn)(unsigned int, int);

#define RX3_BT_PORT 50124u
#define RX3_BT_BROADCAST 0xfffffea9u /* 169.254.255.255, network order */
#define RX3_BT_TICK_US 50000u
#define RX3_BT_SOCKET_AF_INET 2
#define RX3_BT_SOCKET_DGRAM 2
#define RX3_BT_SOCKET_SOL_SOCKET 1
#define RX3_BT_SOCKET_SO_BROADCAST 6
#define RX3_BT_DONTWAIT 0x40

struct rx3_bt_address {
    uint16_t family;
    uint16_t port;
    uint32_t address;
    uint8_t zero[8];
};
struct rx3_bt_timeval { long seconds, microseconds; };
extern int socket(int, int, int);
extern int setsockopt(int, int, int, const void *, unsigned int);
extern ssize_t sendto(int, const void *, size_t, int, const void *, unsigned int);
extern int close(int);
extern int usleep(unsigned int);
extern int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
extern int pthread_join(pthread_t, void **);
#endif
