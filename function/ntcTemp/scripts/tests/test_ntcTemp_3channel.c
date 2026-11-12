#include "ntcTemp.h"
#include "DataCenter.h"

#include <assert.h>
#include <math.h>

struct DataPool g_DataPool;

static void set_raw_adcs(uint32_t ntc1, uint32_t ntc2, uint32_t ntc3)
{
    g_DataPool.ntc1Raw = ntc1;
    g_DataPool.ntc2Raw = ntc2;
    g_DataPool.ntc3Raw = ntc3;
}

static void assert_channel_high_spike_ignored(unsigned channel)
{
    static const uint32_t baselines[3][3] = {
        {2000U, 1800U, 2300U},
        {1800U, 2000U, 2300U},
        {1800U, 2300U, 2000U},
    };
    uint32_t spike_adcs[3];

    ntcTempResetFilter();
    set_raw_adcs(baselines[channel][0], baselines[channel][1],
                 baselines[channel][2]);
    for (unsigned sample = 0; sample < 20U; ++sample) {
        (void)ntcTempGetTemperature();
    }
    const float baseline_temperature = ntcTempGetTemperature();

    spike_adcs[0] = baselines[channel][0];
    spike_adcs[1] = baselines[channel][1];
    spike_adcs[2] = baselines[channel][2];
    spike_adcs[channel] = 4095U;
    set_raw_adcs(spike_adcs[0], spike_adcs[1], spike_adcs[2]);
    assert(fabsf(ntcTempGetTemperature() - baseline_temperature) < 0.15f);
}

int main(void)
{
    g_DataPool = (struct DataPool){0};
    g_DataPool.powerEnabled = true;
    g_DataPool.powerHeater1Enabled = true;
    set_raw_adcs(2000U, 2000U, 2000U);
    for (unsigned i = 0; i < 20U; ++i) {
        (void)ntcTempGetTemperature();
    }
    assert(fabsf(ntcTempGetTemperature() - 73.18f) < 0.2f);
    assert(ntcTempIsInRange(2000U));
    assert(ntcTempNtc1IsInRange(2000U));

    ntcTempResetFilter();
    set_raw_adcs(2300U, 2000U, 2500U);
    for (unsigned sample = 0; sample < 20U; ++sample) {
        (void)ntcTempGetTemperature();
    }
    assert(fabsf(ntcTempGetTemperature() - 73.18f) < 0.2f);

    ntcTempResetFilter();
    set_raw_adcs(2000U, 2000U, 2000U);
    for (unsigned sample = 0; sample < 20U; ++sample) {
        (void)ntcTempGetTemperature();
    }

    for (unsigned channel = 0; channel < 3U; ++channel) {
        assert_channel_high_spike_ignored(channel);
    }

    ntcTempResetFilter();
    set_raw_adcs(2000U, 2000U, 2000U);
    for (unsigned sample = 0; sample < 20U; ++sample) {
        (void)ntcTempGetTemperature();
    }

    set_raw_adcs(0U, 2000U, 2000U);
    assert(fabsf(ntcTempGetTemperature() - 73.18f) < 0.2f);

    g_DataPool.powerEnabled = false;
    set_raw_adcs(2500U, 2500U, 2500U);
    const float power_off_temperature = ntcTempGetTemperature();
    assert(fabsf(power_off_temperature - 75.454f) < 0.1f);
    assert(ntcTempIsInRange(2500U));
    assert(ntcTempIsInRange(3000U));
    assert(!ntcTempIsInRange(4000U));

    g_DataPool.powerEnabled = true;
    g_DataPool.powerHeater1Enabled = true;
    set_raw_adcs(2300U, 2300U, 2300U);
    for (unsigned i = 0; i < 20U; ++i) {
        (void)ntcTempGetTemperature();
    }
    assert(fabsf(ntcTempGetTemperature() - 58.08f) < 0.2f);

    g_DataPool.powerEnabled = false;
    set_raw_adcs(2500U, 2500U, 2500U);
    assert(fabsf(ntcTempGetTemperature() - 75.454f) < 0.1f);

    g_DataPool.powerEnabled = true;
    g_DataPool.powerHeater2Enabled = true;
    assert(fabsf(ntcTempGetTemperature() - 51.16f) < 0.2f);

    for (unsigned i = 0; i < 19U; ++i) {
        (void)ntcTempGetTemperature();
    }
    set_raw_adcs(2000U, 2000U, 2000U);
    assert(fabsf(ntcTempGetTemperature() - 51.16f) < 0.2f);
    ntcTempResetFilter();
    assert(fabsf(ntcTempGetTemperature() - 73.18f) < 0.2f);

    g_DataPool = (struct DataPool){0};
    g_DataPool.powerEnabled = true;
    g_DataPool.powerHeater1Enabled = true;
    g_DataPool.powerHeater1Gear = 10U;
    g_DataPool.temperature = 82.0f;
    set_raw_adcs(2300U, 2200U, 2200U);
    ntcTempResetFilter();
    float stable_temperature = 0.0f;
    for (unsigned sample = 0; sample < 100U; ++sample) {
        stable_temperature = ntcTempGetTemperature();
    }
    set_raw_adcs(1600U, 1600U, 1600U);
    for (unsigned sample = 0; sample < 20U; ++sample) {
        assert(fabsf(ntcTempGetTemperature() - stable_temperature) < 0.5f);
    }

    return 0;
}
