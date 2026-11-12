#ifndef MODBUS_MAP_H
#define MODBUS_MAP_H

#include <stdint.h>

/* Protocol constants shared by the ESP32 Modbus master. Keep device-specific
 * values here so board protocol revisions remain localized. */
#define MODBUS_SLAVE_ACTUATOR              0x02U
#define MODBUS_SLAVE_POWER                 0x03U

#define MODBUS_GATEWAY_PROTOCOL_VERSION    0x010AU

/* Station 1 holding registers. 0x0000..0x000F remain the published debug
 * RAM block; functional registers are appended after that block. */
#define MB_LOCAL_HOLDING_DEBUG_COUNT       16U
#define MB_LOCAL_HOLDING_DISPLAY_FPS       0x0010U
#define MB_LOCAL_HOLDING_POWER_MOTOR_DIRECTION_MODE 0x0011U
#define MB_LOCAL_HOLDING_COUNT             18U
#define MODBUS_DISPLAY_FPS_DEFAULT         10U
#define MODBUS_DISPLAY_FPS_MIN             1U
#define MODBUS_DISPLAY_FPS_MAX             20U

#define MODBUS_POWER_MOTOR_DIRECTION_UNRESTRICTED 0U
#define MODBUS_POWER_MOTOR_DIRECTION_FORWARD_ONLY 1U
#define MODBUS_POWER_MOTOR_DIRECTION_REVERSE_ONLY 2U

/* Station 1, function 04. Firmware version occupies the first two registers.
 * Diagnostics are uint32, high register first. Heap values are bytes;
 * display values describe the last completed one-second window. */
typedef enum {
    MB_LOCAL_INPUT_VERSION_HIGH = 0x0000,
    MB_LOCAL_INPUT_VERSION_LOW = 0x0001,
    MB_LOCAL_INPUT_PSRAM_FREE_HIGH = 0x0014,
    MB_LOCAL_INPUT_INTERNAL_FREE_HIGH = 0x0016,
    MB_LOCAL_INPUT_DISPLAY_FRAMES_HIGH = 0x0018,
    MB_LOCAL_INPUT_DISPLAY_AVERAGE_US_HIGH = 0x001A,
    MB_LOCAL_INPUT_DISPLAY_MAXIMUM_US_HIGH = 0x001C,
    /* Bluetooth MAC, two bytes per register, high byte first. */
    MB_LOCAL_INPUT_BLUETOOTH_MAC_0 = 0x001E,
    MB_LOCAL_INPUT_BLUETOOTH_MAC_COUNT = 3,
    MB_LOCAL_INPUT_POWER_MOTOR_DIRECTION_MODE = 0x0021,
    MB_LOCAL_INPUT_COUNT = MB_LOCAL_INPUT_POWER_MOTOR_DIRECTION_MODE + 1,
} modbus_local_input_t;

#include "sys.h"

#define MODBUS_ACTUATOR_BAUD               ScreenBaud
#define MODBUS_POWER_BAUD                  PowerBaud

#define MODBUS_RESPONSE_TIMEOUT_MS         100
#define MODBUS_BYTE_TIMEOUT_MS             20
/* Each downstream station has an independent polling deadline. Actual polls
 * may be delayed while the shared command queue is busy. */
#define MODBUS_ACTUATOR_POLL_PERIOD_MS     20U
#define MODBUS_POWER_POLL_PERIOD_MS        100U
/* Total attempts, including the initial send (two retries on failure). */
#define MODBUS_COMMAND_RETRIES             3U

typedef enum {
    MB_COIL_WATER_PUMP = 0,
    MB_COIL_BRINE_PUMP = 1,
    MB_COIL_HOSE_PUMP = 2,
    MB_COIL_VALVE = 3,
    MB_COIL_LIGHT = 4,
    MB_COIL_RESERVED_5 = 5,
    MB_COIL_RESERVED_6 = 6,
    MB_COIL_RESERVED_7 = 7,
    MB_COIL_PRESS_MOTOR = 8,
    MB_COIL_RISE_MOTOR = 9,
    MB_COIL_COUNT = 10,
} modbus_actuator_coil_t;

typedef enum {
    MB_ACT_INPUT_TEMPERATURE_HIGH = 0x0000,
    MB_ACT_INPUT_VOLTAGE_HIGH = 0x0002,
    MB_ACT_INPUT_CURRENT_HIGH = 0x0004,
    MB_ACT_INPUT_RISE_COMMAND = 0x0006,
    MB_ACT_INPUT_TURNS_HIGH = 0x0007,
    MB_ACT_INPUT_STATUS = 0x0009,
    MB_ACT_INPUT_FAULT = 0x000A,
    MB_ACT_INPUT_VERSION_HIGH = 0x000B,
    MB_ACT_INPUT_DISCRETE_BITMAP = 0x000E,
    MB_ACT_INPUT_COIL_BITMAP = 0x000F,
    MB_ACT_INPUT_WATER_PUMP_COMMAND = 0x0010,
    MB_ACT_INPUT_HOSE_PUMP_COMMAND = 0x0011,
    MB_ACT_INPUT_PRESS_COMMAND = 0x0012,
    MB_ACT_INPUT_RISE_COMMAND_MIRROR = 0x0013,
    MB_ACT_INPUT_QUEUE_DEPTH = 0x0014,
    MB_ACT_INPUT_MODBUS_QUEUE_OVERFLOW_HIGH = 0x0015,
    MB_ACT_INPUT_MODBUS_CRC_ERROR_HIGH = 0x0017,
    MB_ACT_INPUT_MODBUS_FRAME_ERROR_HIGH = 0x0019,
    MB_ACT_INPUT_LEGACY_QUEUE_OVERFLOW_HIGH = 0x001B,
    MB_ACT_INPUT_TMUX_D0_BITMAP = 0x001D,
    MB_ACT_INPUT_TMUX_D1_BITMAP = 0x001E,
    MB_ACT_INPUT_TMUX_ADC_0 = 0x001F,
    MB_ACT_INPUT_CURRENT_ADC_RAW = 0x0027,

    /* V2 appended region 0x0028~0x004F (40 registers). Standard-only slaves
     * return 0 for these addresses; V2 main board populates them. */
    MB_ACT_INPUT_BUS_AMPERE_HIGH = 0x0028,
    MB_ACT_INPUT_LED_AMPERE_HIGH = 0x002A,
    MB_ACT_INPUT_BRAKE_AMPERE_HIGH = 0x002C,
    MB_ACT_INPUT_STRAIN_GAGE_AMPERE_HIGH = 0x002E,
    MB_ACT_INPUT_RISE_RIGHT_B_HIGH = 0x0030,
    MB_ACT_INPUT_RISE_RIGHT_A_HIGH = 0x0032,
    MB_ACT_INPUT_RISE_LEFT_B_HIGH = 0x0034,
    MB_ACT_INPUT_RISE_LEFT_A_HIGH = 0x0036,
    MB_ACT_INPUT_RISE_CURRENT_MAGNITUDE = 0x0038,
    MB_ACT_INPUT_PRESS_CURRENT_MAGNITUDE = 0x0039,
    MB_ACT_INPUT_RESET_KEY_LEVEL = 0x003A,
    MB_ACT_INPUT_ENCODER_RISE_LEFT_HIGH = 0x003B,
    MB_ACT_INPUT_ENCODER_RISE_RIGHT_HIGH = 0x003D,
    MB_ACT_INPUT_ENCODER_PRESS_RIGHT_HIGH = 0x003F,
    MB_ACT_INPUT_NTC2_HIGH = 0x0041,
    MB_ACT_INPUT_NTC3_HIGH = 0x0043,
    MB_ACT_INPUT_NTC_BACK_HIGH = 0x0045,
    MB_ACT_INPUT_WATER_FLOW_HIGH = 0x0047,
    MB_ACT_INPUT_BRINE_FLOW_HIGH = 0x0049,
    MB_ACT_INPUT_WINDING_A_ZERO = 0x004B,
    MB_ACT_INPUT_WINDING_B_ZERO = 0x004C,
    MB_ACT_INPUT_ADC_READY = 0x004D,
    MB_ACT_INPUT_ADC_SAMPLE_COUNT = 0x004E,
    MB_ACT_INPUT_DISCRETE_BITMAP_HIGH = 0x004F,
    MB_ACT_INPUT_COUNT = 0x0050,
} modbus_actuator_input_t;

/* Station 2 NTC snapshots preserve raw uint32 ADC values. Primary temperature
 * uses NTC2 (0x0041-0x0042), converted via function/ntcTemp and stored in
 * g_DataPool.temperature for getTemp() and the station 1 summary.
 * NTC1 (0x0000-0x0001), NTC3 and NTCBack remain available as raw values. */

typedef enum {
    MB_HOLD_WATER_PUMP_SPEED = 0,
    MB_HOLD_HOSE_PUMP_SPEED = 1,
    MB_HOLD_PRESS_MOTOR = 2,
    MB_HOLD_RISE_MOTOR = 3,
    /* 抬升电流档：0 = 低档（上位机手动点动、开机自检），非 0 = 高档
     * （压制流程全程抬升带载）。幅值数值写在下位机 moter42.c，主站只选档；
     * 写入立即生效，下位机粘滞保存直到再次写入。 */
    MB_HOLD_RISE_CURRENT_GEAR = 4,
    /* 0x0005 起为参数/预留区，站号2 下位机未定义为业务寄存器，主站不使用。
     * 抬升“下降一段”不占用独立寄存器，走 MB_HOLD_RISE_MOTOR 魔数触发。 */
} modbus_actuator_holding_t;

typedef enum {
    MB_POWER_HOLD_ENABLE = 0,
    MB_POWER_HOLD_RAM_1 = 1,
    MB_POWER_HOLD_HEATER_1 = 2,
    MB_POWER_HOLD_HEATER_2 = 3,
    MB_POWER_HOLD_SIGNED_MOTOR_GEAR = 4,
    MB_POWER_HOLD_CYCLE_THRESHOLD = 0x0031,
    MB_POWER_HOLD_CYCLE_TOTAL = 0x0032,
    MB_POWER_HOLD_CALIBRATION_TRIGGER = 0x003C,
    MB_POWER_HOLD_CALIBRATION_STATUS = 0x003D,
} modbus_power_holding_t;

typedef enum {
    MB_POWER_INPUT_MOTOR_RPM = 0x0000,
    MB_POWER_INPUT_MOTOR_STATE = 0x0001,
    MB_POWER_INPUT_PHASE_CURRENT_MA = 0x0002,
    MB_POWER_INPUT_ENABLE = 0x0003,
    MB_POWER_INPUT_HEATER_1_ENABLED = 0x0004,
    MB_POWER_INPUT_HEATER_2_ENABLED = 0x0005,
    MB_POWER_INPUT_HEATER_1_GEAR = 0x0006,
    MB_POWER_INPUT_HEATER_2_GEAR = 0x0007,
    MB_POWER_INPUT_SIGNED_MOTOR_GEAR = 0x0008,
    MB_POWER_INPUT_VERSION_HIGH = 0x000B,
    MB_POWER_INPUT_PC2_ADC_MEDIAN = 0x000E,
    MB_POWER_INPUT_METER_VOLTAGE_HIGH = 0x000F,
    MB_POWER_INPUT_METER_CURRENT_HIGH = 0x0011,
    MB_POWER_INPUT_METER_ACTIVE_POWER_HIGH = 0x0013,
    MB_POWER_INPUT_METER_SAMPLE_VALID = 0x0015,
    MB_POWER_INPUT_METER_FREQUENCY_MILLIHZ_HIGH = 0x0016,
    MB_POWER_INPUT_COUNT = 0x0018,
} modbus_power_input_t;

/* Press/rise use one signed 16-bit speed register:
 * positive = up, negative = down, zero = stop. */
#define MODBUS_DIRECTION_DOWN              0U
#define MODBUS_DIRECTION_UP                1U
#define MODBUS_SIGNED_SPEED_MAX            32767U
/* 抬升“下降一段”触发值：向保持寄存器 0x0003（MB_HOLD_RISE_MOTOR）写入本值，
 * 下位机按内部固定段长度（编码器计数）下降后零电流自锁。段长度写在下位机，
 * 主站不传距离、不读状态。与普通语义区分：正=Up、0=Stop、其它负值=Down。 */
#define MODBUS_RISE_MOTOR_TIMED_DOWN_HOLD  ((uint16_t)(-1000))

/* 抬升"上行一段"触发值：向保持寄存器 0x0003（MB_HOLD_RISE_MOTOR）写入本值，
 * 下位机从最低位向上走内部固定段长度（编码器计数）后零电流自锁，供压制流程
 * 排水定位使用（不再抬到最高位，不依赖高位限位开关）。
 * 段长度写在下位机，主站不传距离、不读状态。
 * -601 与 -1000 同属"魔数"：抬升转速合法区间为 ±600，超出即不可能是真速度。 */
#define MODBUS_RISE_MOTOR_TIMED_UP_HOLD    ((uint16_t)(-601))
/* 抬升电流档取值（保持寄存器 0x0004）：普通上/下行动作用低档，
 * 压制流程全程带载用高档。 */
#define MODBUS_RISE_CURRENT_GEAR_NORMAL    0U
#define MODBUS_RISE_CURRENT_GEAR_LOADED    1U
#define MODBUS_WATER_PUMP_SPEED_MAX        1000U
#define MODBUS_HOSE_PUMP_SPEED_MAX         255U
#define MODBUS_POWER_HEATER_GEAR_MAX       30U
#define MODBUS_POWER_MOTOR_GEAR_MAX        15U
#define MB_POWER_CACHE_CYCLE_CONFIG_COUNT  2U
#define MB_POWER_CACHE_COUNT (MB_POWER_INPUT_COUNT + MB_POWER_CACHE_CYCLE_CONFIG_COUNT)

static inline uint16_t modbus_encode_signed_speed(uint16_t speed,
                                                   uint8_t direction)
{
    int16_t signed_speed;
    if (speed > MODBUS_SIGNED_SPEED_MAX) {
        speed = MODBUS_SIGNED_SPEED_MAX;
    }
    signed_speed = direction == MODBUS_DIRECTION_UP ?
                   (int16_t)speed : (int16_t)-(int32_t)speed;
    return (uint16_t)signed_speed;
}

/* ---- Default Modbus acquisition test mode (compile-time) --------------------
 * Controls whether DataCenterTask enters the acquisition test loop
 * automatically on boot, instead of normal business polling.
 *
 *   0 = disabled (default; normal business polling)
 *   1 = normal pressure test   (business timeouts, MODBUS_TEST_DEFAULT_SLAVE)
 *   2 = violent pressure test  (short 10 ms / 5 ms timeouts for fast failure)
 *
 * The slave is selected by MODBUS_TEST_DEFAULT_SLAVE. Use this only for
 * bench / stress testing; production firmware must keep it at 0.
 *
 * NOTE: The actual #define values for MODBUS_TEST_DEFAULT_MODE,
 * MODBUS_TEST_DEFAULT_SLAVE and MODBUS_TEST_DEFAULT_START_DELAY_MS live in
 * main.c (project configuration entry). Only the mode enum constants below
 * are kept here so both main.c and DataCenter.c share the same encoding.
 */
#define MODBUS_TEST_DEFAULT_MODE_DISABLED  0U
#define MODBUS_TEST_DEFAULT_MODE_NORMAL    1U
#define MODBUS_TEST_DEFAULT_MODE_VIOLENT   2U

/* Default boot-to-test delay (ms) when no override is provided. */
#define MODBUS_TEST_DEFAULT_START_DELAY_MS 10000U

#endif
