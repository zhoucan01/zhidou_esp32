#include "SystemMoniter.h"

#include "ToufuMake.h"
#include "UsartExchange.h"
#include "FunctionSelection.h"

#include "esp_log.h"
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_mac.h"

#include "Bluetooch.h"
#include "DataCenter.h"

EventGroupHandle_t SystemGroup;
extern uint16_t DatafdTempServerHandle;
extern uint16_t SysMoniterServerHandle;
static const char *TAG = "control";


const float Heat1Power[] = {0.0f, 24.f, 47.f, 97.f, 113.f, 126.f, 148.f, 195.f, 234.f, 250.f, 280.f, 300.f, 313.f, 318.f };

const float Heat2Power[] = {0.0f, 58.f, 113.f, 238.f, 277.f, 313.f, 366.f, 484.f, 586.f, 628.f, 710.f,800.f,800.f,800.f };

static float currentHeatPower[2];

//管理OTA，时间查询，固件查询,任务状态查询（做豆浆：抽水、破壁加热、保温、排浆；做豆腐：抽水、破壁、加热、保温、排浆、点卤、蹲脑、压制、抬升；自清洁：进水、搅拌加热、防水、重复；植物奶：加水、破壁、（加热）、出浆）,设备状态
void SystemMonitorTask(void *pvParameters)
{
    SystemGroup = xEventGroupCreate();
    float MoniTemp=0;
    float MoniSpeed=0;
    float MoniCurr=0.0f;
    float Moni[3];
    int systick=0;
    while(1)
    {
        
        // MoniTemp=getTemp();
        // INA226ReadCurrent(&MoniCurr);
        Moni[0]=MoniTemp;
        Moni[1]=MoniCurr;
        Moni[2]=MoniSpeed;
        systick++;
        
        
        // 修复：使用静态缓冲区，存储要发送的数据
        static uint8_t send_buffer[128];  // 或使用栈上的数组
        
        // 方式1：发送单个数值
        int temp = systick;
        SendMessage((uint8_t*)&temp, sizeof(temp), SysMoniterServerHandle);
        vTaskDelay(pdMS_TO_TICKS(1000));
        // ESP_LOGI(TAG,"turns:%f",getTurns());       
    }
}

void setHeat1Power(int heat1Gear)
{
    currentHeatPower[0]=Heat1Power[heat1Gear];
}
void setHeat2Power(int hrat2Gear)
{
    currentHeatPower[1]=Heat2Power[hrat2Gear];
}

float getHeatPower(int HeatNum)
{
    return currentHeatPower[HeatNum];
}