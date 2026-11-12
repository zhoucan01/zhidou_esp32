#include <stdio.h>
#include <stdint.h>
#include "stdbool.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "sys.h"
#include "SoyMilkMake.h"

#include "Music.h"
#include "UsartExchange.h"

#include "DataCenter.h"

#include "driver/gpio.h"

#include "FunctionSelection.h"



TaskHandle_t SoyMilkMakeHandle_t = NULL;
TaskHandle_t SoyMilkMakeHandle =NULL;

void HeatSoyMilk(void);

/* 通用搅拌：0.5s 一拍，onHalf 拍开、offHalf 拍停；本函数跑满 1 秒(2 拍，含 0.5s 延时)，
   以保持外层加热循环仍是 1 秒节拍(加热档位/超时都不需要改)。
   开3停0.5 => stirOneSecond(&cnt, 6, 1); */
void stirOneSecond(int *cnt, int onHalf, int offHalf)
{
    for(int i = 0; i < 2; i++)
    {
        NoBushRun((*cnt < onHalf) ? 1 : 0);
        if(++(*cnt) >= onHalf + offHalf) { *cnt = 0; }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

TaskMoniterHandle_t SoyMilkMakeMoniter;
SoyMilkFuncDrtail_t SoyMilkFuncDrtail;


void SoyMilkMake_Task(void *pvParameters)
{
    
    ESP_LOGI("SoyMilk","SoyMilk Make Start");
    ESP_LOGI("SoyMilk", "=== Task Entry ===");
    ESP_LOGI("SoyMilk", "Task handle: %p", xTaskGetCurrentTaskHandle());
    while(1)
    {
        ESP_LOGI("SoyMilkMake", "SoyMilkMake_Task Start");
        TouFuSoyMilk();
        HeatSoyMilk();
        Play_Music();
  
        FunctionSelectionFlowCompleted(SOYMILK_SEC);
        SoyMilkMakeHandle = NULL;
        vTaskDelete(NULL);
    }
}

void moterDecSpd(int currSpd)
{
    if(currSpd>0)//正转
    {
        for(int i=currSpd;i>0;i--)
        {
            NoBushRun(i);
            CheckDelay(1);
        }
    }
    else//反转
    {
        for(int i=currSpd;i<0;i++)
        {
            NoBushRun(i);
            CheckDelay(1);
        }
    }
    NoBushRun(0);
}

void TouFuSoyMilk(void)
{

    waterPumpOn(200);
    vTaskDelay(pdMS_TO_TICKS(100));
    waterPumpOn(200);

    //1150
    ESP_LOGI("TouFu","抽水开始");
    TickType_t xLastWakeTime = xTaskGetTickCount();
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(60*1000*4));
    ESP_LOGI("TouFu","抽水结束");
  
    waterPumpOff();
    Play_Music();
    //转1分钟，停20s
    NoBushRun(6);
    CheckDelay(60);
    moterDecSpd(6);
    CheckDelay(20);

    //转1分钟，停20s
    NoBushRun(7);
    CheckDelay(60);
    moterDecSpd(7);
    CheckDelay(20);

    //转2分钟，停20s
    NoBushRun(7);
    CheckDelay(60*2);
    moterDecSpd(7);
    CheckDelay(20);

    // 转1分钟，停20s
    // NoBushRun(9);
    // CheckDelay(60);
    // moterDecSpd(9);
    // CheckDelay(20);

    Play_Music();
}

#define EVTspecial 0
#if EVTspecial == 1

void HeatSoyMilk(void)
{
    NoBushRun(3);
    CheckDelay(5);
    Heat1Run(11);
    CheckDelay(1);
    Heat2Run(11);
    xTaskCreate(tempNNTask, "tempNNTask", 3072, NULL, 19, NULL);
    int moterFlag=0;
    while(getTemp()<68)
    {
        moterFlag++;
        if(moterFlag>5)
        {
            moterFlag=0;
            NoBushRun(3);
            CheckDelay(1);
        }
        CheckDelay(1);
        ESP_LOGI("68 temp","now:%f",getTemp());
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    NoBushRun(2);
    vTaskDelay(pdMS_TO_TICKS(500));  
    NoBushRun(0);
    int Num70=0;
    int overTimeFlag=0;
    int taskmoterRunFlag=0;
    while(getTemp()<87)
    {

        vTaskDelay(pdMS_TO_TICKS(100));
        if(0==Num70){Heat1Run(10);vTaskDelay(pdMS_TO_TICKS(100));Heat2Run(10);}
        if(6==Num70){Heat1Run(0);vTaskDelay(pdMS_TO_TICKS(100));Heat2Run(0);}
        Num70++;
        taskmoterRunFlag++;
        if(Num70>8){Num70=0;}
        if(taskmoterRunFlag>10){taskmoterRunFlag=0;}
        if(0==taskmoterRunFlag){NoBushRun(5);}
        if(9==taskmoterRunFlag){NoBushRun(0);}
        CheckDelay(1);
        overTimeFlag++;
        if(overTimeFlag>720){break;}
        ESP_LOGI("80 temp","now:%f",getTemp());
    }//实际温度在90度附近

    overTimeFlag=0;
    NoBushRun(0);
    vTaskDelay(pdMS_TO_TICKS(100));
    Heat1Run(0);
    vTaskDelay(pdMS_TO_TICKS(100));
    Heat2Run(0);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    for(int i=0;i<8;i++)
    {

        //一次缓慢加热
        vTaskDelay(pdMS_TO_TICKS(100));
        NoBushRun(3);
        ESP_LOGI("SoyMilkMake","一次缓慢第%d轮",i);
        vTaskDelay(pdMS_TO_TICKS(1000*8));
        NoBushRun(0);

        vTaskDelay(pdMS_TO_TICKS(100));
        Heat2Run(8);
        vTaskDelay(pdMS_TO_TICKS(100));
        Heat1Run(8);
        vTaskDelay(pdMS_TO_TICKS(1000*8));

        Heat2Run(0);
        vTaskDelay(pdMS_TO_TICKS(100));
        Heat1Run(0);        
        vTaskDelay(pdMS_TO_TICKS(1000*8));

        NoBushRun(5);
        vTaskDelay(pdMS_TO_TICKS(1000*8));
        NoBushRun(0);
        vTaskDelay(pdMS_TO_TICKS(100));

        Heat2Run(5);
        vTaskDelay(pdMS_TO_TICKS(100));
        Heat1Run(5);
        vTaskDelay(pdMS_TO_TICKS(1000*10));

        Heat2Run(0);
        vTaskDelay(pdMS_TO_TICKS(100));
        Heat1Run(0);      
        vTaskDelay(pdMS_TO_TICKS(1000*8));

        NoBushRun(5);
        vTaskDelay(pdMS_TO_TICKS(1000*8));
        NoBushRun(0);
        vTaskDelay(pdMS_TO_TICKS(100));

        Heat2Run(10);
        vTaskDelay(pdMS_TO_TICKS(100));
        Heat1Run(5);
        vTaskDelay(pdMS_TO_TICKS(1000*8)); 

        NoBushRun(5);
        vTaskDelay(pdMS_TO_TICKS(1000*8));
        NoBushRun(0);
        vTaskDelay(pdMS_TO_TICKS(100));

        Heat2Run(0);
        vTaskDelay(pdMS_TO_TICKS(100));
        Heat1Run(0);        
        vTaskDelay(pdMS_TO_TICKS(1000*8));

    }
    
    for(int i=0;i<8;i++)
    {
        NoBushRun(5);
        vTaskDelay(pdMS_TO_TICKS(1000*8));
        NoBushRun(0);
        vTaskDelay(pdMS_TO_TICKS(100));

        ESP_LOGI("SoyMilkMake","二次缓慢第%d轮",i);
        Heat2Run(6);
        vTaskDelay(pdMS_TO_TICKS(1000*8));
        Heat2Run(0);
        vTaskDelay(pdMS_TO_TICKS(1000*10));
    }

//保温
    for(int i=0;i<8;i++)
    {
        NoBushRun(5);
        vTaskDelay(pdMS_TO_TICKS(1000*6));
        NoBushRun(0);
        vTaskDelay(pdMS_TO_TICKS(100));

        ESP_LOGI("SoyMilkMake","二次缓慢第%d轮",i);
        Heat2Run(5);
        vTaskDelay(pdMS_TO_TICKS(1000*8));
        Heat2Run(0);
        vTaskDelay(pdMS_TO_TICKS(1000*10));
    }
  
    Play_Music();
    vTaskDelay(pdMS_TO_TICKS(1000*60*13));

    Heat1Run(0);
    CheckDelay(1);
    Heat2Run(0);
    CheckDelay(1);
    NoBushRun(0);
    CheckDelay(1);
    Play_Music();
    
}

#else

int motorNum = 0;//电机运行计数
int heatNum = 0;//加热运行计数
/* 单段加热超时保护：85℃ 以上各段累计超过 HEAT_STAGE_TIMEOUT_S 秒仍未到温则 */
/* 跳出该段，避免温度上不去时无限死等。每段开始时清零；0→60℃ 与 60→80℃ 两段不限制。 */
int overTimeFlag = 0;
#define HEAT_STAGE_TIMEOUT_S 720

/* 降温撒水：开清水泵 COOL_SPRINKLE_SEC 秒（保持 1 秒搅拌节拍），再关泵。
 * 清水泵 = 线圈 0 / 保持寄存器 0x0000（写入值/10 = 速度百分比）。 */
#define COOL_SPRINKLE_SEC     5
#define COOL_SPRINKLE_SPEED   200U

static void coolSprinkle(void)
{
    ESP_LOGI("soymilk","清水泵降温撒水 %d 秒", COOL_SPRINKLE_SEC);
    waterPumpOn(COOL_SPRINKLE_SPEED);
    for(int i = 0; i < COOL_SPRINKLE_SEC; i++)
    {
        stirOneSecond(&motorNum, 6, 1);
    }
    waterPumpOff();
}

void HeatSoyMilk(void)
{
    ESP_LOGI("SoyMilkMake","HeatSoyMilk Start");

    
    //加热到60度
    while(getTemp()<60)//搅拌开2停0.5
    {  
         Heat1Run(19);

        stirOneSecond(&motorNum, 6, 1);
        heatNum++;
        if(heatNum>=8)   {heatNum=0;}
        ESP_LOGI("60 temp","now:%f,num:%d,H:%d",getTemp(),motorNum,heatNum);
    }    
    heatNum=0;
    motorNum=0;
    
    //加热到80度
    
    while(getTemp()<80)//搅拌开2停0.5
    {
        Heat1Run(10);
        stirOneSecond(&motorNum, 6, 1);
        heatNum++;
        if(heatNum>=8)   {heatNum=0;}
        ESP_LOGI("80 temp","now:%f,num:%d,H:%d",getTemp(),motorNum,heatNum);
    }

    heatNum=0;
    motorNum=0;
    //加热到85度
    overTimeFlag = 0;
    while(getTemp()<85)//搅拌开2停0.5
    {

        if(heatNum<6) {Heat1Run(10);}
        if(heatNum>=6&&heatNum<10) {Heat1Run(0);}       
        stirOneSecond(&motorNum, 6, 1);
        heatNum++;
        if(heatNum>=10)   {heatNum=0;}
        overTimeFlag++;
        if(overTimeFlag>HEAT_STAGE_TIMEOUT_S){break;}
        ESP_LOGI("85 temp","now:%f,num:%d,H:%d",getTemp(),motorNum,heatNum);
    } 

    heatNum=0;
    motorNum=0;
    //加热到90度
    overTimeFlag = 0;
    while(getTemp()<90)//搅拌开2停0.5
    {
        
        if(heatNum<6) {Heat1Run(9);}
        if(heatNum>=6&&heatNum<21) {Heat1Run(0);}       
        stirOneSecond(&motorNum, 8, 1);
        heatNum++;
        if(heatNum>=21)   {heatNum=0;}
        overTimeFlag++;
        if(overTimeFlag>HEAT_STAGE_TIMEOUT_S){break;}
        ESP_LOGI("90 temp","now:%f,num:%d,H:%d",getTemp(),motorNum,heatNum);
    }     

    heatNum=0;
    motorNum=0;
    //加热到93度
    overTimeFlag = 0;
    while(getTemp()<93)//搅拌开2停0.5
    {
        
        if(heatNum<6) {Heat1Run(9);}
        if(heatNum>=6&&heatNum<10) {Heat1Run(0);}       
        stirOneSecond(&motorNum, 6, 1);
        heatNum++;
        if(heatNum>=10)   {heatNum=0;}
        overTimeFlag++;
        if(overTimeFlag>HEAT_STAGE_TIMEOUT_S){break;}
        ESP_LOGI("93 temp","now:%f,num:%d,H:%d",getTemp(),motorNum,heatNum);
    }     

    heatNum=0;
    motorNum=0;
    //加热到95度
    overTimeFlag = 0;
    while(getTemp()<95)//搅拌开2停0.5
    {
        
        if(heatNum<5) {Heat1Run(7);}
        if(heatNum>=5&&heatNum<10) {Heat1Run(0);}       
        stirOneSecond(&motorNum, 6, 1);
        heatNum++;
        if(heatNum>=10)   {heatNum=0;}
        overTimeFlag++;
        if(overTimeFlag>HEAT_STAGE_TIMEOUT_S){break;}
        ESP_LOGI("95 temp","now:%f,num:%d,H:%d",getTemp(),motorNum,heatNum);
    }     

    heatNum=0;
    motorNum=0;
    //加热到98度
    overTimeFlag = 0;
    while(!isTemperatureOver98Confirmed())//搅拌开3停1
    {
        
        if(heatNum<6) {Heat1Run(7);}
        if(heatNum>=6&&heatNum<10) {Heat1Run(0);}       
        stirOneSecond(&motorNum, 6, 2);
        heatNum++;
        if(heatNum>=10)   {heatNum=0;}
        overTimeFlag++;
        if(overTimeFlag>HEAT_STAGE_TIMEOUT_S){break;}
        ESP_LOGI("98 temp","now:%f,num:%d,H:%d",getTemp(),motorNum,heatNum);
    }  

    //降温到85度：关加热后搅拌，等桶温自然降到85度以下（与老代码一致）
    //降到 95℃ / 93℃ 时各开一次清水泵 5 秒帮助降温
    Heat1Run(0);
    heatNum=0;
    motorNum=0;
    {
        bool sprinkle95Done = false;
        bool sprinkle93Done = false;
        while(getTemp()>89)
        {
            float temperature = getTemp();
            if(!sprinkle95Done && temperature <= 95.0f)
            {
                sprinkle95Done = true;
                coolSprinkle();
            }
            if(!sprinkle93Done && temperature <= 93.0f)
            {
                sprinkle93Done = true;
                coolSprinkle();
            }
            stirOneSecond(&motorNum, 6, 1);
            ESP_LOGI("soymilk","dec temp:%f,num:%d",getTemp(),motorNum);
        }
    }
    NoBushRun(0);

    Play_Music();    
}

#endif
//豆浆任务恢复
void SoyMilkResume(void)
{
    ESP_LOGI("SoyMilk","SoyMilk Resume");
}



void SoyMilkPause(void)
{
    ESP_LOGI("SoyMilk","SoyMilk Pulse");
    Heat1Run(0);
    Heat2Run(0);
    NoBushRun(0);
    hosePumpOff();
    waterPumpOff();
    brinePumpOff();
}

void SoyMilkEnd(void)
{
    ESP_LOGI("SoyMilk","SoyMilk End");
    Heat1Run(0);
    Heat2Run(0);
    NoBushRun(0);
    hosePumpOff();
    waterPumpOff();
    brinePumpOff();
    SoyMilkMakeMoniter.isRunning = false;
}


