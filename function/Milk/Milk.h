#ifndef MILK_H
#define MILK_H

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "sys.h"

#include "UsartExchange.h"

#include "stdbool.h"

typedef struct{
    bool PlantMilk_Clean;//是否自清洁
    bool PlantMilk_Heat;
    bool PlantMilk_Temp;
}PlantMilkFunctionDetail_t;
//植物奶功能
void MilkTask(void *pvParameters);
//植物奶功能暂停预处理函数
void PlantMilkPause(void);
//植物奶功能结束预处理函数
void PlantMilkEnd(void);
//植物奶功能恢复预处理函数
void PlantMilkResume(void);

#endif