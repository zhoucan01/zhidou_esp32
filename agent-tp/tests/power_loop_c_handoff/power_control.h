#ifndef POWER_CONTROL_H
#define POWER_CONTROL_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double voltage_v;
    double current_a;
    double active_power_w;
} pc_sample_t;

typedef struct {
    double resistance_ohms;
    double full_power_w;
    double mean_voltage_v;
    double mean_current_a;
    size_t valid_sample_count;
    size_t plateau_sample_count;
} pc_calibration_t;

typedef struct {
    double estimated_full_power_w;
    double integral;
    double previous_error_fraction;
    bool has_previous_error_fraction;
} pc_pid_state_t;

typedef struct {
    double active_tolerance_w;
    double maximum_tolerance_w;
    double stable_since_s;
    unsigned int reversals;
    int previous_error_side;
    bool has_stable_since;
} pc_stability_state_t;

typedef struct {
    double active_tolerance_w;
    unsigned int reversals;
    bool stable;
} pc_stability_result_t;

typedef struct {
    int cycles;
    double error_w;
    double measured_power_w;
    double estimated_full_power_w;
    double integral;
    double error_fraction;
} pc_pid_result_t;

typedef enum {
    PC_OK = 0,
    PC_ERR_INVALID_ARGUMENT,
    PC_ERR_NOT_ENOUGH_SAMPLES,
    PC_ERR_NO_STABLE_PLATEAU,
    PC_ERR_NO_MEMORY
} pc_status_t;

pc_status_t pc_calibrate_resistance(
    const pc_sample_t *samples,
    size_t sample_count,
    pc_calibration_t *result);

int pc_initial_cycles(
    double target_power_w,
    double current_voltage_v,
    double saved_voltage_v,
    int saved_cycles,
    int total_cycles);

void pc_pid_reset(pc_pid_state_t *state);

pc_status_t pc_pid_step(
    pc_pid_state_t *state,
    double target_power_w,
    double measured_power_w,
    int current_cycles,
    int total_cycles,
    pc_pid_result_t *result);

void pc_stability_reset(pc_stability_state_t *state, double initial_tolerance_w);

pc_status_t pc_stability_update(
    pc_stability_state_t *state,
    double error_w,
    double now_s,
    pc_stability_result_t *result);

void pc_scan_max_error_update(
    double *scan_max_error_w,
    double error_w,
    bool stable);

bool pc_full_output_below_target(
    int cycles,
    int total_cycles,
    double feedback_power_w,
    double target_power_w,
    double tolerance_w);

#ifdef __cplusplus
}
#endif

#endif
