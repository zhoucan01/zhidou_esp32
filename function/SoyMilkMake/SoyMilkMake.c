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

#include "tempurature.h"

#define MIDDLE_TEMPERATURE 80.0 // 中等温度限制（摄氏度)

TaskHandle_t SoyMilkMakeHandle_t = NULL;
TaskHandle_t SoyMilkMakeHandle =NULL;


static void MotorLowSpeed(int delay_s);
static void MotorHighSpeed(int delay_s);
void HeatSoyMilk(void);

TaskMoniterHandle_t SoyMilkMakeMoniter;
SoyMilkFuncDrtail_t SoyMilkFuncDrtail;

//创建一个软件定时器，用于溢流检测
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
  
        vTaskDelete(NULL);
    }
}



void SoyMilkLightContian(void)
{

}



void SoyMilkResume(void)
{
    float mk_temp=0.0; // 当前温度值
    mk_temp = getTemp();
    while(mk_temp < SoyMilkMakeMoniter.CurrentNum - 3)
    {
        mk_temp = getTemp();
        
        MotorLowSpeed(4);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}



void SoyMilkPause(void)
{

}

void SoyMilkEnd(void)
{
 
    SoyMilkPause();
    SoyMilkMakeMoniter.isRunning = false;
}

void SoyMilkKeppWarm(uint8_t  KeepWarmTemp)
{
    static int DelayNum=0;
    while(SoyMilkFuncDrtail.SoyMilk_Temp)
    {
        CheckDelay(2);
        DelayNum++;
        if(DelayNum>30)
        {

        }
        if(getTemp()<KeepWarmTemp)
        {     
            CheckDelay(2);  
        }
    }
}

void SoyMilkDecTemp(uint8_t DecTemp)
{
    while(getTemp()>DecTemp)
    {
        MotorHighSpeed(5);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

static void MotorLowSpeed(int delay_s)
{

}

static void MotorHighSpeed(int delay_s)
{

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

    //1050
    ESP_LOGI("TouFu","抽水开始");
    TickType_t xLastWakeTime = xTaskGetTickCount();
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(145*1000));
    ESP_LOGI("TouFu","抽水结束");

    waterPumpOff();
    vTaskDelay(pdMS_TO_TICKS(100));
    waterPumpOff();

    //转1分钟，停20s
    NoBushRun(-9);
    CheckDelay(60);
    moterDecSpd(-9);
    CheckDelay(20);

    //转1分钟，停20s
    NoBushRun(-9);
    CheckDelay(60);
    moterDecSpd(-9);
    CheckDelay(20);

    //转2分钟，停20s
    NoBushRun(-9);
    CheckDelay(60*2);
    moterDecSpd(-9);
    CheckDelay(20);

    //转1分钟，停20s
    NoBushRun(-9);
    CheckDelay(60);
    moterDecSpd(-9);
    CheckDelay(20);

    //转1分钟，停20s
    // NoBushRun(-9);
    // CheckDelay(60);
    // moterDecSpd(-9);
    // CheckDelay(20);

    //转1分钟，停20s
    // NoBushRun(-9);
    // CheckDelay(60);
    // moterDecSpd(-9);
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
void moterPulseRun(void)
{
    static int RunFlag = 0;
    const int RUN_TIME = 7;    // 运行8个周期
    const int TOTAL = 9;      // 总周期（8运行 + 3停止）
    
    RunFlag++;
    if(RunFlag >= TOTAL) RunFlag = 0;
    
    if(RunFlag < RUN_TIME) {
        NoBushRun(3);   // 运行阶段（RunFlag=0~7）
    } else {
        NoBushRun(0);   // 停止阶段（RunFlag=8~10）
    }
}
    int num90=0;
    int overTimeFlag=0;
    int taskmoterRunFlag=0;
void HeatSoyMilk(void)
{
    NoBushRun(3);
    CheckDelay(1);
    Heat1Run(12);
    CheckDelay(1);
    Heat2Run(12);
    while(getTemp()<60)
    {
        CheckDelay(1);
        ESP_LOGI("60 temp","now:%f",getTemp());
    }    

    NoBushRun(2);
    CheckDelay(1);
    NoBushRun(1);
    CheckDelay(1);
    NoBushRun(0);
    CheckDelay(1);
    Heat2Run(0);
    // CheckDelay(60);//自然消泡

    while(getTemp()<80)//80度附近
    {
        // NoBushRun(3);
        Heat2Run(7);
        taskmoterRunFlag++;
        if(taskmoterRunFlag>6){taskmoterRunFlag=0;}
        if(0==taskmoterRunFlag){NoBushRun(3);}
        // if(6==taskmoterRunFlag){NoBushRun(0);}
        CheckDelay(1);
        overTimeFlag++;
        if(overTimeFlag>720){break;}
        ESP_LOGI("80 temp","now:%f",getTemp());
    } 
    overTimeFlag=0;  
    NoBushRun(0);
    CheckDelay(1);
    Heat2Run(0);
    CheckDelay(30);//自然消泡

    while(getTemp()<85)//85度附近
    { 
        // NoBushRun(4);
        if(num90>=0&&num90<=6){Heat1Run(7);vTaskDelay(pdMS_TO_TICKS(100));Heat2Run(7);}
        if(num90>6)           {Heat1Run(0);vTaskDelay(pdMS_TO_TICKS(100));Heat2Run(0);}
        num90++;
        taskmoterRunFlag++;
        if(num90>10){num90=0;}
        if(taskmoterRunFlag>6){taskmoterRunFlag=0;}
        if(0==taskmoterRunFlag){NoBushRun(3);}
        // if(6==taskmoterRunFlag){NoBushRun(0);}
        CheckDelay(1);
        overTimeFlag++;
        if(overTimeFlag>720){break;}
        ESP_LOGI("85 temp","now:%f",getTemp());
    } 
    overTimeFlag=0;  
    NoBushRun(0);
    CheckDelay(1);
    Heat2Run(0);
    CheckDelay(30);//自然消泡

    while(getTemp()<90)//90度附近
    {
        // NoBushRun(4);
        if(num90>=0&&num90<=6){Heat1Run(8);vTaskDelay(pdMS_TO_TICKS(100));Heat2Run(8);}
        if(num90>6)           {Heat1Run(0);vTaskDelay(pdMS_TO_TICKS(100));Heat2Run(0);}
        num90++;
        taskmoterRunFlag++;
        if(num90>10){num90=0;}
        if(taskmoterRunFlag>6){taskmoterRunFlag=0;}
        if(0==taskmoterRunFlag){NoBushRun(3);}
        // if(6==taskmoterRunFlag){NoBushRun(0);}
        CheckDelay(1);
        overTimeFlag++;
        if(overTimeFlag>720){break;}
        ESP_LOGI("90 temp","now:%f",getTemp());
    } 
    overTimeFlag=0;  
    NoBushRun(0);
    CheckDelay(1);
    Heat2Run(0);
    CheckDelay(60);//自然消泡
    NoBushRun(0);
    CheckDelay(1);
    while(getTemp()<93)//93度附近
    {
        // NoBushRun(4);
        if(num90>=0&&num90<=6){Heat1Run(8);vTaskDelay(pdMS_TO_TICKS(100));Heat2Run(8);}
        if(num90>6)           {Heat1Run(0);vTaskDelay(pdMS_TO_TICKS(100));Heat2Run(0);}//6是加热时间
        num90++;
        taskmoterRunFlag++;
        if(num90>10){num90=0;}//(16-6)是停止时间 停10s
        if(taskmoterRunFlag>6){taskmoterRunFlag=0;}
        if(0==taskmoterRunFlag){NoBushRun(3);}
        // if(6==taskmoterRunFlag){NoBushRun(0);}
        CheckDelay(1);
        overTimeFlag++;
        if(overTimeFlag>720){break;}
        ESP_LOGI("93 temp","now:%f",getTemp());
    } 
    overTimeFlag=0; 
    NoBushRun(0);
    CheckDelay(1);
    Heat2Run(0);
    CheckDelay(1);
    CheckDelay(60);

    while(getTemp()<95)//95度附近
    {
        // NoBushRun(4);
        if(num90>=0&&num90<=6){Heat1Run(6);vTaskDelay(pdMS_TO_TICKS(100));Heat2Run(6);}
        if(num90>6)           {Heat1Run(0);vTaskDelay(pdMS_TO_TICKS(100));Heat2Run(0);}
        num90++;
        taskmoterRunFlag++;
        if(num90>10){num90=0;} //(18-6)是停止时间 停12s
        if(taskmoterRunFlag>6){taskmoterRunFlag=0;}
        if(0==taskmoterRunFlag){NoBushRun(3);}
        // if(6==taskmoterRunFlag){NoBushRun(0);}
        CheckDelay(1);
        overTimeFlag++;
        if(overTimeFlag>720){break;}
        ESP_LOGI("95 temp","now:%f",getTemp());
    } 
    overTimeFlag=0; 
    NoBushRun(0);
    CheckDelay(1);
    Heat2Run(0);
    CheckDelay(1);
    CheckDelay(30);

    while(getTemp()<98)//98度附近
    {
        // NoBushRun(4);


        if(num90>=0&&num90<=6){Heat1Run(6);vTaskDelay(pdMS_TO_TICKS(100));Heat2Run(6);}
        if(num90>6)           {Heat1Run(0);vTaskDelay(pdMS_TO_TICKS(100));Heat2Run(0);}
        num90++;
        taskmoterRunFlag++;
        if(num90>12){num90=0;}
        if(taskmoterRunFlag>6){taskmoterRunFlag=0;}
        if(0==taskmoterRunFlag){NoBushRun(3);}
        // if(6==taskmoterRunFlag){NoBushRun(0);}
        CheckDelay(1);
        overTimeFlag++;
        if(overTimeFlag>720){break;}
        ESP_LOGI("98 temp","now:%f",getTemp());
    } 
    //保温阶段
    num90=0;
    NoBushRun(0);
    CheckDelay(1);
    
    for(int i=0;i<180;i++)
    {
        if(getTemp()<98)
        {
            if(num90>=0&&num90<=6){Heat1Run(4);vTaskDelay(pdMS_TO_TICKS(100));Heat2Run(4);}
            if(num90>6)           {Heat1Run(0);vTaskDelay(pdMS_TO_TICKS(100));Heat2Run(0);}
            num90++;
            if(num90>18)
            {
                num90=0;
            }
        }
        else
        {
            Heat2Run(0);
        }
        
        if(taskmoterRunFlag>10){taskmoterRunFlag=0;}
        if(0==taskmoterRunFlag){NoBushRun(2);}
        if(6==taskmoterRunFlag){NoBushRun(0);}
        taskmoterRunFlag++;
        CheckDelay(1);
        ESP_LOGI("keep temp","now:%f",getTemp());
    }

    Heat2Run(0);
    vTaskDelay(pdMS_TO_TICKS(100));
    NoBushRun(0);
    CheckDelay(1);
    taskmoterRunFlag=0;
    while(getTemp()>88)
    {
        if(taskmoterRunFlag>10){taskmoterRunFlag=0;}
        if(0==taskmoterRunFlag){NoBushRun(3);}
        if(6==taskmoterRunFlag){NoBushRun(0);}
        taskmoterRunFlag++;
        CheckDelay(1);
        ESP_LOGI("soymilk","dec temp");
    }
    
    NoBushRun(0);
    overTimeFlag=0;      
    Play_Music();    
}

#endif

void ForceOutSoyMilk(void)
{

    //打开出浆阀门

    //打开出浆泵
    //延时
    vTaskDelay(pdMS_TO_TICKS(100));
    //关闭出浆阀门

}

