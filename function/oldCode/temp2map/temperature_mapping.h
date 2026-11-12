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

/*
 * Heater power level (gear) x temperature 2D compensation.
 *
 * Root cause (datasets 26-10-10 / 26-10-10-n1-1): the calibration
 * curve was fitted on full-power ramp data, where the NTC (mounted
 * close to the heater) runs hotter than the bulk liquid probed by the
 * reference thermometer.  At reduced heater gear the ramp is slow and
 * the NTC reads high by an amount depending on BOTH the gear and the
 * indicated temperature; near boiling convection removes the gradient
 * and the bias collapses along the right edge of every gear row.
 *
 * The 2D grid (6 gear anchors x 27 indicated-temperature nodes, 2.5 C
 * step over 35..100 C) was fitted on the pooled datasets 26-10-10
 * (1728 pairs, gear 8/9/10, 86-101 C) and 26-10-10-n1-1 (6725 pairs,
 * gear 0/8/9/10/19, 36-100 C).  Accuracy after compensation:
 * RMSE 0.70 C (26-10-10) and 0.72 C (26-10-10-n1-1), vs 5.94 C and
 * 12.36 C uncompensated - both datasets < 1.0 C target.
 *
 * Gear anchors: 0, 8, 9, 10, 18, 19.  Gear 18 has no paired data in
 * either dataset (the calibration firmware stopped recording gear);
 * its column is infilled between the gear-10 and gear-19 columns.
 * Gear 0 (heater off) IS compensated: the gear-0 row is measured on
 * the n1-1 soak/transition segments.  Gears between anchors are
 * bilinearly interpolated; gears below 0 clamp to the gear-0 row and
 * gears above 19 clamp to the gear-19 row.  Indicated temperatures
 * below 35 C clamp to the 35 C row, above 100 C to the 100 C row.
 */
#define TEMPERATURE_MAPPING_GEAR_MAX 19

/* 2D compensation grid geometry (see k_gear_temp_compensation in
 * temperature_mapping.c).  The row coordinate is the indicated
 * (pre-compensation) temperature from the lookup table. */
#define TEMPERATURE_MAPPING_COMP_TEMPERATURE_MIN   35.0f
#define TEMPERATURE_MAPPING_COMP_TEMPERATURE_STEP   2.5f
#define TEMPERATURE_MAPPING_COMP_TEMPERATURE_COUNT  27
#define TEMPERATURE_MAPPING_COMP_GEAR_COUNT          6

/*
 * Compensation semantics:
 *   comp(gear, T_indicated): bilinear over the gear x temperature grid
 *     described above; result temperature = lookup(adc) - comp.
 *   gear 0 and gear 19 anchor the grid; gear 1..18 interpolate.
 *   The single-sample gear API evaluates the near-stable lookup curve;
 *   the history gear API first classifies the direction (slope of the
 *   history) and selects heating/stable/cooling exactly like the
 *   legacy APIs.  Both then subtract the 2D compensation evaluated at
 *   the base (lookup) temperature, clamping the result at 0 C.
 */

/* Conversion with explicit heater gear (single sample). */
float temperature_mapping_from_adc_gear(uint16_t adc_raw, int gear);

/* Direction-aware conversion with explicit heater gear. */
float temperature_mapping_from_adc_history_gear(
    const uint16_t adc_history[TEMPERATURE_MAPPING_HISTORY_COUNT],
    uint32_t sample_period_ms, int gear);

#ifdef __cplusplus
}
#endif

#endif
