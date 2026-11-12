#include "ntcTemp.h"
#include "DataCenter.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "esp_attr.h"
#endif
#if defined(ESP_PLATFORM) && defined(CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY)
#define NTC_FILTER_STORAGE_ATTR EXT_RAM_BSS_ATTR
#else
#define NTC_FILTER_STORAGE_ATTR
#endif

#if MACHINE == 3
#include "esp_timer.h"
#endif

#if MACHINE == 3 && NTC_TEMP_LOG_ENABLE
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "ntcTemp";
static int64_t ntc_temp_last_log_us;
#endif

typedef struct {
    float adc;
    float temperature;
} NtcTempPoint;

typedef struct {
    int gear;
    const NtcTempPoint *points;
    size_t count;
    float min_adc;
    float max_adc;
} NtcTempGearCurve;

#if MACHINE == 3
typedef struct {
    uint8_t channel;
    uint8_t phase;
    uint8_t heater_gear;
    float heater_on_seconds;
    float heater_off_seconds;
    int16_t motor_gear;
    float motor_on_seconds;
    float motor_off_seconds;
    uint16_t water_volume_ml; /* Calibration water volume in mL; exact query match. */
    NtcTempPoint point;
} NtcMachine3OperatingPoint;

typedef struct {
    float temperature_below_c;
    NtcTempMachine3OperatingConditions conditions;
} NtcMachine3OperatingProfile;

static const NtcMachine3OperatingPoint machine3_operating_table[] = {
#include "tables/NTC3/ntcTemp_machine3_operating_table.inc"
};

/* Slot zero is not data. The new motor-cooling database is deliberately empty
 * until a measured 3 s on / 2 s off cooling run is supplied. */
static const NtcMachine3OperatingPoint machine3_motor_cooling_table[] = {
    {0},
#include "tables/NTC3/ntcTemp_machine3_motor_cooling_table.inc"
};
#define MACHINE3_MOTOR_COOLING_COUNT \
    (sizeof(machine3_motor_cooling_table) / \
     sizeof(machine3_motor_cooling_table[0]) - 1U)

static const NtcMachine3OperatingProfile machine3_heating_profiles[] = {
    {60.0f, {19U, 1, 1.0f, 0.0f, 2.0f, 6.0f, NTC_TEMP_MACHINE3_DEFAULT_WATER_VOLUME_ML}},
    {80.0f, {10U, 1, 1.0f, 0.0f, 3.0f, 2.0f, NTC_TEMP_MACHINE3_DEFAULT_WATER_VOLUME_ML}},
    {85.0f, {10U, 1, 6.0f, 4.0f, 3.0f, 3.0f, NTC_TEMP_MACHINE3_DEFAULT_WATER_VOLUME_ML}},
    {90.0f, {9U, 1, 6.0f, 15.0f, 4.0f, 3.0f, NTC_TEMP_MACHINE3_DEFAULT_WATER_VOLUME_ML}},
    {93.0f, {9U, 1, 6.0f, 4.0f, 3.0f, 3.0f, NTC_TEMP_MACHINE3_DEFAULT_WATER_VOLUME_ML}},
    {95.0f, {14U, 1, 1.0f, 0.0f, 3.0f, 0.5f, NTC_TEMP_MACHINE3_DEFAULT_WATER_VOLUME_ML}},
    {95.0f, {7U, 1, 5.0f, 5.0f, 3.0f, 3.0f, NTC_TEMP_MACHINE3_DEFAULT_WATER_VOLUME_ML}},
    {98.0f, {7U, 1, 6.0f, 4.0f, 3.0f, 3.0f, NTC_TEMP_MACHINE3_DEFAULT_WATER_VOLUME_ML}},
    {95.0f, {8U, 1, 6.0f, 4.0f, 2.0f, 6.0f, NTC_TEMP_MACHINE3_DEFAULT_WATER_VOLUME_ML}},
    {100.0f, {8U, 1, 6.0f, 4.0f, 2.0f, 6.0f, NTC_TEMP_MACHINE3_DEFAULT_WATER_VOLUME_ML}},
};

static uint8_t machine3_last_heater_gear;
static bool machine3_cooling_phase_latched[3];
static bool machine3_motor_stop_settle_override;
static int64_t machine3_last_heater_on_us;
#endif

#if MACHINE != 3 && MACHINE != 4
static const NtcTempPoint table[] = {
#if MACHINE == 1
#include "tables/NTC2/ntcTemp_table.inc"
#elif MACHINE == 2
#include "tables/NTC2/ntcTemp_machine2_table.inc"
#elif MACHINE == 4
#include "tables/NTC2/ntcTemp_machine4_table.inc"
#elif MACHINE == 6
#include "tables/NTC2/ntcTemp_machine6_table.inc"
#endif
};

#define TABLE_COUNT (sizeof(table) / sizeof(table[0]))
#endif

#if MACHINE != 3
static const NtcTempPoint ntc1_table[] = {
#include "tables/NTC2/ntcTemp_ntc1_table.inc"
};

#define NTC1_TABLE_COUNT (sizeof(ntc1_table) / sizeof(ntc1_table[0]))

#if MACHINE == 4
static const NtcTempPoint ntc3_table[] = {
#include "tables/NTC3/ntcTemp_machine4_ntc3_table.inc"
};

#define NTC3_TABLE_COUNT (sizeof(ntc3_table) / sizeof(ntc3_table[0]))
#endif
#endif

#if MACHINE == 3
#define NTC_CHANNEL_COUNT 3U
#define NTC_MEDIAN_WINDOW_SIZE 20U
#define NTC_ADC_JUMP_LIMIT 200U
#define NTC_ADC_CANDIDATE_COUNT 8U
#define NTC_ADC_CANDIDATE_SPAN 40U
#define NTC_CANDIDATE_MIN_US 140000LL
/* Recovery candidates can arrive slowly after motor/transport sampling gaps.
 * Keep the eight-point confirmation, but accept a 1 Hz stream (up to a 2 s
 * gap) instead of resetting it on every interval longer than 100 ms. */
#define NTC_CANDIDATE_MAX_GAP_US 2000000LL
#define NTC_TREND_MAX_ELAPSED_S 5.0f
#define NTC_ADC_MAX_VALID 4094U
#define NTC_TEMP_TREND_WINDOW_SIZE 100U
#define NTC_TEMP_TREND_FIT_POINTS 10U
#define NTC_TEMP_TREND_SLOPE_COUNT \
    ((NTC_TEMP_TREND_FIT_POINTS * (NTC_TEMP_TREND_FIT_POINTS - 1U)) / 2U)
#define NTC_TEMP_TREND_PERSISTENT_SAMPLES 8U
#define NTC_TEMP_CANDIDATE_SPAN_C 0.2f
#define NTC_TEMP_TREND_SIGMA_MULTIPLIER 3.0f
#define NTC_TEMP_TREND_MIN_THRESHOLD_C 0.5f
#define NTC_TEMP_TREND_MAX_THRESHOLD_C 2.0f
#define NTC_TEMP_MAX_PLAUSIBLE_STEP_C_PER_SECOND 5.0f
#define NTC_ADC_CANDIDATE_RATE_PER_SECOND 1000.0f
#define NTC_HEATING_RECENT_US 10000000LL
#define NTC_MOTOR_ADC_TREND_MIN_POINTS 8U
#define NTC_MOTOR_ADC_TREND_TOLERANCE 40.0f
#define NTC_MOTOR_RECOVERY_MIN_US 500000LL
#define NTC_STOP_EVIDENCE_MAX_GAP_US 6000000LL

#if NTC_TEMP_TREND_WINDOW_SIZE < NTC_TEMP_TREND_FIT_POINTS || \
    NTC_TEMP_TREND_FIT_POINTS < 3U || \
    NTC_TEMP_TREND_PERSISTENT_SAMPLES < 2U
#error "Temperature trend filter configuration is invalid"
#endif

typedef struct {
    float samples[NTC_TEMP_TREND_WINDOW_SIZE];
    int64_t times_us[NTC_TEMP_TREND_WINDOW_SIZE];
    uint16_t sample_count;
    uint16_t sample_next;
    float candidate_samples[NTC_TEMP_TREND_PERSISTENT_SAMPLES];
    uint8_t candidate_count;
    float candidate_min;
    float candidate_max;
    int64_t candidate_start_us;
    int64_t candidate_last_us;
    int candidate_direction;
} NtcTempTrendFilter;

typedef struct {
    uint32_t samples[NTC_ADC_CANDIDATE_COUNT];
    uint8_t count;
    uint32_t minimum;
    uint32_t maximum;
    int64_t start_us;
    int64_t last_us;
    int direction;
    bool stopped_evidence;
} NtcAdcCandidate;

typedef struct {
    uint32_t samples[NTC_ADC_CANDIDATE_COUNT];
    int64_t times_us[NTC_ADC_CANDIDATE_COUNT];
    unsigned count;
    int64_t started_us;
    uint32_t minimum, maximum;
} NtcMotorRecovery;

static float machine3_last_valid_temperatures[NTC_CHANNEL_COUNT];
static bool machine3_temperature_valid[NTC_CHANNEL_COUNT];

static uint32_t ntc_adc_windows[NTC_CHANNEL_COUNT][NTC_MEDIAN_WINDOW_SIZE];
static int64_t ntc_adc_window_times[NTC_CHANNEL_COUNT][NTC_MEDIAN_WINDOW_SIZE];
static uint8_t ntc_adc_window_counts[NTC_CHANNEL_COUNT];
static uint8_t ntc_adc_window_next[NTC_CHANNEL_COUNT];
/* History is only accessed by ordinary task code; no ISR/DMA access. */
static NTC_FILTER_STORAGE_ATTR NtcTempTrendFilter ntc_temperature_filters[NTC_CHANNEL_COUNT];
static NtcAdcCandidate ntc_adc_candidates[NTC_CHANNEL_COUNT];
static NtcMotorRecovery ntc_motor_recovery[NTC_CHANNEL_COUNT];
static bool ntc_motor_adc_authorized[NTC_CHANNEL_COUNT];
static bool ntc_adc_sample_accepted[NTC_CHANNEL_COUNT];
static bool ntc_temperature_sample_accepted[NTC_CHANNEL_COUNT];
static int64_t ntc_adc_last_seen_us[NTC_CHANNEL_COUNT];
static bool machine3_phase_initialized[NTC_CHANNEL_COUNT];
static bool machine3_heating_phase[NTC_CHANNEL_COUNT];
static bool machine3_conditions_valid[NTC_CHANNEL_COUNT];
static NtcTempMachine3OperatingConditions machine3_previous_conditions[NTC_CHANNEL_COUNT];
static bool machine3_motor_adc_matches(unsigned channel, uint32_t adc, int64_t now_us);

static bool machine3_channel_adc_is_rising(unsigned channel)
{
    if (ntc_adc_window_counts[channel] < NTC_MEDIAN_WINDOW_SIZE) {
        return false;
    }

    uint64_t first_half_sum = 0U;
    uint64_t second_half_sum = 0U;
    const unsigned oldest = ntc_adc_window_next[channel];
    for (unsigned index = 0U; index < NTC_MEDIAN_WINDOW_SIZE; ++index) {
        const uint32_t adc = ntc_adc_windows[channel]
            [(oldest + index) % NTC_MEDIAN_WINDOW_SIZE];
        if (adc == 0U) {
            return false;
        }
        if (index < NTC_MEDIAN_WINDOW_SIZE / 2U) {
            first_half_sum += adc;
        } else {
            second_half_sum += adc;
        }
    }
    /* The calibrated NTC curves have lower temperatures at higher ADCs. */
    return second_half_sum > first_half_sum;
}

static bool machine3_heating_requested(unsigned channel)
{
    if (!g_DataPool.powerEnabled) {
        machine3_last_heater_gear = 0U;
        machine3_cooling_phase_latched[channel] = false;
        machine3_last_heater_on_us = 0;
        return false;
    }

    if (g_DataPool.powerHeater1Enabled || g_DataPool.powerHeater2Enabled) {
        /* An enabled flag without a known gear is not evidence for any
         * heating profile (common before the first complete power poll). */
        if (g_DataPool.powerHeater1Gear == 0U &&
            g_DataPool.powerHeater2Gear == 0U &&
            machine3_last_heater_gear == 0U) {
            return false;
        }
        if (g_DataPool.powerHeater1Gear > machine3_last_heater_gear) {
            machine3_last_heater_gear = (uint8_t)g_DataPool.powerHeater1Gear;
        }
        if (g_DataPool.powerHeater2Gear > machine3_last_heater_gear) {
            machine3_last_heater_gear = (uint8_t)g_DataPool.powerHeater2Gear;
        }
        machine3_last_heater_on_us = esp_timer_get_time();
        machine3_cooling_phase_latched[channel] = false;
        return true;
    }

    const int64_t now_us = esp_timer_get_time();
    if (machine3_last_heater_on_us > 0 &&
        now_us >= machine3_last_heater_on_us &&
        now_us - machine3_last_heater_on_us <= NTC_HEATING_RECENT_US) {
        return true;
    }

    if (machine3_temperature_valid[channel] &&
        machine3_last_valid_temperatures[channel] >= 98.0f &&
        machine3_last_heater_gear > 0U &&
        machine3_channel_adc_is_rising(channel)) {
        machine3_cooling_phase_latched[channel] = true;
    }
    return machine3_last_heater_gear > 0U &&
           !machine3_cooling_phase_latched[channel];
}

static NtcTempMachine3OperatingConditions
machine3_default_operating_conditions(bool heating, unsigned channel);
static float machine3_channel_temperature(
    unsigned channel, uint32_t adc, bool heating,
    const NtcTempMachine3OperatingConditions *conditions);

static void machine3_profile_distance(const NtcMachine3OperatingPoint *p,
    uint8_t phase, const NtcTempMachine3OperatingConditions *conditions, float distance[5])
{
    distance[0] = fabsf((float)p->heater_gear - (phase ? conditions->power_gear : 0U));
    distance[1] = fabsf((float)p->motor_gear - conditions->motor_gear);
    distance[2] = fabsf(p->motor_on_seconds - conditions->motor_on_seconds) +
                  fabsf(p->motor_off_seconds - conditions->motor_off_seconds);
    distance[3] = fabsf((float)p->water_volume_ml - conditions->water_volume_ml);
    distance[4] = fabsf(p->heater_on_seconds - (phase ? conditions->heater_on_seconds : 0.0f)) +
                  fabsf(p->heater_off_seconds - (phase ? conditions->heater_off_seconds : 0.0f));
}

/* Lexicographic distance: nearest available power, motor gear/timings,
 * then water and heater timings.
 * Keep the first row on a complete tie for deterministic group selection. */
static const NtcMachine3OperatingPoint *machine3_nearest_profile(
    const NtcMachine3OperatingPoint *table, size_t count,
    unsigned channel, uint8_t phase,
    const NtcTempMachine3OperatingConditions *conditions)
{
    const NtcMachine3OperatingPoint *best = NULL;
    float best_distance[5] = {0};
    for (size_t i = 0; i < count; ++i) {
        const NtcMachine3OperatingPoint *p = &table[i];
        if (p->channel != channel || p->phase != phase) continue;
        float distance[5];
        machine3_profile_distance(p, phase, conditions, distance);
        bool closer = !best;
        if (best) {
            for (unsigned key = 0; key < 5; ++key) {
                if (distance[key] == best_distance[key]) continue;
                closer = distance[key] < best_distance[key];
                break;
            }
        }
        if (closer) {
            best = p;
            memcpy(best_distance, distance, sizeof(best_distance));
        }
    }
    return best;
}

static const NtcMachine3OperatingPoint *machine3_table_for_phase(
    unsigned channel, uint8_t phase,
    const NtcTempMachine3OperatingConditions *conditions, size_t *count)
{
    /* Once this channel has measured motor-cooling rows, do not mix their
     * interpolation or extrapolation with the old cooling profiles. */
    if (phase == 0U && conditions->motor_gear != 0 &&
        MACHINE3_MOTOR_COOLING_COUNT > 0U &&
        machine3_nearest_profile(machine3_motor_cooling_table + 1U,
            MACHINE3_MOTOR_COOLING_COUNT, channel, phase, conditions)) {
        *count = MACHINE3_MOTOR_COOLING_COUNT;
        return machine3_motor_cooling_table + 1U;
    }
    *count = sizeof(machine3_operating_table) /
             sizeof(machine3_operating_table[0]);
    return machine3_operating_table;
}

static bool machine3_same_profile(const NtcMachine3OperatingPoint *a,
                                  const NtcMachine3OperatingPoint *b)
{
    return a->channel == b->channel && a->phase == b->phase &&
        a->heater_gear == b->heater_gear &&
        a->water_volume_ml == b->water_volume_ml &&
        a->motor_gear == b->motor_gear &&
        a->heater_on_seconds == b->heater_on_seconds &&
        a->heater_off_seconds == b->heater_off_seconds &&
        a->motor_on_seconds == b->motor_on_seconds &&
        a->motor_off_seconds == b->motor_off_seconds;
}

/* Pick the closest eligible curve that actually brackets this ADC. Do not
 * select a closer but unusable curve and keep returning the previous value. */
static const NtcMachine3OperatingPoint *machine3_nearest_covered_profile(
    const NtcMachine3OperatingPoint *table, size_t count, unsigned channel,
    uint8_t phase, const NtcTempMachine3OperatingConditions *conditions, uint32_t adc)
{
    const NtcMachine3OperatingPoint *preferred = machine3_nearest_profile(
        table, count, channel, phase, conditions);
    if (preferred) {
        bool lower = false, upper = false;
        for (size_t i = 0; i < count; ++i) {
            if (!machine3_same_profile(preferred, &table[i])) continue;
            lower |= table[i].point.adc <= (float)adc;
            upper |= table[i].point.adc >= (float)adc;
        }
        if (lower && upper) return preferred;
    }
    const NtcMachine3OperatingPoint *best = NULL;
    float best_distance[5] = {0};
    for (size_t i = 0; i < count; ++i) {
        const NtcMachine3OperatingPoint *p = &table[i];
        if (p->channel != channel || p->phase != phase ||
            (i && machine3_same_profile(p, &table[i - 1U]))) continue;
        float distance[5];
        machine3_profile_distance(p, phase, conditions, distance);
        bool lower = false, upper = false;
        for (size_t j = 0; j < count; ++j) {
            if (!machine3_same_profile(p, &table[j])) continue;
            lower |= table[j].point.adc <= (float)adc;
            upper |= table[j].point.adc >= (float)adc;
        }
        if (!lower || !upper) continue;
        bool closer = !best;
        if (best) for (unsigned key = 0; key < 5; ++key) {
            if (distance[key] == best_distance[key]) continue;
            closer = distance[key] < best_distance[key]; break;
        }
        if (closer) { best = p; memcpy(best_distance, distance, sizeof(best_distance)); }
    }
    return best;
}

static bool machine3_operating_profile_contains(
    unsigned channel, uint8_t phase,
    const NtcTempMachine3OperatingConditions *conditions, uint32_t adc)
{
    size_t count;
    const NtcMachine3OperatingPoint *table = machine3_table_for_phase(
        channel, phase, conditions, &count);
    const NtcMachine3OperatingPoint *selected = machine3_nearest_covered_profile(
        table, count, channel, phase, conditions, adc);
    if (!selected) return false;
    float min_adc = INFINITY;
    float max_adc = -INFINITY;
    for (size_t i = 0U; i < count; ++i) {
        const NtcMachine3OperatingPoint *point = &table[i];
        if (machine3_same_profile(point, selected)) {
            if (point->point.adc < min_adc) {
                min_adc = point->point.adc;
            }
            if (point->point.adc > max_adc) {
                max_adc = point->point.adc;
            }
        }
    }
    return (float)adc >= min_adc && (float)adc <= max_adc;
}

static void machine3_reset_channel_filter(unsigned channel)
{
    memset(ntc_adc_windows[channel], 0, sizeof(ntc_adc_windows[channel]));
    memset(ntc_adc_window_times[channel], 0, sizeof(ntc_adc_window_times[channel]));
    ntc_adc_window_counts[channel] = ntc_adc_window_next[channel] = 0U;
    memset(&ntc_temperature_filters[channel], 0, sizeof(ntc_temperature_filters[channel]));
    memset(&ntc_adc_candidates[channel], 0, sizeof(ntc_adc_candidates[channel]));
    memset(&ntc_motor_recovery[channel], 0, sizeof(ntc_motor_recovery[channel]));
    ntc_motor_adc_authorized[channel] = false;
    ntc_adc_sample_accepted[channel] = false;
    ntc_temperature_sample_accepted[channel] = false;
    ntc_adc_last_seen_us[channel] = 0;
    machine3_phase_initialized[channel] = false;
    machine3_conditions_valid[channel] = false;
}

void ntcTempResetFilter(void)
{
    for (unsigned channel = 0; channel < NTC_CHANNEL_COUNT; ++channel)
        machine3_reset_channel_filter(channel);
}

static void machine3_reset_channel_trend(unsigned channel)
{
    NtcTempTrendFilter *filter = &ntc_temperature_filters[channel];
    if (filter->sample_count > 0U) {
        const unsigned last = (filter->sample_next +
            NTC_TEMP_TREND_WINDOW_SIZE - 1U) % NTC_TEMP_TREND_WINDOW_SIZE;
        const float value = filter->samples[last];
        const int64_t time_us = filter->times_us[last];
        memset(filter, 0, sizeof(*filter));
        filter->samples[0] = value;
        filter->times_us[0] = time_us;
        filter->sample_count = 1U;
        filter->sample_next = 1U;
    }
    ntc_adc_candidates[channel].count = 0U;
}
#else
void ntcTempResetFilter(void)
{
}
#endif

void ntcTempResetTemperatureTrend(void)
{
#if MACHINE == 3
    /* Discard pending evidence at a motor boundary. Retain one real accepted
     * anchor so the next burst cannot bypass validation as a fresh startup. */
    for (unsigned channel = 0; channel < NTC_CHANNEL_COUNT; ++channel) {
        machine3_reset_channel_trend(channel);
    }
#endif
}

void ntcTempSetMotorStopSettleOverride(bool enabled)
{
#if MACHINE == 3
    machine3_motor_stop_settle_override = enabled;
#else
    (void)enabled;
#endif
}

#if MACHINE == 1
/* 1号机专用：ADC × 有效加热档位二维标定表。 */
#include "tables/NTC2/ntcTemp_2d_table.inc"

#define NTC2_GEAR_TABLE_COUNT \
    (sizeof(ntc2_gear_tables) / sizeof(ntc2_gear_tables[0]))
#endif

bool ntcTempIsInRange(uint32_t adc)
{
#if MACHINE == 3
    const unsigned channel = NTC_TEMP_MACHINE3_PRIMARY_CHANNEL - 1U;
    const bool heating = machine3_heating_requested(channel);
    const NtcTempMachine3OperatingConditions conditions =
        machine3_default_operating_conditions(heating, channel);
    if (NTC_TEMP_MOTOR_STOP_OPTIMIZATION_ENABLE &&
        g_DataPool.powerMotorRpm != 0U &&
        !machine3_motor_stop_settle_override &&
        !machine3_motor_adc_matches(channel, adc, esp_timer_get_time())) {
        return false;
    }
    return machine3_operating_profile_contains(
        channel, heating ? 1U : 0U, &conditions, adc) ||
        (!heating && isfinite(machine3_channel_temperature(
            channel, adc, false, &conditions)));
    #elif MACHINE == 4
        const double value = (double)adc;
        return value >= ntc3_table[0].adc &&
            value <= ntc3_table[NTC3_TABLE_COUNT - 1U].adc;
#else
    const double value = (double)adc;
    if (value >= table[0].adc && value <= table[TABLE_COUNT - 1U].adc) {
        return true;
    }
#if MACHINE == 1
    return value >= NTC2_CALIBRATED_ADC_MIN &&
           value <= NTC2_CALIBRATED_ADC_MAX;
#else
    return false;
#endif
#endif
}

bool ntcTempNtc1IsInRange(uint32_t adc)
{
#if MACHINE == 3
    const bool heating = machine3_heating_requested(0U);
    const NtcTempMachine3OperatingConditions conditions =
        machine3_default_operating_conditions(heating, 0U);
    if (NTC_TEMP_MOTOR_STOP_OPTIMIZATION_ENABLE &&
        g_DataPool.powerMotorRpm != 0U &&
        !machine3_motor_stop_settle_override &&
        !machine3_motor_adc_matches(0U, adc, esp_timer_get_time())) {
        return false;
    }
    return machine3_operating_profile_contains(
        0U, heating ? 1U : 0U, &conditions, adc) ||
        (!heating && isfinite(machine3_channel_temperature(
            0U, adc, false, &conditions)));
#else
    return (double)adc >= ntc1_table[0].adc &&
           (double)adc <= ntc1_table[NTC1_TABLE_COUNT - 1U].adc;
#endif
}

#if MACHINE != 3
static float temperature_from_adc(const NtcTempPoint *points, size_t count,
                                  float adc)
{
    const float value = adc;
    if (value <= points[0].adc) {
        return points[0].temperature;
    }
    if (value >= points[count - 1U].adc) {
        return points[count - 1U].temperature;
    }

    size_t low = 0U;
    size_t high = count - 1U;
    while (high - low > 1U) {
        const size_t middle = low + (high - low) / 2U;
        if (value < points[middle].adc) {
            high = middle;
        } else {
            low = middle;
        }
    }

    const float fraction = (value - points[low].adc) /
                           (points[high].adc - points[low].adc);
    return points[low].temperature + fraction *
           (points[high].temperature - points[low].temperature);
}
#endif

static float normalize_output_temperature(float temperature)
{
    if (temperature < 25.0f) return 25.0f;
    if (temperature > 100.0f) return 100.0f;
    return temperature >= 97.9f && temperature < 98.0f
               ? 98.0f
               : temperature;
}

#if MACHINE == 3
static uint32_t median_adc_window(unsigned channel)
{
    uint32_t sorted[NTC_MEDIAN_WINDOW_SIZE];
    const unsigned count = ntc_adc_window_counts[channel];
    for (unsigned i = 0; i < count; ++i) {
        uint32_t value = ntc_adc_windows[channel][i];
        unsigned position = i;
        while (position > 0U && sorted[position - 1U] > value) {
            sorted[position] = sorted[position - 1U];
            --position;
        }
        sorted[position] = value;
    }
    if ((count & 1U) != 0U) {
        return sorted[count / 2U];
    }
    return (uint32_t)(((uint64_t)sorted[count / 2U - 1U] +
                       sorted[count / 2U]) / 2U);
}

/* Only this channel's accepted ADCs train the motor-running admission model. */
static bool machine3_motor_adc_matches(unsigned channel, uint32_t adc, int64_t now_us)
{
    const unsigned count = ntc_adc_window_counts[channel];
    if (adc == 0U || adc > NTC_ADC_MAX_VALID || count < NTC_MOTOR_ADC_TREND_MIN_POINTS)
        return false;
    const unsigned first = count == NTC_MEDIAN_WINDOW_SIZE ? ntc_adc_window_next[channel] : 0U;
    const unsigned last = (first + count - 1U) % NTC_MEDIAN_WINDOW_SIZE;
    const int64_t origin = ntc_adc_window_times[channel][first];
    const int64_t latest = ntc_adc_window_times[channel][last];
    if (latest - origin < NTC_CANDIDATE_MIN_US || now_us < latest ||
        now_us - latest > NTC_CANDIDATE_MAX_GAP_US) return false;
    float sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned index = (first + i) % NTC_MEDIAN_WINDOW_SIZE;
        const float x = (float)(ntc_adc_window_times[channel][index] - origin) / 1000000.0f;
        const float y = (float)ntc_adc_windows[channel][index];
        sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    const float denominator = (float)count * sxx - sx * sx;
    if (denominator <= 0.000001f) return false;
    const float slope = ((float)count * sxy - sx * sy) / denominator;
    if (fabsf(slope) > NTC_ADC_CANDIDATE_RATE_PER_SECOND) return false;
    const float intercept = (sy - slope * sx) / (float)count;
    /* A noisy history cannot authorize a new motor-running measurement. */
    for (unsigned i = 0; i < count; ++i) {
        const unsigned index = (first + i) % NTC_MEDIAN_WINDOW_SIZE;
        const float x = (float)(ntc_adc_window_times[channel][index] - origin) / 1000000.0f;
        if (fabsf((float)ntc_adc_windows[channel][index] - (intercept + slope * x)) >
            NTC_MOTOR_ADC_TREND_TOLERANCE) return false;
    }
    const float prediction = intercept + slope * (float)(now_us - origin) / 1000000.0f;
    return fabsf((float)adc - prediction) <= NTC_MOTOR_ADC_TREND_TOLERANCE &&
           fabsf((float)adc - (float)ntc_adc_windows[channel][last]) <= NTC_ADC_JUMP_LIMIT;
}

static bool machine3_recover_motor_adc(unsigned channel, uint32_t adc, int64_t now_us)
{
    NtcMotorRecovery *recovery = &ntc_motor_recovery[channel];
    const unsigned count = ntc_adc_window_counts[channel];
    if (count < NTC_MOTOR_ADC_TREND_MIN_POINTS || adc == 0U || adc > NTC_ADC_MAX_VALID) {
        recovery->count = 0U;
        return false;
    }
    const unsigned last = (ntc_adc_window_next[channel] + NTC_MEDIAN_WINDOW_SIZE - 1U) % NTC_MEDIAN_WINDOW_SIZE;
    const int64_t latest = ntc_adc_window_times[channel][last];
    if (now_us <= latest || now_us - latest <= NTC_CANDIDATE_MAX_GAP_US ||
        fabsf((float)adc - ntc_adc_windows[channel][last]) > NTC_ADC_JUMP_LIMIT) {
        recovery->count = 0U;
        return false;
    }
    if (recovery->count && now_us <= recovery->times_us[recovery->count - 1U]) {
        recovery->count = 0U;
        return false;
    }
    const uint32_t minimum = recovery->count && recovery->minimum < adc ? recovery->minimum : adc;
    const uint32_t maximum = recovery->count && recovery->maximum > adc ? recovery->maximum : adc;
    if (!recovery->count || now_us - recovery->times_us[recovery->count - 1U] > NTC_CANDIDATE_MAX_GAP_US ||
        maximum - minimum > NTC_ADC_CANDIDATE_SPAN) {
        recovery->count = 0U;
        recovery->started_us = now_us;
        recovery->minimum = recovery->maximum = adc;
    } else {
        recovery->minimum = minimum; recovery->maximum = maximum;
    }
    if (recovery->count == NTC_ADC_CANDIDATE_COUNT) {
        memmove(recovery->samples, recovery->samples + 1, (NTC_ADC_CANDIDATE_COUNT - 1U) * sizeof(recovery->samples[0]));
        memmove(recovery->times_us, recovery->times_us + 1, (NTC_ADC_CANDIDATE_COUNT - 1U) * sizeof(recovery->times_us[0]));
        --recovery->count;
    }
    recovery->samples[recovery->count] = adc;
    recovery->times_us[recovery->count++] = now_us;
    if (recovery->count < NTC_ADC_CANDIDATE_COUNT || now_us - recovery->started_us < NTC_MOTOR_RECOVERY_MIN_US)
        return false;
    memcpy(ntc_adc_windows[channel], recovery->samples, sizeof(recovery->samples));
    memcpy(ntc_adc_window_times[channel], recovery->times_us, sizeof(recovery->times_us));
    ntc_adc_window_counts[channel] = NTC_ADC_CANDIDATE_COUNT;
    ntc_adc_window_next[channel] = NTC_ADC_CANDIDATE_COUNT;
    ntc_adc_candidates[channel].count = 0U;
    recovery->count = 0U;
    return true;
}

static uint32_t update_adc_window(unsigned channel, uint32_t adc, int64_t now_us)
{
    NtcAdcCandidate *candidate = &ntc_adc_candidates[channel];
    ntc_adc_sample_accepted[channel] = false;
    ntc_motor_adc_authorized[channel] = false;
    const bool stopped = !NTC_TEMP_MOTOR_STOP_OPTIMIZATION_ENABLE ||
        g_DataPool.powerMotorRpm == 0U || machine3_motor_stop_settle_override;
    const bool initialized = ntc_adc_window_counts[channel] > 0U;
    const uint32_t median = initialized ? median_adc_window(channel) : 0U;
    if (!stopped && !machine3_motor_adc_matches(channel, adc, now_us)) {
        if (machine3_recover_motor_adc(channel, adc, now_us)) {
            ntc_motor_adc_authorized[channel] = ntc_adc_sample_accepted[channel] = true;
            ntc_adc_last_seen_us[channel] = now_us;
            return median_adc_window(channel);
        }
        /* Rejected running points do not count as stopped evidence. */
        if (candidate->count && (!candidate->stopped_evidence ||
            now_us - candidate->last_us > NTC_STOP_EVIDENCE_MAX_GAP_US)) candidate->count = 0U;
        return median;
    }
    ntc_motor_recovery[channel].count = 0U;
    if (initialized && now_us <= ntc_adc_last_seen_us[channel]) {
        candidate->count = 0U;
        return median;
    }
    ntc_adc_last_seen_us[channel] = now_us;
    if (adc == 0U || adc > NTC_ADC_MAX_VALID) {
        candidate->count = 0U;
        return median;
    }
    const uint32_t delta = adc > median ? adc - median : median - adc;
    if (initialized && delta > NTC_ADC_JUMP_LIMIT) {
        const int direction = adc > median ? 1 : -1;
        const uint32_t minimum = adc < candidate->minimum ? adc : candidate->minimum;
        const uint32_t maximum = adc > candidate->maximum ? adc : candidate->maximum;
        if (candidate->count == 0U || direction != candidate->direction ||
            stopped != candidate->stopped_evidence ||
            now_us - candidate->last_us > (stopped ? NTC_STOP_EVIDENCE_MAX_GAP_US : NTC_CANDIDATE_MAX_GAP_US) ||
            (stopped && candidate->count && now_us - candidate->last_us > NTC_CANDIDATE_MAX_GAP_US &&
             fabsf((float)adc - candidate->samples[candidate->count - 1U]) > NTC_ADC_CANDIDATE_SPAN) ||
            (float)(maximum - minimum) > NTC_ADC_CANDIDATE_SPAN +
                NTC_ADC_CANDIDATE_RATE_PER_SECOND *
                (float)(now_us - candidate->start_us) / 1000000.0f ||
            (candidate->count > 0U &&
             fabsf((float)adc - candidate->samples[candidate->count - 1U]) >
                NTC_ADC_CANDIDATE_SPAN + NTC_ADC_CANDIDATE_RATE_PER_SECOND *
                (float)(now_us - candidate->last_us) / 1000000.0f)) {
            candidate->count = 0U;
            candidate->minimum = candidate->maximum = adc;
            candidate->start_us = now_us;
            candidate->direction = direction;
            candidate->stopped_evidence = stopped;
        } else {
            candidate->minimum = minimum;
            candidate->maximum = maximum;
        }
        candidate->last_us = now_us;
        if (candidate->count == NTC_ADC_CANDIDATE_COUNT) {
            memmove(candidate->samples, candidate->samples + 1,
                    (NTC_ADC_CANDIDATE_COUNT - 1U) * sizeof(uint32_t));
            --candidate->count;
        }
        candidate->samples[candidate->count++] = adc;
        if (candidate->count < NTC_ADC_CANDIDATE_COUNT ||
            now_us - candidate->start_us < NTC_CANDIDATE_MIN_US) {
            return median; /* Never put rejected points or substitutes in history. */
        }
        uint32_t sorted[NTC_ADC_CANDIDATE_COUNT];
        memcpy(sorted, candidate->samples, sizeof(sorted));
        for (unsigned i = 1; i < NTC_ADC_CANDIDATE_COUNT; ++i) {
            uint32_t value = sorted[i];
            unsigned j = i;
            while (j > 0U && sorted[j - 1U] > value) {
                sorted[j] = sorted[j - 1U];
                --j;
            }
            sorted[j] = value;
        }
        const uint32_t center = (sorted[NTC_ADC_CANDIDATE_COUNT / 2U - 1U] +
                                 sorted[NTC_ADC_CANDIDATE_COUNT / 2U]) / 2U;
        for (unsigned i = 0; i < NTC_MEDIAN_WINDOW_SIZE; ++i) {
            ntc_adc_windows[channel][i] = center;
            ntc_adc_window_times[channel][i] = now_us;
        }
        ntc_adc_window_counts[channel] = NTC_MEDIAN_WINDOW_SIZE;
        ntc_adc_window_next[channel] = 0U;
        candidate->count = 0U;
        ntc_adc_sample_accepted[channel] = true;
        ntc_motor_adc_authorized[channel] = !stopped;
        return center; /* Temperature layer independently validates the new level. */
    }
    candidate->count = 0U; /* A normal point breaks all previous abnormal evidence. */
    ntc_adc_windows[channel][ntc_adc_window_next[channel]] = adc;
    ntc_adc_window_times[channel][ntc_adc_window_next[channel]] = now_us;
    ntc_adc_window_next[channel] =
        (uint8_t)((ntc_adc_window_next[channel] + 1U) % NTC_MEDIAN_WINDOW_SIZE);
    if (ntc_adc_window_counts[channel] < NTC_MEDIAN_WINDOW_SIZE)
        ++ntc_adc_window_counts[channel];
    ntc_adc_sample_accepted[channel] = true;
    ntc_motor_adc_authorized[channel] = !stopped;
    return median_adc_window(channel);
}

static float median_float(float *values, size_t count)
{
    for (size_t i = 1U; i < count; ++i) {
        const float value = values[i];
        size_t j = i;
        while (j > 0U && values[j - 1U] > value) {
            values[j] = values[j - 1U];
            --j;
        }
        values[j] = value;
    }
    if ((count & 1U) != 0U) {
        return values[count / 2U];
    }
    return (values[count / 2U - 1U] + values[count / 2U]) * 0.5f;
}

static float absolute_float(float value)
{
    return value < 0.0f ? -value : value;
}

static float machine3_history_value(const NtcTempTrendFilter *filter,
                                    size_t index)
{
    const size_t first = filter->sample_count == NTC_TEMP_TREND_WINDOW_SIZE
                             ? filter->sample_next : 0U;
    return filter->samples[(first + index) % NTC_TEMP_TREND_WINDOW_SIZE];
}

static void machine3_history_append(NtcTempTrendFilter *filter, float sample,
                                    int64_t now_us)
{
    filter->samples[filter->sample_next] = sample;
    filter->times_us[filter->sample_next] = now_us;
    filter->sample_next =
        (uint16_t)((filter->sample_next + 1U) % NTC_TEMP_TREND_WINDOW_SIZE);
    if (filter->sample_count < NTC_TEMP_TREND_WINDOW_SIZE) {
        ++filter->sample_count;
    }
}

static int64_t machine3_history_time(const NtcTempTrendFilter *filter,
                                     size_t index)
{
    const size_t first = filter->sample_count == NTC_TEMP_TREND_WINDOW_SIZE
                             ? filter->sample_next : 0U;
    return filter->times_us[(first + index) % NTC_TEMP_TREND_WINDOW_SIZE];
}

static void machine3_fit_robust_trend(const NtcTempTrendFilter *filter,
                                      float *prediction,
                                      float *residual_threshold, int64_t now_us)
{
    const size_t point_count = filter->sample_count <
            NTC_TEMP_TREND_FIT_POINTS
        ? filter->sample_count : NTC_TEMP_TREND_FIT_POINTS;
    float x[NTC_TEMP_TREND_FIT_POINTS];
    float y[NTC_TEMP_TREND_FIT_POINTS];
    float slopes[NTC_TEMP_TREND_SLOPE_COUNT];
    float intercepts[NTC_TEMP_TREND_FIT_POINTS];
    float residuals[NTC_TEMP_TREND_FIT_POINTS];
    float deviations[NTC_TEMP_TREND_FIT_POINTS];

    for (size_t i = 0U; i < point_count; ++i) {
        const size_t history_index = point_count == 1U ? 0U
            : i * (filter->sample_count - 1U) / (point_count - 1U);
        x[i] = (float)(machine3_history_time(filter, history_index) -
                       machine3_history_time(filter, 0U)) / 1000000.0f;
        y[i] = machine3_history_value(filter, history_index);
    }

    size_t slope_count = 0U;
    for (size_t i = 0U; i < point_count; ++i) {
        for (size_t j = i + 1U; j < point_count; ++j) {
            slopes[slope_count++] = (y[j] - y[i]) / (x[j] - x[i]);
        }
    }
    float slope = slope_count > 0U ? median_float(slopes, slope_count) : 0.0f;
    if (slope > NTC_TEMP_MAX_PLAUSIBLE_STEP_C_PER_SECOND) {
        slope = NTC_TEMP_MAX_PLAUSIBLE_STEP_C_PER_SECOND;
    } else if (slope < -NTC_TEMP_MAX_PLAUSIBLE_STEP_C_PER_SECOND) {
        slope = -NTC_TEMP_MAX_PLAUSIBLE_STEP_C_PER_SECOND;
    }
    for (size_t i = 0U; i < point_count; ++i) {
        intercepts[i] = y[i] - slope * x[i];
    }
    const float intercept = median_float(intercepts, point_count);
    for (size_t i = 0U; i < point_count; ++i) {
        residuals[i] = y[i] - (intercept + slope * x[i]);
    }
    const float residual_median = median_float(residuals, point_count);
    for (size_t i = 0U; i < point_count; ++i) {
        deviations[i] = absolute_float(residuals[i] - residual_median);
    }
    const float mad = median_float(deviations, point_count);
    float threshold = NTC_TEMP_TREND_SIGMA_MULTIPLIER * 1.4826f * mad;
    if (threshold < NTC_TEMP_TREND_MIN_THRESHOLD_C) {
        threshold = NTC_TEMP_TREND_MIN_THRESHOLD_C;
    }
    const float query_time = (float)(now_us - machine3_history_time(filter, 0U)) /
                             1000000.0f;
    *prediction = intercept + slope * query_time;
    const float last_sample =
        machine3_history_value(filter, filter->sample_count - 1U);
    float elapsed_s = (float)(now_us - machine3_history_time(
        filter, filter->sample_count - 1U)) / 1000000.0f;
    if (elapsed_s < 0.0f) elapsed_s = 0.0f;
    if (elapsed_s > NTC_TREND_MAX_ELAPSED_S) elapsed_s = NTC_TREND_MAX_ELAPSED_S;
    const float maximum_prediction_delta =
        NTC_TEMP_MAX_PLAUSIBLE_STEP_C_PER_SECOND * elapsed_s;
    const float prediction_delta = *prediction - last_sample;
    if (prediction_delta > maximum_prediction_delta) {
        *prediction = last_sample + maximum_prediction_delta;
    } else if (prediction_delta <
               -maximum_prediction_delta) {
        *prediction = last_sample - maximum_prediction_delta;
    }
    if (threshold > NTC_TEMP_TREND_MAX_THRESHOLD_C) {
        threshold = NTC_TEMP_TREND_MAX_THRESHOLD_C;
    }
    *residual_threshold = threshold;
}

static float machine3_trend_filter(unsigned channel, float sample, int64_t now_us)
{
    NtcTempTrendFilter *filter = &ntc_temperature_filters[channel];
    if (!isfinite(sample)) {
        filter->candidate_count = 0U;
        return filter->sample_count > 0U ?
            machine3_history_value(filter, filter->sample_count - 1U) : NAN;
    }
    if (filter->sample_count == 0U) {
        machine3_history_append(filter, sample, now_us);
        return sample;
    }
    const float last = machine3_history_value(filter, filter->sample_count - 1U);
    const int64_t last_us = machine3_history_time(filter, filter->sample_count - 1U);
    if (now_us <= last_us) {
        filter->candidate_count = 0U;
        return last;
    }
    float prediction;
    float residual_threshold;
    machine3_fit_robust_trend(filter, &prediction, &residual_threshold, now_us);
    const float residual = sample - prediction;
    if (absolute_float(residual) <= residual_threshold) {
        filter->candidate_count = 0U;
        machine3_history_append(filter, sample, now_us);
        return sample;
    }

    const int direction = residual > 0.0f ? 1 : -1;
    const float minimum = sample < filter->candidate_min ? sample : filter->candidate_min;
    const float maximum = sample > filter->candidate_max ? sample : filter->candidate_max;
    if (filter->candidate_count == 0U || direction != filter->candidate_direction ||
        now_us <= filter->candidate_last_us ||
        now_us - filter->candidate_last_us > NTC_STOP_EVIDENCE_MAX_GAP_US ||
        maximum - minimum > NTC_TEMP_CANDIDATE_SPAN_C +
            NTC_TEMP_MAX_PLAUSIBLE_STEP_C_PER_SECOND *
            (float)(now_us - filter->candidate_start_us) / 1000000.0f ||
        (filter->candidate_count > 0U &&
         absolute_float(sample - filter->candidate_samples[filter->candidate_count - 1U]) >
            NTC_TEMP_CANDIDATE_SPAN_C + NTC_TEMP_MAX_PLAUSIBLE_STEP_C_PER_SECOND *
            (float)(now_us - filter->candidate_last_us) / 1000000.0f)) {
        filter->candidate_count = 0U;
        filter->candidate_min = filter->candidate_max = sample;
        filter->candidate_start_us = now_us;
        filter->candidate_direction = direction;
    } else {
        filter->candidate_min = minimum;
        filter->candidate_max = maximum;
    }
    filter->candidate_last_us = now_us;
    if (filter->candidate_count == NTC_TEMP_TREND_PERSISTENT_SAMPLES) {
        memmove(filter->candidate_samples, filter->candidate_samples + 1,
                (NTC_TEMP_TREND_PERSISTENT_SAMPLES - 1U) * sizeof(float));
        --filter->candidate_count;
    }
    filter->candidate_samples[filter->candidate_count++] = sample;
    const float elapsed_s = (float)(now_us - last_us) / 1000000.0f;
    const float allowed_change = NTC_TEMP_TREND_MIN_THRESHOLD_C +
        NTC_TEMP_MAX_PLAUSIBLE_STEP_C_PER_SECOND * elapsed_s;
    if (filter->candidate_count >= NTC_TEMP_TREND_PERSISTENT_SAMPLES &&
        now_us - filter->candidate_start_us >= NTC_CANDIDATE_MIN_US &&
        absolute_float(filter->candidate_min - last) <= allowed_change &&
        absolute_float(filter->candidate_max - last) <= allowed_change) {
        float sorted[NTC_TEMP_TREND_PERSISTENT_SAMPLES];
        memcpy(sorted, filter->candidate_samples, sizeof(sorted));
        const float center = median_float(sorted, NTC_TEMP_TREND_PERSISTENT_SAMPLES);
        memset(filter, 0, sizeof(*filter));
        machine3_history_append(filter, center, now_us);
        return center;
    }
    /* Rejected measurements and extrapolated substitutes never train the model.
     * Do not advance its timestamp: elapsed time must describe real evidence. */
    return last;
}

static NtcTempMachine3OperatingConditions
machine3_default_operating_conditions(bool heating, unsigned channel)
{
    NtcTempMachine3OperatingConditions conditions = {
        0U, 1, 0.0f, 0.0f, 3.0f, 2.0f,
        NTC_TEMP_MACHINE3_DEFAULT_WATER_VOLUME_ML,
    };
    if (heating) {
        uint16_t heater_gear = 0U;
        if (g_DataPool.powerEnabled) {
            if (g_DataPool.powerHeater1Enabled &&
                g_DataPool.powerHeater1Gear > heater_gear) {
                heater_gear = g_DataPool.powerHeater1Gear;
            }
            if (g_DataPool.powerHeater2Enabled &&
                g_DataPool.powerHeater2Gear > heater_gear) {
                heater_gear = g_DataPool.powerHeater2Gear;
            }
        }
        if (heater_gear > 0U) {
            machine3_last_heater_gear = (uint8_t)heater_gear;
        } else {
            heater_gear = machine3_last_heater_gear;
        }
        const NtcMachine3OperatingProfile *nearest_profile = NULL;
        float nearest_distance = 0.0f;
        unsigned nearest_gear_distance = UINT16_MAX;
        for (size_t i = 0U;
             i < sizeof(machine3_heating_profiles) /
                 sizeof(machine3_heating_profiles[0]); ++i) {
            const NtcMachine3OperatingProfile *profile =
                &machine3_heating_profiles[i];
            const unsigned gear_distance = heater_gear >= profile->conditions.power_gear
                ? heater_gear - profile->conditions.power_gear
                : profile->conditions.power_gear - heater_gear;
            float distance = machine3_temperature_valid[channel]
                ? machine3_last_valid_temperatures[channel] - profile->temperature_below_c
                : 0.0f;
            if (distance < 0.0f) {
                distance = -distance;
            }
            if (!nearest_profile || gear_distance < nearest_gear_distance ||
                (gear_distance == nearest_gear_distance && distance < nearest_distance)) {
                nearest_profile = profile;
                nearest_distance = distance;
                nearest_gear_distance = gear_distance;
            }
        }
        if (nearest_profile) {
            conditions = nearest_profile->conditions;
            /* Keep the actual request so database gear distance remains honest. */
            conditions.power_gear = heater_gear;
            return conditions;
        }
        conditions.power_gear = heater_gear;
        conditions.motor_gear = 0;
        conditions.heater_on_seconds = -1.0f;
        conditions.heater_off_seconds = -1.0f;
        conditions.motor_on_seconds = -1.0f;
        conditions.motor_off_seconds = -1.0f;
    }
    return conditions;
}

static float machine3_channel_temperature(
    unsigned channel, uint32_t adc, bool heating,
    const NtcTempMachine3OperatingConditions *conditions)
{
    float selected_max_adc = -INFINITY;
    bool using_motor_cooling_table = false;
    if (!NTC_TEMP_MOTOR_STOP_OPTIMIZATION_ENABLE ||
        g_DataPool.powerMotorRpm == 0U ||
        machine3_motor_stop_settle_override ||
        (ntc_motor_adc_authorized[channel] && ntc_adc_sample_accepted[channel] &&
         adc == median_adc_window(channel)) ||
        machine3_motor_adc_matches(channel, adc, esp_timer_get_time())) {
        const uint8_t phase = heating ? 1U : 0U;
        size_t count;
        const NtcMachine3OperatingPoint *table = machine3_table_for_phase(
            channel, phase, conditions, &count);
        using_motor_cooling_table =
            table == machine3_motor_cooling_table + 1U;
        const NtcMachine3OperatingPoint *selected = machine3_nearest_covered_profile(
            table, count, channel, phase, conditions, adc);
        if (!selected) selected = machine3_nearest_profile(
            table, count, channel, phase, conditions);
        if (!selected) return NAN;

        const NtcMachine3OperatingPoint *lower = NULL;
        const NtcMachine3OperatingPoint *upper = NULL;
        const NtcMachine3OperatingPoint *first = NULL;
        const NtcMachine3OperatingPoint *second = NULL;
        const NtcMachine3OperatingPoint *penultimate = NULL;
        const NtcMachine3OperatingPoint *last = NULL;
        for (size_t i = 0U; i < count; ++i) {
            const NtcMachine3OperatingPoint *point = &table[i];
            if (!machine3_same_profile(point, selected)) {
                continue;
            }
            if (!first) {
                first = point;
            } else {
                /* A two-node curve already has a valid final segment. */
                penultimate = last ? last : first;
                last = point;
                if (!second) {
                    second = point;
                }
            }
            if (point->point.adc <= (float)adc) {
                lower = point;
            }
            if (point->point.adc >= (float)adc && !upper) {
                upper = point;
            }
        }
        if (last) selected_max_adc = last->point.adc;
        if (lower && upper) {
            if (lower == upper || lower->point.adc == upper->point.adc) {
                return lower->point.temperature;
            }
            const float fraction = ((float)adc - lower->point.adc) /
                (upper->point.adc - lower->point.adc);
            return lower->point.temperature + fraction *
                (upper->point.temperature - lower->point.temperature);
        }
        if (first && second && penultimate && last) {
            if (!lower && first->point.temperature >
                              second->point.temperature) {
                const float extended_adc = first->point.adc -
                    5.0f * (second->point.adc - first->point.adc) /
                    (first->point.temperature - second->point.temperature);
                if ((float)adc >= extended_adc) {
                    const float fraction = ((float)adc - first->point.adc) /
                        (second->point.adc - first->point.adc);
                    return first->point.temperature + fraction *
                        (second->point.temperature - first->point.temperature);
                }
            } else if (lower == last && !upper &&
                       penultimate->point.temperature >
                           last->point.temperature) {
                const float extended_adc = last->point.adc +
                    5.0f * (last->point.adc - penultimate->point.adc) /
                    (penultimate->point.temperature - last->point.temperature);
                if ((float)adc <= extended_adc) {
                    const float fraction = ((float)adc -
                                            penultimate->point.adc) /
                        (last->point.adc - penultimate->point.adc);
                    return penultimate->point.temperature + fraction *
                        (last->point.temperature -
                         penultimate->point.temperature);
                }
            }
        }
    }

    if (!heating && !using_motor_cooling_table &&
        (float)adc > selected_max_adc &&
        selected_max_adc > -INFINITY) {
        /* The measured cooling group ends around 74 C. For a colder boot,
         * prefer that group first, then use this machine's measured low-temp
         * heating profile only where it actually covers the ADC. */
        const NtcMachine3OperatingPoint *nearest = NULL;
        float nearest_distance = INFINITY;
        for (size_t i = 0U;
             i < sizeof(machine3_operating_table) /
                 sizeof(machine3_operating_table[0]); ++i) {
            const NtcMachine3OperatingPoint *point =
                &machine3_operating_table[i];
            if (point->channel != channel || point->phase != 1U ||
                point->water_volume_ml != conditions->water_volume_ml ||
                point->point.temperature >= 80.0f) {
                continue;
            }
            const float distance = fabsf(point->point.adc - (float)adc);
            if (distance < nearest_distance) {
                nearest = point;
                nearest_distance = distance;
            }
        }
        if (nearest) {
            const NtcTempMachine3OperatingConditions low_temp = {
                nearest->heater_gear, nearest->motor_gear,
                nearest->heater_on_seconds, nearest->heater_off_seconds,
                nearest->motor_on_seconds, nearest->motor_off_seconds,
                nearest->water_volume_ml,
            };
            return machine3_channel_temperature(channel, adc, true, &low_temp);
        }
    }
    return NAN;
}

static float machine3_temperature_from_three_ntcs(
    const NtcTempMachine3OperatingConditions *conditions)
{
    const int64_t sample_time_us = esp_timer_get_time();
    const uint32_t raw_adcs[NTC_CHANNEL_COUNT] = {
        g_DataPool.ntc1Raw, g_DataPool.ntc2Raw, g_DataPool.ntc3Raw,
    };
    float temperatures[NTC_CHANNEL_COUNT];
    for (unsigned channel = 0; channel < NTC_CHANNEL_COUNT; ++channel) {
        ntc_temperature_sample_accepted[channel] = false;
        const bool heating = machine3_heating_requested(channel);
        const NtcTempMachine3OperatingConditions selected = conditions ? *conditions :
            machine3_default_operating_conditions(heating, channel);
        if (!machine3_phase_initialized[channel] || heating != machine3_heating_phase[channel]) {
            machine3_reset_channel_filter(channel);
            machine3_heating_phase[channel] = heating;
            machine3_phase_initialized[channel] = true;
        }
        const NtcTempMachine3OperatingConditions *previous = &machine3_previous_conditions[channel];
        if (machine3_conditions_valid[channel] &&
            (selected.power_gear != previous->power_gear ||
             selected.motor_gear != previous->motor_gear ||
             selected.heater_on_seconds != previous->heater_on_seconds ||
             selected.heater_off_seconds != previous->heater_off_seconds ||
             selected.motor_on_seconds != previous->motor_on_seconds ||
             selected.motor_off_seconds != previous->motor_off_seconds ||
             selected.water_volume_ml != previous->water_volume_ml)) {
            machine3_reset_channel_trend(channel);
        }
        machine3_previous_conditions[channel] = selected;
        machine3_conditions_valid[channel] = true;
        const uint32_t filtered_adc = update_adc_window(channel, raw_adcs[channel], sample_time_us);
        const float converted_temperature = ntc_adc_sample_accepted[channel]
            ? machine3_channel_temperature(channel, filtered_adc, heating, &selected)
            : NAN;
        if (isfinite(converted_temperature)) {
            temperatures[channel] = machine3_trend_filter(
                channel, converted_temperature, sample_time_us);
            ntc_temperature_sample_accepted[channel] =
                machine3_history_time(&ntc_temperature_filters[channel],
                    ntc_temperature_filters[channel].sample_count - 1U) == sample_time_us;
            machine3_last_valid_temperatures[channel] = temperatures[channel];
            machine3_temperature_valid[channel] = true;
        } else {
            if (g_DataPool.powerMotorRpm == 0U ||
                raw_adcs[channel] == 0U || raw_adcs[channel] > NTC_ADC_MAX_VALID)
                ntc_temperature_filters[channel].candidate_count = 0U;
            temperatures[channel] = machine3_temperature_valid[channel]
                ? machine3_last_valid_temperatures[channel] : NAN;
        }
    }

#if NTC_TEMP_LOG_ENABLE
    const int64_t now_us = esp_timer_get_time();
    if (now_us - ntc_temp_last_log_us >= 1000000) {
        ESP_LOGI(TAG, "NTC temperatures: NTC1=%.2f C, NTC2=%.2f C, NTC3=%.2f C",
                 normalize_output_temperature(temperatures[0]),
                 normalize_output_temperature(temperatures[1]),
                 normalize_output_temperature(temperatures[2]));
        ntc_temp_last_log_us = now_us;
    }
#endif

    const unsigned primary = NTC_TEMP_MACHINE3_PRIMARY_CHANNEL - 1U;
    /* Keep the configured sensor as the sole application-temperature source.
     * Its own filter retains the last valid reading (or NAN before startup). */
    return temperatures[primary];
}

float ntcTempGetTemperatureWithConditions(
    NtcTempMachine3OperatingConditions conditions)
{
    const float temperature = machine3_temperature_from_three_ntcs(&conditions);
    return normalize_output_temperature(temperature);
}
#endif

#if MACHINE == 1
static float temperature_from_gear_curves(uint32_t adc, int gear)
{
    const float value = (float)adc;
    size_t lower = 0U;
    while (lower + 1U < NTC2_GEAR_TABLE_COUNT &&
           gear > ntc2_gear_tables[lower + 1U].gear) {
        ++lower;
    }

    const NtcTempGearCurve *low_curve = &ntc2_gear_tables[lower];
    if (gear <= low_curve->gear || lower + 1U == NTC2_GEAR_TABLE_COUNT) {
        if (value < low_curve->min_adc || value > low_curve->max_adc) {
            return temperature_from_adc(table, TABLE_COUNT, adc);
        }
        return temperature_from_adc(low_curve->points, low_curve->count, adc);
    }

    const NtcTempGearCurve *high_curve = &ntc2_gear_tables[lower + 1U];
    if (gear == high_curve->gear) {
        if (value < high_curve->min_adc || value > high_curve->max_adc) {
            return temperature_from_adc(table, TABLE_COUNT, adc);
        }
        return temperature_from_adc(high_curve->points, high_curve->count, adc);
    }
    if (value < low_curve->min_adc || value > low_curve->max_adc ||
        value < high_curve->min_adc || value > high_curve->max_adc) {
        return temperature_from_adc(table, TABLE_COUNT, adc);
    }

    const float low_temperature =
        temperature_from_adc(low_curve->points, low_curve->count, adc);
    const float high_temperature =
        temperature_from_adc(high_curve->points, high_curve->count, adc);
    const float fraction = (float)(gear - low_curve->gear) /
                           (float)(high_curve->gear - low_curve->gear);
    return low_temperature + fraction * (high_temperature - low_temperature);
}
#endif

float ntcTempGetTemperature(void)
{
#if MACHINE == 3
    const float temperature =
        machine3_temperature_from_three_ntcs(NULL);
#elif MACHINE == 4
    const uint32_t adc = g_DataPool.ntc3Raw;
    const float temperature =
        temperature_from_adc(ntc3_table, NTC3_TABLE_COUNT, (float)adc);
#else
    const uint32_t adc = g_DataPool.ntc2Raw;
#if MACHINE == 1
    /* 1号机：ADC × 有效加热档位二维查表；曲线未覆盖处自动回退一维表。 */
    int gear = 0;
    if (g_DataPool.powerEnabled) {
        if (g_DataPool.powerHeater1Enabled) {
            gear = (int)g_DataPool.powerHeater1Gear;
        }
        if (g_DataPool.powerHeater2Enabled &&
            (int)g_DataPool.powerHeater2Gear > gear) {
            gear = (int)g_DataPool.powerHeater2Gear;
        }
    }
    const float temperature = temperature_from_gear_curves(adc, gear);
#else
    const float temperature = temperature_from_adc(table, TABLE_COUNT, adc);
#endif
#endif
    return normalize_output_temperature(temperature);
}

bool ntcTempPrimarySampleAccepted(void)
{
#if MACHINE == 3
    return ntc_temperature_sample_accepted[NTC_TEMP_MACHINE3_PRIMARY_CHANNEL - 1U];
#else
    return true;
#endif
}

float ntcTempGetNtc1Temperature(void)
{
#if MACHINE == 3
    const bool heating = machine3_heating_requested(0U);
    const NtcTempMachine3OperatingConditions conditions =
        machine3_default_operating_conditions(heating, 0U);
    const float temperature = machine3_channel_temperature(
        0U, g_DataPool.ntc1Raw, heating, &conditions);
    if (isfinite(temperature)) {
        machine3_last_valid_temperatures[0U] = temperature;
        machine3_temperature_valid[0U] = true;
        return normalize_output_temperature(temperature);
    }
    return machine3_temperature_valid[0U]
        ? normalize_output_temperature(machine3_last_valid_temperatures[0U]) : NAN;
#else
    return normalize_output_temperature(
        temperature_from_adc(ntc1_table, NTC1_TABLE_COUNT, g_DataPool.ntc1Raw));
#endif
}
