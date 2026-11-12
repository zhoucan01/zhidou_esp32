#include "power_control.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); \
            return EXIT_FAILURE; \
        } \
    } while (0)

static int nearly_equal(double actual, double expected, double tolerance)
{
    return fabs(actual - expected) <= tolerance;
}

int main(void)
{
    const pc_sample_t samples[] = {
        {200.6, 0.188, 2.581},
        {199.0, 3.372, 471.489},
        {197.39, 4.774, 941.843},
        {197.39, 4.774, 941.843},
        {197.40, 4.779, 942.944},
        {197.325, 4.778, 942.479},
        {197.4, 3.73, 942.0}
    };
    const pc_sample_t ramp_only[] = {
        {200.0, 0.2, 2.0},
        {199.0, 2.0, 100.0},
        {198.0, 4.0, 400.0}
    };
    pc_calibration_t calibration;
    pc_pid_state_t pid;
    pc_pid_result_t pid_result;
    pc_stability_state_t stability;
    pc_stability_result_t stability_result;
    double max_error = 0.0;

    CHECK(pc_calibrate_resistance(
              samples, sizeof(samples) / sizeof(samples[0]), &calibration) == PC_OK,
          "resistance calibration should accept a stable plateau");
    CHECK(calibration.valid_sample_count == 7U, "all positive measurements count as valid");
    CHECK(calibration.plateau_sample_count == 4U,
          "transients and one inconsistent plateau outlier must be excluded");
    CHECK(nearly_equal(calibration.resistance_ohms, 41.3, 0.2),
          "calibrated resistance should be close to 41.3 ohms");
    CHECK(nearly_equal(calibration.full_power_w, 942.4, 2.0),
          "full power should represent the stable plateau");
    CHECK(nearly_equal(calibration.mean_voltage_v, 197.37, 0.1),
          "mean plateau voltage should exclude the ramp");
    CHECK(pc_calibrate_resistance(
              ramp_only, sizeof(ramp_only) / sizeof(ramp_only[0]), &calibration) ==
              PC_ERR_NO_STABLE_PLATEAU,
          "a ramp with no three-sample full-power plateau must fail safely");

    CHECK(pc_initial_cycles(100.0, 201.0, 199.0, 9, 100) == 9,
          "voltage compensation should round the current-profile start cycles");
    CHECK(pc_initial_cycles(100.0, 220.0, 199.0, 9, 100) == 7,
          "higher current voltage should reduce initial cycles by voltage squared");
    CHECK(pc_initial_cycles(100.0, 1.0, 199.0, 9, 100) == 100,
          "initial cycles should be capped at full output");

    pc_pid_reset(&pid);
    CHECK(pc_pid_step(&pid, 100.0, 50.0, 5, 100, &pid_result) == PC_OK,
          "PID calculation should succeed");
    CHECK(pid_result.cycles >= 1 && pid_result.cycles <= 100,
          "PID output must remain within cycle bounds");
    CHECK(pid_result.cycles > 5, "positive power error should increase output");
    CHECK(pc_pid_step(&pid, 100.0, 0.0, 5, 100, &pid_result) == PC_OK &&
              pid_result.cycles == 6,
          "near-zero power should increment the output by one cycle");

    pc_stability_reset(&stability, 10.0);
    CHECK(pc_stability_update(&stability, 5.0, 1.0, &stability_result) == PC_OK &&
              !stability_result.stable,
          "first in-tolerance sample starts, but does not complete, stability");
    CHECK(pc_stability_update(&stability, -5.0, 3.0, &stability_result) == PC_OK &&
              !stability_result.stable,
          "reversal should remain inside the stability window");
    CHECK(pc_stability_update(&stability, 5.0, 5.1, &stability_result) == PC_OK &&
              stability_result.stable,
          "in-tolerance error for four seconds should be stable");
    pc_stability_reset(&stability, 10.0);
    CHECK(pc_stability_update(&stability, 5.0, 0.0, &stability_result) == PC_OK,
          "stability reversal setup should start");
    CHECK(pc_stability_update(&stability, -5.0, 1.0, &stability_result) == PC_OK,
          "first reversal should be counted");
    CHECK(pc_stability_update(&stability, 5.0, 2.0, &stability_result) == PC_OK,
          "second reversal should be counted");
    CHECK(pc_stability_update(&stability, -5.0, 3.0, &stability_result) == PC_OK,
          "third reversal should be counted");
    CHECK(pc_stability_update(&stability, 5.0, 4.0, &stability_result) == PC_OK &&
              nearly_equal(stability_result.active_tolerance_w, 20.0, 0.001),
          "four reversals should relax the stability tolerance by ten watts");
    pc_scan_max_error_update(&max_error, 5.0, false);
    CHECK(nearly_equal(max_error, 0.0, 0.001),
          "unstable error must not affect scan maximum");
    pc_scan_max_error_update(&max_error, -7.0, true);
    CHECK(nearly_equal(max_error, 7.0, 0.001),
          "stable error should update scan maximum by absolute value");
    pc_scan_max_error_update(&max_error, -4.0, true);
    CHECK(nearly_equal(max_error, 7.0, 0.001),
          "stable maximum should remain monotonic");

    CHECK(pc_full_output_below_target(100, 100, 80.0, 100.0, 10.0),
          "full output still below target should stop higher scan points");
    CHECK(!pc_full_output_below_target(99, 100, 80.0, 100.0, 10.0),
          "non-full output should not trigger the full-output limit");
    CHECK(!pc_full_output_below_target(100, 100, 95.0, 100.0, 10.0),
          "power within tolerance should not trigger the limit");

    puts("PASS: 28 power-control assertions");
    return EXIT_SUCCESS;
}
