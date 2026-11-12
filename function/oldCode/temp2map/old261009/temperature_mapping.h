#ifndef A1_TEMPERATURE_MAPPING_H
#define A1_TEMPERATURE_MAPPING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ADC source: Station 2 NTC1 ADC high word (register 0x0000-0x0001,
 * 16-bit decimal value �?the real ADC count, NOT the 32-bit merge).
 *
 * Direction: ADC decreases as temperature increases (NTC thermistor).
 * Table range: 11.1 C .. 101.1 C.
 */
#define TEMPERATURE_MAPPING_ADC_MIN 1600U
#define TEMPERATURE_MAPPING_ADC_MAX 3700U
#define TEMPERATURE_MAPPING_HISTORY_COUNT 20U

/* Sampling cadence contract for the history-based conversion. Feed samples
 * at approximately this period; a gap longer than the reset threshold must
 * clear the history and restart collection. These values are part of the
 * calibration (the slope threshold assumes this cadence) and are tuned
 * together with the tables. */
#define TEMPERATURE_MAPPING_SAMPLE_PERIOD_MS 900U
#define TEMPERATURE_MAPPING_SAMPLE_RESET_MS  1200U

/*
 * Calibration data: 2026-10-09, machine n1, data set 0 (26-10-9-n1-0).
 * ADC source: Station 2 NTC1 ADC high word (16-bit decimal, Col[47]).
 * Reference: external TXT sensor (1.txt), 832 time-aligned pairs.
 * Method: 4th-order polynomial fit on ln(ADC/(4095-ADC)) voltage-divider
 *         transform, anchored at known endpoints (1613->101.2 C,
 *         3655->27.3 C).  85-entry lookup table at 25-ADC intervals
 *         with linear interpolation between entries.
 *
 * Accuracy (vs reference, ADC 1600-3300 reliable range):
 *   R^2 = 0.998    max error = 1.11 C
 *   Endpoint check: ADC=1613 -> 100.8 C (actual 101.2, err 0.4)
 *                   ADC=3655 -> 27.3 C  (actual 27.3,  err 0.0)
 *
 * Run3 independent validation (778 pairs, not used in calibration):
 *   80-100 C: RMSE 0.44-1.08 C, mean bias ~0 C (excellent)
 *   40-70 C: RMSE 11-27 C (thermal lag, NTC faster than reference)
 *
 * Note: heating/stable/cooling tables currently share the same
 * calibration curve.  When per-direction data becomes available,
 * replace the individual tables with direction-specific values.
 */

/* Near-stable conversion (no history). */
float temperature_mapping_from_adc(uint16_t adc_raw);

/*
 * Direction-aware conversion. Samples are ordered oldest to newest.
 * The slope is normalized to the calibration cadence, so any uniform
 * sampling period in [800, 1000] ms gives consistent direction
 * classification.
 */
float temperature_mapping_from_adc_history(
    const uint16_t adc_history[TEMPERATURE_MAPPING_HISTORY_COUNT],
    uint32_t sample_period_ms);

#ifdef __cplusplus
}
#endif

#endif
