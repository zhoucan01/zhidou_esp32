#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "sys.h"
#include "SelfClean.h"
#include "DataCenter.h"
#include "FunctionSelection.h"

#include "UsartExchange.h"
#include "Music.h"
//自清洁任务句柄
TaskHandle_t SelfCleanHandle = NULL;

void SelfCleanTask(void *pvParameters)
{
    ESP_LOGI("Task", "Task Start: SelfCleanTask, handle=%p",
             xTaskGetCurrentTaskHandle());

    if (!runPowerCalibration(60000U)) {
        ESP_LOGE("SelfCleanTask", "功率校准失败，取消自清洁任务");
        SelfCleanEnd();
        setHighBroad(0); /* 中止未完成的校准并关闭动力板输出。 */
        FunctionSelectionFlowCompleted(SELFCLEAN_SEC);
        SelfCleanHandle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // while(1)
    // {
        
    //     //加400ml水
    //     ESP_LOGI("selfClean","selfClean 400ml Start");
    //     waterPumpOn(200);
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     waterPumpOn(200);
    //     vTaskDelay(pdMS_TO_TICKS(50*1000));
    //     waterPumpOff();
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     waterPumpOff();
    //     vTaskDelay(pdMS_TO_TICKS(100));

    //     NoBushRun(-3);
    //     vTaskDelay(pdMS_TO_TICKS(1000*60));
    //     NoBushRun(0);
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     //打开阀门等待一分钟后，完全打开后，打开蠕动泵80s
    //     valveOn();
    //     vTaskDelay(pdMS_TO_TICKS(40000));
    //     hosePumpOn(90);
    //     vTaskDelay(pdMS_TO_TICKS(50000));
    //     //关闭阀门等待一分钟后，完全关闭后
    //     hosePumpOff();
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     valveOff();
    //     vTaskDelay(pdMS_TO_TICKS(60000));
    //     ESP_LOGI("selfClean","selfClean 400ml end");
    //     Play_Music();
       

    //     //加700ml水
    //     ESP_LOGI("selfClean","selfClean 700ml Start");
    //     waterPumpOn(200);
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     waterPumpOn(200);
    //     vTaskDelay(pdMS_TO_TICKS(88*1000));
    //     waterPumpOff();
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     waterPumpOff();

    //     NoBushRun(-9);
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     Heat1Run(8);
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     // Heat2Run(8);
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     int overTimeFlag=0;
    //     while(getTemp()<90)
    //     {
    //         overTimeFlag++;
    //         if(overTimeFlag>60*10)
    //         {
    //             ESP_LOGE("selfClean","selfClean 700ml heat over time");
    //             break;
    //         }
    //         vTaskDelay(pdMS_TO_TICKS(1000));
    //     }
    //     Heat1Run(0);
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     // Heat2Run(0);
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     for(int i=0;i<4;i++)
    //     {
    //         ESP_LOGW("SelfCleanTask","第%d轮",i);
    //         NoBushRun(-9);
    //         vTaskDelay(pdMS_TO_TICKS(20000));
    //         NoBushRun(0);
    //         CheckDelay(2);
    //     }

    //     //打开阀门等待一分钟后，完全打开后，打开蠕动泵80s
    //     NoBushRun(-3);
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     valveOn();
    //     vTaskDelay(pdMS_TO_TICKS(60000));
    //     hosePumpOn(90);
    //     vTaskDelay(pdMS_TO_TICKS(80000));
    //     NoBushRun(0);
    //     //关闭阀门等待一分钟后，完全关闭后
    //     hosePumpOff();
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     valveOff();
    //     vTaskDelay(pdMS_TO_TICKS(60000));     
    //     ESP_LOGI("selfClean","selfClean 500ml end");         
    //     Play_Music();

    //     //加400ml水
    //     ESP_LOGI("selfClean","selfClean 400ml start");   
    //     waterPumpOn(200);
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     waterPumpOn(200);
    //     vTaskDelay(pdMS_TO_TICKS(50*1000));
    //     waterPumpOff();
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     waterPumpOff();
    //     vTaskDelay(pdMS_TO_TICKS(100));

    //     for(int i=0;i<4;i++)
    //     {
    //         ESP_LOGW("SelfCleanTask","第%d轮",i);
    //         NoBushRun(-9);
    //         vTaskDelay(pdMS_TO_TICKS(20000));
    //         NoBushRun(0);
    //         CheckDelay(2);
    //     }
    //     //打开阀门等待一分钟后，完全打开后，打开蠕动泵80s
    //     valveOn();
    //     vTaskDelay(pdMS_TO_TICKS(60000));
    //     NoBushRun(-9);
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     hosePumpOn(90);
    //     vTaskDelay(pdMS_TO_TICKS(80000));
    //     NoBushRun(0);
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     //关闭阀门等待一分钟后，完全关闭后
    //     hosePumpOff();
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     hosePumpOff();
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     valveOff();
    //     vTaskDelay(pdMS_TO_TICKS(60000));
    //     ESP_LOGI("selfClean","selfClean 400ml end");  

    //     //加500ml水
    //     ESP_LOGI("selfClean","selfClean 500ml start");  
    //     waterPumpOn(200);
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     waterPumpOn(200);
    //     vTaskDelay(pdMS_TO_TICKS(63*1000));
    //     waterPumpOff();
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     waterPumpOff();
    //     vTaskDelay(pdMS_TO_TICKS(100)); 

    //     NoBushRun(-9);
    //     vTaskDelay(pdMS_TO_TICKS(1000*60*5));
        
    //     //打开阀门等待一分钟后，完全打开后，打开蠕动泵80s
    //     valveOn();
    //     vTaskDelay(pdMS_TO_TICKS(60000));
    //     NoBushRun(-9);
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     hosePumpOn(90);
    //     vTaskDelay(pdMS_TO_TICKS(80000));
    //     NoBushRun(0);
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     //关闭阀门等待一分钟后，完全关闭后
    //     hosePumpOff();
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     valveOff();
    //     vTaskDelay(pdMS_TO_TICKS(60000));
    //     Play_Music();
    //     ESP_LOGI("selfClean","selfClean 500ml end"); 

    //     FunctionSelectionFlowCompleted(SELFCLEAN_SEC);
    //     SelfCleanHandle = NULL;
    //     vTaskDelete(NULL);
    // }

    ESP_LOGI("selfClean","selfClean start");
    //豆渣初步清洗
    valveOn();
    vTaskDelay(pdMS_TO_TICKS(10000));
    NoBushRun(3);
    vTaskDelay(pdMS_TO_TICKS(20000));
    hosePumpOn(100);
    vTaskDelay(pdMS_TO_TICKS(20000));
    NoBushRun(0);
    hosePumpOff();
    vTaskDelay(pdMS_TO_TICKS(100));
    waterPumpOn(200);
    ESP_LOGI("selfClean","开始进水");
    vTaskDelay(pdMS_TO_TICKS(60*1000));
    waterPumpOff();
    ESP_LOGI("selfClean","进水完成");
    vTaskDelay(pdMS_TO_TICKS(100));
    for(uint8_t i=0;i<40;i++)
    {
        if((i+1)%4!=0)NoBushRun(14);
        else NoBushRun(0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    NoBushRun(1);
    hosePumpOn(100);
    vTaskDelay(pdMS_TO_TICKS(25000));
    hosePumpOff();
    NoBushRun(0);
    vTaskDelay(pdMS_TO_TICKS(100));
    valveOff();

    Play_Music();
    ESP_LOGI("selfClean","clearner 14g");
    vTaskDelay(pdMS_TO_TICKS(60*1000));

    //清洁剂清洗
    Play_Music();
    waterPumpOn(200);
    ESP_LOGI("selfClean","开始进水");
    vTaskDelay(pdMS_TO_TICKS(80*1000));
    waterPumpOff();
    ESP_LOGI("selfClean","进水完成");
    vTaskDelay(pdMS_TO_TICKS(100));
    NoBushRun(5);
    vTaskDelay(pdMS_TO_TICKS(30*1000));
    NoBushRun(0);
    vTaskDelay(pdMS_TO_TICKS(100));
    Heat1Run(14);
    while(getTemp()<98){vTaskDelay(pdMS_TO_TICKS(200));}
    Heat1Run(0);
    ESP_LOGI("selfClean","heating completed");
    for(uint8_t i=0;i<3;i++)
    {
        ESP_LOGI("selfClean","清洗剂第%d轮清洗",i+1);
        vTaskDelay(pdMS_TO_TICKS(180*1000));
        for(uint16_t j=0;j<180;j++)
        {
            if((j+1)%4!=0)NoBushRun(14);
            else NoBushRun(0);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        if(i<2)NoBushRun(0);
    }
    valveOn();
    vTaskDelay(pdMS_TO_TICKS(100));
    NoBushRun(1);
    vTaskDelay(pdMS_TO_TICKS(30000));
    hosePumpOn(100);
    vTaskDelay(pdMS_TO_TICKS(25000));
    hosePumpOff();
    vTaskDelay(pdMS_TO_TICKS(100));
    NoBushRun(0);

    //清洗泡沫和残留
    ESP_LOGI("selfClean","clear the residue");
    for(uint8_t i=0;i<2;i++)
    {
        waterPumpOn(200);
        ESP_LOGI("selfClean","开始进水");
        vTaskDelay(pdMS_TO_TICKS(60*1000));
        waterPumpOff();
        ESP_LOGI("selfClean","进水完成");
        vTaskDelay(pdMS_TO_TICKS(100));
        for(uint8_t j=0;j<40;j++)
        {
            if((j+1)%4!=0)NoBushRun(14);
            else NoBushRun(0);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        NoBushRun(1);
        hosePumpOn(100);
        vTaskDelay(pdMS_TO_TICKS(25000));
        hosePumpOff();
        vTaskDelay(pdMS_TO_TICKS(100));
        NoBushRun(0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    valveOff();
    vTaskDelay(pdMS_TO_TICKS(30000));
    NoBushRun(0);
    vTaskDelay(pdMS_TO_TICKS(100));
    Heat1Run(0);
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI("selfClean","selfClean accomplished");
    Play_Music();

    FunctionSelectionFlowCompleted(SELFCLEAN_SEC);
    SelfCleanHandle = NULL;
    vTaskDelete(NULL);
}

void SelfCleanPause(void)
{
    ESP_LOGI("selfClean","selfClean Pause");
    Heat1Run(0);
    // Heat2Run(0);
    NoBushRun(0);
    hosePumpOff();
    waterPumpOff();
    brinePumpOff();
}

void SelfCleanResume(void)
{
    ESP_LOGI("selfClean","selfClean Resume");
}

void SelfCleanEnd(void)
{
    ESP_LOGI("selfClean","selfClean End");
    Heat1Run(0);
    Heat2Run(0);
    NoBushRun(0);
    hosePumpOff();
    waterPumpOff();
    brinePumpOff();
}
