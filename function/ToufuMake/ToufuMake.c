#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "sys.h"

#include "ToufuMake.h"

#include "FunctionSelection.h"
#include "UsartExchange.h"

#include "DataCenter.h"
#include "press.h"
#include "Music.h"

const int StagePressure1 = 2500;
const int StagePressure2 = 3500;

const int Stage1Time = 60*2;
const int Stage2Time = 60*10;

const int BreakTime  = 10*10;

#include "tempurature.h"

TaskHandle_t TouFuHandle = NULL;

TaskMoniterHandle_t TouFuMoniter;

typedef struct{
    bool Press_Auto;
    int  Press_TargetValue;
}PressFunctionDetail_t;

typedef struct{
    bool Toufu_Clean;//是否自清洁
    bool Toufu_hardness;//嫩老豆腐
}ToufuFunctionDetail_t;

PressFunctionDetail_t PressFunctionDetail;

void braineAct(void)
{
    for(int i=0;i<4;i++)
    {
        NoBushRun(10);
        CheckDelay(1);
        brinePumpOn();
        vTaskDelay(pdMS_TO_TICKS(1000*5));
        //打开抽豆浆泵
        hosePumpOn(200);
        //等待15s
        CheckDelay(15);
        NoBushRun(0);
        vTaskDelay(pdMS_TO_TICKS(100));
        brinePumpOff();
        vTaskDelay(pdMS_TO_TICKS(100));
        hosePumpOff();
        CheckDelay(30);
    }
}
enum{
    waterInSatge,
    breakSoyStage,
    heatSoyMilkSatge,
    brineStage,
    waitToufuBrineStage,
    pressTouFuStage
}curTaskStage;

int taskCurStage;

void TouFuMakeTask(void* pvParameters)
{
    ESP_LOGI("TouFu","TouFu Make Start");
    while(1)
    {
        valveOff();
            
        vTaskDelay(pdMS_TO_TICKS(30*1000));

        TouFuMoniter.isRunning= true;
        riseMoterDown(23);
        
        // //开始制作豆浆

        taskCurStage=breakSoyStage;
        TouFuSoyMilk();
        ESP_LOGI("toufu","milk complate");
        taskCurStage = heatSoyMilkSatge;
        HeatSoyMilk();

        //获取加热的豆浆成功    
        Play_Music();
        
        vTaskDelay(pdMS_TO_TICKS(100));
        riseMoterDown(23);
        //打开阀
        vTaskDelay(pdMS_TO_TICKS(100));
        valveOn();
        vTaskDelay(pdMS_TO_TICKS(1000*30));
        riseMoterDown(23);
        //打开抽点卤剂的泵
        taskCurStage = brineStage;
        braineAct();
        vTaskDelay(pdMS_TO_TICKS(100));
        valveOff();
        //播放音乐
        Play_Music();
        taskCurStage = waitToufuBrineStage;
        //等待8分钟
        int countDown=480;
        while(countDown>0)
        {
            countDown--;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        riseMoterUp(23);
        vTaskDelay(pdMS_TO_TICKS(100));
        riseMoterUp(23);
        vTaskDelay(pdMS_TO_TICKS(100));
        //开始压制豆腐
        taskCurStage = pressTouFuStage;
        TouFuPress();
        riseMoterDown(23);
        Play_Music();

        TouFuMoniter.isRunning= false;
        TouFuMoniter.isEnd = true;
        vTaskDelete(NULL);
    }
}



void TouFuPause(void)
{
    ESP_LOGI("豆腐","豆腐任务停止");
    // PressSteptterMotor_Up();
    // vTaskDelay(pdMS_TO_TICKS(20000));
    pressMoterStop();
}


