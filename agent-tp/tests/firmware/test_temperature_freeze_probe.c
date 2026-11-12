#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "ntcTemp.c"
struct DataPool g_DataPool;
static int64_t probe_now;
int64_t esp_timer_get_time(void) { return probe_now; }
static void seed(unsigned start, unsigned step)
{
    g_DataPool = (struct DataPool){0}; ntcTempResetFilter();
    for (unsigned i=0;i<20;++i) {
        probe_now=1000000+i*20000;
        update_adc_window(0,start+i*step,probe_now);
    }
}
int main(void)
{
    seed(1600,10); g_DataPool.powerMotorRpm=500; probe_now+=20000;
    unsigned filtered=update_adc_window(0,1800,probe_now);
    bool raw_ok=machine3_motor_adc_matches(0,1800,probe_now);
    bool median_ok=machine3_motor_adc_matches(0,filtered,probe_now);
    printf("double gate: raw=1800 accepted=%d, median=%u accepted=%d\n",raw_ok,filtered,median_ok);
    assert(raw_ok && ntc_adc_sample_accepted[0] && !median_ok);
    seed(2000,0); g_DataPool.powerMotorRpm=500;
    for(unsigned i=0;i<150;++i) {
        probe_now+=20000; update_adc_window(0,2400,probe_now);
    }
    bool recovery_ok=machine3_motor_adc_matches(0,2000,probe_now);
    printf("stale running history: returning to ADC=2000 after 3s accepted=%d\n",recovery_ok);
    assert(!recovery_ok);
    seed(2000,0);
    for(unsigned cycle=0;cycle<20;++cycle) {
        for(unsigned i=0;i<7;++i) {
            probe_now+=20000;
            assert(update_adc_window(0,2301,probe_now)==2000);
        }
        ntcTempResetTemperatureTrend();
    }
    printf("short bursts: 20 x 7 candidates with motor-boundary reset, trusted median=%u\n",median_adc_window(0));
    assert(median_adc_window(0)==2000);
}
