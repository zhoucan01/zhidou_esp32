#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "sys.h"
#include "SelfClean.h"
#include "DataCenter.h"

#include "UsartExchange.h"
#include "Music.h"
//自清洁任务句柄
TaskHandle_t SelfCleanHandle = NULL;

void WaterIn(void) {
    // 1.打开进水泵
    // PumpDeal(PUMP_WATERIN,PUMP_ON);
    // vTaskDelay(pdMS_TO_TICKS(32000)); // 等待28秒
    //  // 3. 关闭进水泵
    // PumpDeal(PUMP_WATERIN,PUMP_OFF);
    ESP_LOGW("selfclean","Inlet valve opened");
}



void CloseAll(void) {

    ESP_LOGW("selfclean","All pumps and motors closed");
}


void HeapModeClean(float fTagetTem)
{
    float current_temp = 0.0; // 当前温度值
    current_temp = getTemp();
    
    while(current_temp>fTagetTem)
    {
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
}

void SelfCleanDelay(int DelayTime)
{
    for(int i=0;i<DelayTime*10;i++)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void LightSelfClean(void)
{
    CloseAll(); // 关闭所有泵和电机
    WaterIn(); // 进水
    HeapModeClean(80);
    for(int i=0;i<5;i++)
    {   

    }
    
}


void DeepSelfClean(void){
    for(int i=0;i<3;i++)
    {
        CloseAll(); // 关闭所有泵和电机
        WaterIn(); // 进水
        

       
      
    }

}
void SelfCleanTask(void *pvParameters)
{
    ESP_LOGI("SelfCleanTask","SelfClean Start");
    while(1)
    {
        //加400ml水
        ESP_LOGI("selfClean","selfClean 400ml Start");
        waterPumpOn(200);
        vTaskDelay(pdMS_TO_TICKS(100));
        waterPumpOn(200);
        vTaskDelay(pdMS_TO_TICKS(50*1000));
        waterPumpOff();
        vTaskDelay(pdMS_TO_TICKS(100));
        waterPumpOff();
        vTaskDelay(pdMS_TO_TICKS(100));

        NoBushRun(3);
        vTaskDelay(pdMS_TO_TICKS(1000*60));
        NoBushRun(0);
        vTaskDelay(pdMS_TO_TICKS(100));
        //打开阀门等待一分钟后，完全打开后，打开蠕动泵80s
        valveOn();
        vTaskDelay(pdMS_TO_TICKS(40000));
        hosePumpOn(200);
        vTaskDelay(pdMS_TO_TICKS(50000));
        //关闭阀门等待一分钟后，完全关闭后
        hosePumpOff();
        vTaskDelay(pdMS_TO_TICKS(100));
        valveOff();
        vTaskDelay(pdMS_TO_TICKS(60000));
        ESP_LOGI("selfClean","selfClean 400ml end");
        Play_Music();
       

        //加700ml水
        ESP_LOGI("selfClean","selfClean 700ml Start");
        waterPumpOn(200);
        vTaskDelay(pdMS_TO_TICKS(100));
        waterPumpOn(200);
        vTaskDelay(pdMS_TO_TICKS(88*1000));
        waterPumpOff();
        vTaskDelay(pdMS_TO_TICKS(100));
        waterPumpOff();

        NoBushRun(9);
        vTaskDelay(pdMS_TO_TICKS(100));
        Heat1Run(8);
        vTaskDelay(pdMS_TO_TICKS(100));
        Heat2Run(8);
        vTaskDelay(pdMS_TO_TICKS(100));
        int overTimeFlag=0;
        while(getTemp()<90)
        {
            overTimeFlag++;
            if(overTimeFlag>60*10)
            {
                ESP_LOGE("selfClean","selfClean 700ml heat over time");
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        Heat1Run(0);
        vTaskDelay(pdMS_TO_TICKS(100));
        Heat2Run(0);
        vTaskDelay(pdMS_TO_TICKS(100));
        for(int i=0;i<4;i++)
        {
            ESP_LOGW("SelfCleanTask","第%d轮",i);
            NoBushRun(9);
            vTaskDelay(pdMS_TO_TICKS(20000));
            NoBushRun(0);
            vTaskDelay(pdMS_TO_TICKS(100));
            NoBushRun(-9);
            vTaskDelay(pdMS_TO_TICKS(20000));
            NoBushRun(0);
            CheckDelay(2);
        }

        //打开阀门等待一分钟后，完全打开后，打开蠕动泵80s
        NoBushRun(3);
        vTaskDelay(pdMS_TO_TICKS(100));
        valveOn();
        vTaskDelay(pdMS_TO_TICKS(60000));
        hosePumpOn(200);
        vTaskDelay(pdMS_TO_TICKS(80000));
        NoBushRun(0);
        //关闭阀门等待一分钟后，完全关闭后
        hosePumpOff();
        vTaskDelay(pdMS_TO_TICKS(100));
        valveOff();
        vTaskDelay(pdMS_TO_TICKS(60000));     
        ESP_LOGI("selfClean","selfClean 500ml end");         
        Play_Music();

        //加400ml水
        ESP_LOGI("selfClean","selfClean 400ml start");   
        waterPumpOn(200);
        vTaskDelay(pdMS_TO_TICKS(100));
        waterPumpOn(200);
        vTaskDelay(pdMS_TO_TICKS(50*1000));
        waterPumpOff();
        vTaskDelay(pdMS_TO_TICKS(100));
        waterPumpOff();
        vTaskDelay(pdMS_TO_TICKS(100));

        for(int i=0;i<4;i++)
        {
            ESP_LOGW("SelfCleanTask","第%d轮",i);
            NoBushRun(9);
            vTaskDelay(pdMS_TO_TICKS(20000));
            NoBushRun(0);
            vTaskDelay(pdMS_TO_TICKS(100));
            NoBushRun(-9);
            vTaskDelay(pdMS_TO_TICKS(20000));
            NoBushRun(0);
            CheckDelay(2);
        }
        //打开阀门等待一分钟后，完全打开后，打开蠕动泵80s
        valveOn();
        vTaskDelay(pdMS_TO_TICKS(60000));
        NoBushRun(9);
        vTaskDelay(pdMS_TO_TICKS(100));
        hosePumpOn(200);
        vTaskDelay(pdMS_TO_TICKS(80000));
        NoBushRun(0);
        vTaskDelay(pdMS_TO_TICKS(100));
        //关闭阀门等待一分钟后，完全关闭后
        hosePumpOff();
        vTaskDelay(pdMS_TO_TICKS(100));
        hosePumpOff();
        vTaskDelay(pdMS_TO_TICKS(100));
        valveOff();
        vTaskDelay(pdMS_TO_TICKS(60000));
        ESP_LOGI("selfClean","selfClean 400ml end");  

        //加500ml水
        ESP_LOGI("selfClean","selfClean 500ml start");  
        waterPumpOn(200);
        vTaskDelay(pdMS_TO_TICKS(100));
        waterPumpOn(200);
        vTaskDelay(pdMS_TO_TICKS(63*1000));
        waterPumpOff();
        vTaskDelay(pdMS_TO_TICKS(100));
        waterPumpOff();
        vTaskDelay(pdMS_TO_TICKS(100)); 

        NoBushRun(9);
        vTaskDelay(pdMS_TO_TICKS(1000*60*5));
        
        //打开阀门等待一分钟后，完全打开后，打开蠕动泵80s
        valveOn();
        vTaskDelay(pdMS_TO_TICKS(60000));
        NoBushRun(9);
        vTaskDelay(pdMS_TO_TICKS(100));
        hosePumpOn(200);
        vTaskDelay(pdMS_TO_TICKS(80000));
        NoBushRun(0);
        vTaskDelay(pdMS_TO_TICKS(100));
        //关闭阀门等待一分钟后，完全关闭后
        hosePumpOff();
        vTaskDelay(pdMS_TO_TICKS(100));
        valveOff();
        vTaskDelay(pdMS_TO_TICKS(60000));
        Play_Music();
        ESP_LOGI("selfClean","selfClean 500ml end"); 

        vTaskDelete(NULL);
    }
}

void SelfCleanPause(void)
{
    NoBushRun(0);
    vTaskDelay(pdMS_TO_TICKS(100));
    Heat1Run(0);
    vTaskDelay(pdMS_TO_TICKS(100));
    Heat2Run(0);
    vTaskDelay(pdMS_TO_TICKS(100)); 
    waterPumpOff();
    vTaskDelay(pdMS_TO_TICKS(100));
}

void SelfCleanResume(void)
{

    

}

void SelfCleanEnd(void)
{
    NoBushRun(0);
    vTaskDelay(pdMS_TO_TICKS(100));
    Heat1Run(0);
    vTaskDelay(pdMS_TO_TICKS(100));
    Heat2Run(0);
    vTaskDelay(pdMS_TO_TICKS(100)); 
    waterPumpOff();
    vTaskDelay(pdMS_TO_TICKS(100));    
}