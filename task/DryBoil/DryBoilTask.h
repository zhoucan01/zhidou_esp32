#ifndef DRY_BOIL_TASK_H
#define DRY_BOIL_TASK_H

#include <stdbool.h>
#include "esp_err.h"
#include "dry_boil_detector.h"

#define DRY_BOIL_TASK_STACK_SIZE 3072
#define DRY_BOIL_TASK_PRIORITY   17

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float temperature_c;
    uint8_t actual_power_0_13;
    bool temperature_valid;
} dry_boil_sample_t;

/* Callbacks run in the detection task. Keep them nonblocking and well below
 * 500 ms. Return false for unavailable/stale data. context must outlive task. */
typedef struct {
    bool (*read_sample)(dry_boil_sample_t *sample, void *context);
    void (*stop_heating)(void *context);
    void (*alarm)(const db_result_t *result, void *context); /* Optional. */
    void *context;
    bool (*ready)(void *context); /* Optional: wait for initial data in task. */
    /* Optional pair: accepted heating-command counter and stops that cannot
     * override a newer command. A new counter while latched starts a new cycle. */
    uint32_t (*heating_sequence)(void *context);
    void (*stop_for_sequence)(uint32_t sequence, void *context);
} dry_boil_port_t;

typedef struct {
    bool started;
    bool sample_valid;
    bool cycle_active;
    bool alarm_latched;
    bool reset_pending;
    dry_boil_sample_t sample;
    db_result_t result;
} dry_boil_status_t;

/* FreeRTOS entry: create once with xTaskCreate(). NULL selects the project's
 * lower-heater adapter. Otherwise argument points to a persistent port config.
 * Initial-data waiting happens here, after the scheduler starts running. */
void DryBoilTask(void *argument);

/* Create a single persistent task (3072-byte stack, priority 17).
 * Copies port; uses the supplied algorithm's unchanged default thresholds.
 * Duplicate starts return ESP_ERR_INVALID_STATE; never clear an alarm.
 * Call from task context with scheduling enabled, after data becomes available. */
esp_err_t DryBoilTaskStartWithPort(const dry_boil_port_t *port);

/* Request a new cycle while stopped, with valid temperature below 103 C.
 * ESP_OK means queued, not completed. Keep heating off until reset_pending is
 * false AND cycle_active/alarm_latched are false. Worker rechecks conditions;
 * unsafe requests are discarded without clearing the detector. */
esp_err_t DryBoilTaskReset(void);
void DryBoilTaskGetStatus(dry_boil_status_t *status);

#ifdef __cplusplus
}
#endif
#endif
