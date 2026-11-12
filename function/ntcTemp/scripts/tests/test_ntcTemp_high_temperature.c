#include "ntcTemp.h"
#include "DataCenter.h"

#include <assert.h>
#include <math.h>

struct DataPool g_DataPool;

int main(void)
{
    g_DataPool = (struct DataPool){0};
    g_DataPool.ntc2Raw = 1581U;
    assert(fabsf(ntcTempGetTemperature() - 98.0f) < 0.05f);

    g_DataPool.ntc2Raw = 1557U;
    assert(fabsf(ntcTempGetTemperature() - 98.662f) < 0.05f);
    return 0;
}
