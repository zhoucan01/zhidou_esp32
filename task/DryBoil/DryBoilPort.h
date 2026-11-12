#ifndef DRY_BOIL_PORT_H
#define DRY_BOIL_PORT_H

#include "DryBoilTask.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DRY_BOIL_HEATER_1 = 1, /* Lower heater only; upper heater may also run. */
    DRY_BOIL_HEATER_2 = 2, /* Heater 1 must be off. */
    DRY_BOIL_HEATERS_SAME_GEAR = 3 /* Both actual gears must match. */
} dry_boil_heater_mode_t;

/* Default adapter used by DryBoilTask(NULL): lower heater, with data readiness. */
void DryBoilPortInit(dry_boil_port_t *port);

/* Select the heating arrangement used to calibrate the supplied algorithm.
 * No default choice: the source files do not identify the calibrated heater.
 * Start after DataCenter has acquired both boards' first input snapshots.
 * Returns ESP_ERR_INVALID_STATE if snapshots are not ready or already started. */
esp_err_t DryBoilTaskStart(dry_boil_heater_mode_t mode);

#ifdef __cplusplus
}
#endif
#endif
