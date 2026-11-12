#ifndef HEATING_COMMAND_H
#define HEATING_COMMAND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ModbusMap.h"

static inline bool is_heating_register(uint16_t address)
{
    return address == MB_POWER_HOLD_HEATER_1 ||
           address == MB_POWER_HOLD_HEATER_2;
}

/* Recognize actual nonzero heater writes, never polls, motor/enable writes,
 * zero-power commands, or malformed frames. Does not alter gateway traffic. */
static inline bool power_frame_starts_heating(const uint8_t *frame, size_t size)
{
    if (!frame || size < 8 || frame[0] != MODBUS_SLAVE_POWER) return false;
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < size - 2; ++i) {
        crc ^= frame[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xa001u : 0u);
    }
    if (frame[size - 2] != (uint8_t)crc ||
        frame[size - 1] != (uint8_t)(crc >> 8)) return false;
    uint16_t address = ((uint16_t)frame[2] << 8) | frame[3];
    uint16_t value = ((uint16_t)frame[4] << 8) | frame[5];
    if (frame[1] == 0x06)
        return size == 8 && is_heating_register(address) && value != 0;
    if (frame[1] != 0x10 || size < 9 || value == 0 || value > 123 ||
        frame[6] != value * 2u || size != (size_t)frame[6] + 9 ||
        (uint32_t)address + value > MB_POWER_HOLD_SIGNED_MOTOR_GEAR + 1u)
        return false;
    for (uint16_t i = 0; i < value; ++i) {
        if (is_heating_register(address + i) &&
            (frame[7 + i * 2] != 0 || frame[8 + i * 2] != 0)) return true;
    }
    return false;
}
#endif
