#include "power_control.h"

#include <math.h>
#include <stdlib.h>

static double pc_clamp(double value, double low, double high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

static int pc_compare_double(const void *left, const void *right)
{
    const double a = *(const double *)left;
    const double b = *(const double *)right;
    return (a > b) - (a < b);
}

static double pc_median(double *values, size_t count)
{
    qsort(values, count, sizeof(*values), pc_compare_double);
    if ((count % 2U) != 0U) {
        return values[count / 2U];
    }
    return (values[count / 2U - 1U] + values[count / 2U]) / 2.0;
}

pc_status_t pc_calibrate_resistance(
    const pc_sample_t *samples,
    size_t sample_count,
    pc_calibration_t *result)
{
    size_t index;
    size_t valid_count = 0U;
    size_t plateau_count = 0U;
    double peak_power = 0.0;
    double *resistance_values;
    double median_resistance;
    double sum_power = 0.0;
    double sum_current_squared = 0.0;
    double sum_voltage = 0.0;
    double sum_current = 0.0;

    if (samples == NULL || result == NULL || sample_count == 0U) {
        return PC_ERR_INVALID_ARGUMENT;
    }

    for (index = 0U; index < sample_count; ++index) {
        const pc_sample_t *sample = &samples[index];
        if (!isfinite(sample->voltage_v) || !isfinite(sample->current_a) ||
            !isfinite(sample->active_power_w)) {
            continue;
        }
        if (sample->voltage_v <= 0.0 || sample->current_a <= 0.0 ||
            sample->active_power_w <= 0.0) {
            continue;
        }
        ++valid_count;
        if (sample->active_power_w > peak_power) {
            peak_power = sample->active_power_w;
        }
    }
    result->valid_sample_count = valid_count;
    result->plateau_sample_count = 0U;
    result->resistance_ohms = 0.0;
    result->full_power_w = 0.0;
    result->mean_voltage_v = 0.0;
    result->mean_current_a = 0.0;

    if (valid_count < 3U) {
        return PC_ERR_NOT_ENOUGH_SAMPLES;
    }

    if (sample_count > (size_t)-1 / sizeof(*resistance_values)) {
        return PC_ERR_NO_MEMORY;
    }
    resistance_values = (double *)malloc(sample_count * sizeof(*resistance_values));
    if (resistance_values == NULL) {
        return PC_ERR_NO_MEMORY;
    }
    for (index = 0U; index < sample_count; ++index) {
        const pc_sample_t *sample = &samples[index];
        if (isfinite(sample->voltage_v) && isfinite(sample->current_a) &&
            isfinite(sample->active_power_w) && sample->voltage_v > 0.0 &&
            sample->current_a > 0.0 && sample->active_power_w > 0.0 &&
            sample->active_power_w >= peak_power * 0.9) {
            resistance_values[plateau_count++] =
                sample->active_power_w / (sample->current_a * sample->current_a);
        }
    }
    if (plateau_count < 3U) {
        free(resistance_values);
        return PC_ERR_NO_STABLE_PLATEAU;
    }

    median_resistance = pc_median(resistance_values, plateau_count);
    free(resistance_values);
    for (index = 0U; index < sample_count; ++index) {
        const pc_sample_t *sample = &samples[index];
        double resistance;
        if (!isfinite(sample->voltage_v) || !isfinite(sample->current_a) ||
            !isfinite(sample->active_power_w) || sample->voltage_v <= 0.0 ||
            sample->current_a <= 0.0 || sample->active_power_w <= 0.0 ||
            sample->active_power_w < peak_power * 0.9) {
            continue;
        }
        resistance = sample->active_power_w / (sample->current_a * sample->current_a);
        if (fabs(resistance - median_resistance) <= median_resistance * 0.05) {
            sum_power += sample->active_power_w;
            sum_current_squared += sample->current_a * sample->current_a;
            sum_voltage += sample->voltage_v;
            sum_current += sample->current_a;
            ++result->plateau_sample_count;
        }
    }
    if (result->plateau_sample_count < 3U || sum_current_squared <= 0.0) {
        result->plateau_sample_count = 0U;
        return PC_ERR_NO_STABLE_PLATEAU;
    }

    result->resistance_ohms = sum_power / sum_current_squared;
    result->full_power_w = sum_power / (double)result->plateau_sample_count;
    result->mean_voltage_v = sum_voltage / (double)result->plateau_sample_count;
    result->mean_current_a = sum_current / (double)result->plateau_sample_count;
    if (!isfinite(result->resistance_ohms) || result->resistance_ohms <= 0.0 ||
        !isfinite(result->full_power_w) || result->full_power_w <= 0.0) {
        return PC_ERR_NO_STABLE_PLATEAU;
    }
    return PC_OK;
}

int pc_initial_cycles(
    double target_power_w,
    double current_voltage_v,
    double saved_voltage_v,
    int saved_cycles,
    int total_cycles)
{
    double adjusted;
    if (!isfinite(target_power_w) || target_power_w <= 0.0 ||
        !isfinite(current_voltage_v) || current_voltage_v <= 0.0 ||
        !isfinite(saved_voltage_v) || saved_voltage_v <= 0.0 ||
        total_cycles <= 0) {
        return 0;
    }
    adjusted = (double)saved_cycles *
        (saved_voltage_v / current_voltage_v) *
        (saved_voltage_v / current_voltage_v);
    if (adjusted < 1.0) {
        return 1;
    }
    if (adjusted > (double)total_cycles) {
        return total_cycles;
    }
    return (int)floor(adjusted + 0.5);
}

void pc_pid_reset(pc_pid_state_t *state)
{
    if (state != NULL) {
        state->estimated_full_power_w = 0.0;
        state->integral = 0.0;
        state->previous_error_fraction = 0.0;
        state->has_previous_error_fraction = false;
    }
}

pc_status_t pc_pid_step(
    pc_pid_state_t *state,
    double target_power_w,
    double measured_power_w,
    int current_cycles,
    int total_cycles,
    pc_pid_result_t *result)
{
    double error;
    double error_fraction = 0.0;
    double raw_cycles;
    double proposed_integral;
    double derivative = 0.0;
    double next_full_power = 0.0;
    int next_cycles;

    if (state == NULL || result == NULL || !isfinite(target_power_w) ||
        !isfinite(measured_power_w) || target_power_w <= 0.0 ||
        measured_power_w < 0.0 || total_cycles <= 0 ||
        current_cycles < 0 || current_cycles > total_cycles) {
        return PC_ERR_INVALID_ARGUMENT;
    }

    error = target_power_w - measured_power_w;
    if (measured_power_w > 0.1 && current_cycles > 0) {
        const double observed_full_power =
            measured_power_w * (double)total_cycles / (double)current_cycles;
        next_full_power = state->estimated_full_power_w <= 0.0
            ? observed_full_power
            : 0.5 * state->estimated_full_power_w + 0.5 * observed_full_power;
        if (!isfinite(next_full_power) || next_full_power <= 0.0) {
            return PC_ERR_INVALID_ARGUMENT;
        }
        error_fraction = error / next_full_power;
        proposed_integral = pc_clamp(
            state->integral + 0.05 * error_fraction * (double)total_cycles,
            -10.0,
            10.0);
        if (state->has_previous_error_fraction) {
            derivative = error_fraction - state->previous_error_fraction;
        }
        raw_cycles =
            target_power_w / next_full_power * (double)total_cycles +
            0.25 * error_fraction * (double)total_cycles +
            proposed_integral +
            0.1 * derivative * (double)total_cycles;
        next_cycles = (int)floor(raw_cycles + 0.5);
        if (next_cycles < 0) {
            next_cycles = 0;
        } else if (next_cycles > total_cycles) {
            next_cycles = total_cycles;
        }
        if ((raw_cycles > 0.0 && raw_cycles < (double)total_cycles) ||
            (raw_cycles >= (double)total_cycles && error_fraction < 0.0) ||
            (raw_cycles <= 0.0 && error_fraction > 0.0)) {
            state->integral = proposed_integral;
        }
        state->estimated_full_power_w = next_full_power;
        state->previous_error_fraction = error_fraction;
        state->has_previous_error_fraction = true;
    } else if (measured_power_w <= 0.1) {
        next_cycles = current_cycles < total_cycles ? current_cycles + 1 : total_cycles;
    } else {
        next_cycles = current_cycles;
    }

    result->cycles = next_cycles;
    result->error_w = error;
    result->measured_power_w = measured_power_w;
    result->estimated_full_power_w = state->estimated_full_power_w;
    result->integral = state->integral;
    result->error_fraction = error_fraction;
    return PC_OK;
}

void pc_stability_reset(pc_stability_state_t *state, double initial_tolerance_w)
{
    if (state != NULL) {
        state->active_tolerance_w = initial_tolerance_w;
        state->maximum_tolerance_w = 3.0 * initial_tolerance_w;
        state->stable_since_s = 0.0;
        state->reversals = 0U;
        state->previous_error_side = 0;
        state->has_stable_since = false;
    }
}

pc_status_t pc_stability_update(
    pc_stability_state_t *state,
    double error_w,
    double now_s,
    pc_stability_result_t *result)
{
    double error_magnitude;
    int error_side;
    bool stable = false;

    if (state == NULL || result == NULL || !isfinite(error_w) ||
        !isfinite(now_s) || !isfinite(state->active_tolerance_w) ||
        state->active_tolerance_w <= 0.0) {
        return PC_ERR_INVALID_ARGUMENT;
    }

    error_magnitude = fabs(error_w);
    if (error_magnitude <= 30.0) {
        error_side = error_w > 0.0 ? 1 : (error_w < 0.0 ? -1 : 0);
        if (error_side != 0 && state->previous_error_side != 0 &&
            error_side != state->previous_error_side) {
            ++state->reversals;
            if (state->reversals > 3U &&
                state->active_tolerance_w < state->maximum_tolerance_w) {
                state->active_tolerance_w =
                    fmin(state->maximum_tolerance_w,
                         state->active_tolerance_w + 10.0);
                state->reversals = 0U;
                state->has_stable_since = false;
            }
        }
        if (error_side != 0) {
            state->previous_error_side = error_side;
        }
    } else {
        state->previous_error_side = 0;
        state->reversals = 0U;
    }

    if (error_magnitude <= state->active_tolerance_w) {
        if (!state->has_stable_since) {
            state->stable_since_s = now_s;
            state->has_stable_since = true;
        }
        stable = now_s - state->stable_since_s >= 4.0;
    } else {
        state->has_stable_since = false;
    }

    result->active_tolerance_w = state->active_tolerance_w;
    result->reversals = state->reversals;
    result->stable = stable;
    return PC_OK;
}

void pc_scan_max_error_update(
    double *scan_max_error_w,
    double error_w,
    bool stable)
{
    if (scan_max_error_w != NULL && stable && isfinite(error_w)) {
        *scan_max_error_w = fmax(*scan_max_error_w, fabs(error_w));
    }
}

bool pc_full_output_below_target(
    int cycles,
    int total_cycles,
    double feedback_power_w,
    double target_power_w,
    double tolerance_w)
{
    return cycles >= total_cycles && feedback_power_w < target_power_w - tolerance_w;
}
