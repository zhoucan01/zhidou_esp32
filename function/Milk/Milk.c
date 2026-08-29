#include "Milk.h"
#include "MUsic.h"

TaskHandle_t MilkHandle = NULL;
PlantMilkFunctionDetail_t PlantMilkFunctionDetail;
#include "DataCenter.h"

void LowSpeedBreak(uint8_t iBreakTime)
{

}

void HighSpeedBreak(int iBreakTime)
{

}


void MilkTask(void *pvParameters){
    ESP_LOGI("Milk","Milk Make Start");
    while(1){
        for(int i=0;i<2;i++){
            
            ESP_LOGW("Milk", "低速" );
            NoBushRun(5);
            vTaskDelay(pdMS_TO_TICKS(1000*60*2));
            NoBushRun(0);
            vTaskDelay(pdMS_TO_TICKS(1000*60*1));
            ESP_LOGW("Milk", "高速" );
            NoBushRun(11);
            vTaskDelay(pdMS_TO_TICKS(1000*60*2));
            NoBushRun(0);
            ESP_LOGW("Milk", "现在是第%d轮", i);
            vTaskDelay(pdMS_TO_TICKS(1000*60*1));
        }
        hosePumpOn(200);
        vTaskDelay(pdMS_TO_TICKS(1000*80));
        hosePumpOff();
        Play_Music();
        vTaskDelete(NULL);
    }
}

void PlantMilkPause(void)
{
    NoBushRun(0);
    vTaskDelay(pdMS_TO_TICKS(100));
    Heat1Run(0);
    vTaskDelay(pdMS_TO_TICKS(100));
    Heat2Run(0);
    vTaskDelay(pdMS_TO_TICKS(100));    
}

void PlantMilkEnd(void)
{
    NoBushRun(0);
    vTaskDelay(pdMS_TO_TICKS(100));
    Heat1Run(0);
    vTaskDelay(pdMS_TO_TICKS(100));
    Heat2Run(0);
    vTaskDelay(pdMS_TO_TICKS(100));     
    PlantMilkPause();
}


void PlantMilkResume(void)
{
    float mk_temp=0.0; // 当前温度值
    mk_temp = getTemp();
    if(PlantMilkFunctionDetail.PlantMilk_Heat)
    {
        while(mk_temp<PlantMilkFunctionDetail.PlantMilk_Temp)
        {
            
        }
        
    }
}
