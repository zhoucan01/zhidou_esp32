#include "ntcTemp.h"
#include "DataCenter.h"

#include <assert.h>
#include <math.h>

struct DataPool g_DataPool;

static void reset_data(void)
{
    g_DataPool = (struct DataPool){0};
}

int main(void)
{
    reset_data();
    g_DataPool.ntc2Raw = 1900U;
    g_DataPool.powerHeater2Enabled = true;
    g_DataPool.powerHeater2Gear = 19U;
    assert(fabsf(ntcTempGetTemperature() - 90.0f) < 0.1f);

    reset_data();
    g_DataPool.ntc2Raw = 2468U;
    g_DataPool.powerEnabled = true;
    g_DataPool.powerHeater1Enabled = true;
    g_DataPool.powerHeater1Gear = 10U;
    assert(fabsf(ntcTempGetTemperature() - 68.3f) < 0.05f);

    reset_data();
    g_DataPool.ntc2Raw = 3470U;
    g_DataPool.powerEnabled = true;
    g_DataPool.powerHeater2Enabled = true;
    g_DataPool.powerHeater2Gear = 19U;
    assert(fabsf(ntcTempGetTemperature() - 34.2f) < 0.05f);

    reset_data();
    g_DataPool.ntc2Raw = 1802U;
    g_DataPool.powerEnabled = true;
    g_DataPool.powerHeater1Enabled = true;
    g_DataPool.powerHeater1Gear = 7U;
    assert(ntcTempGetTemperature() > 91.3f);
    assert(ntcTempGetTemperature() < 91.6f);

    reset_data();
    g_DataPool.ntc2Raw = 2500U;
    g_DataPool.powerEnabled = true;
    g_DataPool.powerHeater2Enabled = true;
    g_DataPool.powerHeater2Gear = 14U;
    assert(fabsf(ntcTempGetTemperature() - 71.12755f) < 0.05f);

    assert(ntcTempIsInRange(1623U));
    assert(ntcTempIsInRange(3470U));
    assert(!ntcTempIsInRange(0U));
    assert(!ntcTempIsInRange(3471U));
    return 0;
}