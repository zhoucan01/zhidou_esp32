#include "ntcTemp.h"
#include "DataCenter.h"
#include "esp_timer.h"

#include <assert.h>
#include <math.h>

struct DataPool g_DataPool;

int main(void)
{
    g_DataPool = (struct DataPool){0};
    g_DataPool.powerEnabled = true;
    g_DataPool.powerHeater1Enabled = true;
    g_DataPool.powerHeater1Gear = 10U;
    g_DataPool.temperature = 82.0f;
    g_DataPool.ntc1Raw = 2300U;
    g_DataPool.ntc2Raw = 2200U;
    g_DataPool.ntc3Raw = 2200U;

    const NtcTempMachine3OperatingConditions unmatched_conditions = {
        10U, 1, 7.0f, 3.0f, 2.0f, 6.0f, NTC_TEMP_MACHINE3_DEFAULT_WATER_VOLUME_ML,
    };
    {
    /* Nearest timing group is 6/4 seconds; its ADC span can differ by channel. */
    NtcTempMachine3OperatingConditions nearest_conditions = unmatched_conditions;
    nearest_conditions.heater_on_seconds = 6.0f;
    nearest_conditions.heater_off_seconds = 4.0f;
    const float nearest_temperature = ntcTempGetTemperatureWithConditions(unmatched_conditions);
    ntcTempResetFilter();
    const float exact_nearest_temperature = ntcTempGetTemperatureWithConditions(nearest_conditions);
    assert((isnan(nearest_temperature) && isnan(exact_nearest_temperature)) ||
           fabsf(nearest_temperature - exact_nearest_temperature) < 0.001f);
    }

    const NtcTempMachine3OperatingConditions conditions = {
        10U, 1, 1.0f, 0.0f, 2.0f, 6.0f, NTC_TEMP_MACHINE3_DEFAULT_WATER_VOLUME_ML,
    };
    ntcTempResetFilter();
    float temperature = 0.0f;
    for (unsigned sample = 0; sample < 20U; ++sample) {
        temperature = ntcTempGetTemperatureWithConditions(conditions);
    }
    const float explicit_temperature = temperature;
    ntcTempResetFilter();
    for (unsigned sample = 0; sample < 20U; ++sample) {
        temperature = ntcTempGetTemperature();
    }
    assert(fabsf(temperature - explicit_temperature) < 0.1f);
#if NTC_TEMP_MACHINE3_PRIMARY_CHANNEL == 1
    assert(fabsf(temperature - 76.799f) < 0.1f);
#elif NTC_TEMP_MACHINE3_PRIMARY_CHANNEL == 2
    assert(fabsf(temperature - 73.965f) < 0.1f);
#else
    assert(fabsf(temperature - 75.192f) < 0.1f);
#endif
    assert(fabsf(ntcTempGetNtc1Temperature() - 76.799f) < 0.1f);

    g_DataPool.powerMotorGear = 1;
    g_DataPool.powerMotorRpm = 0U;
    g_DataPool.temperature = 75.0f;
    g_DataPool.ntc1Raw = 2523U;
    ntcTempResetFilter();
    const float motor_stopped_temperature = ntcTempGetNtc1Temperature();
    assert(isfinite(motor_stopped_temperature));
    g_DataPool.powerMotorRpm = 100U;
    assert(fabsf(ntcTempGetNtc1Temperature() -
                 motor_stopped_temperature) < 0.1f);
    g_DataPool.powerMotorGear = 0;
    g_DataPool.powerMotorRpm = 0U;
    g_DataPool.temperature = 82.0f;
    g_DataPool.ntc1Raw = 2300U;

#if NTC_TEMP_MACHINE3_PRIMARY_CHANNEL == 1
    g_DataPool.powerEnabled = true;
    g_DataPool.powerHeater1Enabled = true;
    g_DataPool.powerHeater1Gear = 10U;
    g_DataPool.powerMotorGear = 1;
    g_DataPool.powerMotorRpm = 0U;
    g_DataPool.temperature = 80.4f;
    g_DataPool.ntc1Raw = 2178U;
    g_DataPool.ntc2Raw = 2101U;
    g_DataPool.ntc3Raw = 2111U;
    ntcTempResetFilter();
    const float heating_cycle_on = ntcTempGetTemperature();
    g_DataPool.powerHeater1Enabled = false;
    g_DataPool.powerHeater1Gear = 0U;
    const float heating_cycle_off = ntcTempGetTemperature();
    assert(fabsf(heating_cycle_on - 80.3f) < 1.0f);
    assert(fabsf(heating_cycle_off - heating_cycle_on) < 1.0f);

    g_DataPool.powerMotorRpm = 100U;
    g_DataPool.powerHeater1Enabled = true;
    g_DataPool.powerHeater1Gear = 10U;
    g_DataPool.temperature = 98.5f;
    g_DataPool.ntc1Raw = 2300U;
    g_DataPool.ntc2Raw = 2200U;
    g_DataPool.ntc3Raw = 2200U;
    ntcTempResetFilter();
    for (unsigned sample = 0U; sample < 20U; ++sample) {
        (void)ntcTempGetTemperature();
    }

    g_DataPool.powerHeater1Enabled = false;
    g_DataPool.powerHeater1Gear = 0U;
    const float stable_adc_heating_temperature = ntcTempGetNtc1Temperature();
    assert(fabsf(stable_adc_heating_temperature - heating_cycle_on) < 1.0f);
    (void)ntcTempGetTemperature();
    assert(fabsf(ntcTempGetNtc1Temperature() -
                 stable_adc_heating_temperature) < 0.1f);

    g_DataPool.powerHeater1Enabled = true;
    g_DataPool.powerHeater1Gear = 10U;
    ntcTempResetFilter();
    for (unsigned sample = 0U; sample < 20U; ++sample) {
        g_DataPool.ntc1Raw = 2205U + sample * 5U;
        (void)ntcTempGetTemperature();
    }
    g_DataPool.powerHeater1Enabled = false;
    g_DataPool.powerHeater1Gear = 0U;
    g_DataPool.ntc1Raw = 2300U;
    g_DataPool.temperature = 97.99f;
    (void)ntcTempGetTemperature();
    assert(fabsf(ntcTempGetNtc1Temperature() -
                 stable_adc_heating_temperature) < 0.1f);

    /* Recent heat keeps the heating table for 10 s; preserve the ADC trend
     * while advancing the host test clock beyond that dwell window. */
    for (unsigned tick = 0U; tick < 501U; ++tick) {
        (void)esp_timer_get_time();
    }
    g_DataPool.powerMotorRpm = 0U;
    g_DataPool.temperature = 98.0f;
    (void)ntcTempGetTemperature();
    const float cooling_temperature = ntcTempGetNtc1Temperature();
    assert(isfinite(cooling_temperature));
    assert(ntcTempNtc1IsInRange(2300U));
    g_DataPool.ntc1Raw = 1871U;
    const float cooling_1871 = ntcTempGetNtc1Temperature();
    g_DataPool.ntc1Raw = 1874U;
    const float cooling_1874 = ntcTempGetNtc1Temperature();
    assert(ntcTempNtc1IsInRange(1871U));
    assert(ntcTempNtc1IsInRange(1874U));
    assert(fabsf(cooling_1874 - cooling_1871) < 0.2f);
    g_DataPool.ntc1Raw = 4095U;
    assert(!ntcTempNtc1IsInRange(4095U));
    assert(fabsf(ntcTempGetNtc1Temperature() - cooling_1874) < 0.1f);
    g_DataPool.ntc1Raw = 2300U;
    g_DataPool.temperature = 90.0f;
    (void)ntcTempGetTemperature();
    assert(fabsf(ntcTempGetNtc1Temperature() - cooling_temperature) < 0.1f);

    g_DataPool.powerHeater1Enabled = true;
    g_DataPool.powerHeater1Gear = 10U;
    assert(!ntcTempNtc1IsInRange(2300U));

    g_DataPool.powerEnabled = false;
    (void)ntcTempGetTemperature();
    g_DataPool.powerEnabled = true;
    g_DataPool.powerHeater1Enabled = true;
    g_DataPool.powerHeater1Gear = 10U;
    g_DataPool.powerMotorGear = 0;
    g_DataPool.temperature = 82.0f;
    g_DataPool.ntc1Raw = 2300U;
    g_DataPool.ntc2Raw = 2200U;
    g_DataPool.ntc3Raw = 2200U;
    g_DataPool.powerMotorRpm = 0U;
#endif

    g_DataPool.ntc1Raw = 2012U;
    g_DataPool.ntc2Raw = 1846U;
    g_DataPool.ntc3Raw = 1946U;
    ntcTempResetFilter();
    for (unsigned sample = 0; sample < 20U; ++sample) {
        temperature = ntcTempGetTemperatureWithConditions(conditions);
    }
#if NTC_TEMP_MACHINE3_PRIMARY_CHANNEL == 1
    assert(fabsf(temperature - 84.695f) < 0.1f);
#elif NTC_TEMP_MACHINE3_PRIMARY_CHANNEL == 2
    assert(fabsf(temperature - 84.964f) < 0.1f);
#else
    assert(fabsf(temperature - 84.273f) < 0.1f);
#endif

    g_DataPool.ntc1Raw = 2887U;
    g_DataPool.ntc2Raw = 2592U;
    g_DataPool.ntc3Raw = 2516U;
    ntcTempResetFilter();
    for (unsigned sample = 0; sample < 20U; ++sample) {
        temperature = ntcTempGetTemperatureWithConditions(conditions);
    }
#if NTC_TEMP_MACHINE3_PRIMARY_CHANNEL == 1
    assert(fabsf(temperature - 56.115f) < 0.1f);
#elif NTC_TEMP_MACHINE3_PRIMARY_CHANNEL == 2
    assert(fabsf(temperature - 55.710f) < 0.1f);
#else
    assert(fabsf(temperature - 55.482f) < 0.1f);
#endif

    g_DataPool.ntc1Raw = 2011U;
    g_DataPool.ntc2Raw = 1845U;
    g_DataPool.ntc3Raw = 1945U;
    ntcTempResetFilter();
    for (unsigned sample = 0; sample < 20U; ++sample) {
        temperature = ntcTempGetTemperatureWithConditions(conditions);
    }
    assert(isfinite(temperature));

    g_DataPool.ntc1Raw = 2888U;
    g_DataPool.ntc2Raw = 2593U;
    g_DataPool.ntc3Raw = 2517U;
    ntcTempResetFilter();
    for (unsigned sample = 0; sample < 20U; ++sample) {
        temperature = ntcTempGetTemperatureWithConditions(conditions);
    }
    assert(isfinite(temperature));

    g_DataPool.ntc1Raw = 2300U;
    g_DataPool.ntc2Raw = 2200U;
    g_DataPool.ntc3Raw = 2200U;
    ntcTempResetFilter();
    {
    /* Nearest timing group is 6/4 seconds; its ADC span can differ by channel. */
    NtcTempMachine3OperatingConditions nearest_conditions = unmatched_conditions;
    nearest_conditions.heater_on_seconds = 6.0f;
    nearest_conditions.heater_off_seconds = 4.0f;
    const float nearest_temperature = ntcTempGetTemperatureWithConditions(unmatched_conditions);
    ntcTempResetFilter();
    const float exact_nearest_temperature = ntcTempGetTemperatureWithConditions(nearest_conditions);
    assert((isnan(nearest_temperature) && isnan(exact_nearest_temperature)) ||
           fabsf(nearest_temperature - exact_nearest_temperature) < 0.001f);
    }

#if NTC_TEMP_MACHINE3_PRIMARY_CHANNEL == 2
    g_DataPool.powerHeater1Gear = 11U;
    g_DataPool.ntc1Raw = 2000U;
    g_DataPool.ntc2Raw = 2000U;
    g_DataPool.ntc3Raw = 2000U;
    ntcTempResetFilter();
    for (unsigned sample = 0; sample < 20U; ++sample) {
        temperature = ntcTempGetTemperature();
    }
    assert(isfinite(temperature));
#endif

    g_DataPool.powerEnabled = false;
    g_DataPool.powerHeater1Enabled = false;
    g_DataPool.powerHeater2Enabled = false;
    g_DataPool.ntc1Raw = 1933U;
    g_DataPool.ntc2Raw = 1933U;
    g_DataPool.ntc3Raw = 1934U;
    ntcTempResetFilter();
    for (unsigned sample = 0; sample < 20U; ++sample) {
        temperature = ntcTempGetTemperatureWithConditions(conditions);
    }
    assert(isfinite(temperature));

    return 0;
}
