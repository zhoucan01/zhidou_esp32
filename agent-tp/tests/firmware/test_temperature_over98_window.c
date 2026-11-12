/* Tests the actual production filter extracted into build/. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
static struct { float temperature; } g_DataPool;
static int64_t test_now;
static int64_t esp_timer_get_time(void) { return test_now; }
#include "temperature_over98.inc"

static void reset(void)
{
    test_now=0; ntc_temp_over98_sampled=false;
    ntc_temp_over98_last_sample_us=0; ntc_temp_over98_next_slot=0;
    ntc_temp_over98_slot_count=0;
    memset(ntc_temp_over98_hits,0,sizeof(ntc_temp_over98_hits));
}
static void sample(int64_t time_us, float temp)
{
    test_now=time_us; g_DataPool.temperature=temp;
    update_ntc_temp_over98_hold();
}
int main(void)
{
    /* No sample yet. */
    reset(); assert(!isTemperatureOver98Confirmed());

    /* Reads inside one motor-stop window (< 1 s apart) count only once. */
    sample(0,98); sample(999999,99);
    assert(ntc_temp_over98_slot_count==1);
    assert(!isTemperatureOver98Confirmed());

    /* Three qualifying fresh samples confirm. */
    sample(1000000,98); assert(!isTemperatureOver98Confirmed());
    sample(2000000,98); assert(isTemperatureOver98Confirmed());

    /* Sampling cadence does not matter: wide gaps are fine, 3 samples is 3 samples. */
    reset(); sample(0,98); sample(4000000,98); assert(!isTemperatureOver98Confirmed());
    sample(8000000,98); assert(isTemperatureOver98Confirmed());

    /* Below 97 C the count restarts from scratch. */
    sample(9000000,96.0f); assert(!isTemperatureOver98Confirmed());
    assert(ntc_temp_over98_slot_count==0);

    /* Nonconsecutive hits inside the last five samples. */
    reset(); sample(0,98); sample(1000000,97.5f); sample(2000000,98);
    assert(!isTemperatureOver98Confirmed());
    sample(3000000,98); assert(isTemperatureOver98Confirmed());

    /* Rolling window: old hits get pushed out by fresh misses. */
    reset(); sample(0,98); sample(1000000,98); sample(2000000,98);
    assert(isTemperatureOver98Confirmed());
    sample(3000000,98); sample(4000000,98);
    sample(5000000,97.5f); sample(6000000,97.5f); sample(7000000,97.5f);
    assert(!isTemperatureOver98Confirmed());

    /* Non-finite temperatures are not even counted as a sample. */
    reset(); sample(0,98);
    unsigned counted = ntc_temp_over98_slot_count;
    sample(1000000,NAN); assert(ntc_temp_over98_slot_count==counted);
    sample(2000000,INFINITY); assert(ntc_temp_over98_slot_count==counted);
    puts("98C sample-window regression: PASS");
    return 0;
}
