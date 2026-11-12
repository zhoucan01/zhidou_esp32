#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "sys.h"

// #include "ToufuMake.h"  // Press.c 不调用 ToufuMake 中的函数，移除以避免循环依赖


#include "FunctionSelection.h"
#include "UsartExchange.h"
#include "DataCenter.h"
#include "FaultDeal.h"



#include "Music.h"
#include "press.h"

#include <math.h>

/* 压制电机（站2 保持寄存器 0x0002）速度参数统一 = rpm×10（下位机 task/modbusTask.c
 * 的 pressCmdValueToRpmX10()）：800 = 80rpm，500 = 50rpm，8 = 0.8rpm；
 * 流程内取值不超过 1000（= 100rpm）。 */
TaskHandle_t PressHandle = NULL;

TaskMoniterHandle_t PressMoniter;


void PressTask(void* pvParameters)
{   
    while(1)
    {
        ESP_LOGI("press","press Make Start");
        if (!TouFuPress()) {
            ESP_LOGE("press", "Toufu press flow failed");
        }
        FunctionSelectionFlowCompleted(PRESSTOUFU_SEC);
        PressHandle = NULL;
        vTaskDelete(NULL);
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
    pressMoterDown(1000);   /* 100rpm */
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
            pressMoterDown(1000);   /* 100rpm */
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
                pressMoterDown(500);     /* 50rpm */
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
    pressMoterUp(1000);     /* 100rpm */
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
            pressMoterUp(1000);      /* 100rpm */
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
            pressMoterUp(1000);      /* 100rpm */
            printf("重新尝试上升...\n");
            // 不更新 fLastRunRoll，保持基准不变
        }
    }
}

/* 快压到位判据：用站2 输入寄存器 0x003F/0x0040「编码器_压制右」的原始脉冲数
 * （高字在前，int32 有符号），到达 PRESS_FAST_STOP_COUNTS 即停快压，再由
 * TouFuPress() 转低速压制。50000 脉冲 = 50 圈（0x0007/0x0008 的 turns 是脉冲/1000）。 */
#define PRESS_FAST_STOP_COUNTS 53000

/* ---- 压制接触位（锤头杆"对上位"） ----------------------------------------
 * 压制流程一开始（抬升动作之前），先把锤头杆下到下面这个编码器位置与压制板接触，
 * 把丝杆空行程吃掉，再走快压 / 慢压 / 保压，避免慢压阶段还在空行程里爬。
 * 编码器 0 = 最高位（下位机上行到顶会自动清零），低位 = 75000 计数，
 * 数值取站2 输入寄存器 0x003F/0x0040「编码器_压制右」（有符号 int32）。 */
#define PRESS_CONTACT_ENCODER_COUNTS  20000
#define PRESS_CONTACT_RPM             800U    /* 800 = 80rpm，与快压同速 */
#define PRESS_CONTACT_TIMEOUT_MS      60000U   /* 接触最长等待，防编码器异常死等 */
#define PRESS_CONTACT_SETTLE_MS       300U     /* 停稳后再等一拍 */

/* ---- 压制“软到位”与保压（主站自己判，不用下位机低位信号） --------------------------
 * 下位机低位判据是压制编码器绝对值 >= moter57LowEncNum(75000)，但机械到底时编码器
 * 只到 ~74998（稳定后 ~74959），这个信号永远不来，所以主站不再等它，改成自己按站2
 * 输入寄存器 0x003F/0x0040「编码器_压制右」判断：
 *   1) 慢压一直压到 PRESS_SOFT_STOP_COUNTS 才算到位（不计时、不判堵转）；
 *   2) 到位之后才开始计时：保持继续下压保压 PRESS_HOLD_MS，压不动（编码器不动）就停电机，
 *      但保压计时照走；
 *   3) 时间到直接进收尾（抬升回高位 -> 压制抬头）。
 * 慢压速度按压制速度接口的 ×10 语义（值 = rpm×10），PRESS_SLOW_RPM = 8 即 0.8rpm。 */
#define PRESS_SOFT_STOP_COUNTS        74000
#define PRESS_SLOW_RPM                8U      /* 8 = 0.8rpm 慢压 */
#define PRESS_HOLD_MS                 600000U  /* 保压保持下压 10 分钟 */
#define PRESS_HOLD_STALL_MS           3000U    /* 编码器连续 3 秒不动 = 压不动 */
#define PRESS_POLL_MS                 100U     /* 轮询周期 */

void pressFastStage(void)
{
    //快速下压到豆腐位置
    uint8_t logCount = 0;

    pressMoterDown(800);    /* 80rpm */

    while(g_DataPool.pressRightEncoderPos < PRESS_FAST_STOP_COUNTS)
    {
        // Print once per second to avoid affecting the 100 ms control loop.
        if (++logCount >= 10) {
            ESP_LOGI("press", "Fast press encoder: %d / %d",
                     (int)g_DataPool.pressRightEncoderPos,
                     (int)PRESS_FAST_STOP_COUNTS);
            logCount = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    pressMoterDown(0);
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI("press", "Fast press complete, encoder: %d",
             (int)g_DataPool.pressRightEncoderPos);
}

/* 锤头杆下行到接触位：轮询压制编码器，到达 PRESS_CONTACT_ENCODER_COUNTS 即停。
 * 已经在接触位或更靠下的位置时直接返回（例如上一轮没退回最高位）。 */
static void pressGoToContact(void)
{
    const int maxLoop = (int)(PRESS_CONTACT_TIMEOUT_MS / 100U);
    int waited;

    if (g_DataPool.pressRightEncoderPos >= PRESS_CONTACT_ENCODER_COUNTS)
    {
        ESP_LOGI("press", "压制已在接触位（编码器 %d），跳过接触步骤",
                 (int)g_DataPool.pressRightEncoderPos);
        return;
    }

    ESP_LOGI("press", "锤头杆下行接触压制板，目标编码器 %d",
             (int)PRESS_CONTACT_ENCODER_COUNTS);
    pressMoterDown(PRESS_CONTACT_RPM);

    for (waited = 0; waited < maxLoop; waited++)
    {
        if (g_DataPool.pressRightEncoderPos >= PRESS_CONTACT_ENCODER_COUNTS)
        {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    pressMoterStop();
    vTaskDelay(pdMS_TO_TICKS(PRESS_CONTACT_SETTLE_MS));

    if (waited >= maxLoop)
    {
        ESP_LOGW("press", "接触位等待超时（编码器 %d），继续流程",
                 (int)g_DataPool.pressRightEncoderPos);
    }
    else
    {
        ESP_LOGI("press", "锤头杆已接触，编码器 %d",
                 (int)g_DataPool.pressRightEncoderPos);
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
        pressMoterDown(500);    /* 50rpm */
        
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



/* ---- 抬升段动作时序（主站不读下位机状态，按固定延时推进） ----------------
 * 段长度都写在下位机 app/moter42.c，主站只发触发值：
 *   上行一段 RISE_MOTER_UP_SEGMENT_COUNTS  = 27930 编码器计数 ≈ 27.9 圈
 *     （下位机最低位驻留回抬 RISE_MOTER_PARK_COUNTS=70，段动作从驻留位起算，
 *       故段长由 28000 减 70，排水位绝对高度不变）
 *   下降一段 RISE_MOTER_SEGMENT_COUNTS     = 14409 编码器计数 ≈ 14.4 圈（到定型位）
 * 编码器分辨率 RISE_MOTER_ENC_PPR = 1000 计数/圈，下位机固定 90.0 RPM
 * （RISE_MOTER_RPM_X10 = 900）另有 5 秒 S 曲线加速，所以延时按 18.7s / 9.6s
 * 理论值再加加速与机械余量给。下位机不回报段状态，触发后只看延时。
 */
#define RISE_UP_SEGMENT_WAIT_MS    25000U
#define RISE_DOWN_SEGMENT_WAIT_MS  15000U
/* 段动作停稳后再等一拍，让下位机完成停机制动再收下一条命令。 */
#define RISE_SEG_STOP_SETTLE_MS     300U

/* 抬升"下降到最低位"（压制前归位）：发普通下行命令，下位机下行
 * 到底后自己停机并锁存低位标志（离散输入 0x0001 = riseMoterLowPos），主站只等
 * 这个信号，不等固定延时。已经在最低位时下位机会拒绝下行命令，信号本来就是 1。 */
#define RISE_TO_BOTTOM_RPM         45U
#define RISE_TO_BOTTOM_TIMEOUT_MS  120000U   /* 到底最长等待，防信号异常时死等 */
#define RISE_AT_LOW_SETTLE_MS      300U      /* 到底停稳后再等一拍 */

/* 压制流程尾段：保压结束后抬升先上升到高位（普通上行命令 + 等高位限位信号），
 * 抬升到高位之后再抬压制头。 */
#define RISE_TO_HIGH_RPM           23U
#define RISE_AT_HIGH_SETTLE_MS     300U      /* 到高位停稳后再等一拍 */

/* 抬升下降到最低位并等低位信号：下位机下行到底后自己停机并锁存低位
 * （离散输入 0x0001 = riseMoterLowPositionGet）。带超时，避免信号异常时死等。 */
static void riseGoToBottom(void)
{
    int waited;

    ESP_LOGI("press", "抬升开始下降到最低位");
    riseMoterDown(RISE_TO_BOTTOM_RPM);

    for(waited = 0; waited < (int)(RISE_TO_BOTTOM_TIMEOUT_MS / 100U); waited++)
    {
        if(getSensorData(riseLowPos) == 1)
        {
            riseMoterStop();
            vTaskDelay(pdMS_TO_TICKS(RISE_AT_LOW_SETTLE_MS));
            ESP_LOGI("press", "抬升已到最低位（等了 %d ms）", waited * 100);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    riseMoterStop();
    ESP_LOGW("press", "抬升到最低位超时（%d ms），继续流程",
             (int)RISE_TO_BOTTOM_TIMEOUT_MS);
}


bool TouFuPress(void)
{  
    /* 0) 压制全程抬升都带载：先把抬升电流档切到高档 */
    riseMoterSetCurrentGear(1);

    /* 1) 压制锤头杆先下行到接触位，与压制板对上位（吃掉丝杆空行程） */
    pressGoToContact();

    /* 2) 抬升先下降到最低位：抬升每段位移都从最低位起算，先归位 */
    riseGoToBottom();

    /* 3) 从最低位向上走固定段长到排水位（下位机 -601 触发） */
    ESP_LOGI("press", "抬升从最低位向上走到排水位");
    riseMoterTimedUpAndHold();
    vTaskDelay(pdMS_TO_TICKS(RISE_UP_SEGMENT_WAIT_MS));
    vTaskDelay(pdMS_TO_TICKS(RISE_SEG_STOP_SETTLE_MS));

    ESP_LOGI("press", "抬升到达排水位，开始排水等待");
    vTaskDelay(pdMS_TO_TICKS(180 * 1000));

    /* 4) 再向下走固定段长到定型位（下位机 -1000 触发） */
    ESP_LOGI("press", "排水完成，抬升下降一段到定型位后自锁");
    riseMoterTimedDownAndHold();
    vTaskDelay(pdMS_TO_TICKS(RISE_DOWN_SEGMENT_WAIT_MS));

    /* 5) 快压接近豆腐，再慢压到“软到位”，然后继续下压保压 10 分钟。
     * 到位判据完全由主站按压制编码器 0x003F/0x0040 判断，不再等下位机的低位信号 */
    pressFastStage();

    ESP_LOGI("press", "快速接近完成，开始低速压制");
    pressMoterDown(PRESS_SLOW_RPM);

    /* 慢压：一直压到“软到位”（编码器 >= PRESS_SOFT_STOP_COUNTS）为止。
     * 这一段不计时、也不判堵转；到了 74000 才开始后面的各种判断
     * （保压 10 分钟计时 / 压不动判定）。 */
    {
        int logCount = 0;

        while(g_DataPool.pressRightEncoderPos < PRESS_SOFT_STOP_COUNTS)
        {
            if(++logCount >= (int)(1000U / PRESS_POLL_MS))
            {
                ESP_LOGI("press", "慢压编码器: %d / %d",
                         (int)g_DataPool.pressRightEncoderPos,
                         (int)PRESS_SOFT_STOP_COUNTS);
                logCount = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(PRESS_POLL_MS));
        }

        ESP_LOGI("press", "压制已到软到位（编码器 %d），开始计保压 10 分钟",
                 (int)g_DataPool.pressRightEncoderPos);
    }

    /* 保压：保持下压 PRESS_HOLD_MS；压不动（编码器 PRESS_HOLD_STALL_MS 不变）就停电机，
     * 但保压计时照走，时间到就收尾。 */
    {
        const int holdLoop = (int)(PRESS_HOLD_MS / PRESS_POLL_MS);
        const int stallLoop = (int)(PRESS_HOLD_STALL_MS / PRESS_POLL_MS);
        int32_t lastEnc = g_DataPool.pressRightEncoderPos;
        int stallCount = 0;
        bool motorStopped = false;
        int i;

        ESP_LOGI("press", "开始保压 %d 秒（保持下压）", (int)(PRESS_HOLD_MS / 1000U));

        for(i = 0; i < holdLoop; i++)
        {
            vTaskDelay(pdMS_TO_TICKS(PRESS_POLL_MS));

            if(g_DataPool.pressRightEncoderPos != lastEnc)
            {
                lastEnc = g_DataPool.pressRightEncoderPos;
                stallCount = 0;
            }
            else
            {
                stallCount++;
            }

            if(!motorStopped && stallCount >= stallLoop)
            {
                pressMoterStop();
                motorStopped = true;
                ESP_LOGI("press", "压不动了（编码器 %d），停止下压，继续保压计时",
                         (int)lastEnc);
            }
        }

        pressMoterStop();
        ESP_LOGI("press", "保压完成，编码器 %d", (int)g_DataPool.pressRightEncoderPos);
    }

    /* 6) 保压完成：抬升先上升到高位 */
    // ESP_LOGI("press", "保压完成，抬升开始上升到高位");
    // riseMoterUp(RISE_TO_HIGH_RPM);
    // while(getSensorData(riseHighPos) != 1)
    // {
    //     vTaskDelay(pdMS_TO_TICKS(100));
    // }
    // riseMoterStop();
    // vTaskDelay(pdMS_TO_TICKS(RISE_AT_HIGH_SETTLE_MS));

    // /* 7) 抬升已到高位，再把压制头抬到最高位 */
    // ESP_LOGI("press", "抬升已到高位，压制电机开始抬升");
    // pressMoterUp(800);      /* 80rpm */
    // while(getSensorData(pressHighPos) != 1)
    // {
    //     vTaskDelay(pdMS_TO_TICKS(100));
    // }
    // pressMoterStop();

    // ESP_LOGI("press", "压制流程完成，可以取出豆腐");
    // Play_Music();

    // /* 压制结束：抬升电流档回到低档（手动点动 / 开机自检用） */
    // riseMoterSetCurrentGear(0);
    return true;
}


void pressPause(void)
{
    ESP_LOGI("press","press Pause");
    pressMoterStop();
    hosePumpOff();
    waterPumpOff();
    brinePumpOff();
}


void pressResume(void)
{
    ESP_LOGI("press","press Resume");
}


void pressEnd(void)
{
    ESP_LOGI("press","press End");
    pressMoterStop();
    /* 任务被停止时也把抬升电流档写回低档，避免高档粘滞到手动点动 */
    riseMoterSetCurrentGear(0);
    hosePumpOff();
    waterPumpOff();
    brinePumpOff();
}
