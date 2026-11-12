#include "DryBoilTask.h"
#include "DryBoilPort.h"

#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "DryBoil";
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
static dry_boil_status_t shared_status;
static dry_boil_port_t task_port;
static bool start_claimed;

static void dry_boil_run(void *argument)
{
    (void)argument;
    db_detector_t detector;
    db_config_t config;
    dry_boil_status_t status = {.started = true};
    const TickType_t period = pdMS_TO_TICKS((uint32_t)(DB_SAMPLE_PERIOD_S * 1000.0f));
    bool sample_gap = false;
    uint32_t alarm_sequence = 0;

    portENTER_CRITICAL(&status_lock);
    shared_status = status;
    portEXIT_CRITICAL(&status_lock);
    if (task_port.ready) {
        while (!task_port.ready(task_port.context)) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    TickType_t wake = xTaskGetTickCount();

    db_default_config(&config);
    db_init(&detector);
    for (;;) {
        /* Only a command accepted AFTER this alarm releases the latch.
         * An unchanged cached nonzero gear must never release it. */
        if (status.alarm_latched && task_port.heating_sequence &&
            task_port.heating_sequence(task_port.context) != alarm_sequence) {
            db_init(&detector);
            status.cycle_active = false;
            status.alarm_latched = false;
            memset(&status.result, 0, sizeof(status.result));
            sample_gap = false;
        }
        dry_boil_sample_t sample = {0};
        bool available = task_port.read_sample(&sample, task_port.context);
        bool valid = available && sample.temperature_valid &&
                     isfinite(sample.temperature_c) &&
                     sample.temperature_c >= -40.0f &&
                     sample.temperature_c <= 200.0f &&
                     sample.actual_power_0_13 <= 13u;
        bool reset;
        portENTER_CRITICAL(&status_lock);
        reset = shared_status.reset_pending;
        portEXIT_CRITICAL(&status_lock);

        if (reset && valid && sample.actual_power_0_13 == 0u &&
            sample.temperature_c < config.absolute_safety_temperature_c) {
            db_init(&detector);
            status.cycle_active = false;
            status.alarm_latched = false;
            memset(&status.result, 0, sizeof(status.result));
            sample_gap = false;
        }

        /* Start the learning clock at actual heating, not at task creation.
         * Zero-power intervals within a cycle must not clear history/latches. */
        if (valid && sample.actual_power_0_13 > 0u) {
            status.cycle_active = true;
        }
        if (status.cycle_active || !valid || status.alarm_latched ||
            sample.temperature_c >= config.absolute_safety_temperature_c) {
            status.result = db_step(&detector, &config, sample.temperature_c,
                                    sample.actual_power_0_13,
                                    valid && !sample_gap);
        }

        bool first_alarm = !status.alarm_latched &&
                           status.result.state == DB_DRY_CONFIRMED;
        if (first_alarm && task_port.heating_sequence) {
            alarm_sequence = task_port.heating_sequence(task_port.context);
        }
        status.alarm_latched = status.result.state == DB_DRY_CONFIRMED;
        status.sample = sample;
        status.sample_valid = valid;
        /* Preserve a reset request submitted during this sample. */
        portENTER_CRITICAL(&status_lock);
        status.reset_pending = reset ? false : shared_status.reset_pending;
        shared_status = status;
        portEXIT_CRITICAL(&status_lock);

        if (status.alarm_latched) {
            /* Existing heater APIs enqueue commands without an acknowledgement.
             * Retry every period; never automatically resume heating. */
            if (task_port.stop_for_sequence) {
                task_port.stop_for_sequence(alarm_sequence, task_port.context);
            } else {
                task_port.stop_heating(task_port.context);
            }
            if (first_alarm) {
                ESP_LOGE(TAG, "Protection latched: T=%.2f P=%u valid=%u gap=%u",
                         (double)sample.temperature_c,
                         (unsigned)sample.actual_power_0_13,
                         (unsigned)valid, (unsigned)sample_gap);
                if (task_port.alarm) {
                    task_port.alarm(&status.result, task_port.context);
                }
            }
        }

        /* Never fabricate rapid catch-up samples after a missed deadline.
         * An active cycle with a missed period fails closed on the next step. */
        sample_gap = xTaskDelayUntil(&wake, period) == pdFALSE;
        if (sample_gap) {
            wake = xTaskGetTickCount();
            vTaskDelayUntil(&wake, period);
        }
    }
}

static esp_err_t claim_port(const dry_boil_port_t *port)
{
    if (!port || !port->read_sample || !port->stop_heating ||
        (!!port->heating_sequence != !!port->stop_for_sequence)) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&status_lock);
    if (start_claimed) {
        portEXIT_CRITICAL(&status_lock);
        return ESP_ERR_INVALID_STATE;
    }
    start_claimed = true;
    task_port = *port;
    portEXIT_CRITICAL(&status_lock);
    return ESP_OK;
}

void DryBoilTask(void *argument)
{
    dry_boil_port_t port;
    if (argument) {
        port = *(const dry_boil_port_t *)argument;
    } else {
        DryBoilPortInit(&port);
    }
    esp_err_t error = claim_port(&port);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Cannot run dry-boil task: %d", (int)error);
        vTaskDelete(NULL);
        return;
    }
    dry_boil_run(NULL);
}

esp_err_t DryBoilTaskStartWithPort(const dry_boil_port_t *port)
{
    esp_err_t error = claim_port(port);
    if (error != ESP_OK) return error;
    if (xTaskCreate(dry_boil_run, "DryBoil", DRY_BOIL_TASK_STACK_SIZE,
                    NULL, DRY_BOIL_TASK_PRIORITY, NULL) != pdPASS) {
        portENTER_CRITICAL(&status_lock);
        start_claimed = false;
        portEXIT_CRITICAL(&status_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t DryBoilTaskReset(void)
{
    db_config_t config;
    db_default_config(&config);
    portENTER_CRITICAL(&status_lock);
    if (!shared_status.started || !shared_status.sample_valid ||
        shared_status.sample.actual_power_0_13 != 0u ||
        shared_status.sample.temperature_c >= config.absolute_safety_temperature_c) {
        portEXIT_CRITICAL(&status_lock);
        return ESP_ERR_INVALID_STATE;
    }
    shared_status.reset_pending = true;
    portEXIT_CRITICAL(&status_lock);
    return ESP_OK;
}

void DryBoilTaskGetStatus(dry_boil_status_t *status)
{
    if (status) {
        portENTER_CRITICAL(&status_lock);
        *status = shared_status;
        portEXIT_CRITICAL(&status_lock);
    }
}
