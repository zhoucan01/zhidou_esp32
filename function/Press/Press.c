#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "sys.h"

#include "ToufuMake.h"


#include "FunctionSelection.h"
#include "UsartExchange.h"
#include "DataCenter.h"

#include "Music.h"
#include "press.h"

#include <math.h>

TaskHandle_t PressHandle = NULL;

TaskMoniterHandle_t PressMoniter;

// PressFunctionDetail_t PressFunctionDetail;

#define PUSHMOTERPOS_HIGH   1
#define PUSHMOTERPOS_LOW    16



void PressTask(void* pvParameters)
{   
    while(1)
    {
        ESP_LOGI("press","press Make Start");
   
        TouFuPress();
        vTaskDelete(NULL);
    }
}


void PressStageFast(int iRunTime)
{
    int iRunTimeNum=0;
    while(iRunTimeNum<iRunTime)
    {
        iRunTimeNum++;
        vTaskDelay(pdMS_TO_TICKS(1000));

    }
}

void PressStageSlow(int iRunTime)
{
    int iRunTimeNum = 0;
    float fNowRunRoll;
    float fLastRunRoll = getTurns();
    int iWaitCount = 0;
    const int WAIT_TIME = 60;  // 等待60秒
    
    // 开始下压
    pressMoterDown(200);
    printf("开始压制豆腐\n");
    
    while(iRunTimeNum < iRunTime)
    {
        iRunTimeNum++;
        vTaskDelay(pdMS_TO_TICKS(1000));
        fNowRunRoll = getTurns();
        float fDelta = fNowRunRoll - fLastRunRoll;  // 只计算一次
        
        if(fDelta < -0.001f)
        {
            // 还能压动，继续下压
            pressMoterDown(200);
            iWaitCount = 0;
            fLastRunRoll = fNowRunRoll;  // 更新基准
            printf("第%d秒：压制中，位移=%.3f\n", iRunTimeNum, fDelta);
        }
        else
        {
            // 压不动了，停止并等待排水
            pressMoterStop();
            iWaitCount++;
            printf("第%d秒：压不动，等待排水 (%d/%d)\n", iRunTimeNum, iWaitCount, WAIT_TIME);
            
            if(iWaitCount >= WAIT_TIME)
            {
                // 等待结束，立即重新下压
                iWaitCount = 0;
                fLastRunRoll = fNowRunRoll;  // 以当前位置为新基准
                pressMoterDown(50);
                printf("等待结束，继续下压\n");
            }
            // 压不动时不更新 fLastRunRoll，保留基准检测等待期间的位移
        }
    }
    
    pressMoterStop();
    printf("压制结束\n");
}

void PressStageUp(void)
{
    float fNowRunRoll;
    float fLastRunRoll = getTurns();
    const float MOVE_THRESHOLD = 0.001f;
    const int RETRY_COUNT = 3;  // 重试次数
    int iRetry = 0;
    
    // 开始上升
    pressMoterUp(200);
    printf("开始上升，初始圈数：%.3f\n", fLastRunRoll);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
        fNowRunRoll = getTurns();   
        float fDelta = fNowRunRoll - fLastRunRoll;  
        
        if(fabs(fDelta) > MOVE_THRESHOLD)
        {
            // 角度变化大，继续上升
            pressMoterUp(200);
            fLastRunRoll = fNowRunRoll;
            iRetry = 0;  // 复位重试计数
            printf("上升中：角度变化=%.3f (圈数：%.3f)\n", fDelta, fNowRunRoll);
        }
        else
        {
            // 角度变化小，尝试重试
            iRetry++;
            printf("角度变化小(%.3f)，重试 %d/%d\n", fDelta, iRetry, RETRY_COUNT);
            
            if(iRetry >= RETRY_COUNT)
            {
                // 重试次数用完，确认不动了
                pressMoterStop();
                printf("上升完成，最终圈数：%.3f\n", fNowRunRoll);
                return;
            }
            
            // 还没到重试次数，重新发送上升指令，再试一次
            pressMoterUp(200);
            printf("重新尝试上升...\n");
            // 不更新 fLastRunRoll，保持基准不变
        }
    }
}

void pressFastStage(void)
{
    //快速下压到豆腐位置
    while(getTurns() < 100)
    {
        pressMoterDown(150);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void pressStage(void)
{
    float currentTurns = 0.0f;
    float previousTurns = 0.0f;
    float turnsDifference = 0.0f;
    float turnsThreshold = 0.005f; // 设置一个阈值，表示圈数变化的最小值
    while(getSensorData(pressLowPos) != 1)
    {
        // 每次进入循环都先启动电机
        pressMoterDown(50);
        
        // 获取初始圈数
        previousTurns = getTurns();
        
        // 延时一下让电机运行一会儿
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        // 获取当前圈数
        currentTurns = getTurns();
        
        // 计算圈数差值（取绝对值）
        turnsDifference = currentTurns - previousTurns;
        if(turnsDifference < 0)
        {
            turnsDifference = -turnsDifference;
        }
        ESP_LOGI("pressStage", "Current Turns: %f, Previous Turns: %f, Difference: %f", currentTurns, previousTurns, turnsDifference);
        // 如果圈数变化小于阈值，停止电机并等待10秒
        if(turnsDifference < turnsThreshold)
        {
            pressMoterStop();
            vTaskDelay(pdMS_TO_TICKS(5000));  // 停止10秒
        }
    }
}

void riseStage(void)
{

}

void TouFuPress(void)
{
 //  pressFastStage();
    pressMoterDown(10);
    //等待豆腐排水
    while(getSensorData(pressLowPos) != 1)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    Play_Music();
    pressMoterStop();
    vTaskDelay(pdMS_TO_TICKS(100));
    riseMoterDown(23);
    vTaskDelay(pdMS_TO_TICKS(100));
    pressMoterUp(200);
    while(getSensorData(pressHighPos) != 1)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
        
    }
    pressMoterStop();
    
    Play_Music();
}
