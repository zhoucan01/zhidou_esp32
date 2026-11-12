/* Host regression for production curve selection; does not model motor/filter gates. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "ntcTemp.c"
struct DataPool g_DataPool;
int64_t esp_timer_get_time(void) { return 1000000; }
int main(void) {
    g_DataPool.powerEnabled = true;
    g_DataPool.powerHeater1Enabled = true;
    g_DataPool.powerHeater1Gear = 7;
    for (unsigned channel = 0; channel < 3; ++channel) {
        machine3_temperature_valid[channel] = true;
        machine3_last_valid_temperatures[channel] = 94;
        NtcTempMachine3OperatingConditions c = machine3_default_operating_conditions(true, channel);
        assert(c.power_gear == 7 && c.motor_gear == 1);
        assert(c.heater_on_seconds == 5 && c.heater_off_seconds == 5);
        assert(c.motor_on_seconds == 3 && c.motor_off_seconds == 3);
        /* The 5/5 NTC1 profile has only two nodes: both extrapolation
         * directions must work within 5 C and reject farther ADCs. */
        if (channel == 0) {
            for (unsigned adc = 1639; adc <= 1868; ++adc)
                assert(isfinite(machine3_channel_temperature(channel, adc, true, &c)));
            assert(isnan(machine3_channel_temperature(channel, 1, true, &c)));
            assert(isnan(machine3_channel_temperature(channel, 4095, true, &c)));
        }
        machine3_last_valid_temperatures[channel] = 97;
        c = machine3_default_operating_conditions(true, channel);
        assert(c.heater_on_seconds == 6 && c.heater_off_seconds == 4);
        /* NTC1 observed high-temperature ADC span, including curve gaps. */
        if (channel == 0) for (unsigned adc = 1639; adc <= 1868; ++adc)
            assert(isfinite(machine3_channel_temperature(channel, adc, true, &c)));
        assert(isnan(machine3_channel_temperature(channel, 1, true, &c)));
        assert(isnan(machine3_channel_temperature(channel, 4095, true, &c)));
    }
    /* Missing 6/12/30 gears retain their actual gear and usable inferred timing. */
    const unsigned missing[] = {6, 12, 30};
    for (unsigned i = 0; i < 3; ++i) {
        g_DataPool.powerHeater1Gear = missing[i];
        NtcTempMachine3OperatingConditions c = machine3_default_operating_conditions(true, 0);
        assert(c.power_gear == missing[i] && c.motor_gear == 1);
        assert(c.heater_on_seconds > 0 && c.motor_on_seconds > 0);
        const NtcMachine3OperatingPoint *p = machine3_nearest_profile(
            machine3_operating_table, sizeof(machine3_operating_table)/sizeof(machine3_operating_table[0]), 0, 1, &c);
        assert(p);
        if (missing[i] == 12) {
            /* Both 10 and the newly measured 14 are two gears away. */
            assert(p->heater_gear == 10 || p->heater_gear == 14);
        } else {
            assert(p->heater_gear == (missing[i] == 6 ? 7 : 19));
        }
        assert(isfinite(machine3_channel_temperature(0, 1700, true, &c)));
    }
    puts("PASS: gear7 templates, full observed NTC1 ADC span, missing gears and ADC guards");
}
