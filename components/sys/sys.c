#include <stdio.h>
#include "sys.h"


void CheckDelay(int delay_s)
{
    for(int i=0;i<delay_s*10;i++)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
