#ifndef DRY_BOIL_DETECTOR_H
#define DRY_BOIL_DETECTOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 0.5 s sampling, 20 s regression window. Change both constants together. */
#define DB_SAMPLE_PERIOD_S 0.5f
#define DB_WINDOW_SAMPLES  41u

typedef enum {
    DB_LEARNING = 0,
    DB_WATER_LIKELY,
    DB_DRY_SUSPECTED,
    DB_DRY_CONFIRMED
} db_state_t;

typedef struct {
    /* 10.2 is the observed upper edge of the supplied 500 ml data at 35-75 C.
       Recalibrate with repeated minimum-water tests before production. */
    float wet_normalized_rate_c_min_p10;
    float suspect_ratio;
    float confirm_ratio;
    float ignore_first_s;
    float confirm_time_s;              /* Default 10 s: catches the measured P3/P6 dry-rise plateau. */
    float minimum_mean_power;          /* Default 3: P1/P2 remain outside the calibrated range. */
    float minimum_temperature_c;
    float maximum_decision_temperature_c;
    float absolute_safety_temperature_c;
} db_config_t;

typedef struct {
    float temperature[DB_WINDOW_SAMPLES];
    uint8_t power[DB_WINDOW_SAMPLES];
    uint8_t head;
    uint8_t count;
    uint16_t elapsed_ticks;
    uint16_t suspicious_ticks;
    uint8_t dry_latched;
} db_detector_t;

typedef struct {
    db_state_t state;
    float rise_rate_c_min;
    float mean_power;
    float normalized_rate_c_min_p10;
    float score;
} db_result_t;

void db_default_config(db_config_t *config);
void db_init(db_detector_t *detector);

/* Call exactly once every DB_SAMPLE_PERIOD_S. power_0_13 must be the actual
   applied heater level. A confirmed result remains latched until db_init(). */
db_result_t db_step(db_detector_t *detector, const db_config_t *config,
                    float temperature_c, uint8_t power_0_13,
                    uint8_t temperature_valid);

/* Simplified single-instance API for product loops.
   Call db_simple_reset() once when a new heating cycle starts, then the loop
   only needs db_is_dry_boil(). It returns 1 after dry-boil confirmation and
   remains 1 until reset. */
void db_simple_reset(void);
uint8_t db_is_dry_boil(float temperature_c, uint8_t actual_power_0_13);

#ifdef __cplusplus
}
#endif
#endif
