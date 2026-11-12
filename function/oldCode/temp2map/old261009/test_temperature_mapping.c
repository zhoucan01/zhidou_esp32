#include "temperature_mapping.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void check(int condition, const char *name)
{
    if (condition) {
        ++tests_passed;
        printf("  [PASS] %s\n", name);
    } else {
        ++tests_failed;
        printf("  [FAIL] %s\n", name);
    }
}

static int approx(float a, float b, float tol)
{
    return fabsf(a - b) <= tol;
}

/* Convenience wrapper for stable lookup */
static float t1(uint16_t adc)
{
    return temperature_mapping_from_adc(adc);
}

/* ── Test 1: Clamp at and beyond table boundaries ─────────────────── */
static void test_clamp(void)
{
    printf("\n=== Test 1: Clamp boundaries ===\n");
    check(approx(t1(0),    101.1f, 0.05f),
          "ADC=0 clamps to 101.1 C (hottest)");
    check(approx(t1(1600),  101.1f, 0.05f),
          "ADC=1600 -> 101.1 C (table min)");
    check(approx(t1(3700),   11.1f, 0.05f),
          "ADC=3700 -> 11.1 C (table max)");
    check(approx(t1(4000),   11.1f, 0.05f),
          "ADC=4000 clamps to 11.1 C (coldest)");
    check(approx(t1(65535),  11.1f, 0.05f),
          "ADC=65535 clamps to 11.1 C");
}

/* ── Test 2: Anchor points match table exactly ────────────────────── */
static void test_anchors(void)
{
    printf("\n=== Test 2: Anchor points ===\n");
    check(approx(t1(1700),  98.4f, 0.01f),
          "ADC=1700 -> 98.4 C");
    check(approx(t1(2000),  91.3f, 0.01f),
          "ADC=2000 -> 91.3 C");
    check(approx(t1(2500),  84.1f, 0.01f),
          "ADC=2500 -> 84.1 C");
    check(approx(t1(3000),  80.4f, 0.01f),
          "ADC=3000 -> 80.4 C");
    check(approx(t1(3300),  73.8f, 0.01f),
          "ADC=3300 -> 73.8 C");
    check(approx(t1(3500),  58.5f, 0.01f),
          "ADC=3500 -> 58.5 C");
}

/* ── Test 3: Linear interpolation between entries ─────────────────── */
static void test_interpolation(void)
{
    printf("\n=== Test 3: Interpolation ===\n");
    /* ADC 2100 -> 89.4, ADC 2125 -> 89.0, midpoint 2112 -> 89.2 */
    check(approx(t1(2112),  89.2f, 0.05f),
          "ADC=2112 -> ~89.2 C (midpoint interp)");
    /* ADC 2500 -> 84.1, ADC 2525 -> 83.9, midpoint 2512 -> 84.0 */
    check(approx(t1(2512),  84.0f, 0.05f),
          "ADC=2512 -> ~84.0 C (midpoint interp)");
    /* ADC 3000 -> 80.4, ADC 3025 -> 80.1, midpoint 3012 -> 80.25 */
    check(approx(t1(3012),  80.25f, 0.05f),
          "ADC=3012 -> ~80.25 C (midpoint interp)");
}

/* ── Test 4: Monotonicity (ADC up -> temp down) ────────────────────── */
static void test_monotonicity(void)
{
    printf("\n=== Test 4: Monotonicity ===\n");
    int ok = 1;
    for (uint16_t adc = TEMPERATURE_MAPPING_ADC_MIN;
         adc < TEMPERATURE_MAPPING_ADC_MAX; ++adc) {
        const float t0 = t1(adc);
        const float t1v = t1((uint16_t)(adc + 1U));
        if (t1v > t0 + 0.001f) {
            ok = 0;
            printf("  non-monotonic at ADC=%u: %.2f -> %.2f\n", adc, t0, t1v);
            break;
        }
    }
    check(ok, "Temperature is monotonically non-increasing over full ADC range");
}

/* ── Test 5: History-based direction detection ────────────────────── */
static void test_history_direction(void)
{
    printf("\n=== Test 5: History-based direction detection ===\n");
    uint16_t hist[TEMPERATURE_MAPPING_HISTORY_COUNT];

    /* Stable: constant ADC -> stable table */
    for (size_t i = 0; i < TEMPERATURE_MAPPING_HISTORY_COUNT; ++i)
        hist[i] = 2500;
    {
        const float t = temperature_mapping_from_adc_history(
            hist, TEMPERATURE_MAPPING_SAMPLE_PERIOD_MS);
        check(approx(t, 84.1f, 0.01f),
              "Stable history (ADC=2500) -> 84.1 C");
    }

    /* Heating: ADC decreasing over time (slope < -0.2) -> heating table */
    for (size_t i = 0; i < TEMPERATURE_MAPPING_HISTORY_COUNT; ++i)
        hist[i] = (uint16_t)(3000 - (int)(i * 10));  /* 3000, 2990, ..., 2810 */
    {
        const float t = temperature_mapping_from_adc_history(
            hist, TEMPERATURE_MAPPING_SAMPLE_PERIOD_MS);
        const float expected = t1(2810);
        check(approx(t, expected, 0.01f),
              "Heating history (ADC decreasing) -> heating table lookup");
    }

    /* Cooling: ADC increasing over time (slope > +0.2) -> cooling table */
    for (size_t i = 0; i < TEMPERATURE_MAPPING_HISTORY_COUNT; ++i)
        hist[i] = (uint16_t)(2500 + (int)(i * 10));  /* 2500, 2510, ..., 2690 */
    {
        const float t = temperature_mapping_from_adc_history(
            hist, TEMPERATURE_MAPPING_SAMPLE_PERIOD_MS);
        const float expected = t1(2690);
        check(approx(t, expected, 0.01f),
              "Cooling history (ADC increasing) -> cooling table lookup");
    }

    /* Different sample period: slope normalization keeps classification */
    for (size_t i = 0; i < TEMPERATURE_MAPPING_HISTORY_COUNT; ++i)
        hist[i] = (uint16_t)(3000 - (int)(i * 10));
    {
        /* 450ms period: scale = 0.5, slope doubles in magnitude -> still heating */
        const float t = temperature_mapping_from_adc_history(
            hist, 450);
        const float expected = t1(2810);
        check(approx(t, expected, 0.01f),
              "Heating with 450ms period -> still heating table");
    }
}

int main(void)
{
    printf("=== temperature_mapping self-test ===\n");
    printf("Calibration: 26-10-9-n1-0, Station 2 NTC1 ADC high word (16-bit)\n");
    printf("Table: 85 entries, 25-ADC intervals, 1600-3700\n\n");

    test_clamp();
    test_anchors();
    test_interpolation();
    test_monotonicity();
    test_history_direction();

    printf("\n=== Summary ===\n");
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("  %s\n", tests_failed == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return tests_failed == 0 ? 0 : 1;
}
