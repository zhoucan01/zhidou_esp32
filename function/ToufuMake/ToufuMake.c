#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "sys.h"

#include "ToufuMake.h"
#include "brine.h"
#include "SoyMilkMake.h"


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

enum{
    waterInSatge,
    breakSoyStage,
    heatSoyMilkSatge,
    brineStage,
    waitToufuBrineStage,
    pressTouFuStage
}curTaskStage;

int taskCurStage;


/* ---- 流程门禁：站2(AT32) 必须在线 -----------------------------------------
 * 站2 挂的是水泵/阀门/抬升/压制，以及温度、编码器、限位反馈。站2 不在线时，
 * 发给它的命令重试 2 次后就被静默丢弃（DataCenter execute_command_with_retry），
 * 而流程里的等待条件（编码器到位/限位/温度）读到的全是旧值或 0，会死等或直接
 * 漏掉动作（例如不加水就破壁）。所以流程开始前先等它上线再往下跑。 */
#define ACTUATOR_ONLINE_TIMEOUT_MS  60000U
#define ACTUATOR_ONLINE_POLL_MS     200U

static bool waitActuatorOnline(uint32_t timeout_ms)
{
    const int maxLoop = (int)(timeout_ms / ACTUATOR_ONLINE_POLL_MS);
    int waited;

    if (ModbusActuatorOnline()) {
        return true;
    }

    ESP_LOGW("TouFu", "站2(AT32)未上线，等待中（最多 %u ms）",
             (unsigned)timeout_ms);

    for (waited = 0; waited < maxLoop; waited++) {
        vTaskDelay(pdMS_TO_TICKS(ACTUATOR_ONLINE_POLL_MS));
        if (ModbusActuatorOnline()) {
            ESP_LOGI("TouFu", "站2(AT32)已上线（等了 %d ms）",
                     waited * (int)ACTUATOR_ONLINE_POLL_MS);
            return true;
        }
    }
    return false;
}

void TouFuMakeTask(void* pvParameters)
{
    ESP_LOGI("TouFu","TouFu Make Start");

    if (!runPowerCalibration(60000U)) {
        ESP_LOGE("TouFu", "功率校准失败，取消豆腐任务");
        TouFuEnd();
        riseMoterStop();
        setHighBroad(0); /* 中止未完成的校准并关闭动力板输出。 */
        TouFuMoniter.isRunning = false;
        TouFuMoniter.isEnd = true;
        FunctionSelectionFlowCompleted(TOUFU_SEC);
        TouFuHandle = NULL;
        vTaskDelete(NULL);
        return;
    }
    while(1)
    {
        /* 门禁：站2(AT32) 必须在线，否则流程里的站2 动作与反馈全都无效 */
        if (!waitActuatorOnline(ACTUATOR_ONLINE_TIMEOUT_MS))
        {
            ESP_LOGE("TouFu", "站2(AT32)未上线，取消豆腐流程");
        }
        else
        {
            valveOff();
            
            vTaskDelay(pdMS_TO_TICKS(30*1000));

            TouFuMoniter.isRunning= true;
            riseMoterDown(23);
        
            //开始制作豆浆

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
            Play_Music();
            //开始压制豆腐
            taskCurStage = pressTouFuStage;
            if (!TouFuPress()) {
                ESP_LOGE("TouFu", "压制流程失败，豆腐任务结束");
            } else {
                Play_Music();
            }
        }
        TouFuMoniter.isRunning= false;
        TouFuMoniter.isEnd = true;
        FunctionSelectionFlowCompleted(TOUFU_SEC);
        TouFuHandle = NULL;
        vTaskDelete(NULL);
    }
}



void TouFuPause(void)
{
    ESP_LOGI("豆腐","豆腐任务暂停");
    pressMoterStop();
    Heat1Run(0);
    Heat2Run(0);
    NoBushRun(0);
    hosePumpOff();
    waterPumpOff();
    brinePumpOff();
}

void TouFuEnd(void)
{
    ESP_LOGI("豆腐","豆腐任务停止");
    pressMoterStop();
    Heat1Run(0);
    Heat2Run(0);
    NoBushRun(0);
    hosePumpOff();
    waterPumpOff();
    brinePumpOff();
}

void TouFuResume(void)
{
    ESP_LOGI("豆腐","豆腐任务恢复");
}


int getTouFuTaskStatus(void)   
{
    return taskCurStage;
}
