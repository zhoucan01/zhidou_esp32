#include "brine.h"
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "sys.h"

#include "UsartExchange.h"
#include "DataCenter.h"
#include "FunctionSelection.h"

TaskHandle_t brineHandle = NULL;

/* 点卤参数：循环三轮，每轮
 *   开点卤泵（BRINE_PUMP_RUN_SEC）→ 开蠕动泵（抽豆浆泵）
 *   → 关点卤泵、蠕动泵 → 轮间等待；最后一轮后面不等，直接交给流程下一步。
 * 蠕动泵时长/轮间等待按轮次给：
 *   第 1 轮 抽 5 s(70%)，停 30 s；第 2 轮 抽 15 s(90%)，停 30 s；第 3 轮 抽 5 s(90%)，不停。 */
#define BRINE_ROUNDS          3
#define BRINE_PUMP_RUN_SEC    3

static const int BRINE_HOSE_SEC[BRINE_ROUNDS]   = { 10, 15,  15 };  /* 每轮蠕动泵(抽豆浆泵)秒数 */
static const int BRINE_HOSE_SPEED[BRINE_ROUNDS] = { 70, 70, 80 }; /* 每轮蠕动泵转速(%)，第 1 轮慢点出豆浆 */
static const int BRINE_GAP_SEC[BRINE_ROUNDS]    = { 30, 30, 0 };  /* 每轮之间的等待秒数，0=不等 */

//点卤功能
void braineAct(void)
{
    for(int i=0;i<BRINE_ROUNDS;i++)
    {
        brinePumpOn();
        CheckDelay(BRINE_PUMP_RUN_SEC);

        //打开抽豆浆泵（蠕动泵）
        hosePumpOn((uint8_t)BRINE_HOSE_SPEED[i]);
        CheckDelay(BRINE_HOSE_SEC[i]);

        brinePumpOff();
        hosePumpOff();

        //轮间等待；最后一轮为 0，直接交给流程下一步
        if(BRINE_GAP_SEC[i] > 0)
        {
            CheckDelay(BRINE_GAP_SEC[i]);
        }
    }
}

void brineTask(void *pvParameters)
{
    while (1)
    {
        ESP_LOGI("brine","brine action start!");
        /* code */
        braineAct();
        FunctionSelectionFlowCompleted(BRINE_SEC);
        brineHandle = NULL;
        vTaskDelete(NULL);
    }
}

void brinePause(void)
{
    ESP_LOGI("brine","brine action Pause!");
    brinePumpOff();
    hosePumpOff();
}

void brineResume(void)
{
    ESP_LOGI("brine","brine action Resume!");
}

void brineEnd(void)
{
    ESP_LOGI("brine","brine action End!");
    brinePumpOff();
    hosePumpOff();
}
