#include "temperature_mapping.h"

#include <stddef.h>

typedef struct {
    float adc;
    float temperature_c;
} temperature_mapping_point_t;

/*
 * Calibration: 2026-10-09, machine n1, data set 0 (26-10-9-n1-0).
 * ADC source: Station 2 NTC1 ADC high word (16-bit decimal, Col[47]).
 * Reference: external TXT sensor (1.txt), 832 time-aligned pairs.
 * Method: 4th-order polynomial fit on ln(ADC/(4095-ADC)) voltage-divider
 *         transform, anchored at known endpoints (1613 -> 101.2 C,
 *         3655 -> 27.3 C).  85-entry lookup table at 25-ADC intervals
 *         with linear interpolation.
 *
 * ADC decreases as temperature increases (NTC thermistor).
 *
 * Fit quality (ADC 1600-3300, 37 calibration points):
 *   R^2 = 0.998    max error = 1.11 C
 *
 * Validation:
 *   Run1 RMSE = 3.40 C (includes thermal-lag scatter during ramps)
 *   Run2 high-temp RMSE = 1.61 C (ADC < 1750, T > 90 C)
 *   Run2 full RMSE = 19.42 C (thermal lag: NTC and reference
 *     thermometer not synchronised at start of run)
 *   Run3 independent validation (778 pairs, not used in calibration):
 *     80-100 C: RMSE 0.44-1.08 C, mean bias ~0 C (excellent)
 *     40-70 C: RMSE 11-27 C (thermal lag, NTC faster than reference)
 *   Run3 ADC range 1607-3416, 100% within table coverage
 *
 * Note: heating/stable/cooling tables currently share the same
 * calibration curve (Run1 heating data).  When per-direction data
 * becomes available, replace the individual tables.
 */
static const temperature_mapping_point_t k_heating_table[] = {
    {1600.0f, 101.1f}, {1625.0f, 100.4f}, {1650.0f,  99.7f},
    {1675.0f,  99.0f}, {1700.0f,  98.4f}, {1725.0f,  97.7f},
    {1750.0f,  97.1f}, {1775.0f,  96.4f}, {1800.0f,  95.8f},
    {1825.0f,  95.2f}, {1850.0f,  94.6f}, {1875.0f,  94.0f},
    {1900.0f,  93.4f}, {1925.0f,  92.9f}, {1950.0f,  92.3f},
    {1975.0f,  91.8f}, {2000.0f,  91.3f}, {2025.0f,  90.8f},
    {2050.0f,  90.3f}, {2075.0f,  89.8f}, {2100.0f,  89.4f},
    {2125.0f,  89.0f}, {2150.0f,  88.5f}, {2175.0f,  88.1f},
    {2200.0f,  87.7f}, {2225.0f,  87.4f}, {2250.0f,  87.0f},
    {2275.0f,  86.6f}, {2300.0f,  86.3f}, {2325.0f,  86.0f},
    {2350.0f,  85.7f}, {2375.0f,  85.4f}, {2400.0f,  85.1f},
    {2425.0f,  84.8f}, {2450.0f,  84.6f}, {2475.0f,  84.3f},
    {2500.0f,  84.1f}, {2525.0f,  83.9f}, {2550.0f,  83.7f},
    {2575.0f,  83.5f}, {2600.0f,  83.3f}, {2625.0f,  83.1f},
    {2650.0f,  82.9f}, {2675.0f,  82.7f}, {2700.0f,  82.6f},
    {2725.0f,  82.4f}, {2750.0f,  82.2f}, {2775.0f,  82.1f},
    {2800.0f,  81.9f}, {2825.0f,  81.8f}, {2850.0f,  81.6f},
    {2875.0f,  81.4f}, {2900.0f,  81.2f}, {2925.0f,  81.1f},
    {2950.0f,  80.8f}, {2975.0f,  80.6f}, {3000.0f,  80.4f},
    {3025.0f,  80.1f}, {3050.0f,  79.8f}, {3075.0f,  79.5f},
    {3100.0f,  79.1f}, {3125.0f,  78.7f}, {3150.0f,  78.2f},
    {3175.0f,  77.7f}, {3200.0f,  77.1f}, {3225.0f,  76.4f},
    {3250.0f,  75.6f}, {3275.0f,  74.8f}, {3300.0f,  73.8f},
    {3325.0f,  72.6f}, {3350.0f,  71.3f}, {3375.0f,  69.8f},
    {3400.0f,  68.1f}, {3425.0f,  66.2f}, {3450.0f,  64.0f},
    {3475.0f,  61.4f}, {3500.0f,  58.5f}, {3525.0f,  55.2f},
    {3550.0f,  51.3f}, {3575.0f,  46.9f}, {3600.0f,  41.8f},
    {3625.0f,  35.8f}, {3650.0f,  28.8f}, {3675.0f,  20.7f},
    {3700.0f,  11.1f},
};

/* Stable and cooling tables share the same curve until per-direction
 * calibration data is available. */
static const temperature_mapping_point_t k_stable_table[] = {
    {1600.0f, 101.1f}, {1625.0f, 100.4f}, {1650.0f,  99.7f},
    {1675.0f,  99.0f}, {1700.0f,  98.4f}, {1725.0f,  97.7f},
    {1750.0f,  97.1f}, {1775.0f,  96.4f}, {1800.0f,  95.8f},
    {1825.0f,  95.2f}, {1850.0f,  94.6f}, {1875.0f,  94.0f},
    {1900.0f,  93.4f}, {1925.0f,  92.9f}, {1950.0f,  92.3f},
    {1975.0f,  91.8f}, {2000.0f,  91.3f}, {2025.0f,  90.8f},
    {2050.0f,  90.3f}, {2075.0f,  89.8f}, {2100.0f,  89.4f},
    {2125.0f,  89.0f}, {2150.0f,  88.5f}, {2175.0f,  88.1f},
    {2200.0f,  87.7f}, {2225.0f,  87.4f}, {2250.0f,  87.0f},
    {2275.0f,  86.6f}, {2300.0f,  86.3f}, {2325.0f,  86.0f},
    {2350.0f,  85.7f}, {2375.0f,  85.4f}, {2400.0f,  85.1f},
    {2425.0f,  84.8f}, {2450.0f,  84.6f}, {2475.0f,  84.3f},
    {2500.0f,  84.1f}, {2525.0f,  83.9f}, {2550.0f,  83.7f},
    {2575.0f,  83.5f}, {2600.0f,  83.3f}, {2625.0f,  83.1f},
    {2650.0f,  82.9f}, {2675.0f,  82.7f}, {2700.0f,  82.6f},
    {2725.0f,  82.4f}, {2750.0f,  82.2f}, {2775.0f,  82.1f},
    {2800.0f,  81.9f}, {2825.0f,  81.8f}, {2850.0f,  81.6f},
    {2875.0f,  81.4f}, {2900.0f,  81.2f}, {2925.0f,  81.1f},
    {2950.0f,  80.8f}, {2975.0f,  80.6f}, {3000.0f,  80.4f},
    {3025.0f,  80.1f}, {3050.0f,  79.8f}, {3075.0f,  79.5f},
    {3100.0f,  79.1f}, {3125.0f,  78.7f}, {3150.0f,  78.2f},
    {3175.0f,  77.7f}, {3200.0f,  77.1f}, {3225.0f,  76.4f},
    {3250.0f,  75.6f}, {3275.0f,  74.8f}, {3300.0f,  73.8f},
    {3325.0f,  72.6f}, {3350.0f,  71.3f}, {3375.0f,  69.8f},
    {3400.0f,  68.1f}, {3425.0f,  66.2f}, {3450.0f,  64.0f},
    {3475.0f,  61.4f}, {3500.0f,  58.5f}, {3525.0f,  55.2f},
    {3550.0f,  51.3f}, {3575.0f,  46.9f}, {3600.0f,  41.8f},
    {3625.0f,  35.8f}, {3650.0f,  28.8f}, {3675.0f,  20.7f},
    {3700.0f,  11.1f},
};

static const temperature_mapping_point_t k_cooling_table[] = {
    {1600.0f, 101.1f}, {1625.0f, 100.4f}, {1650.0f,  99.7f},
    {1675.0f,  99.0f}, {1700.0f,  98.4f}, {1725.0f,  97.7f},
    {1750.0f,  97.1f}, {1775.0f,  96.4f}, {1800.0f,  95.8f},
    {1825.0f,  95.2f}, {1850.0f,  94.6f}, {1875.0f,  94.0f},
    {1900.0f,  93.4f}, {1925.0f,  92.9f}, {1950.0f,  92.3f},
    {1975.0f,  91.8f}, {2000.0f,  91.3f}, {2025.0f,  90.8f},
    {2050.0f,  90.3f}, {2075.0f,  89.8f}, {2100.0f,  89.4f},
    {2125.0f,  89.0f}, {2150.0f,  88.5f}, {2175.0f,  88.1f},
    {2200.0f,  87.7f}, {2225.0f,  87.4f}, {2250.0f,  87.0f},
    {2275.0f,  86.6f}, {2300.0f,  86.3f}, {2325.0f,  86.0f},
    {2350.0f,  85.7f}, {2375.0f,  85.4f}, {2400.0f,  85.1f},
    {2425.0f,  84.8f}, {2450.0f,  84.6f}, {2475.0f,  84.3f},
    {2500.0f,  84.1f}, {2525.0f,  83.9f}, {2550.0f,  83.7f},
    {2575.0f,  83.5f}, {2600.0f,  83.3f}, {2625.0f,  83.1f},
    {2650.0f,  82.9f}, {2675.0f,  82.7f}, {2700.0f,  82.6f},
    {2725.0f,  82.4f}, {2750.0f,  82.2f}, {2775.0f,  82.1f},
    {2800.0f,  81.9f}, {2825.0f,  81.8f}, {2850.0f,  81.6f},
    {2875.0f,  81.4f}, {2900.0f,  81.2f}, {2925.0f,  81.1f},
    {2950.0f,  80.8f}, {2975.0f,  80.6f}, {3000.0f,  80.4f},
    {3025.0f,  80.1f}, {3050.0f,  79.8f}, {3075.0f,  79.5f},
    {3100.0f,  79.1f}, {3125.0f,  78.7f}, {3150.0f,  78.2f},
    {3175.0f,  77.7f}, {3200.0f,  77.1f}, {3225.0f,  76.4f},
    {3250.0f,  75.6f}, {3275.0f,  74.8f}, {3300.0f,  73.8f},
    {3325.0f,  72.6f}, {3350.0f,  71.3f}, {3375.0f,  69.8f},
    {3400.0f,  68.1f}, {3425.0f,  66.2f}, {3450.0f,  64.0f},
    {3475.0f,  61.4f}, {3500.0f,  58.5f}, {3525.0f,  55.2f},
    {3550.0f,  51.3f}, {3575.0f,  46.9f}, {3600.0f,  41.8f},
    {3625.0f,  35.8f}, {3650.0f,  28.8f}, {3675.0f,  20.7f},
    {3700.0f,  11.1f},
};

static float lookup(const temperature_mapping_point_t *table,
                    size_t count, uint16_t adc_raw)
{
    const float adc = (float)adc_raw;
    size_t low = 0U;
    size_t high = count - 1U;

    if (adc <= table[0].adc)
        return table[0].temperature_c;
    if (adc >= table[high].adc)
        return table[high].temperature_c;
    while (high - low > 1U) {
        const size_t middle = low + (high - low) / 2U;
        if (adc < table[middle].adc)
            high = middle;
        else
            low = middle;
    }
    return table[low].temperature_c +
           (adc - table[low].adc) *
           (table[high].temperature_c - table[low].temperature_c) /
           (table[high].adc - table[low].adc);
}

float temperature_mapping_from_adc(uint16_t adc_raw)
{
    return lookup(k_stable_table,
                  sizeof(k_stable_table) / sizeof(k_stable_table[0]),
                  adc_raw);
}

float temperature_mapping_from_adc_history(
    const uint16_t adc_history[TEMPERATURE_MAPPING_HISTORY_COUNT],
    uint32_t sample_period_ms)
{
    const float period =
        (sample_period_ms == 0U) ? (float)TEMPERATURE_MAPPING_SAMPLE_PERIOD_MS
                                 : (float)sample_period_ms;
    const float scale = period / (float)TEMPERATURE_MAPPING_SAMPLE_PERIOD_MS;
    const float slope =
        (((float)adc_history[TEMPERATURE_MAPPING_HISTORY_COUNT - 1U] -
          (float)adc_history[0]) /
         (float)(TEMPERATURE_MAPPING_HISTORY_COUNT - 1U)) * scale;
    const uint16_t current =
        adc_history[TEMPERATURE_MAPPING_HISTORY_COUNT - 1U];

    if (slope < -0.2f)
        return lookup(k_heating_table,
                      sizeof(k_heating_table) / sizeof(k_heating_table[0]),
                      current);
    if (slope > 0.2f)
        return lookup(k_cooling_table,
                      sizeof(k_cooling_table) / sizeof(k_cooling_table[0]),
                      current);
    return temperature_mapping_from_adc(current);
}
