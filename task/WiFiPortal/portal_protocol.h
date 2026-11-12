#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "portal_network.h"

/* Respond only to one ordinary DNS question, never follow compressed names.
 * All A questions resolve to the local AP; AAAA gets an empty success reply. */
static inline size_t portal_dns_reply(uint8_t *packet, size_t length, size_t capacity)
{
    if (length > capacity || length < 17 || packet[2] & 0xf8 || packet[4] || packet[5] != 1) return 0;
    size_t end = 12;
    while (end < length && packet[end]) {
        unsigned label = packet[end++];
        if (label > 63 || label > length - end) return 0;
        end += label;
    }
    if (end >= length || length - ++end < 4) return 0;
    unsigned type = (packet[end] << 8) | packet[end+1];
    unsigned klass = (packet[end+2] << 8) | packet[end+3];
    end += 4;
    if (end > capacity) return 0;
    packet[2] = 0x81; packet[3] = 0x80;
    memset(packet + 6, 0, 6);
    if (type != 1 || klass != 1) return end;
    if (capacity - end < 16) return 0;
    static const uint8_t answer[] = {0xc0,0x0c, 0,1, 0,1, 0,0,0,0, 0,4, WIFI_PORTAL_IP_BYTES};
    packet[7] = 1;
    memcpy(packet + end, answer, sizeof(answer));
    return end + sizeof(answer);
}
