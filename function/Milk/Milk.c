#include "Milk.h"
#include "MUsic.h"

TaskHandle_t MilkHandle = NULL;
PlantMilkFunctionDetail_t PlantMilkFunctionDetail;
#include "DataCenter.h"
#include "FunctionSelection.h"
#include "SoyMilkMake.h"    /* stirOneSecond(): 0.5s 一拍的开停搅拌 */

static bool isRun;
static bool faultPaused;    /* 因上盖/水箱缺失被挂起，传感器恢复后自动继续 */
static bool faultHooked;    /* milk 检测项是否已注册，防止任务被删后残留 */
static int milkfault[10];

/* ================= 植物奶(燕麦奶)工艺 =================
 * 流程：关阀 -> 清水泵定量加水 800ml(清水泵流量计 9182 脉冲)
 *       -> 破壁 2 轮(9 档 9000 转正转)，同时同步升温到 60℃
 *       -> 60℃ 恒温 15 分钟(7 档 350W，回差 2℃)
 *       -> 升温到 95℃(14 档 700W)
 *       -> 95℃ 恒温 5 分钟(8 档 400W，回差 2℃)
 *       -> 开阀等 30s -> 蠕动泵 90% 排浆 60s
 *
 * 约定：电机 N 档 = N*1000 转(正数正转、负数反转)，加热 N 档 = N*50W，
 *       蠕动泵档位是百分比(90 = 90%)。
 * 工艺参数全在下面这组宏里，改参数不用动流程代码。 */
#define MILK_WATER_PULSES      9182U /* 自动加水 800ml(清水泵流量计实测脉冲) */
#define MILK_WATER_SPEED       200U  /* 清水泵速度 20%，与豆浆流程标定速度一致 */
#define MILK_WATER_TIMEOUT_S   300   /* 定量超时保护：流量计或清水泵异常时不能一直抽水 */
#define MILK_BREAK_ROUNDS      2     /* 破壁轮数 */
#define MILK_BREAK_SEC         60    /* 每轮破壁秒数 */
#define MILK_BREAK_GAP_S       20    /* 轮与轮之间的停顿秒数 */
#define MILK_BREAK_GEAR        (9)   /* 破壁档位：9 档 = 9000 转（所有无刷电机只允许正转） */
#define MILK_HEAT_GEAR         14    /* 破壁同步升温 / 两段升温档位：14 档 = 700W */
#define MILK_HOLD_GEAR         7     /* 60℃ 恒温档位：7 档 = 350W */
#define MILK_HOLD_TEMP         60.0f /* 破壁同步升温目标 = 恒温温度 ℃ */
#define MILK_HOLD_MIN          15    /* 60℃ 恒温分钟数 */
#define MILK_HOLD_HYST         2.0f  /* 恒温回差 ℃：>=目标关加热，掉到(目标-H)以下才开 */
#define MILK_HEAT2_TEMP        95.0f /* 第二段升温目标 ℃ */
#define MILK_HEAT2_MIN         5     /* 95℃ 恒温分钟数 */
#define MILK_HEAT2_GEAR        8     /* 95℃ 恒温档位：8 档 = 400W */
#define MILK_DISCHARGE_GEAR    90    /* 排浆蠕动泵档位(%) */
#define MILK_DISCHARGE_S       60    /* 排浆秒数，按实际流量标定 */
#define MILK_VALVE_OPEN_S      30    /* 开阀后等阀门完全打开的秒数 */
#define MILK_STIR_ON_HALF      6     /* 搅拌开 3s(6 拍 x 0.5s) */
#define MILK_STIR_OFF_HALF     1     /* 搅拌停 0.5s(1 拍) */
#define MILK_STAGE_TIMEOUT_MIN 60    /* 单段升温超时保护：温度异常时不能一直加热 */

/* 站2(AT32) 在线门禁：阀门、温度、限位都在站2，不在线时温度读不到，
 * 升温循环会一直等下去，所以开流程前先等它上线。 */
#define MILK_ACTUATOR_TIMEOUT_MS  60000U
#define MILK_ACTUATOR_POLL_MS     200U

static bool milkWaitActuatorOnline(uint32_t timeout_ms)
{
    const int maxLoop = (int)(timeout_ms / MILK_ACTUATOR_POLL_MS);

    if (ModbusActuatorOnline()) {
        return true;
    }

    ESP_LOGW("Milk", "站2(AT32)未上线，等待中(最多 %u ms)", (unsigned)timeout_ms);

    for (int waited = 0; waited < maxLoop; waited++) {
        vTaskDelay(pdMS_TO_TICKS(MILK_ACTUATOR_POLL_MS));
        if (ModbusActuatorOnline()) {
            ESP_LOGI("Milk", "站2(AT32)已上线(等了 %d ms)",
                     waited * (int)MILK_ACTUATOR_POLL_MS);
            return true;
        }
    }
    return false;
}

/* 边搅拌边用 gear 档加热到 target；到温返回 true，超时返回 false。
 * motorNum 由调用者持有，整个流程共用同一个搅拌节拍。 */
static bool milkHeatTo(float target, int gear, int *motorNum)
{
    for (int sec = 0; sec < MILK_STAGE_TIMEOUT_MIN * 60; sec++) {
        if (getTemp() >= target) {
            Heat1Run(0);
            ESP_LOGI("Milk", "到温 %.1f℃", getTemp());
            return true;
        }
        Heat1Run(gear);
        stirOneSecond(motorNum, MILK_STIR_ON_HALF, MILK_STIR_OFF_HALF);
        if ((sec % 10) == 0) {
            ESP_LOGI("Milk", "加热中 %.1f℃ / %.1f℃ (%d 档)", getTemp(), target, gear);
        }
    }
    Heat1Run(0);
    ESP_LOGE("Milk", "升温超时(%d 分钟未到 %.1f℃)", MILK_STAGE_TIMEOUT_MIN, target);
    return false;
}

/* 恒温：>=target 关加热，掉到 target-回差 以下再用 gear 档开，全程搅拌 */
static void milkHold(float target, int gear, int minutes, int *motorNum)
{
    const int total = minutes * 60;
    bool heating = false;

    for (int sec = 0; sec < total; sec++) {
        const float t = getTemp();
        if (t >= target) {
            heating = false;
        } else if (t < target - MILK_HOLD_HYST) {
            heating = true;
        }
        Heat1Run(heating ? gear : 0);
        stirOneSecond(motorNum, MILK_STIR_ON_HALF, MILK_STIR_OFF_HALF);
        if ((sec % 60) == 0) {
            ESP_LOGI("Milk", "恒温 %.0f℃ %d/%d 分钟，%.1f℃，加热:%d(%d 档)，剩余:%ds",
                     target, sec / 60, minutes, t, (int)heating, gear, total - sec);
        }
    }
    Heat1Run(0);
}

/* 破壁：ROUNDS 轮，每轮 BREAK_SEC 秒，轮间停 BREAK_GAP_S 秒；
 * 同一个 1 秒节拍里同步升温到 target，到温立刻关加热
 * (破壁结束后余温不会再往上冲)。动力板电机与上下加热可同时运行。 */
static void milkBreakWithHeat(float target)
{
    const int roundSec = MILK_BREAK_SEC + MILK_BREAK_GAP_S;
    const int totalSec = MILK_BREAK_ROUNDS * MILK_BREAK_SEC +
                         (MILK_BREAK_ROUNDS - 1) * MILK_BREAK_GAP_S;

    ESP_LOGI("Milk", "破壁 %d 轮(%d 档)并同步升温到 %.0f℃",
             MILK_BREAK_ROUNDS, MILK_BREAK_GEAR, target);

    for (int sec = 0; sec < totalSec; sec++) {
        const int inRound = sec % roundSec;
        if (inRound == 0) {
            ESP_LOGI("Milk", "破壁第 %d/%d 轮", sec / roundSec + 1, MILK_BREAK_ROUNDS);
        }
        NoBushRun(inRound < MILK_BREAK_SEC ? MILK_BREAK_GEAR : 0);
        Heat1Run(getTemp() < target ? MILK_HEAT_GEAR : 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI("Milk", "破壁中 %.1f℃", getTemp());
    }
    NoBushRun(0);
}

/* 清水泵定量加水：开泵后每 100ms 读一次累积脉冲，够数就停泵。
 * 下位机停泵会把清水流量计清零，所以进函数先停一次泵归零，正好给下一段定量用。
 * 返回 false 表示超时没加够，多半是流量计或清水泵异常。 */
static bool milkDoseWater(uint32_t targetPulses)
{
    waterPumpOff();                     /* 停泵即清零流量计 */
    vTaskDelay(pdMS_TO_TICKS(300));
    waterPumpOn(MILK_WATER_SPEED);

    for (int tenth = 0; tenth < MILK_WATER_TIMEOUT_S * 10; tenth++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        const uint32_t pulses = g_DataPool.waterFlowPulses;
        if ((tenth % 10) == 0) {
            ESP_LOGI("Milk", "加水中 pulses=%lu/%lu",
                     (unsigned long)pulses, (unsigned long)targetPulses);
        }
        if (pulses >= targetPulses) {
            waterPumpOff();
            ESP_LOGI("Milk", "加水完成 pulses=%lu", (unsigned long)pulses);
            return true;
        }
    }

    waterPumpOff();
    ESP_LOGW("Milk", "加水超时(%ds)，已停泵", MILK_WATER_TIMEOUT_S);
    return false;
}

/* 植物奶主流程(不含任务框架，方便以后加配方分支) */
static void PlantMilkMake(void)
{
    int motorNum = 0;   /* 搅拌节拍计数，各段共用 */

    /* 1) 关阀后自动加 800ml 清水，量够了才开始破壁 */
    valveOff();
    ESP_LOGI("Milk", "关阀完成，清水泵自动加水 800ml(%u 脉冲)", (unsigned)MILK_WATER_PULSES);
    if (!milkDoseWater(MILK_WATER_PULSES)) {
        ESP_LOGE("Milk", "加水失败，流程结束");
        return;
    }

    /* 2) 破壁 2 轮，同时同步升温到 60℃ */
    milkBreakWithHeat(MILK_HOLD_TEMP);

    /* 3) 破壁结束时还没到 60℃，边搅拌边继续加热 */
    if (getTemp() < MILK_HOLD_TEMP) {
        ESP_LOGI("Milk", "继续升温到 %.0f℃", MILK_HOLD_TEMP);
        if (!milkHeatTo(MILK_HOLD_TEMP, MILK_HEAT_GEAR, &motorNum)) {
            ESP_LOGE("Milk", "第一段升温超时，流程结束");
            return;
        }
    }

    /* 4) 60℃ 恒温 15 分钟(7 档) */
    ESP_LOGI("Milk", "%.0f℃ 恒温 %d 分钟(%d 档)", MILK_HOLD_TEMP, MILK_HOLD_MIN, MILK_HOLD_GEAR);
    milkHold(MILK_HOLD_TEMP, MILK_HOLD_GEAR, MILK_HOLD_MIN, &motorNum);

    /* 5) 第二段升温 60℃ -> 95℃(14 档) */
    ESP_LOGI("Milk", "第二段升温到 %.0f℃", MILK_HEAT2_TEMP);
    if (!milkHeatTo(MILK_HEAT2_TEMP, MILK_HEAT_GEAR, &motorNum)) {
        ESP_LOGE("Milk", "第二段升温超时，流程结束");
        return;
    }

    /* 6) 95℃ 恒温 5 分钟(8 档，比升温低，防冲温/防糊底) */
    ESP_LOGI("Milk", "%.0f℃ 恒温 %d 分钟(%d 档)", MILK_HEAT2_TEMP, MILK_HEAT2_MIN, MILK_HEAT2_GEAR);
    milkHold(MILK_HEAT2_TEMP, MILK_HEAT2_GEAR, MILK_HEAT2_MIN, &motorNum);

    Play_Music();

    /* 7) 停加热和搅拌，开阀等它完全打开 */
    Heat1Run(0);
    NoBushRun(0);
    ESP_LOGI("Milk", "打开阀门，等待 %d 秒", MILK_VALVE_OPEN_S);
    valveOn();
    CheckDelay(MILK_VALVE_OPEN_S);

    /* 8) 蠕动泵排浆 */
    ESP_LOGI("Milk", "排浆 %d 秒(%d%%)", MILK_DISCHARGE_S, MILK_DISCHARGE_GEAR);
    hosePumpOn(MILK_DISCHARGE_GEAR);
    CheckDelay(MILK_DISCHARGE_S);
    hosePumpOff();
}

/* 注销 milk 检测项(幂等)：正常结束和 UI 停止都走这里 */
static void milkUnregisterFault(void)
{
    if (!faultHooked) {
        return;
    }
    faultHooked = false;
    FaultDetectUnregister(milkfault[0]);
}

void MilkTask(void *pvParameters)
{
    ESP_LOGI("Milk", "Milk Make Start");

    if (!runPowerCalibration(60000U)) {
        ESP_LOGE("Milk", "功率校准失败，取消植物奶任务");
        PlantMilkEnd();
        brinePumpOff();
        setHighBroad(0); /* 中止未完成的校准并关闭动力板输出。 */
        FunctionSelectionFlowCompleted(MILK_SEC);
        MilkHandle = NULL;
        vTaskDelete(NULL);
        return;
    }
    isRun = true;
    /* 上盖/水箱在位检测暂时停用：传感器抖动会把植物奶流程挂起。
     * 需要恢复时把下面两行取消注释即可(milkFaultDec 一直保留)。 */
    // milkfault[0] = FaultDetectRegister("milk", milkFaultDec);
    // faultHooked = (milkfault[0] > 0);
    faultPaused = false;

    if (!milkWaitActuatorOnline(MILK_ACTUATOR_TIMEOUT_MS)) {
        ESP_LOGE("Milk", "站2(AT32)未上线，取消植物奶流程");
    } else {
        PlantMilkMake();
    }

    /* 收尾保险：不管上面哪条路径退出，执行器在这里统一关一次 */
    Heat1Run(0);
    Heat2Run(0);
    NoBushRun(0);
    hosePumpOff();
    waterPumpOff();
    brinePumpOff();
    Play_Music();

    /* 取消注册 */
    isRun = false;
    milkUnregisterFault();
    FunctionSelectionFlowCompleted(MILK_SEC);
    MilkHandle = NULL;
    vTaskDelete(NULL);
}

void PlantMilkPause(void)
{
    isRun = false;
    ESP_LOGI("PlantMilk", "PlantMilk Pause");
    NoBushRun(0);
    Heat1Run(0);
    Heat2Run(0);
    hosePumpOff();
    waterPumpOff();
}

void PlantMilkEnd(void)
{
    isRun = false;
    faultPaused = false;
    ESP_LOGI("PlantMilk", "PlantMilk End");
    NoBushRun(0);
    Heat1Run(0);
    Heat2Run(0);
    hosePumpOff();
    waterPumpOff();
    /* 停止是直接把任务删掉，任务自己的收尾代码不会执行，
     * 所以检测项必须在这里注销，否则会留下一个永远在跑的检测函数。 */
    milkUnregisterFault();
}


void PlantMilkResume(void)
{
    isRun = true;
    ESP_LOGI("PlantMilk", "PlantMilk Resume");
}

/* 流程中检测上盖/水箱：不在位就关执行器并挂起任务，恢复后自动继续。
 * 注意：返回值必须先初始化好。流程没在跑的时候要返回 FAULT_NONE，
 *       否则会把栈上的垃圾当成严重性，每 20ms 刷一条错误日志。 */
faultResult_t milkFaultDec(void)
{
    faultResult_t milkErr = { .errCode = 0, .faultLevel = FAULT_NONE };

    TaskHandle_t task = MilkHandle;
    if (!isRun || task == NULL) {
        return milkErr;
    }

    /* 保留本地极性修复：DataCenter 保存原始输入，0 表示在位。 */
    const bool lidOk   = (0 == getSensorData(lidAvail));
    const bool waterOk = (0 == getSensorData(waterBoxAvail));

    if (!lidOk || !waterOk) {
        if (!faultPaused) {
            faultPaused = true;
            ESP_LOGW("Milk", "上盖:%s 水箱:%s，暂停植物奶流程",
                     lidOk ? "在位" : "缺失", waterOk ? "在位" : "缺失");
            Heat1Run(0);
            Heat2Run(0);
            NoBushRun(0);
            hosePumpOff();
            waterPumpOff();
            vTaskSuspend(task);
        }
        milkErr.errCode = 1;
        milkErr.faultLevel = FAULT_ERROR;
        return milkErr;
    }

    if (faultPaused) {
        faultPaused = false;
        ESP_LOGI("Milk", "上盖/水箱已恢复，继续植物奶流程");
        vTaskResume(task);
    }
    return milkErr;
}
