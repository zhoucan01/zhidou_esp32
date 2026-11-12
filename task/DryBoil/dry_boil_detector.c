#include "dry_boil_detector.h"

#include <string.h>

static db_result_t db_result(db_state_t state)
{
    db_result_t r;
    memset(&r, 0, sizeof(r));
    r.state = state;
    return r;
}

void db_default_config(db_config_t *c)
{
    if (!c) return;
    c->wet_normalized_rate_c_min_p10 = 10.2f;
    c->suspect_ratio = 1.15f;
    c->confirm_ratio = 1.18f;
    c->ignore_first_s = 35.0f;
    c->confirm_time_s = 10.0f;
    c->minimum_mean_power = 3.0f;
    c->minimum_temperature_c = 35.0f;
    c->maximum_decision_temperature_c = 75.0f;
    c->absolute_safety_temperature_c = 103.0f;
}

void db_init(db_detector_t *d)
{
    if (d) memset(d, 0, sizeof(*d));
}

static void db_push(db_detector_t *d, float temperature, uint8_t power)
{
    d->temperature[d->head] = temperature;
    d->power[d->head] = power > 13u ? 13u : power;
    d->head = (uint8_t)((d->head + 1u) % DB_WINDOW_SAMPLES);
    if (d->count < DB_WINDOW_SAMPLES) d->count++;
}

static void db_calculate_window(const db_detector_t *d, db_result_t *r)
{
    uint8_t i;
    uint8_t oldest = (uint8_t)((d->head + DB_WINDOW_SAMPLES - d->count)
                               % DB_WINDOW_SAMPLES);
    float mean_x = ((float)d->count - 1.0f) * 0.5f;
    float mean_t = 0.0f, sum_power = 0.0f, xy = 0.0f, xx = 0.0f;

    for (i = 0u; i < d->count; ++i) {
        uint8_t p = (uint8_t)((oldest + i) % DB_WINDOW_SAMPLES);
        mean_t += d->temperature[p];
        sum_power += (float)d->power[p];
    }
    mean_t /= (float)d->count;
    r->mean_power = sum_power / (float)d->count;
    for (i = 0u; i < d->count; ++i) {
        uint8_t p = (uint8_t)((oldest + i) % DB_WINDOW_SAMPLES);
        float x = (float)i - mean_x;
        xy += x * (d->temperature[p] - mean_t);
        xx += x * x;
    }
    /* x is sample index: convert C/sample to C/min. */
    r->rise_rate_c_min = xx > 0.0f ? (xy / xx) * (60.0f / DB_SAMPLE_PERIOD_S) : 0.0f;
    r->normalized_rate_c_min_p10 = r->mean_power > 0.1f
        ? r->rise_rate_c_min * 10.0f / r->mean_power : 0.0f;
}

db_result_t db_step(db_detector_t *d, const db_config_t *c,
                    float temperature, uint8_t power, uint8_t valid)
{
    db_result_t r = db_result(DB_LEARNING);
    uint16_t confirm_ticks;
    uint16_t ignore_ticks;
    uint8_t valid_zone;
    if (!d || !c) return r;
    if (d->dry_latched) return db_result(DB_DRY_CONFIRMED);
    if (!valid || temperature < -40.0f || temperature > 200.0f) {
        d->dry_latched = 1u; /* Sensor failure must stop heating fail-safe. */
        return db_result(DB_DRY_CONFIRMED);
    }
    if (temperature >= c->absolute_safety_temperature_c) {
        d->dry_latched = 1u;
        return db_result(DB_DRY_CONFIRMED);
    }

    db_push(d, temperature, power);
    if (d->elapsed_ticks != 0xffffu) d->elapsed_ticks++;
    ignore_ticks = (uint16_t)(c->ignore_first_s / DB_SAMPLE_PERIOD_S + 0.5f);
    if (d->elapsed_ticks < ignore_ticks || d->count < DB_WINDOW_SAMPLES) return r;

    db_calculate_window(d, &r);
    if (c->wet_normalized_rate_c_min_p10 > 0.1f)
        r.score = r.normalized_rate_c_min_p10 / c->wet_normalized_rate_c_min_p10;
    valid_zone = temperature >= c->minimum_temperature_c
              && temperature <= c->maximum_decision_temperature_c
              && r.mean_power >= c->minimum_mean_power;
    if (!valid_zone) { d->suspicious_ticks = 0u; return r; }

    if (r.score >= c->confirm_ratio) {
        if (d->suspicious_ticks != 0xffffu) d->suspicious_ticks++;
        confirm_ticks = (uint16_t)(c->confirm_time_s / DB_SAMPLE_PERIOD_S + 0.5f);
        if (d->suspicious_ticks >= confirm_ticks) {
            d->dry_latched = 1u;
            r.state = DB_DRY_CONFIRMED;
        } else r.state = DB_DRY_SUSPECTED;
    } else if (r.score >= c->suspect_ratio) {
        if (d->suspicious_ticks != 0xffffu) d->suspicious_ticks++;
        r.state = DB_DRY_SUSPECTED;
    } else {
        d->suspicious_ticks = 0u;
        r.state = DB_WATER_LIKELY;
    }
    return r;
}

static db_detector_t db_simple_detector;
static db_config_t db_simple_config;
static uint8_t db_simple_initialized;

void db_simple_reset(void)
{
    db_default_config(&db_simple_config);
    db_init(&db_simple_detector);
    db_simple_initialized = 1u;
}

uint8_t db_is_dry_boil(float temperature_c, uint8_t actual_power_0_13)
{
    db_result_t result;
    uint8_t valid = temperature_c >= -40.0f && temperature_c <= 200.0f;
    if (!db_simple_initialized) db_simple_reset();
    result = db_step(&db_simple_detector, &db_simple_config, temperature_c,
                     actual_power_0_13, valid);
    return result.state == DB_DRY_CONFIRMED ? 1u : 0u;
}
