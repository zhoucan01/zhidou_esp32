/* Run from project root:
 * cc -std=c99 -Wall -Wextra -I function/nanoModbus
 * agent-tp/tests/firmware/test_modbus_write_ack.c
 * function/nanoModbus/nanomodbus.c -o build/test_modbus_write_ack
 * build/test_modbus_write_ack
 */
#include "nanomodbus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t response[8];
static unsigned length, offset;
static int scenario;

static void crc(void)
{
    uint16_t value = nmbs_crc_calc(response, 6, NULL);
    response[6] = value >> 8;
    response[7] = value & 255;
}

static int32_t read_bytes(uint8_t *buf, uint16_t count, int32_t timeout, void *arg)
{
    (void)timeout; (void)arg;
    unsigned available = length - offset;
    if (count > available) count = available;
    memcpy(buf, response + offset, count);
    offset += count;
    return count;
}

static int32_t write_bytes(const uint8_t *buf, uint16_t count, int32_t timeout, void *arg)
{
    (void)timeout; (void)arg;
    assert(count == 8);
    memcpy(response, buf, 8);
    offset = 0;
    length = 8;
    switch (scenario) {
        case 0: length = 0; break; /* no response */
        case 1: length = 4; break; /* truncated response */
        case 2: response[0] = 2; crc(); break;
        case 3: response[1] = 5; crc(); break;
        case 4: response[3] ^= 1; crc(); break;
        case 5: response[5] ^= 1; crc(); break;
        case 6: response[7] ^= 1; break;
        /* 7: valid ACK; 8: local echo; 9: identical old ACK delivered
         * after the write. These three byte streams are indistinguishable. */
    }
    return count;
}

int main(void)
{
    nmbs_platform_conf platform;
    nmbs_platform_conf_create(&platform);
    platform.transport = NMBS_TRANSPORT_RTU;
    platform.read = read_bytes;
    platform.write = write_bytes;
    nmbs_t client;
    assert(nmbs_client_create(&client, &platform) == NMBS_ERROR_NONE);
    nmbs_set_destination_rtu_address(&client, 3);
    nmbs_set_read_timeout(&client, 100);
    nmbs_set_byte_timeout(&client, 20);
    const nmbs_error expected[] = {
        NMBS_ERROR_TIMEOUT, NMBS_ERROR_TIMEOUT, NMBS_ERROR_INVALID_UNIT_ID,
        NMBS_ERROR_INVALID_RESPONSE, NMBS_ERROR_INVALID_RESPONSE,
        NMBS_ERROR_INVALID_RESPONSE, NMBS_ERROR_CRC,
        NMBS_ERROR_NONE, NMBS_ERROR_NONE, NMBS_ERROR_NONE
    };
    for (scenario = 0; scenario < 10; ++scenario)
        assert(nmbs_write_single_register(&client, 1, 10) == expected[scenario]);
    puts("PASS: 10 ACK scenarios; identical echo/late ACK accepted as expected");
    return 0;
}
