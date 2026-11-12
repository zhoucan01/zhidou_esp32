#include "DataCenter.h"

#include <math.h>
#include <string.h>

#include "ModbusMap.h"
#include "HeatingCommand.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_rom_sys.h"   /* ets_delay_us */
#include "esp_timer.h"     /* esp_timer_get_time */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nanomodbus.h"
#include "sys.h"
#include "usart.h"
#include "ntcTemp.h"

#define COMMAND_QUEUE_CAPACITY 32U
#define MODBUS_GATEWAY_FRAME_MAX 256U
#define MODBUS_GATEWAY_RESPONSE_TIMEOUT_MS 500U
#define MODBUS_GATEWAY_GAP_TIMEOUT_MS 30U

/* Keep a 20-sample window. After sorting high to low, average ranks 4..7
 * (one-based) to reject isolated spikes while retaining a responsive value. */
#define NTC_TEMPERATURE_MEDIAN_WINDOW 20U
#define NTC_ADC_MAX_STEP 200U
#define NTC_MOTOR_STOP_SETTLE_US 200000LL
#define NTC_MOTOR_STOP_SETTLE_SAMPLES 5U

/* Three qualifying one-second samples in the trailing five-second window.
 * Use fresh filtered temperatures only; never backfill missed seconds. */
#define NTC_TEMP_OVER98_THRESHOLD_C 98.0f
#define NTC_TEMP_OVER98_WINDOW_US 5000000LL
#define NTC_TEMP_OVER98_SAMPLE_US 1000000LL
#define NTC_TEMP_OVER98_REQUIRED 3U

static const char *TAG = "DataCenter";

typedef struct {
    uart_port_t uart;
} modbus_transport_t;

static modbus_transport_t modbus_transport = {
    .uart = ScreenUart,
};
static bool power_calibration_response_trace;

char version[3][4] = {
    {ESP32S3_VERSION_MAJOR, ESP32S3_VERSION_MINOR,
     ESP32S3_VERSION_PATCH, ESP32S3_VERSION_BUILD}, /* ESP32-S3 */
    {0, 0, 0, 1}, /* slave 0x02 */
    {0, 0, 0, 1}, /* slave 0x03 */
};

bool sensorState[sensorNum];
bool moterState[moterNum];
struct DataPool g_DataPool = {0};
modbus_test_stats_t g_ModbusTestStats = {0};
static uint8_t power_motor_direction_mode;
static portMUX_TYPE power_motor_direction_lock = portMUX_INITIALIZER_UNLOCKED;

/* Pending default test schedule, set by DataCenterScheduleDefaultTest()
 * from main.c. DataCenterTask consumes it once after startup. */
static struct {
    bool pending;
    bool aggressive;
    uint8_t slave;
    uint32_t delay_ms;
} g_default_test_schedule = { .pending = false };

static uint16_t actuator_input_cache[MB_ACT_INPUT_COUNT];
static uint16_t power_input_cache[MB_POWER_CACHE_COUNT];
static bool actuator_input_cache_valid;
static int64_t actuator_input_cache_time_us;
static bool power_input_cache_valid;
static portMUX_TYPE input_cache_lock = portMUX_INITIALIZER_UNLOCKED;
/* Increment only after a fresh, successful station-3 calibration-status read. */
static uint32_t power_calibration_status_generation;
#if NTC_TEMP_MOTOR_STOP_OPTIMIZATION_ENABLE
static int64_t ntc_motor_stop_command_us;
static uint8_t ntc_motor_stop_settle_samples_remaining;
#endif

typedef enum {
    MB_COMMAND_WRITE_COIL,
    MB_COMMAND_WRITE_REGISTER,
    MB_COMMAND_WRITE_REGISTER_AND_COIL,
    MB_COMMAND_WRITE_POWER_CHANNEL,
    MB_COMMAND_RECOVER_POWER_CHANNEL,
    MB_COMMAND_ENABLE_POWER_FOR_CALIBRATION,
    MB_COMMAND_START_POWER_CALIBRATION,
    MB_COMMAND_RAW_GATEWAY,
    MB_COMMAND_POLL,
} modbus_command_type_t;

typedef struct {
    modbus_command_type_t type;
    uint8_t slave;
    uint16_t address;
    uint16_t value;
    uint16_t coil_address;
    bool reply_to_debug;
    bool raw_heating_request;
    bool guarded_heater_stop;
    uint32_t heating_sequence;
    uint32_t target_sequence;
} modbus_command_t;

typedef struct {
    modbus_command_t commands[COMMAND_QUEUE_CAPACITY];
    size_t read_index;
    size_t write_index;
    size_t count;
    portMUX_TYPE lock;
} command_ring_buffer_t;

static command_ring_buffer_t command_queue = {
    .lock = portMUX_INITIALIZER_UNLOCKED,
};
static uint32_t heating_command_sequence;

volatile uint16_t g_TargetHeater1Gear;
volatile uint16_t g_TargetHeater2Gear;
volatile int16_t g_TargetPowerMotorGear;

/* All target/recovery state is protected by command_queue.lock. */
#define POWER_RECOVERY_CHANNELS 3U
#define POWER_RECOVERY_LOW_SAMPLES 2U
#define POWER_RECOVERY_FEEDBACK_MAX_AGE_US 2000000LL
/* 功率校准：触发后多久没见到 1(校准中) 就重发；触发前清残留状态最多等多久 */
#define POWER_CAL_START_TIMEOUT_MS 5000U
#define POWER_CAL_CLEAR_TIMEOUT_MS 30000U
#define POWER_CAL_CLEAR_TOGGLE_MS  10000U
#define POWER_CAL_START_ATTEMPTS   3U
static uint32_t power_target_sequence[POWER_RECOVERY_CHANNELS];
static bool power_recovery_pending[POWER_RECOVERY_CHANNELS];
static unsigned power_low_samples[2];
static uint32_t power_low_sequence[2];
static struct {
    bool valid;
    bool meter_valid;
    bool enabled;
    uint16_t calibration_status;
    uint16_t rpm;
    uint32_t power_mw;
    int64_t received_us;
} power_recovery_feedback;

static int power_target_index(uint16_t address)
{
    if (address == MB_POWER_HOLD_HEATER_1) return 0;
    if (address == MB_POWER_HOLD_HEATER_2) return 1;
    if (address == MB_POWER_HOLD_SIGNED_MOTOR_GEAR) return 2;
    return -1;
}

static uint16_t power_target_value(unsigned index)
{
    if (index == 0U) return g_TargetHeater1Gear;
    if (index == 1U) return g_TargetHeater2Gear;
    return (uint16_t)g_TargetPowerMotorGear;
}

static void remember_power_target(uint16_t address, uint16_t value)
{
    if (address == MB_POWER_HOLD_ENABLE && value == 0U) {
        remember_power_target(MB_POWER_HOLD_HEATER_1, 0U);
        remember_power_target(MB_POWER_HOLD_HEATER_2, 0U);
        remember_power_target(MB_POWER_HOLD_SIGNED_MOTOR_GEAR, 0U);
        return;
    }
    const int index = power_target_index(address);
    if (index < 0) return;
    if (index < 2 && value > MODBUS_POWER_HEATER_GEAR_MAX) return;
    if (index == 2 && ((int16_t)value < -(int)MODBUS_POWER_MOTOR_GEAR_MAX ||
                       (int16_t)value > (int)MODBUS_POWER_MOTOR_GEAR_MAX)) return;
    if (power_target_value((unsigned)index) != value) {
        ++power_target_sequence[index];
        if (index < 2) power_low_samples[index] = 0U;
    }
    if (index == 0) g_TargetHeater1Gear = value;
    else if (index == 1) g_TargetHeater2Gear = value;
    else g_TargetPowerMotorGear = (int16_t)value;
}

/* Track valid FC06/FC10 gateway writes as well as the local API. Polls and
 * invalid frames must never become recovery targets. Caller holds queue lock. */
static void remember_power_frame(const uint8_t *frame, size_t size)
{
    if (size < 8U || frame[0] != MODBUS_SLAVE_POWER) return;
    uint16_t crc = 0xffffU;
    for (size_t i = 0U; i < size - 2U; ++i) {
        crc ^= frame[i];
        for (unsigned bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xa001U : 0U);
    }
    if (frame[size - 2U] != (uint8_t)crc ||
        frame[size - 1U] != (uint8_t)(crc >> 8)) return;
    uint16_t address = ((uint16_t)frame[2] << 8) | frame[3];
    uint16_t value = ((uint16_t)frame[4] << 8) | frame[5];
    if (frame[1] == 0x06 && size == 8U) {
        remember_power_target(address, value);
    } else if (frame[1] == 0x10 && size >= 9U && value > 0U &&
               value <= 123U && frame[6] == value * 2U &&
               size == (size_t)frame[6] + 9U &&
               (uint32_t)address + value <= MB_POWER_HOLD_SIGNED_MOTOR_GEAR + 1U) {
        for (uint16_t i = 0U; i < value; ++i) {
            remember_power_target(address + i,
                ((uint16_t)frame[7U + i * 2U] << 8) | frame[8U + i * 2U]);
        }
    }
}

typedef struct {
    bool busy;
    uint8_t request[MODBUS_GATEWAY_FRAME_MAX];
    size_t request_length;
    uint8_t response[MODBUS_GATEWAY_FRAME_MAX];
    size_t response_length;
    bool success;
    TaskHandle_t waiter;
    portMUX_TYPE lock;
} gateway_transaction_t;

static gateway_transaction_t gateway_transaction = {
    .lock = portMUX_INITIALIZER_UNLOCKED,
};

static bool command_queue_push(const modbus_command_t *command)
{
    bool queued = false;
    portENTER_CRITICAL(&command_queue.lock);
    if (command_queue.count < COMMAND_QUEUE_CAPACITY) {
        command_queue.commands[command_queue.write_index] = *command;
        command_queue.write_index =
            (command_queue.write_index + 1U) % COMMAND_QUEUE_CAPACITY;
        command_queue.count++;
        if (command->slave == MODBUS_SLAVE_POWER) {
            if (command->type == MB_COMMAND_WRITE_POWER_CHANNEL ||
                command->type == MB_COMMAND_WRITE_REGISTER) {
                remember_power_target(command->address, command->value);
            } else if (command->type == MB_COMMAND_RAW_GATEWAY) {
                remember_power_frame(gateway_transaction.request,
                                     gateway_transaction.request_length);
            }
        }
        if (command->slave == MODBUS_SLAVE_POWER &&
            (((command->type == MB_COMMAND_WRITE_POWER_CHANNEL ||
               command->type == MB_COMMAND_WRITE_REGISTER) &&
              is_heating_register(command->address) && command->value != 0) ||
             (command->type == MB_COMMAND_RAW_GATEWAY && command->raw_heating_request))) {
            ++heating_command_sequence;
        }
        queued = true;
    }
    portEXIT_CRITICAL(&command_queue.lock);

    if (!queued) {
        ESP_LOGW(TAG, "Modbus command queue full, slave=%u address=%u",
                 command->slave, command->address);
    }
    return queued;
}

static bool command_queue_pop(modbus_command_t *command)
{
    bool available = false;
    portENTER_CRITICAL(&command_queue.lock);
    if (command_queue.count > 0U) {
        *command = command_queue.commands[command_queue.read_index];
        command_queue.read_index =
            (command_queue.read_index + 1U) % COMMAND_QUEUE_CAPACITY;
        command_queue.count--;
        available = true;
    }
    portEXIT_CRITICAL(&command_queue.lock);
    return available;
}

static bool recovery_target_current(const modbus_command_t *command)
{
    const int index = power_target_index(command->address);
    bool current = false;
    portENTER_CRITICAL(&command_queue.lock);
    if (index >= 0) {
        current = power_target_sequence[index] == command->target_sequence &&
                  power_target_value((unsigned)index) == command->value &&
                  command->value != 0U;
    }
    portEXIT_CRITICAL(&command_queue.lock);
    return current;
}

/* Queue a single ordered 0 -> target transaction, without changing the saved
 * target or heating-command sequence. Caller holds command_queue.lock. */
static void queue_power_recovery(unsigned index, uint16_t address)
{
    if (power_recovery_pending[index] ||
        command_queue.count == COMMAND_QUEUE_CAPACITY) return;
    command_queue.commands[command_queue.write_index] = (modbus_command_t) {
        .type = MB_COMMAND_RECOVER_POWER_CHANNEL,
        .slave = MODBUS_SLAVE_POWER,
        .address = address,
        .value = power_target_value(index),
        .target_sequence = power_target_sequence[index],
    };
    command_queue.write_index =
        (command_queue.write_index + 1U) % COMMAND_QUEUE_CAPACITY;
    ++command_queue.count;
    power_recovery_pending[index] = true;
}

static void power_recovery_check(void)
{
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&command_queue.lock);
    const bool usable = power_recovery_feedback.valid &&
        now - power_recovery_feedback.received_us <=
            POWER_RECOVERY_FEEDBACK_MAX_AGE_US &&
        power_recovery_feedback.enabled &&
        power_recovery_feedback.calibration_status != 1U &&
        !g_ModbusTestStats.active;
    for (unsigned index = 0U; index < 2U; ++index) {
        if (power_low_sequence[index] != power_target_sequence[index]) {
            power_low_samples[index] = 0U;
            power_low_sequence[index] = power_target_sequence[index];
        }
        if (!usable || !power_recovery_feedback.meter_valid ||
            power_target_value(index) < 4U ||
            power_recovery_feedback.power_mw >= 200000U) {
            power_low_samples[index] = 0U;
            continue;
        }
        if (++power_low_samples[index] >= POWER_RECOVERY_LOW_SAMPLES) {
            queue_power_recovery(index, index == 0U ?
                MB_POWER_HOLD_HEATER_1 : MB_POWER_HOLD_HEATER_2);
            power_low_samples[index] = 0U;
        }
    }
    if (usable && g_TargetPowerMotorGear != 0 &&
        power_recovery_feedback.rpm == 0U) {
        queue_power_recovery(2U, MB_POWER_HOLD_SIGNED_MOTOR_GEAR);
    }
    portEXIT_CRITICAL(&command_queue.lock);
}

void PowerRecoveryTask(void *pvParameters)
{
    (void)pvParameters;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000));
        power_recovery_check();
    }
}

uint32_t DataCenterHeatingCommandSequence(void)
{
    portENTER_CRITICAL(&command_queue.lock);
    uint32_t sequence = heating_command_sequence;
    portEXIT_CRITICAL(&command_queue.lock);
    return sequence;
}

bool DataCenterStopHeatersIfUnchanged(uint32_t sequence)
{
    bool queued = false;
    portENTER_CRITICAL(&command_queue.lock);
    if (sequence == heating_command_sequence &&
        command_queue.count <= COMMAND_QUEUE_CAPACITY - 2) {
        for (uint16_t address = MB_POWER_HOLD_HEATER_1;
             address <= MB_POWER_HOLD_HEATER_2; ++address) {
            command_queue.commands[command_queue.write_index] = (modbus_command_t) {
                .type = MB_COMMAND_WRITE_POWER_CHANNEL,
                .slave = MODBUS_SLAVE_POWER,
                .address = address,
                .value = 0,
                .guarded_heater_stop = true,
                .heating_sequence = sequence,
            };
            command_queue.write_index =
                (command_queue.write_index + 1) % COMMAND_QUEUE_CAPACITY;
            ++command_queue.count;
            remember_power_target(address, 0U);
        }
        queued = true;
    }
    portEXIT_CRITICAL(&command_queue.lock);
    return queued;
}

static void queue_coil(uint8_t slave, uint16_t address, bool value)
{
    const modbus_command_t command = {
        .type = MB_COMMAND_WRITE_COIL,
        .slave = slave,
        .address = address,
        .value = value ? 1U : 0U,
        .reply_to_debug = false,
    };
    command_queue_push(&command);
}

static void queue_register(uint8_t slave, uint16_t address, uint16_t value)
{
    const modbus_command_t command = {
        .type = MB_COMMAND_WRITE_REGISTER,
        .slave = slave,
        .address = address,
        .value = value,
        .reply_to_debug = false,
    };
    command_queue_push(&command);
}

static void queue_signed_motor_speed(uint16_t speed_register,
                                     uint16_t speed, uint8_t direction)
{
    const modbus_command_t command = {
        .type = MB_COMMAND_WRITE_REGISTER,
        .slave = MODBUS_SLAVE_ACTUATOR,
        .address = speed_register,
        .value = modbus_encode_signed_speed(speed, direction),
        .reply_to_debug = false,
    };
    command_queue_push(&command);
}

static int32_t modbus_uart_read(uint8_t *buffer, uint16_t count,
                                int32_t timeout_ms, void *arg)
{
    modbus_transport_t *transport = (modbus_transport_t *)arg;
    /* DE is hardware-controlled in RS485 half-duplex mode; no software
     * toggle or settle delay is needed before reading. */
    TickType_t timeout = timeout_ms < 0 ? portMAX_DELAY :
                         pdMS_TO_TICKS((uint32_t)timeout_ms);
    const int32_t received = uart_read_bytes(transport->uart, buffer, count,
                                              timeout);
    if (power_calibration_response_trace &&
        transport->uart == PowerUart && received > 0) {
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, buffer, (size_t)received, ESP_LOG_WARN);
    }
    return received;
}

static int32_t modbus_uart_write(const uint8_t *buffer, uint16_t count,
                                 int32_t timeout_ms, void *arg)
{
    modbus_transport_t *transport = (modbus_transport_t *)arg;
    /* Wait for the bus to be idle for at least the Modbus t3.5 inter-frame
     * time before asserting DE and starting a new transaction. This complies
     * with the RTU framing spec and avoids colliding with a frame still on
     * the line (e.g. a late slave response or a previous broadcast). */
    uart_modbus_wait_bus_idle(transport->uart);
    /* DE is asserted HIGH by the UART hardware automatically when TX starts
     * and pulled LOW after the last stop bit leaves the TX pin. No software
     * toggle is needed. */
    int written = uart_write_bytes(transport->uart, buffer, count);
    if (written != count) {
        ESP_LOGE(TAG, "uart_write_bytes failed: uart=%d written=%d count=%d",
                 transport->uart, written, count);
        uart_modbus_reset_transport(transport->uart);
        return written;
    }

    TickType_t timeout = timeout_ms < 0 ? portMAX_DELAY :
                         pdMS_TO_TICKS((uint32_t)timeout_ms);
    esp_err_t tx_err = uart_wait_tx_done(transport->uart, timeout);
    if (tx_err != ESP_OK) {
        ESP_LOGE(TAG, "uart_wait_tx_done failed: uart=%d err=%s",
                 transport->uart, esp_err_to_name(tx_err));
        uart_modbus_reset_transport(transport->uart);
        return -1;
    }
    /* DE is released by hardware after the last stop bit; no settle delay
     * is needed before reading the slave response. */
    return written;
}

static void modbus_uart_flush(nmbs_t *nmbs, void *arg)
{
    (void)nmbs;
    modbus_transport_t *transport = (modbus_transport_t *)arg;
    uart_flush_input(transport->uart);
}

static nmbs_error modbus_client_init(nmbs_t *client)
{
    nmbs_platform_conf platform;
    nmbs_platform_conf_create(&platform);
    platform.transport = NMBS_TRANSPORT_RTU;
    platform.read = modbus_uart_read;
    platform.write = modbus_uart_write;
    platform.flush = modbus_uart_flush;
    platform.arg = &modbus_transport;

    nmbs_error error = nmbs_client_create(client, &platform);
    if (error == NMBS_ERROR_NONE) {
        nmbs_set_read_timeout(client, MODBUS_RESPONSE_TIMEOUT_MS);
        nmbs_set_byte_timeout(client, MODBUS_BYTE_TIMEOUT_MS);
    }
    return error;
}

static bool select_slave(nmbs_t *client, uint8_t slave)
{
    if (slave == MODBUS_SLAVE_ACTUATOR) {
        modbus_transport.uart = ScreenUart;
    } else if (slave == MODBUS_SLAVE_POWER) {
        modbus_transport.uart = PowerUart;
    } else {
        ESP_LOGE(TAG, "No UART mapped for slave %u", slave);
        return false;
    }
    nmbs_set_destination_rtu_address(client, slave);
    return true;
}

static bool execute_raw_gateway(void)
{
    uart_port_t uart = modbus_transport.uart;
    gateway_transaction.response_length = 0;
    uart_flush_input(uart);

    /* Wait for the bus to be idle for at least the Modbus t3.5 inter-frame
     * time before starting a new gateway transaction. DE is asserted by
     * hardware automatically when TX starts. */
    uart_modbus_wait_bus_idle(uart);
    int written = uart_write_bytes(uart, gateway_transaction.request,
                                   gateway_transaction.request_length);
    if (written != (int)gateway_transaction.request_length) {
        ESP_LOGE(TAG, "gateway tx write failed: uart=%d written=%d expected=%d",
                 uart, written, (int)gateway_transaction.request_length);
        uart_modbus_reset_transport(uart);
        return false;
    }
    esp_err_t tx_err = uart_wait_tx_done(uart, pdMS_TO_TICKS(500));
    if (tx_err != ESP_OK) {
        ESP_LOGE(TAG, "gateway tx_done failed: uart=%d err=%s",
                 uart, esp_err_to_name(tx_err));
        uart_modbus_reset_transport(uart);
        return false;
    }
    /* DE is released by hardware after the last stop bit; no settle delay
     * is needed before reading the slave response. */

    int received = uart_read_bytes(
        uart, gateway_transaction.response, 1,
        pdMS_TO_TICKS(MODBUS_GATEWAY_RESPONSE_TIMEOUT_MS));
    if (received != 1) {
        return false;
    }
    gateway_transaction.response_length = 1;

    while (gateway_transaction.response_length < MODBUS_GATEWAY_FRAME_MAX) {
        received = uart_read_bytes(
            uart,
            gateway_transaction.response + gateway_transaction.response_length,
            MODBUS_GATEWAY_FRAME_MAX - gateway_transaction.response_length,
            pdMS_TO_TICKS(MODBUS_GATEWAY_GAP_TIMEOUT_MS));
        if (received <= 0) {
            break;
        }
        gateway_transaction.response_length += (size_t)received;
    }
    return gateway_transaction.response_length >= 5U;
}

static nmbs_error execute_command(nmbs_t *client,
                                  const modbus_command_t *command)
{
    if (command->guarded_heater_stop &&
        command->heating_sequence != DataCenterHeatingCommandSequence()) {
        return NMBS_ERROR_NONE;
    }
    if (!select_slave(client, command->slave)) {
        return NMBS_ERROR_TRANSPORT;
    }

    switch (command->type) {
        case MB_COMMAND_WRITE_COIL:
            return nmbs_write_single_coil(client, command->address,
                                          command->value != 0U);

        case MB_COMMAND_WRITE_REGISTER:
            return nmbs_write_single_register(client, command->address,
                                              command->value);

        case MB_COMMAND_WRITE_REGISTER_AND_COIL: {
            nmbs_error error = nmbs_write_single_register(client,
                                                          command->address,
                                                          command->value);
            if (error != NMBS_ERROR_NONE) {
                return error;
            }
            return nmbs_write_single_coil(client, command->coil_address, true);
        }

        case MB_COMMAND_WRITE_POWER_CHANNEL: {
#if NTC_TEMP_MOTOR_STOP_OPTIMIZATION_ENABLE
            if (command->address == MB_POWER_HOLD_SIGNED_MOTOR_GEAR) {
                portENTER_CRITICAL(&input_cache_lock);
                if (command->value == 0U) {
                    ntc_motor_stop_command_us = esp_timer_get_time();
                    ntc_motor_stop_settle_samples_remaining =
                        NTC_MOTOR_STOP_SETTLE_SAMPLES;
                } else {
                    ntc_motor_stop_command_us = 0;
                    ntc_motor_stop_settle_samples_remaining = 0U;
                }
                portEXIT_CRITICAL(&input_cache_lock);
            }
#endif
            if (command->value != 0U) {
                nmbs_error error = nmbs_write_single_register(
                    client, MB_POWER_HOLD_ENABLE, 1U);
                if (error != NMBS_ERROR_NONE) {
                    return error;
                }
            }
            return nmbs_write_single_register(client, command->address,
                                              command->value);
        }

        case MB_COMMAND_RECOVER_POWER_CHANNEL: {
            if (!recovery_target_current(command)) return NMBS_ERROR_NONE;
            ESP_LOGW(TAG, "Recover station 3 address=0x%04X target=%d: 0 -> target",
                     command->address, (int)(int16_t)command->value);
            nmbs_error error = nmbs_write_single_register(
                client, command->address, 0U);
            if (error != NMBS_ERROR_NONE) return error;
            /* A stop or a new target may have arrived while waiting for ACK. */
            if (!recovery_target_current(command)) return NMBS_ERROR_NONE;
            return nmbs_write_single_register(client, command->address,
                                              command->value);
        }

        case MB_COMMAND_ENABLE_POWER_FOR_CALIBRATION: {
            ESP_LOGI(TAG, "Power calibration enable request: slave=%u "
                     "uart=%d address=0x%04X value=1",
                     command->slave, modbus_transport.uart,
                     MB_POWER_HOLD_ENABLE);
            power_calibration_response_trace = true;
            const nmbs_error error = nmbs_write_single_register(
                client, MB_POWER_HOLD_ENABLE, 1U);
            power_calibration_response_trace = false;
            if (error != NMBS_ERROR_NONE) {
                ESP_LOGW(TAG, "Power calibration enable response failed: %s",
                         nmbs_strerror(error));
            }
            return error;
        }

        case MB_COMMAND_START_POWER_CALIBRATION: {
            ESP_LOGI(TAG, "Power calibration trigger request: slave=%u "
                     "uart=%d address=0x%04X value=1",
                     command->slave, modbus_transport.uart,
                     MB_POWER_HOLD_CALIBRATION_TRIGGER);
            power_calibration_response_trace = true;
            const nmbs_error error = nmbs_write_single_register(
                client, MB_POWER_HOLD_CALIBRATION_TRIGGER, 1U);
            power_calibration_response_trace = false;
            if (error != NMBS_ERROR_NONE) {
                ESP_LOGW(TAG, "Power calibration trigger write failed: %s",
                         nmbs_strerror(error));
            }
            return error;
        }

        case MB_COMMAND_RAW_GATEWAY:
            gateway_transaction.success = execute_raw_gateway();
            if (command->reply_to_debug) {
                xTaskNotifyGive(gateway_transaction.waiter);
            }
            return gateway_transaction.success ? NMBS_ERROR_NONE :
                   NMBS_ERROR_TIMEOUT;

        case MB_COMMAND_POLL:
            return NMBS_ERROR_INVALID_ARGUMENT;
    }
    return NMBS_ERROR_INVALID_ARGUMENT;
}

static void execute_command_with_retry(nmbs_t *client,
                                       const modbus_command_t *command)
{
    nmbs_error error = NMBS_ERROR_INVALID_ARGUMENT;
    for (unsigned attempt = 0; attempt < MODBUS_COMMAND_RETRIES; ++attempt) {
        error = execute_command(client, command);
        if (error == NMBS_ERROR_NONE) {
            break;
        }
        /* On failure, wait for the bus to settle before retrying. The t3.5
         * inter-frame gap plus a small backoff avoids colliding with a
         * still-busy slave or a late response on the line. */
        uart_port_t uart = (command->slave == MODBUS_SLAVE_POWER) ?
                            PowerUart : ScreenUart;
        uart_modbus_reset_transport(uart);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (command->type == MB_COMMAND_RECOVER_POWER_CHANNEL) {
        const int index = power_target_index(command->address);
        portENTER_CRITICAL(&command_queue.lock);
        if (index >= 0) power_recovery_pending[index] = false;
        portEXIT_CRITICAL(&command_queue.lock);
    }
    if (error != NMBS_ERROR_NONE) {
        ESP_LOGW(TAG, "Modbus write failed: slave=%u address=%u error=%s",
                 command->slave, command->address, nmbs_strerror(error));
    }
}

static float registers_to_float(uint16_t high_word, uint16_t low_word)
{
    uint32_t raw = ((uint32_t)high_word << 16) | low_word;
    float value;
    memcpy(&value, &raw, sizeof(value));
    return value;
}

static void registers_to_version(const uint16_t *registers, char output[4])
{
    output[0] = (char)(registers[0] >> 8);
    output[1] = (char)(registers[0] & 0xffU);
    output[2] = (char)(registers[1] >> 8);
    output[3] = (char)(registers[1] & 0xffU);
}

static uint32_t registers_to_u32(uint16_t high_word, uint16_t low_word)
{
    return ((uint32_t)high_word << 16) | low_word;
}

static int32_t registers_to_i32(uint16_t high_word, uint16_t low_word)
{
    return (int32_t)(((uint32_t)high_word << 16) | low_word);
}

static float ntc_temperature_filtered;
static float ntc_temperature_samples[NTC_TEMPERATURE_MEDIAN_WINDOW];
static size_t ntc_temperature_sample_count;
static size_t ntc_temperature_sample_next;
static uint32_t ntc2_adc_filtered;
static bool ntc2_adc_filter_ready;
#if NTC_TEMP_MOTOR_STOP_OPTIMIZATION_ENABLE
static bool ntc_motor_was_running;
static int64_t ntc_motor_stopped_since_us;
#endif


static bool ntc_temperature_update_allowed(bool *allow_motor_stop_override)
{
    *allow_motor_stop_override = false;
#if !NTC_TEMP_MOTOR_STOP_OPTIMIZATION_ENABLE
    return true;
#else
    if (!power_input_cache_valid) {
        /* Station 2 may be ready before station 3. With no trustworthy
         * power/motor feedback, allow a temperature from the cooling
         * profile; the default zeroed power state selects that profile. */
        ntc_motor_was_running = false;
        ntc_motor_stopped_since_us = 0;
        return true;
    }

    const int64_t now_us = esp_timer_get_time();
    int64_t stop_command_us;
    portENTER_CRITICAL(&input_cache_lock);
    stop_command_us = ntc_motor_stop_command_us;
    if (ntc_motor_stop_settle_samples_remaining > 0U &&
        now_us - ntc_motor_stop_command_us >= NTC_MOTOR_STOP_SETTLE_US) {
        --ntc_motor_stop_settle_samples_remaining;
        *allow_motor_stop_override = true;
    }
    portEXIT_CRITICAL(&input_cache_lock);
    if (*allow_motor_stop_override) {
        if (g_DataPool.powerMotorRpm == 0U) {
            ntc_motor_was_running = false;
            if (ntc_motor_stopped_since_us == 0) {
                ntc_motor_stopped_since_us = stop_command_us;
            }
        }
        return true;
    }

    if (g_DataPool.powerMotorRpm != 0U) {
        /* A motor edge is not a temperature change. Running ADC admission
         * rejects interference without erasing qualified stopped evidence. */
        ntc_motor_was_running = true;
        ntc_motor_stopped_since_us = 0;
#if MACHINE == 3
        /* Each channel's ADC trend gate decides whether running data is safe. */
        return true;
#else
        return false;
#endif
    }

    if (ntc_motor_was_running || ntc_motor_stopped_since_us == 0) {
        ntc_motor_was_running = false;
        /* The command-based 200 ms settle window has already been used by
         * the override. Do not add a second wait when delayed RPM reaches 0;
         * that would split a 0.5 s stop into two sub-confirmation bursts. */
        ntc_motor_stopped_since_us = stop_command_us > 0 ? stop_command_us : now_us;
    }

    return now_us - ntc_motor_stopped_since_us >=
           NTC_MOTOR_STOP_SETTLE_US;
#endif
}

static uint32_t filter_ntc2_adc(uint32_t raw)
{
    /* 4095 and values outside the calibration span are invalid for the
     * temperature lookup. A large one-frame jump is also rejected; the raw
     * value remains available in g_DataPool.ntc2Raw for diagnostics. */
    if (!ntcTempIsInRange(raw)) {
        return ntc2_adc_filter_ready ? ntc2_adc_filtered : raw;
    }
    if (!ntc2_adc_filter_ready) {
        ntc2_adc_filter_ready = true;
        ntc2_adc_filtered = raw;
        return raw;
    }
    const uint32_t delta = raw > ntc2_adc_filtered
                               ? raw - ntc2_adc_filtered
                               : ntc2_adc_filtered - raw;
    if (delta <= NTC_ADC_MAX_STEP) {
        ntc2_adc_filtered = raw;
    }
    return ntc2_adc_filtered;
}

static float filter_ntc_temperature(float sample)
{
    ntc_temperature_samples[ntc_temperature_sample_next] = sample;
    ntc_temperature_sample_next =
        (ntc_temperature_sample_next + 1U) % NTC_TEMPERATURE_MEDIAN_WINDOW;
    if (ntc_temperature_sample_count < NTC_TEMPERATURE_MEDIAN_WINDOW) {
        ntc_temperature_sample_count++;
    }

    float sorted[NTC_TEMPERATURE_MEDIAN_WINDOW];
    memcpy(sorted, ntc_temperature_samples,
           ntc_temperature_sample_count * sizeof(sorted[0]));
    for (size_t i = 1U; i < ntc_temperature_sample_count; ++i) {
        const float value = sorted[i];
        size_t j = i;
        while (j > 0U && sorted[j - 1U] < value) {
            sorted[j] = sorted[j - 1U];
            --j;
        }
        sorted[j] = value;
    }
    if (ntc_temperature_sample_count < 7U) {
        ntc_temperature_filtered =
            sorted[ntc_temperature_sample_count / 2U];
    } else {
        float total = 0.0f;
        for (size_t i = 3U; i <= 6U; ++i) {
            total += sorted[i];
        }
        ntc_temperature_filtered = total / 4.0f;
    }
    return ntc_temperature_filtered;
}

static portMUX_TYPE ntc_temp_over98_lock = portMUX_INITIALIZER_UNLOCKED;
static bool ntc_temp_over98_sampled;
static int64_t ntc_temp_over98_last_sample_us;
static int64_t ntc_temp_over98_hits[NTC_TEMP_OVER98_REQUIRED];
static unsigned ntc_temp_over98_hit_count;

static void update_ntc_temp_over98_hold(void)
{
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&ntc_temp_over98_lock);
    if (ntc_temp_over98_sampled &&
        now - ntc_temp_over98_last_sample_us < NTC_TEMP_OVER98_SAMPLE_US) {
        portEXIT_CRITICAL(&ntc_temp_over98_lock);
        return;
    }
    ntc_temp_over98_sampled = true;
    ntc_temp_over98_last_sample_us = now;
    if (isfinite(g_DataPool.temperature) &&
        g_DataPool.temperature >= NTC_TEMP_OVER98_THRESHOLD_C) {
        if (ntc_temp_over98_hit_count == NTC_TEMP_OVER98_REQUIRED) {
            memmove(ntc_temp_over98_hits, ntc_temp_over98_hits + 1,
                    (NTC_TEMP_OVER98_REQUIRED - 1U) * sizeof(ntc_temp_over98_hits[0]));
            --ntc_temp_over98_hit_count;
        }
        ntc_temp_over98_hits[ntc_temp_over98_hit_count++] = now;
    }
    portEXIT_CRITICAL(&ntc_temp_over98_lock);
}

bool isTemperatureOver98Confirmed(void)
{
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&ntc_temp_over98_lock);
    const bool confirmed = ntc_temp_over98_hit_count == NTC_TEMP_OVER98_REQUIRED &&
        now - ntc_temp_over98_hits[0] < NTC_TEMP_OVER98_WINDOW_US;
    portEXIT_CRITICAL(&ntc_temp_over98_lock);
    return confirmed;
}

static void poll_actuator_registers(nmbs_t *client)
{
    uint16_t registers[MB_ACT_INPUT_COUNT];
    if (!select_slave(client, MODBUS_SLAVE_ACTUATOR)) {
        return;
    }
    if (nmbs_read_input_registers(client, 0, MB_ACT_INPUT_COUNT, registers) !=
            NMBS_ERROR_NONE) {
        return;
    }

    /* Preserve raw NTC snapshots; NTC2 supplies the primary temperature. */
    g_DataPool.ntc1Raw = registers_to_u32(
        registers[MB_ACT_INPUT_TEMPERATURE_HIGH],
        registers[MB_ACT_INPUT_TEMPERATURE_HIGH + 1U]);

    portENTER_CRITICAL(&input_cache_lock);
    memcpy(actuator_input_cache, registers, sizeof(registers));
    actuator_input_cache_time_us = esp_timer_get_time();
    actuator_input_cache_valid = true;
    portEXIT_CRITICAL(&input_cache_lock);
    g_DataPool.voltage = registers_to_float(
        registers[MB_ACT_INPUT_VOLTAGE_HIGH],
        registers[MB_ACT_INPUT_VOLTAGE_HIGH + 1]);
    g_DataPool.current = registers_to_float(
        registers[MB_ACT_INPUT_CURRENT_HIGH],
        registers[MB_ACT_INPUT_CURRENT_HIGH + 1]);
    g_DataPool.speed = (int16_t)registers[MB_ACT_INPUT_RISE_COMMAND];
    g_DataPool.turns = registers_to_float(
        registers[MB_ACT_INPUT_TURNS_HIGH],
        registers[MB_ACT_INPUT_TURNS_HIGH + 1]);
    g_DataPool.statusWord = registers[MB_ACT_INPUT_STATUS];
    g_DataPool.faultCode = registers[MB_ACT_INPUT_FAULT];
    registers_to_version(&registers[MB_ACT_INPUT_VERSION_HIGH], version[1]);

    uint16_t inputs = registers[MB_ACT_INPUT_DISCRETE_BITMAP];
    uint16_t inputs_high = registers[MB_ACT_INPUT_DISCRETE_BITMAP_HIGH];
    uint16_t coils = registers[MB_ACT_INPUT_COIL_BITMAP];
    for (unsigned i = 0; i < 16U; ++i) {
        sensorState[i] = ((inputs >> i) & 1U) != 0U;
    }
    sensorState[brineLowPos] = (inputs_high & (1U << 0)) != 0U;
    sensorState[modeAvail] = (inputs_high & (1U << 1)) != 0U;
    moterState[Valve] = ((coils >> MB_COIL_VALVE) & 1U) != 0U;
    moterState[pressMoter] = ((coils >> MB_COIL_PRESS_MOTOR) & 1U) != 0U;
    moterState[clearWaterPump] =
        ((coils >> MB_COIL_WATER_PUMP) & 1U) != 0U;
    moterState[drinePump] = ((coils >> MB_COIL_BRINE_PUMP) & 1U) != 0U;
    moterState[hosePump] = ((coils >> MB_COIL_HOSE_PUMP) & 1U) != 0U;
    moterState[riseMoter] = ((coils >> MB_COIL_RISE_MOTOR) & 1U) != 0U;

    g_DataPool.actuatorCoilBitmap = coils;
    g_DataPool.waterPumpCommand = registers[MB_ACT_INPUT_WATER_PUMP_COMMAND];
    g_DataPool.hosePumpCommand = registers[MB_ACT_INPUT_HOSE_PUMP_COMMAND];
    g_DataPool.pressMotorCommand =
        (int16_t)registers[MB_ACT_INPUT_PRESS_COMMAND];
    g_DataPool.riseMotorCommand =
        (int16_t)registers[MB_ACT_INPUT_RISE_COMMAND_MIRROR];
    uint16_t queue_depth = registers[MB_ACT_INPUT_QUEUE_DEPTH];
    g_DataPool.legacyQueueDepth = (uint8_t)(queue_depth >> 8);
    g_DataPool.modbusQueueDepth = (uint8_t)queue_depth;
    g_DataPool.modbusQueueOverflowCount = registers_to_u32(
        registers[MB_ACT_INPUT_MODBUS_QUEUE_OVERFLOW_HIGH],
        registers[MB_ACT_INPUT_MODBUS_QUEUE_OVERFLOW_HIGH + 1]);
    g_DataPool.modbusCrcErrorCount = registers_to_u32(
        registers[MB_ACT_INPUT_MODBUS_CRC_ERROR_HIGH],
        registers[MB_ACT_INPUT_MODBUS_CRC_ERROR_HIGH + 1]);
    g_DataPool.modbusFrameErrorCount = registers_to_u32(
        registers[MB_ACT_INPUT_MODBUS_FRAME_ERROR_HIGH],
        registers[MB_ACT_INPUT_MODBUS_FRAME_ERROR_HIGH + 1]);
    g_DataPool.legacyQueueOverflowCount = registers_to_u32(
        registers[MB_ACT_INPUT_LEGACY_QUEUE_OVERFLOW_HIGH],
        registers[MB_ACT_INPUT_LEGACY_QUEUE_OVERFLOW_HIGH + 1]);
    g_DataPool.tmuxD0Bitmap = (uint8_t)registers[MB_ACT_INPUT_TMUX_D0_BITMAP];
    g_DataPool.tmuxD1Bitmap = (uint8_t)registers[MB_ACT_INPUT_TMUX_D1_BITMAP];
    for (unsigned i = 0; i < 8U; ++i) {
        g_DataPool.tmuxAdcRaw[i] = registers[MB_ACT_INPUT_TMUX_ADC_0 + i];
    }
    g_DataPool.currentAdcRaw = registers[MB_ACT_INPUT_CURRENT_ADC_RAW];
    g_DataPool.actuatorDiscreteBitmapHigh = inputs_high;

    /* V2 appended region 0x0028~0x004F. Standard-only slaves return 0 for
     * these addresses, so the fields stay zero on non-V2 hardware. */
    g_DataPool.busAmpere = registers_to_float(
        registers[MB_ACT_INPUT_BUS_AMPERE_HIGH],
        registers[MB_ACT_INPUT_BUS_AMPERE_HIGH + 1]);
    g_DataPool.ledAmpere = registers_to_float(
        registers[MB_ACT_INPUT_LED_AMPERE_HIGH],
        registers[MB_ACT_INPUT_LED_AMPERE_HIGH + 1]);
    g_DataPool.brakeAmpere = registers_to_float(
        registers[MB_ACT_INPUT_BRAKE_AMPERE_HIGH],
        registers[MB_ACT_INPUT_BRAKE_AMPERE_HIGH + 1]);
    g_DataPool.strainGageAmpere = registers_to_float(
        registers[MB_ACT_INPUT_STRAIN_GAGE_AMPERE_HIGH],
        registers[MB_ACT_INPUT_STRAIN_GAGE_AMPERE_HIGH + 1]);
    g_DataPool.riseRightCurrentB = registers_to_float(
        registers[MB_ACT_INPUT_RISE_RIGHT_B_HIGH],
        registers[MB_ACT_INPUT_RISE_RIGHT_B_HIGH + 1]);
    g_DataPool.riseRightCurrentA = registers_to_float(
        registers[MB_ACT_INPUT_RISE_RIGHT_A_HIGH],
        registers[MB_ACT_INPUT_RISE_RIGHT_A_HIGH + 1]);
    g_DataPool.riseLeftCurrentB = registers_to_float(
        registers[MB_ACT_INPUT_RISE_LEFT_B_HIGH],
        registers[MB_ACT_INPUT_RISE_LEFT_B_HIGH + 1]);
    g_DataPool.riseLeftCurrentA = registers_to_float(
        registers[MB_ACT_INPUT_RISE_LEFT_A_HIGH],
        registers[MB_ACT_INPUT_RISE_LEFT_A_HIGH + 1]);
    g_DataPool.riseCurrentMagnitude =
        registers[MB_ACT_INPUT_RISE_CURRENT_MAGNITUDE];
    g_DataPool.pressCurrentMagnitude =
        registers[MB_ACT_INPUT_PRESS_CURRENT_MAGNITUDE];
    g_DataPool.resetKeyLevel = registers[MB_ACT_INPUT_RESET_KEY_LEVEL];
    g_DataPool.riseLeftEncoderPos = registers_to_i32(
        registers[MB_ACT_INPUT_ENCODER_RISE_LEFT_HIGH],
        registers[MB_ACT_INPUT_ENCODER_RISE_LEFT_HIGH + 1]);
    g_DataPool.riseRightEncoderPos = registers_to_i32(
        registers[MB_ACT_INPUT_ENCODER_RISE_RIGHT_HIGH],
        registers[MB_ACT_INPUT_ENCODER_RISE_RIGHT_HIGH + 1]);
    g_DataPool.pressRightEncoderPos = registers_to_i32(
        registers[MB_ACT_INPUT_ENCODER_PRESS_RIGHT_HIGH],
        registers[MB_ACT_INPUT_ENCODER_PRESS_RIGHT_HIGH + 1]);
    g_DataPool.ntc2Raw = registers_to_u32(
        registers[MB_ACT_INPUT_NTC2_HIGH],
        registers[MB_ACT_INPUT_NTC2_HIGH + 1U]);
    g_DataPool.ntc3Raw = registers_to_u32(
        registers[MB_ACT_INPUT_NTC3_HIGH],
        registers[MB_ACT_INPUT_NTC3_HIGH + 1U]);
    bool allow_motor_stop_override = false;
    const bool update_temperature =
        ntc_temperature_update_allowed(&allow_motor_stop_override);
    ntcTempSetMotorStopSettleOverride(allow_motor_stop_override);
    g_DataPool.ntc2FilteredRaw = filter_ntc2_adc(g_DataPool.ntc2Raw);
    if (update_temperature) {
        const float temperature = ntcTempGetTemperature();
        if (isfinite(temperature) && ntcTempPrimarySampleAccepted()) {
            g_DataPool.temperature = filter_ntc_temperature(temperature);
            update_ntc_temp_over98_hold();
        }
    }
    ntcTempSetMotorStopSettleOverride(false);
    g_DataPool.ntcBackRaw = registers_to_u32(
        registers[MB_ACT_INPUT_NTC_BACK_HIGH],
        registers[MB_ACT_INPUT_NTC_BACK_HIGH + 1U]);
    g_DataPool.waterFlowPulses = registers_to_u32(
        registers[MB_ACT_INPUT_WATER_FLOW_HIGH],
        registers[MB_ACT_INPUT_WATER_FLOW_HIGH + 1]);
    g_DataPool.brineFlowPulses = registers_to_u32(
        registers[MB_ACT_INPUT_BRINE_FLOW_HIGH],
        registers[MB_ACT_INPUT_BRINE_FLOW_HIGH + 1]);
    g_DataPool.windingAZero = registers[MB_ACT_INPUT_WINDING_A_ZERO];
    g_DataPool.windingBZero = registers[MB_ACT_INPUT_WINDING_B_ZERO];
    g_DataPool.adcReady = registers[MB_ACT_INPUT_ADC_READY];
    g_DataPool.adcSampleCount = registers[MB_ACT_INPUT_ADC_SAMPLE_COUNT];
}

static void poll_power_registers(nmbs_t *client)
{
    uint16_t registers[MB_POWER_INPUT_COUNT];
    uint16_t cycle_config[MB_POWER_CACHE_CYCLE_CONFIG_COUNT];
    if (!select_slave(client, MODBUS_SLAVE_POWER)) {
        return;
    }
    if (nmbs_read_input_registers(client, 0, MB_POWER_INPUT_COUNT, registers) !=
            NMBS_ERROR_NONE) {
        return;
    }
    const int64_t input_received_us = esp_timer_get_time();
    /* Publish FC04 immediately; auxiliary FC03 failures must not freeze
     * meter readings, motor feedback or the downstream firmware version. */
    portENTER_CRITICAL(&input_cache_lock);
    memcpy(power_input_cache, registers, sizeof(registers));
    power_input_cache_valid = true;
    portEXIT_CRITICAL(&input_cache_lock);

    g_DataPool.powerMotorRpm = registers[MB_POWER_INPUT_MOTOR_RPM];
    g_DataPool.powerMotorState = registers[MB_POWER_INPUT_MOTOR_STATE];
    g_DataPool.powerMotorPhaseCurrentMa =
        registers[MB_POWER_INPUT_PHASE_CURRENT_MA];
    g_DataPool.powerEnabled = registers[MB_POWER_INPUT_ENABLE] != 0U;
    g_DataPool.powerHeater1Enabled =
        registers[MB_POWER_INPUT_HEATER_1_ENABLED] != 0U;
    g_DataPool.powerHeater2Enabled =
        registers[MB_POWER_INPUT_HEATER_2_ENABLED] != 0U;
    g_DataPool.powerHeater1Gear = registers[MB_POWER_INPUT_HEATER_1_GEAR];
    g_DataPool.powerHeater2Gear = registers[MB_POWER_INPUT_HEATER_2_GEAR];
    g_DataPool.powerMotorGear =
        (int16_t)registers[MB_POWER_INPUT_SIGNED_MOTOR_GEAR];
    g_DataPool.powerPc2AdcMedian =
        registers[MB_POWER_INPUT_PC2_ADC_MEDIAN];
    g_DataPool.powerMeterVoltageMv = registers_to_u32(
        registers[MB_POWER_INPUT_METER_VOLTAGE_HIGH],
        registers[MB_POWER_INPUT_METER_VOLTAGE_HIGH + 1U]);
    g_DataPool.powerMeterCurrentMa = registers_to_u32(
        registers[MB_POWER_INPUT_METER_CURRENT_HIGH],
        registers[MB_POWER_INPUT_METER_CURRENT_HIGH + 1U]);
    g_DataPool.powerMeterActivePowerMw = registers_to_u32(
        registers[MB_POWER_INPUT_METER_ACTIVE_POWER_HIGH],
        registers[MB_POWER_INPUT_METER_ACTIVE_POWER_HIGH + 1U]);
    g_DataPool.powerMeterSampleValid =
        registers[MB_POWER_INPUT_METER_SAMPLE_VALID] != 0U;
    g_DataPool.powerMeterFrequencyMilliHz = registers_to_u32(
        registers[MB_POWER_INPUT_METER_FREQUENCY_MILLIHZ_HIGH],
        registers[MB_POWER_INPUT_METER_FREQUENCY_MILLIHZ_HIGH + 1U]);
    moterState[breakMoter] = g_DataPool.powerMotorRpm != 0U;
    registers_to_version(&registers[MB_POWER_INPUT_VERSION_HIGH], version[2]);

    if (nmbs_read_holding_registers(
            client, MB_POWER_HOLD_CYCLE_THRESHOLD,
            MB_POWER_CACHE_CYCLE_CONFIG_COUNT, cycle_config) == NMBS_ERROR_NONE) {
        portENTER_CRITICAL(&input_cache_lock);
        memcpy(power_input_cache + MB_POWER_INPUT_COUNT, cycle_config,
               sizeof(cycle_config));
        g_DataPool.powerCycleThreshold = cycle_config[0];
        g_DataPool.powerCycleTotal = cycle_config[1];
        portEXIT_CRITICAL(&input_cache_lock);
    }
    uint16_t calibration_status = 0U;
    const bool calibration_valid = nmbs_read_holding_registers(
        client, MB_POWER_HOLD_CALIBRATION_STATUS, 1U,
        &calibration_status) == NMBS_ERROR_NONE;
    if (calibration_valid) {
        portENTER_CRITICAL(&input_cache_lock);
        g_DataPool.powerCalibrationStatus = calibration_status;
        ++power_calibration_status_generation;
        portEXIT_CRITICAL(&input_cache_lock);
    }

    portENTER_CRITICAL(&command_queue.lock);
    /* Unknown calibration state must not authorize automatic retriggering. */
    power_recovery_feedback.valid = calibration_valid;
    power_recovery_feedback.meter_valid = g_DataPool.powerMeterSampleValid;
    power_recovery_feedback.enabled = g_DataPool.powerEnabled;
    power_recovery_feedback.calibration_status = calibration_status;
    power_recovery_feedback.rpm = g_DataPool.powerMotorRpm;
    power_recovery_feedback.power_mw = g_DataPool.powerMeterActivePowerMw;
    power_recovery_feedback.received_us = input_received_us;
    portEXIT_CRITICAL(&command_queue.lock);

}

static void poll_group(nmbs_t *client, unsigned poll_step)
{
    switch (poll_step) {
        case 0:
            poll_actuator_registers(client);
            break;
        default:
            poll_power_registers(client);
            break;
    }
}

static bool queue_poll(unsigned poll_step)
{
    const modbus_command_t command = {
        .type = MB_COMMAND_POLL,
        .address = (uint16_t)poll_step,
        .reply_to_debug = false,
    };
    return command_queue_push(&command);
}

void ModbusTestStart(uint8_t slave, bool aggressive)
{
    if (slave != MODBUS_SLAVE_ACTUATOR && slave != MODBUS_SLAVE_POWER) {
        return;
    }
    /* Reset statistics first, then activate. The DataCenterTask picks up
     * the active flag on its next loop iteration and enters the test loop.
     * The aggressive flag selects whether short timeouts are applied so
     * a dead slave fails fast (violent mode) or the default business
     * timeouts are used (normal mode). */
    g_ModbusTestStats.active = false;
    g_ModbusTestStats.aggressive = aggressive;
    g_ModbusTestStats.slave = slave;
    g_ModbusTestStats.attempts = 0;
    g_ModbusTestStats.ok = 0;
    g_ModbusTestStats.timeout = 0;
    g_ModbusTestStats.crc = 0;
    g_ModbusTestStats.invalid = 0;
    g_ModbusTestStats.total_us = 0;
    g_ModbusTestStats.min_us = 0;
    g_ModbusTestStats.max_us = 0;
    g_ModbusTestStats.active = true;
}

void ModbusTestStop(void)
{
    g_ModbusTestStats.active = false;
}

void DataCenterScheduleDefaultTest(uint8_t slave, bool aggressive,
                                   uint32_t delay_ms)
{
    /* slave=0 or delay_ms=0 means "no default test"; clear the schedule. */
    if (slave == 0U || delay_ms == 0U) {
        g_default_test_schedule.pending = false;
        return;
    }
    g_default_test_schedule.pending = true;
    g_default_test_schedule.aggressive = aggressive;
    g_default_test_schedule.slave = slave;
    g_default_test_schedule.delay_ms = delay_ms;
}

static void run_modbus_test(nmbs_t *client)
{
    const uint8_t slave = g_ModbusTestStats.slave;
    const uint16_t count = (slave == MODBUS_SLAVE_ACTUATOR) ?
                           MB_ACT_INPUT_COUNT : MB_POWER_INPUT_COUNT;
    uint16_t registers[MB_ACT_INPUT_COUNT];

    if (!select_slave(client, slave)) {
        g_ModbusTestStats.attempts++;
        g_ModbusTestStats.invalid++;
        return;
    }

    const int64_t start = esp_timer_get_time();
    const nmbs_error error =
        nmbs_read_input_registers(client, 0, count, registers);
    const uint32_t elapsed_us = (uint32_t)(esp_timer_get_time() - start);

    g_ModbusTestStats.attempts++;
    g_ModbusTestStats.total_us += elapsed_us;
    if (g_ModbusTestStats.min_us == 0U ||
        elapsed_us < g_ModbusTestStats.min_us) {
        g_ModbusTestStats.min_us = elapsed_us;
    }
    if (elapsed_us > g_ModbusTestStats.max_us) {
        g_ModbusTestStats.max_us = elapsed_us;
    }

    if (error == NMBS_ERROR_NONE) {
        g_ModbusTestStats.ok++;
    } else if (error == NMBS_ERROR_TIMEOUT) {
        g_ModbusTestStats.timeout++;
    } else if (error == NMBS_ERROR_CRC) {
        g_ModbusTestStats.crc++;
    } else {
        g_ModbusTestStats.invalid++;
    }
}

#define MODBUS_TEST_REPORT_INTERVAL_MS 5000U
/* Aggressive timeouts used when the test is started in violent mode so
 * that a missing slave response fails fast instead of blocking the
 * tight read loop for the full MODBUS_RESPONSE_TIMEOUT_MS. The byte
 * timeout is kept non-zero so a slow-but-alive slave is still read. */
#define MODBUS_TEST_AGGRESSIVE_READ_TIMEOUT_MS  10
#define MODBUS_TEST_AGGRESSIVE_BYTE_TIMEOUT_MS   5

void ModbusTestPrintStats(void)
{
    const modbus_test_stats_t *s = &g_ModbusTestStats;
    const uint64_t failures = s->timeout + s->crc + s->invalid;
    const uint64_t avg_us = (s->attempts > 0U) ?
                            s->total_us / s->attempts : 0U;
    printf("TEST:STATS slave=%02u attempts=%llu ok=%llu fail=%llu "
           "timeout=%llu crc=%llu invalid=%llu avg_us=%llu min_us=%lu "
           "max_us=%lu\r\n",
           (unsigned)s->slave,
           (unsigned long long)s->attempts, (unsigned long long)s->ok,
           (unsigned long long)failures, (unsigned long long)s->timeout,
           (unsigned long long)s->crc, (unsigned long long)s->invalid,
           (unsigned long long)avg_us, (unsigned long)s->min_us,
           (unsigned long)s->max_us);
}

void DataCenterTask(void *pvParameters)
{
    (void)pvParameters;
    nmbs_t client;
    nmbs_error error = modbus_client_init(&client);
    if (error != NMBS_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to initialize Modbus client: %s",
                 nmbs_strerror(error));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Modbus RTU master ready: slave 2=UART%d/%d, "
             "slave 3=UART%d/%d", ScreenUart, MODBUS_ACTUATOR_BAUD,
             PowerUart, MODBUS_POWER_BAUD);

    /* Default pressure test schedule (set by main.c via
     * DataCenterScheduleDefaultTest). Wait delay_ms after startup before
     * entering the test loop, so the rest of the system (LCD/Wi-Fi/BLE)
     * has time to settle. The operator can still stop it via "TEST:STOP". */
    if (g_default_test_schedule.pending) {
        const uint32_t delay_ms = g_default_test_schedule.delay_ms;
        ESP_LOGW(TAG, "Default pressure test scheduled: slave=0x%02X "
                 "aggressive=%d start in %lu ms",
                 g_default_test_schedule.slave,
                 g_default_test_schedule.aggressive,
                 (unsigned long)delay_ms);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        ModbusTestStart(g_default_test_schedule.slave,
                        g_default_test_schedule.aggressive);
        g_default_test_schedule.pending = false;
    }

    TickType_t next_actuator_poll = xTaskGetTickCount();
    TickType_t next_power_poll = next_actuator_poll;
    TickType_t next_test_report = 0;
    bool test_timeouts_applied = false;
    for (;;) {
        /* Test mode: stop automatic polling and read the selected slave
         * as fast as possible, counting outcomes. The flag is cleared by
         * ModbusTestStop() from the debug console. Statistics are printed
         * every MODBUS_TEST_REPORT_INTERVAL_MS so the operator can watch
         * live without sending TEST:STATS manually. When aggressive is
         * set, short read/byte timeouts are applied so a dead slave fails
         * fast; otherwise the default business timeouts are used so a
         * slow-but-alive slave is still read correctly. Timeouts are
         * restored to business values when the test ends. */
        if (g_ModbusTestStats.active) {
            if (!test_timeouts_applied && g_ModbusTestStats.aggressive) {
                nmbs_set_read_timeout(&client,
                    MODBUS_TEST_AGGRESSIVE_READ_TIMEOUT_MS);
                nmbs_set_byte_timeout(&client,
                    MODBUS_TEST_AGGRESSIVE_BYTE_TIMEOUT_MS);
                test_timeouts_applied = true;
            }
            run_modbus_test(&client);
            TickType_t now = xTaskGetTickCount();
            if (next_test_report == 0U) {
                next_test_report = now +
                    pdMS_TO_TICKS(MODBUS_TEST_REPORT_INTERVAL_MS);
            } else if ((int32_t)(now - next_test_report) >= 0) {
                ModbusTestPrintStats();
                next_test_report = now +
                    pdMS_TO_TICKS(MODBUS_TEST_REPORT_INTERVAL_MS);
            }
            continue;
        }
        if (test_timeouts_applied) {
            nmbs_set_read_timeout(&client, MODBUS_RESPONSE_TIMEOUT_MS);
            nmbs_set_byte_timeout(&client, MODBUS_BYTE_TIMEOUT_MS);
            test_timeouts_applied = false;
        }
        next_test_report = 0;
        modbus_command_t command;
        if (command_queue_pop(&command)) {
            if (command.type == MB_COMMAND_RAW_GATEWAY) {
                (void)execute_command(&client, &command);
            } else if (command.type == MB_COMMAND_POLL) {
                poll_group(&client, command.address);
            } else {
                execute_command_with_retry(&client, &command);
            }
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        bool poll_queued = false;
        if ((int32_t)(now - next_actuator_poll) >= 0 && queue_poll(0U)) {
            next_actuator_poll =
                now + pdMS_TO_TICKS(MODBUS_ACTUATOR_POLL_PERIOD_MS);
            poll_queued = true;
        }
        if ((int32_t)(now - next_power_poll) >= 0 && queue_poll(1U)) {
            next_power_poll =
                now + pdMS_TO_TICKS(MODBUS_POWER_POLL_PERIOD_MS);
            poll_queued = true;
        }
        if (!poll_queued) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
}

bool ModbusGatewayTransaction(const uint8_t *request, size_t request_length,
                              uint8_t *response, size_t response_capacity,
                              size_t *response_length)
{
    if (request == NULL || response == NULL || response_length == NULL ||
        request_length < 4U || request_length > MODBUS_GATEWAY_FRAME_MAX ||
        response_capacity == 0U ||
        (request[0] != MODBUS_SLAVE_ACTUATOR &&
         request[0] != MODBUS_SLAVE_POWER)) {
        return false;
    }

    portENTER_CRITICAL(&gateway_transaction.lock);
    if (gateway_transaction.busy) {
        portEXIT_CRITICAL(&gateway_transaction.lock);
        return false;
    }
    gateway_transaction.busy = true;
    gateway_transaction.request_length = request_length;
    memcpy(gateway_transaction.request, request, request_length);
    gateway_transaction.waiter = xTaskGetCurrentTaskHandle();
    portEXIT_CRITICAL(&gateway_transaction.lock);

    const modbus_command_t command = {
        .type = MB_COMMAND_RAW_GATEWAY,
        .slave = request[0],
        .reply_to_debug = true,
        .raw_heating_request = power_frame_starts_heating(request, request_length),
    };
    if (!command_queue_push(&command)) {
        gateway_transaction.busy = false;
        return false;
    }

    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    bool success = gateway_transaction.success &&
                   gateway_transaction.response_length <= response_capacity;
    if (success) {
        memcpy(response, gateway_transaction.response,
               gateway_transaction.response_length);
        *response_length = gateway_transaction.response_length;
    } else {
        *response_length = 0;
    }
    gateway_transaction.busy = false;
    return success;
}

bool DataCenterGetSensorSnapshot(uint16_t *raw_inputs)
{
    if (raw_inputs == NULL) {
        return false;
    }

    *raw_inputs = UINT16_MAX;
    portENTER_CRITICAL(&input_cache_lock);
    const int64_t age_us = esp_timer_get_time() - actuator_input_cache_time_us;
    const bool valid = actuator_input_cache_valid && age_us >= 0 &&
                       age_us < (int64_t)DATACENTER_SENSOR_TIMEOUT_MS * 1000;
    if (valid) {
        *raw_inputs = actuator_input_cache[MB_ACT_INPUT_DISCRETE_BITMAP];
    }
    portEXIT_CRITICAL(&input_cache_lock);
    return valid;
}

bool ModbusCopyCachedInputRegisters(uint8_t slave, uint16_t address,
                                    uint16_t quantity, uint16_t *registers)
{
    const uint16_t *source;
    uint16_t count;
    bool valid;

    if (registers == NULL || quantity == 0U) {
        return false;
    }
    if (slave == MODBUS_SLAVE_ACTUATOR) {
        source = actuator_input_cache;
        count = MB_ACT_INPUT_COUNT;
    } else if (slave == MODBUS_SLAVE_POWER) {
        source = power_input_cache;
        count = MB_POWER_CACHE_COUNT;
    } else {
        return false;
    }
    if ((uint32_t)address + quantity > count) {
        return false;
    }

    portENTER_CRITICAL(&input_cache_lock);
    valid = slave == MODBUS_SLAVE_ACTUATOR ? actuator_input_cache_valid :
                                            power_input_cache_valid;
    if (valid) {
        memcpy(registers, source + address, quantity * sizeof(*registers));
    }
    portEXIT_CRITICAL(&input_cache_lock);
    return valid;
}

bool ModbusActuatorOnline(void)
{
    bool valid;

    portENTER_CRITICAL(&input_cache_lock);
    valid = actuator_input_cache_valid;
    portEXIT_CRITICAL(&input_cache_lock);
    return valid;
}

bool ModbusPowerOnline(void)
{
    bool valid;

    portENTER_CRITICAL(&input_cache_lock);
    valid = power_input_cache_valid;
    portEXIT_CRITICAL(&input_cache_lock);
    return valid;
}

void waterPumpOn(uint16_t waterPumpSpeed)
{
    if (waterPumpSpeed > MODBUS_WATER_PUMP_SPEED_MAX) {
        waterPumpSpeed = MODBUS_WATER_PUMP_SPEED_MAX;
    }
    const modbus_command_t command = {
        .type = MB_COMMAND_WRITE_REGISTER_AND_COIL,
        .slave = MODBUS_SLAVE_ACTUATOR,
        .address = MB_HOLD_WATER_PUMP_SPEED,
        .value = waterPumpSpeed,
        .coil_address = MB_COIL_WATER_PUMP,
        .reply_to_debug = false,
    };
    command_queue_push(&command);
}

void waterPumpOff(void)
{
    queue_coil(MODBUS_SLAVE_ACTUATOR, MB_COIL_WATER_PUMP, false);
}

void brinePumpOn(void)
{
    queue_coil(MODBUS_SLAVE_ACTUATOR, MB_COIL_BRINE_PUMP, true);
}

void brinePumpOff(void)
{
    queue_coil(MODBUS_SLAVE_ACTUATOR, MB_COIL_BRINE_PUMP, false);
}

void hosePumpOn(uint8_t hosePumpSpeed)
{
    /* 下位机线圈 0x0002 置 1 会用它编译期的默认速度（PUMP_DEFAULT_HOSE_PERCENT）
     * 开泵，所以必须先写线圈、再写速度寄存器 0x0001，速度才不会被默认值覆盖。
     * 速度寄存器非 0 时下位机会按该值开泵（0~255，>127 变负=反向）。 */
    queue_coil(MODBUS_SLAVE_ACTUATOR, MB_COIL_HOSE_PUMP, true);

    const modbus_command_t command = {
        .type = MB_COMMAND_WRITE_REGISTER,
        .slave = MODBUS_SLAVE_ACTUATOR,
        .address = MB_HOLD_HOSE_PUMP_SPEED,
        .value = hosePumpSpeed,
        .reply_to_debug = false,
    };
    command_queue_push(&command);
}

void hosePumpSetSignedSpeed(int16_t signedSpeed)
{
    if (signedSpeed > 100) {
        signedSpeed = 100;
    } else if (signedSpeed < -100) {
        signedSpeed = -100;
    }
    const modbus_command_t command = {
        .type = MB_COMMAND_WRITE_REGISTER_AND_COIL,
        .slave = MODBUS_SLAVE_ACTUATOR,
        .address = MB_HOLD_HOSE_PUMP_SPEED,
        .value = (uint16_t)signedSpeed,
        .coil_address = MB_COIL_HOSE_PUMP,
        .reply_to_debug = false,
    };
    command_queue_push(&command);
}

void hosePumpOff(void)
{
    queue_coil(MODBUS_SLAVE_ACTUATOR, MB_COIL_HOSE_PUMP, false);
}

void pressMoterDown(uint16_t speed)
{
    queue_signed_motor_speed(MB_HOLD_PRESS_MOTOR, speed,
                             MODBUS_DIRECTION_DOWN);
}

void pressMoterUp(uint16_t speed)
{
    queue_signed_motor_speed(MB_HOLD_PRESS_MOTOR, speed,
                             MODBUS_DIRECTION_UP);
}

void pressMoterStop(void)
{
    queue_register(MODBUS_SLAVE_ACTUATOR, MB_HOLD_PRESS_MOTOR, 0U);
}

void riseMoterDown(uint16_t speed)
{
    queue_signed_motor_speed(MB_HOLD_RISE_MOTOR, speed,
                             MODBUS_DIRECTION_DOWN);
}

void riseMoterUp(uint16_t speed)
{
    queue_signed_motor_speed(MB_HOLD_RISE_MOTOR, speed,
                             MODBUS_DIRECTION_UP);
}

void riseMoterStop(void)
{
    queue_register(MODBUS_SLAVE_ACTUATOR, MB_HOLD_RISE_MOTOR, 0U);
}

void riseMoterTimedDownAndHold(void)
{
    queue_register(MODBUS_SLAVE_ACTUATOR, MB_HOLD_RISE_MOTOR,
                   MODBUS_RISE_MOTOR_TIMED_DOWN_HOLD);
}

void riseMoterTimedUpAndHold(void)
{
    queue_register(MODBUS_SLAVE_ACTUATOR, MB_HOLD_RISE_MOTOR,
                   MODBUS_RISE_MOTOR_TIMED_UP_HOLD);
}

/* 抬升电流档：0=低档（普通上/下行、上位机手动点动），非 0=高档（压制流程带载）。
 * 幅值数值写在下位机 moter42.c，主站只写档位；下位机粘滞保存，离开流程要写回低档。 */
void riseMoterSetCurrentGear(uint8_t loadedGear)
{
    uint16_t value = MODBUS_RISE_CURRENT_GEAR_NORMAL;
    if (loadedGear != MODBUS_RISE_CURRENT_GEAR_NORMAL) {
        value = MODBUS_RISE_CURRENT_GEAR_LOADED;
    }
    queue_register(MODBUS_SLAVE_ACTUATOR, MB_HOLD_RISE_CURRENT_GEAR, value);
}

void ws2812Coler(uint8_t lednum, uint8_t red, uint8_t green, uint8_t blue)
{
    (void)lednum;
    queue_coil(MODBUS_SLAVE_ACTUATOR, MB_COIL_LIGHT,
               red != 0U || green != 0U || blue != 0U);
}

static void queue_power_channel(uint16_t address, uint16_t value)
{
    const modbus_command_t command = {
        .type = MB_COMMAND_WRITE_POWER_CHANNEL,
        .slave = MODBUS_SLAVE_POWER,
        .address = address,
        .value = value,
        .reply_to_debug = false,
    };
    command_queue_push(&command);
}

void setPowerEnabled(bool enabled)
{
    queue_register(MODBUS_SLAVE_POWER, MB_POWER_HOLD_ENABLE,
                   enabled ? 1U : 0U);
}

bool setPowerCalibration(void)
{
    const modbus_command_t command = {
        .type = MB_COMMAND_START_POWER_CALIBRATION,
        .slave = MODBUS_SLAVE_POWER,
        .reply_to_debug = false,
    };
    return command_queue_push(&command);
}

uint16_t getPowerCalibrationStatus(void)
{
    return g_DataPool.powerCalibrationStatus;
}

static uint16_t power_calibration_status(void)
{
    uint16_t status;
    portENTER_CRITICAL(&input_cache_lock);
    status = g_DataPool.powerCalibrationStatus;
    portEXIT_CRITICAL(&input_cache_lock);
    return status;
}

/* 一次校准跑完后站3 的状态会停在 2(完成)，这时再写触发它不会重新跑，
 * 主站就一直等不到 1(校准中)。实测它要过十几秒才会自己回 0，
 * 所以这里按时间窗口等：先写触发 0，再每 200ms 探一次，最多等 30 秒；
 * 到第 10 秒还没清掉，就关一次动力板总开关再打开，逼它复位。 */
static bool power_calibration_clear_stale(void)
{
    const uint16_t status = power_calibration_status();
    if (status == 0U) {
        return true;
    }

    ESP_LOGW(TAG, "Power calibration status stale (%u), clearing before trigger",
             (unsigned)status);
    queue_register(MODBUS_SLAVE_POWER, MB_POWER_HOLD_CALIBRATION_TRIGGER, 0U);

    bool toggled = false;
    for (unsigned waited = 0U; waited < POWER_CAL_CLEAR_TIMEOUT_MS; waited += 200U) {
        if (power_calibration_status() == 0U) {
            ESP_LOGI(TAG, "Power calibration status cleared after %u ms", waited);
            return true;
        }
        if (!toggled && waited >= POWER_CAL_CLEAR_TOGGLE_MS) {
            toggled = true;
            ESP_LOGW(TAG, "Calibration status still %u, toggling power enable",
                     (unsigned)power_calibration_status());
            queue_register(MODBUS_SLAVE_POWER, MB_POWER_HOLD_ENABLE, 0U);
            vTaskDelay(pdMS_TO_TICKS(500));
            queue_register(MODBUS_SLAVE_POWER, MB_POWER_HOLD_ENABLE, 1U);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    ESP_LOGE(TAG, "Power calibration status still %u after %u ms",
             (unsigned)power_calibration_status(),
             (unsigned)POWER_CAL_CLEAR_TIMEOUT_MS);
    return false;
}

/* 触发校准并确认状态真的走到 1(校准中)；卡住就重发，不干等整段超时 */
static bool power_calibration_start(void)
{
    for (unsigned attempt = 0U; attempt < POWER_CAL_START_ATTEMPTS; ++attempt) {
        if (!setPowerCalibration()) {
            ESP_LOGE(TAG, "Unable to queue power calibration command");
            return false;
        }
        const TickType_t start = xTaskGetTickCount();
        while ((TickType_t)(xTaskGetTickCount() - start) <
               pdMS_TO_TICKS(POWER_CAL_START_TIMEOUT_MS)) {
            const uint16_t status = power_calibration_status();
            if (status == 1U) {
                return true;
            }
            if (status == 3U) {
                ESP_LOGE(TAG, "Power calibration reported device timeout");
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        ESP_LOGW(TAG, "Power calibration not started within %u ms (attempt %u/%u)",
                 (unsigned)POWER_CAL_START_TIMEOUT_MS,
                 attempt + 1U, (unsigned)POWER_CAL_START_ATTEMPTS);
    }
    ESP_LOGE(TAG, "Power calibration never reported running");
    return false;
}

bool runPowerCalibration(uint32_t timeout_ms)
{
    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
    ESP_LOGI(TAG, "Power calibration starting: enable_feedback=%u "
             "status=%u timeout=%u ms",
             (unsigned)g_DataPool.powerEnabled,
             (unsigned)power_calibration_status(),
             (unsigned)timeout_ms);

    if (!power_calibration_clear_stale()) {
        ESP_LOGE(TAG, "Power calibration status could not be cleared");
        return false;
    }
    const modbus_command_t enable_command = {
        .type = MB_COMMAND_ENABLE_POWER_FOR_CALIBRATION,
        .slave = MODBUS_SLAVE_POWER,
        .reply_to_debug = false,
    };
    if (!command_queue_push(&enable_command)) {
        ESP_LOGE(TAG, "Unable to queue power enable for calibration");
        return false;
    }

    while (!g_DataPool.powerEnabled &&
           (TickType_t)(xTaskGetTickCount() - start) < timeout) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "Waiting for power enable feedback: enabled=%u",
                 (unsigned)g_DataPool.powerEnabled);
    }
    if (!g_DataPool.powerEnabled) {
        ESP_LOGE(TAG, "Power station enable was not confirmed");
        return false;
    }
    ESP_LOGI(TAG, "Power enable confirmed; queuing calibration trigger");

    if (!power_calibration_start()) {
        return false;
    }

    /* power_calibration_start() 已经确认过状态是 1(校准中) */
    bool calibration_started = true;
    uint16_t previous_status = UINT16_MAX;
    TickType_t last_progress = xTaskGetTickCount();
    uint32_t seen_generation;
    portENTER_CRITICAL(&input_cache_lock);
    seen_generation = power_calibration_status_generation;
    portEXIT_CRITICAL(&input_cache_lock);

    while ((TickType_t)(xTaskGetTickCount() - last_progress) < timeout) {
        uint16_t status;
        uint32_t generation;
        portENTER_CRITICAL(&input_cache_lock);
        status = g_DataPool.powerCalibrationStatus;
        generation = power_calibration_status_generation;
        portEXIT_CRITICAL(&input_cache_lock);
        if (generation != seen_generation) {
            seen_generation = generation;
            if (status != previous_status) {
                ESP_LOGI(TAG, "Power calibration status changed: %u",
                         (unsigned)status);
                previous_status = status;
            }
            if (status == 1U) {
                calibration_started = true;
                last_progress = xTaskGetTickCount();
            } else if (status == 2U && calibration_started) {
                ESP_LOGI(TAG, "Power calibration completed");
                return true;
            } else if (status == 3U) {
                ESP_LOGE(TAG, "Power calibration reported device timeout");
                return false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGE(TAG, "Power calibration status not confirmed running for %u ms",
             (unsigned)timeout_ms);
    return false;
}

bool DataCenterSetPowerMotorDirectionMode(uint8_t mode)
{
    if (mode > MODBUS_POWER_MOTOR_DIRECTION_REVERSE_ONLY) return false;
    portENTER_CRITICAL(&power_motor_direction_lock);
    power_motor_direction_mode = mode;
    portEXIT_CRITICAL(&power_motor_direction_lock);
    return true;
}

uint8_t DataCenterGetPowerMotorDirectionMode(void)
{
    uint8_t mode;
    portENTER_CRITICAL(&power_motor_direction_lock);
    mode = power_motor_direction_mode;
    portEXIT_CRITICAL(&power_motor_direction_lock);
    return mode;
}

void setPowerMotor(uint16_t gear, uint8_t direction)
{
    uint8_t direction_mode = DataCenterGetPowerMotorDirectionMode();
    if (direction_mode == MODBUS_POWER_MOTOR_DIRECTION_FORWARD_ONLY) {
        direction = MODBUS_DIRECTION_UP;
    } else if (direction_mode == MODBUS_POWER_MOTOR_DIRECTION_REVERSE_ONLY) {
        direction = MODBUS_DIRECTION_DOWN;
    }
    if (gear > MODBUS_POWER_MOTOR_GEAR_MAX) {
        gear = MODBUS_POWER_MOTOR_GEAR_MAX;
    }
#if NTC_TEMP_MOTOR_STOP_OPTIMIZATION_ENABLE
    portENTER_CRITICAL(&input_cache_lock);
    if (gear == 0U) {
        ntc_motor_stop_command_us = esp_timer_get_time();
        ntc_motor_stop_settle_samples_remaining =
            NTC_MOTOR_STOP_SETTLE_SAMPLES;
    } else {
        ntc_motor_stop_command_us = 0;
        ntc_motor_stop_settle_samples_remaining = 0U;
    }
    portEXIT_CRITICAL(&input_cache_lock);
#endif
    queue_power_channel(MB_POWER_HOLD_SIGNED_MOTOR_GEAR,
                        modbus_encode_signed_speed(gear, direction));
}

void setPowerHeater1(uint16_t gear)
{
    if (gear > MODBUS_POWER_HEATER_GEAR_MAX) {
        gear = MODBUS_POWER_HEATER_GEAR_MAX;
    }
    queue_power_channel(MB_POWER_HOLD_HEATER_1, gear);
}

void setPowerHeater2(uint16_t gear)
{
    if (gear > MODBUS_POWER_HEATER_GEAR_MAX) {
        gear = MODBUS_POWER_HEATER_GEAR_MAX;
    }
    queue_power_channel(MB_POWER_HOLD_HEATER_2, gear);
}

void setHighBroad(uint8_t value)
{
    uint8_t selector = (value >> 5) & 0x03U;
    uint8_t direction = (value >> 4) & 0x01U;
    uint8_t gear = value & 0x0fU;

    switch (selector) {
        case 1:
            setPowerHeater1(gear);
            break;
        case 2:
            setPowerHeater2(gear);
            break;
        case 3:
            setPowerMotor(gear, direction);
            break;
        default:
            setPowerHeater1(gear);
            setPowerHeater2(gear);
            break;
    }
}

void valveOn(void)
{
    queue_coil(MODBUS_SLAVE_ACTUATOR, MB_COIL_VALVE, true);
}

void valveOff(void)
{
    queue_coil(MODBUS_SLAVE_ACTUATOR, MB_COIL_VALVE, false);
}

void ledOn(void)
{
    queue_coil(MODBUS_SLAVE_ACTUATOR, MB_COIL_LIGHT, true);
}

void ledOff(void)
{
    queue_coil(MODBUS_SLAVE_ACTUATOR, MB_COIL_LIGHT, false);
}

char (*getVersion(void))[4]
{
    return version;
}
