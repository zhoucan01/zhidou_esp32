#include "DebugConsole.h"
#include "usrdug.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "DataCenter.h"
#include "FunctionSelection.h"
#include "ModbusMap.h"
#include "display_stats.h"
#include "myLvgl.h"
#include "esp_heap_caps.h"
#include "driver/uart.h"
#include "esp_system.h"  /* esp_restart() */

#define DEBUG_UART UART_NUM_0
#define MODBUS_FRAME_CAPACITY CONTROL_MODBUS_FRAME_CAPACITY
#define LOCAL_SLAVE_ADDRESS 0x01U
#define LOCAL_HOLDING_COUNT MB_LOCAL_HOLDING_COUNT
#define LOCAL_INPUT_COUNT MB_LOCAL_INPUT_COUNT
#define LOCAL_FLOW_COMMAND_ADDRESS 0x0100U
#define LOCAL_ACTUATOR_CACHE_BASE 0x0100U
#define LOCAL_ACTUATOR_CACHE_COUNT 80U
#define LOCAL_POWER_CACHE_BASE 0x0200U
#define LOCAL_POWER_CACHE_COUNT MB_POWER_CACHE_COUNT
#define LOCAL_GATEWAY_PROTOCOL_VERSION MODBUS_GATEWAY_PROTOCOL_VERSION
#define HEARTBEAT_INTERVAL_MS 10000U

static uint16_t local_holding[LOCAL_HOLDING_COUNT];
static uint16_t local_flow_command;
/* A request is copied into DataCenter's queue transaction storage before this
 * buffer is reused for the response. No full ASCII line buffer is needed. */
static uint8_t debug_frame[MODBUS_FRAME_CAPACITY];
static uint8_t cache_fail_station;  /* 0=none, 2 or 3 when cache read fails */

extern EventGroupHandle_t FuncTypeGroup;
extern EventGroupHandle_t FuncOrderGroup;
extern uint8_t BluetoothMac[6];

static uint16_t modbus_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xffffU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8U; ++bit) {
            crc = (crc & 1U) != 0U ? (crc >> 1) ^ 0xa001U : crc >> 1;
        }
    }
    return crc;
}

static bool frame_crc_valid(const uint8_t *frame, size_t length)
{
    if (length < 4U) {
        return false;
    }
    uint16_t received = (uint16_t)frame[length - 2U] |
                        ((uint16_t)frame[length - 1U] << 8);
    return received == modbus_crc16(frame, length - 2U);
}

static size_t append_crc(uint8_t *frame, size_t payload_length)
{
    uint16_t crc = modbus_crc16(frame, payload_length);
    frame[payload_length] = (uint8_t)crc;
    frame[payload_length + 1U] = (uint8_t)(crc >> 8);
    return payload_length + 2U;
}

static int hex_nibble(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static void print_frame(const uint8_t *frame, size_t length)
{
    /* 仅由 DebugConsoleTask 调用；先组帧，静态缓冲区避免增加任务栈占用。
     * 单次 fwrite 持有与 ESP_LOG 共用的 stdout 流锁，防止日志插入帧内。 */
    static char text[sizeof("MODBUS-RX:[]\r\n") + MODBUS_FRAME_CAPACITY * 3U];
    static const char hex[] = "0123456789ABCDEF";
    if (length > MODBUS_FRAME_CAPACITY) {
        return;
    }
    size_t used = sizeof("MODBUS-RX:[") - 1U;
    memcpy(text, "MODBUS-RX:[", used);
    for (size_t i = 0; i < length; ++i) {
        if (i != 0U) text[used++] = ' ';
        text[used++] = hex[frame[i] >> 4];
        text[used++] = hex[frame[i] & 0x0FU];
    }
    memcpy(text + used, "]\r\n", 3U);
    used += 3U;
    fwrite(text, 1, used, stdout);
}

static void float_to_registers(float value, uint16_t *high, uint16_t *low)
{
    uint32_t raw;
    memcpy(&raw, &value, sizeof(raw));
    *high = (uint16_t)(raw >> 16);
    *low = (uint16_t)raw;
}

static void build_local_inputs(uint16_t registers[LOCAL_INPUT_COUNT])
{
    memset(registers, 0, sizeof(uint16_t) * LOCAL_INPUT_COUNT);
    registers[MB_LOCAL_INPUT_VERSION_HIGH] =
        ((uint16_t)(uint8_t)version[0][0] << 8) | (uint8_t)version[0][1];
    registers[MB_LOCAL_INPUT_VERSION_LOW] =
        ((uint16_t)(uint8_t)version[0][2] << 8) | (uint8_t)version[0][3];
    registers[2] = LOCAL_SLAVE_ADDRESS;
    registers[3] = LOCAL_GATEWAY_PROTOCOL_VERSION;
    registers[4] = g_DataPool.statusWord;
    registers[5] = g_DataPool.faultCode;
    registers[6] = g_DataPool.powerMotorRpm;
    registers[7] = g_DataPool.powerMotorState;
    registers[8] = (g_DataPool.powerHeater1Enabled ? 1U : 0U) |
                   (g_DataPool.powerHeater2Enabled ? 2U : 0U) |
                   (g_DataPool.powerEnabled ? 4U : 0U);
    for (unsigned i = 0; i < sensorNum; ++i) {
        registers[9U + i / 16U] |= (uint16_t)sensorState[i] << (i % 16U);
    }
    for (unsigned i = 0; i < moterNum; ++i) {
        registers[11] |= (uint16_t)moterState[i] << i;
    }
    float_to_registers(g_DataPool.temperature, &registers[12], &registers[13]);
    float_to_registers(g_DataPool.voltage, &registers[14], &registers[15]);
    float_to_registers(g_DataPool.current, &registers[16], &registers[17]);
    FuncSecType_t current_flow = GetCurTaskState();
    registers[18] = current_flow < NOTASK_SEC ?
                    (uint16_t)(0x51U + current_flow) : 0U;
    registers[19] = current_flow < NOTASK_SEC ?
                    (uint16_t)(1U << GetCurTaskOrder()) : 0U;

    display_stats_t display;
    display_stats_get(&display);
    const uint32_t diagnostics[] = {
        (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        display.frames,
        display.average_us,
        display.maximum_us,
    };
    const uint16_t addresses[] = {
        MB_LOCAL_INPUT_PSRAM_FREE_HIGH,
        MB_LOCAL_INPUT_INTERNAL_FREE_HIGH,
        MB_LOCAL_INPUT_DISPLAY_FRAMES_HIGH,
        MB_LOCAL_INPUT_DISPLAY_AVERAGE_US_HIGH,
        MB_LOCAL_INPUT_DISPLAY_MAXIMUM_US_HIGH,
    };
    for (unsigned i = 0; i < sizeof(diagnostics) / sizeof(diagnostics[0]); ++i) {
        registers[addresses[i]] = (uint16_t)(diagnostics[i] >> 16);
        registers[addresses[i] + 1U] = (uint16_t)diagnostics[i];
    }
    for (unsigned i = 0; i < MB_LOCAL_INPUT_BLUETOOTH_MAC_COUNT; ++i) {
        unsigned offset = i * 2U;
        registers[MB_LOCAL_INPUT_BLUETOOTH_MAC_0 + i] =
            ((uint16_t)BluetoothMac[offset] << 8) |
            BluetoothMac[offset + 1U];
    }
    registers[MB_LOCAL_INPUT_POWER_MOTOR_DIRECTION_MODE] =
        DataCenterGetPowerMotorDirectionMode();
}

static bool local_holding_range_valid(uint16_t address, uint16_t quantity)
{
    return (uint32_t)address + quantity <= LOCAL_HOLDING_COUNT ||
           (address == LOCAL_FLOW_COMMAND_ADDRESS && quantity == 1U);
}

static bool submit_flow_control(const uint8_t command[2])
{
    if (command[0] < 0x51U || command[0] > 0x56U ||
        (command[1] != FuncStart_bit && command[1] != FuncPause_bit &&
         command[1] != FuncResume_bit && command[1] != FuncStop_bit) ||
        FuncTypeGroup == NULL || FuncOrderGroup == NULL) {
        return false;
    }
    xEventGroupSetBits(FuncTypeGroup, BIT(command[0] - 0x51U));
    xEventGroupSetBits(FuncOrderGroup, command[1]);
    return true;
}

static bool flow_control_command_valid(const uint8_t command[2])
{
    return command[0] >= 0x51U && command[0] <= 0x56U &&
           (command[1] == FuncStart_bit || command[1] == FuncPause_bit ||
            command[1] == FuncResume_bit || command[1] == FuncStop_bit);
}

static uint16_t read_local_holding(uint16_t address)
{
    if (address == LOCAL_FLOW_COMMAND_ADDRESS) return local_flow_command;
    if (address == MB_LOCAL_HOLDING_DISPLAY_FPS) return DisplayFrameRateGet();
    if (address == MB_LOCAL_HOLDING_POWER_MOTOR_DIRECTION_MODE)
        return DataCenterGetPowerMotorDirectionMode();
    return local_holding[address];
}

static bool local_holding_value_valid(uint16_t address, uint16_t value)
{
    if (address == MB_LOCAL_HOLDING_DISPLAY_FPS)
        return value >= MODBUS_DISPLAY_FPS_MIN && value <= MODBUS_DISPLAY_FPS_MAX;
    if (address == MB_LOCAL_HOLDING_POWER_MOTOR_DIRECTION_MODE)
        /* 本机所有无刷电机只允许正转：该寄存器只接受 1，写 0/2 一律拒绝。 */
        return value == MODBUS_POWER_MOTOR_DIRECTION_FORWARD_ONLY;
    return true;
}

static void write_local_holding(uint16_t address, uint16_t value)
{
    if (address == MB_LOCAL_HOLDING_DISPLAY_FPS) {
        (void)DisplayFrameRateSet(value);
    } else if (address == MB_LOCAL_HOLDING_POWER_MOTOR_DIRECTION_MODE) {
        /* 二次保护：即使上层漏了校验，也不允许把方向解锁成反转。 */
        if (value == MODBUS_POWER_MOTOR_DIRECTION_FORWARD_ONLY) {
            (void)DataCenterSetPowerMotorDirectionMode((uint8_t)value);
        }
    } else {
        local_holding[address] = value;
    }
}

static bool local_input_range_valid(uint16_t address, uint16_t quantity)
{
    uint32_t end = (uint32_t)address + quantity;
    return end <= LOCAL_INPUT_COUNT ||
           (address >= LOCAL_ACTUATOR_CACHE_BASE &&
            end <= LOCAL_ACTUATOR_CACHE_BASE + LOCAL_ACTUATOR_CACHE_COUNT) ||
           (address >= LOCAL_POWER_CACHE_BASE &&
            end <= LOCAL_POWER_CACHE_BASE + LOCAL_POWER_CACHE_COUNT);
}

static bool read_local_inputs(uint16_t address, uint16_t quantity,
                              uint16_t *registers)
{
    if (address < LOCAL_INPUT_COUNT) {
        uint16_t inputs[LOCAL_INPUT_COUNT];
        build_local_inputs(inputs);
        memcpy(registers, inputs + address, quantity * sizeof(*registers));
        return true;
    }
    if (address >= LOCAL_ACTUATOR_CACHE_BASE &&
        (uint32_t)address + quantity <=
            LOCAL_ACTUATOR_CACHE_BASE + LOCAL_ACTUATOR_CACHE_COUNT) {
        bool ok = ModbusCopyCachedInputRegisters(
            0x02U, address - LOCAL_ACTUATOR_CACHE_BASE, quantity, registers);
        if (!ok) cache_fail_station = 0x02U;
        return ok;
    }
    if (address >= LOCAL_POWER_CACHE_BASE &&
        (uint32_t)address + quantity <=
            LOCAL_POWER_CACHE_BASE + LOCAL_POWER_CACHE_COUNT) {
        bool ok = ModbusCopyCachedInputRegisters(
            0x03U, address - LOCAL_POWER_CACHE_BASE, quantity, registers);
        if (!ok) cache_fail_station = 0x03U;
        return ok;
    }
    return false;
}

static size_t local_exception(uint8_t function, uint8_t code,
                              uint8_t response[MODBUS_FRAME_CAPACITY])
{
    response[0] = LOCAL_SLAVE_ADDRESS;
    response[1] = function | 0x80U;
    response[2] = code;
    return append_crc(response, 3U);
}

static size_t handle_local_frame(const uint8_t *request, size_t request_length,
                                 uint8_t response[MODBUS_FRAME_CAPACITY])
{
    uint8_t function = request[1];
    if (request_length < 8U) return local_exception(function, 0x03, response);
    uint16_t address = ((uint16_t)request[2] << 8) | request[3];
    uint16_t quantity = ((uint16_t)request[4] << 8) | request[5];

    if (function == 0x03U || function == 0x04U) {
        if (request_length != 8U || quantity == 0U || quantity > 125U)
            return local_exception(function, 0x03, response);
        if ((function == 0x03U &&
             !local_holding_range_valid(address, quantity)) ||
            (function == 0x04U &&
             !local_input_range_valid(address, quantity)))
            return local_exception(function, 0x02, response);
        uint16_t inputs[125U];
        if (function == 0x04U &&
            !read_local_inputs(address, quantity, inputs))
            return local_exception(function, 0x04, response);
        response[0] = LOCAL_SLAVE_ADDRESS;
        response[1] = function;
        response[2] = (uint8_t)(quantity * 2U);
        for (uint16_t i = 0; i < quantity; ++i) {
            uint16_t value = function == 0x03U ?
                             read_local_holding(address + i) :
                             inputs[i];
            response[3U + i * 2U] = (uint8_t)(value >> 8);
            response[4U + i * 2U] = (uint8_t)value;
        }
        return append_crc(response, 3U + quantity * 2U);
    }

    if (function == 0x06U) {
        if (request_length != 8U) return local_exception(function, 0x03, response);
        if (!local_holding_range_valid(address, 1U))
            return local_exception(function, 0x02, response);
        if (address == LOCAL_FLOW_COMMAND_ADDRESS) {
            uint8_t command[2] = {
                (uint8_t)(quantity >> 8),
                (uint8_t)quantity,
            };
            if (!flow_control_command_valid(command))
                return local_exception(function, 0x03, response);
            if (!submit_flow_control(command))
                return local_exception(function, 0x06, response);
            local_flow_command = quantity;
        } else {
            if (!local_holding_value_valid(address, quantity))
                return local_exception(function, 0x03, response);
            write_local_holding(address, quantity);
        }
        memcpy(response, request, request_length);
        return request_length;
    }

    if (function == 0x10U) {
        if (request_length < 9U || quantity == 0U || quantity > 123U ||
            request[6] != quantity * 2U ||
            request_length != (size_t)request[6] + 9U)
            return local_exception(function, 0x03, response);
        if ((uint32_t)address + quantity > LOCAL_HOLDING_COUNT)
            return local_exception(function, 0x02, response);
        for (uint16_t i = 0; i < quantity; ++i) {
            uint16_t value = ((uint16_t)request[7U + i * 2U] << 8) |
                             request[8U + i * 2U];
            if (!local_holding_value_valid(address + i, value))
                return local_exception(function, 0x03, response);
        }
        for (uint16_t i = 0; i < quantity; ++i) {
            write_local_holding(address + i,
                ((uint16_t)request[7U + i * 2U] << 8) |
                request[8U + i * 2U]);
        }
        memcpy(response, request, 6U);
        return append_crc(response, 6U);
    }

    return local_exception(function, 0x01, response);
}

control_modbus_result_t ControlModbusProcess(
    const uint8_t *request, size_t request_length,
    uint8_t response[CONTROL_MODBUS_FRAME_CAPACITY], size_t *response_length)
{
    if (request == NULL || response == NULL || response_length == NULL ||
        request_length > CONTROL_MODBUS_FRAME_CAPACITY ||
        !frame_crc_valid(request, request_length))
        return CONTROL_MODBUS_BAD_CRC;

    if (request[0] == LOCAL_SLAVE_ADDRESS) {
        *response_length = handle_local_frame(request, request_length, response);
    } else if (request[0] == 0x02U || request[0] == 0x03U) {
        if (!ModbusGatewayTransaction(request, request_length, response,
                                      CONTROL_MODBUS_FRAME_CAPACITY,
                                      response_length))
            return CONTROL_MODBUS_TIMEOUT;
        if (!frame_crc_valid(response, *response_length))
            return CONTROL_MODBUS_BAD_RESPONSE_CRC;
    } else {
        return CONTROL_MODBUS_BAD_SLAVE;
    }
    return CONTROL_MODBUS_OK;
}

static void handle_frame(size_t request_length)
{
    cache_fail_station = 0U;
    uint8_t response[MODBUS_FRAME_CAPACITY];
    size_t response_length = 0;
    control_modbus_result_t result = ControlModbusProcess(
        debug_frame, request_length, response, &response_length);
    if (result == CONTROL_MODBUS_OK) {
        print_frame(response, response_length);
        if (response_length >= 3U && (response[1] & 0x80U) != 0U &&
            response[2] == 0x04U && cache_fail_station != 0U) {
            printf("CACHE:STATION=%02u NOT_READY\r\n", cache_fail_station);
            cache_fail_station = 0U;
        }
    } else {
        static const char *errors[] = {
            "", "ERROR:CRC\r\n", "ERROR:SLAVE\r\n",
            "ERROR:TIMEOUT\r\n", "ERROR:RESPONSE_CRC\r\n",
        };
        printf("%s", errors[result]);
    }
}

static void handle_test_command(const char *line)
{
    /* Supported formats (case-insensitive, after "TEST:"):
     *   START 02 [VIOLENT] | START 03 [VIOLENT]
     *   START SLAVE=02 [VIOLENT] | START SLAVE=03 [VIOLENT]
     *   02 [VIOLENT] | 03 [VIOLENT]   (short form of START)
     *   STOP | END
     *   STATS
     *   RESET | REBOOT            soft-reset the ESP32 via esp_restart()
     * Spaces and '=' are skipped when extracting the slave number.
     * The optional VIOLENT keyword (anywhere after the slave number)
     * enables short read/byte timeouts so a dead slave fails fast. */
    const char *p = line + 5U;  /* skip "TEST:" */
    while (*p == ' ' || *p == '\t') ++p;

    /* Extract first token (command or slave digits). */
    char token[12];
    size_t tlen = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' &&
           tlen < sizeof(token) - 1U) {
        token[tlen++] = toupper((unsigned char)*p);
        ++p;
    }
    token[tlen] = '\0';

    /* Skip "SLAVE=" prefix if present. */
    if (strcmp(token, "SLAVE") == 0) {
        while (*p == '=' || *p == ' ' || *p == '\t') ++p;
        tlen = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' &&
               tlen < sizeof(token) - 1U) {
            token[tlen++] = *p;
            ++p;
        }
        token[tlen] = '\0';
    }

    /* Scan the remaining tail for the VIOLENT keyword. */
    bool aggressive = false;
    {
        char tail[24];
        size_t tail_len = 0;
        while (*p && tail_len < sizeof(tail) - 1U) {
            if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
                tail[tail_len++] = toupper((unsigned char)*p);
            }
            ++p;
        }
        tail[tail_len] = '\0';
        if (strstr(tail, "VIOLENT") != NULL) {
            aggressive = true;
        }
    }

    if (strcmp(token, "START") == 0) {
        /* Extract slave number after START. */
        while (*p == ' ' || *p == '\t' || *p == '=') ++p;
        tlen = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' &&
               tlen < sizeof(token) - 1U) {
            token[tlen++] = *p;
            ++p;
        }
        token[tlen] = '\0';
        /* Strip optional "SLAVE=" prefix. */
        char *eq = strchr(token, '=');
        if (eq != NULL) {
            memmove(token, eq + 1, strlen(eq + 1) + 1U);
        }
        /* Re-scan tail for VIOLENT after the slave number. */
        {
            char tail[24];
            size_t tail_len = 0;
            while (*p && tail_len < sizeof(tail) - 1U) {
                if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
                    tail[tail_len++] = toupper((unsigned char)*p);
                }
                ++p;
            }
            tail[tail_len] = '\0';
            if (strstr(tail, "VIOLENT") != NULL) {
                aggressive = true;
            }
        }
        if (strcmp(token, "02") == 0) {
            ModbusTestStart(MODBUS_SLAVE_ACTUATOR, aggressive);
            printf("TEST:START slave=02%s\r\n", aggressive ? " violent" : "");
        } else if (strcmp(token, "03") == 0) {
            ModbusTestStart(MODBUS_SLAVE_POWER, aggressive);
            printf("TEST:START slave=03%s\r\n", aggressive ? " violent" : "");
        } else {
            printf("ERROR:TEST_BAD_SLAVE\r\n");
        }
    } else if (strcmp(token, "STOP") == 0 || strcmp(token, "END") == 0) {
        ModbusTestStop();
        printf("TEST:STOP\r\n");
    } else if (strcmp(token, "STATS") == 0) {
        ModbusTestPrintStats();
    } else if (strcmp(token, "RESET") == 0 || strcmp(token, "REBOOT") == 0) {
        /* Soft-reset the ESP32. esp_restart() triggers a clean reboot:
         * Wi-Fi/BLE/NVS are not flushed by this call, so callers that
         * need persistence should flush before issuing RESET. The
         * confirmation line is sent before the reset so the operator
         * knows the command was accepted. */
        printf("TEST:RESET rebooting now\r\n");
        uart_wait_tx_done(DEBUG_UART, pdMS_TO_TICKS(100));
        esp_restart();
    } else if (strcmp(token, "02") == 0) {
        ModbusTestStart(MODBUS_SLAVE_ACTUATOR, aggressive);
        printf("TEST:START slave=02%s\r\n", aggressive ? " violent" : "");
    } else if (strcmp(token, "03") == 0) {
        ModbusTestStart(MODBUS_SLAVE_POWER, aggressive);
        printf("TEST:START slave=03%s\r\n", aggressive ? " violent" : "");
    } else {
        printf("ERROR:UNKNOWN_TEST_COMMAND\r\n");
    }
}

void DebugConsoleTask(void *pvParameters)
{
    (void)pvParameters;
    static const char prefix[] = "MODBUS-TX:[";
    static const char test_prefix[] = "TEST:";
    size_t prefix_index = 0;
    size_t frame_length = 0;
    int high_nibble = -1;
    bool in_frame = false;
    char line_buf[48];
    size_t line_len = 0;
    TickType_t next_heartbeat;
    if (!uart_is_driver_installed(DEBUG_UART) &&
        uart_driver_install(DEBUG_UART, 1024, 0, 0, NULL, 0) != ESP_OK) {
        printf("ERROR:UART_INIT\r\n");
        vTaskDelete(NULL);
        return;
    }

    if (!usrdug_init()) printf("ERROR:NB_INIT\r\n");
    printf("Modbus gateway ready. NB+SOYMODE=1..7,0|1; "
           "NB+VALVE=OPEN|CLOSE; "
           "NB+HOSEPUMP=FORWARD|REVERSE|STOP\r\n");
    next_heartbeat = xTaskGetTickCount() +
                     pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS);
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        if ((int32_t)(now - next_heartbeat) >= 0) {
            char heartbeat[40];
            int length = snprintf(heartbeat, sizeof(heartbeat),
                                  "HEARTBEAT:[%lu]\r\n",
                                  (unsigned long)(now * portTICK_PERIOD_MS));
            if (length > 0) {
                uart_write_bytes(DEBUG_UART, heartbeat, (size_t)length);
            }
            next_heartbeat = now + pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS);
        }

        uint8_t byte;
        if (uart_read_bytes(DEBUG_UART, &byte, 1, pdMS_TO_TICKS(100)) != 1) {
            /* No byte received within 100 ms: if we have a partial TEST:
             * line buffered, dispatch it now so commands without a trailing
             * newline still work. */
            if (line_len > 0U) {
                line_buf[line_len] = '\0';
                if (strncmp(line_buf, test_prefix,
                            sizeof(test_prefix) - 1U) == 0) {
                    handle_test_command(line_buf);
                }
                usrdug_command(line_buf);
                line_len = 0;
            }
            continue;
        }

        if (!in_frame) {
            /* MODBUS-TX prefix matching has priority over TEST: command
             * collection. The two state machines share the same byte
             * stream, but 'T' (TEST: prefix[0]) also appears inside the
             * "MODBUS-TX:" prefix at position 7. If TEST: collection were
             * allowed to start while MODBUS-TX matching is in progress,
             * the 'T' would be swallowed into line_buf and the MODBUS-TX
             * matcher would reset, so the frame would never be captured.
             * Therefore: feed the byte to the MODBUS-TX matcher first; if
             * it advances or restarts the match, continue immediately.
             * Only when the matcher is fully idle (prefix_index == 0 and
             * the byte is not 'M') do we collect TEST: input. */
            if (line_len == 0U &&
                toupper((unsigned char)byte) == prefix[prefix_index]) {
                if (++prefix_index == sizeof(prefix) - 1U) {
                    in_frame = true;
                    prefix_index = 0;
                    frame_length = 0;
                    high_nibble = -1;
                    line_len = 0;
                }
                continue;
            }
            if (prefix_index > 0U) {
                /* Partial MODBUS-TX match broken by this byte. Restart
                 * matching from byte 0 if this byte is 'M', otherwise
                 * drop to idle so TEST: collection can resume. */
                prefix_index = toupper((unsigned char)byte) == prefix[0] ?
                               1U : 0U;
                continue;
            }
            /* MODBUS-TX matcher is idle (prefix_index == 0) and byte is
             * not 'M': collect TEST: commands. Dispatch on newline,
             * buffer-full, or 100 ms idle gap (handled above). */
            if (line_len > 0U || toupper((unsigned char)byte) == test_prefix[0] ||
                toupper((unsigned char)byte) == 'N') {
                if (line_len < sizeof(line_buf) - 1U) {
                    line_buf[line_len++] = (char)toupper((unsigned char)byte);
                }
                if (byte == '\r' || byte == '\n' || byte == '\0' ||
                    line_len == sizeof(line_buf) - 1U) {
                    line_buf[line_len] = '\0';
                    if (strncmp(line_buf, test_prefix,
                                sizeof(test_prefix) - 1U) == 0) {
                        handle_test_command(line_buf);
                    }
                    usrdug_command(line_buf);
                    line_len = 0;
                }
            }
            continue;
        }

        if (byte == ']') {
            if (high_nibble < 0 && frame_length >= 4U) {
                handle_frame(frame_length);
            } else {
                printf("ERROR:FORMAT\r\n");
            }
            in_frame = false;
            high_nibble = -1;
            frame_length = 0;
            continue;
        }
        if (isspace((unsigned char)byte)) {
            continue;
        }

        int nibble = hex_nibble((char)byte);
        if (nibble < 0) {
            printf("ERROR:FORMAT\r\n");
            in_frame = false;
            high_nibble = -1;
            frame_length = 0;
        } else if (high_nibble < 0) {
            high_nibble = nibble;
        } else if (frame_length < sizeof(debug_frame)) {
            debug_frame[frame_length++] =
                (uint8_t)((high_nibble << 4) | nibble);
            high_nibble = -1;
        } else {
            printf("ERROR:FRAME_TOO_LONG\r\n");
            in_frame = false;
            high_nibble = -1;
            frame_length = 0;
        }
    }
}
