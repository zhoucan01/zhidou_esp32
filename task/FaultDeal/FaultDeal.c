#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "FaultDeal.h"
// ============ 1. 定义 ============


static detectEntry_t detectTable[MAX_DETECTORS];
static const char *TAG = "FaultDeal";
// static int FaultDetectExecute(void);
// ============ 2. 初始化（可选） ============
void FaultDetectInit(void) {
    memset(detectTable, 0, sizeof(detectTable));
    ESP_LOGI(TAG, "故障检测模块初始化完成");
}

// ============ 3. 注册 ============
int FaultDetectRegister(const char *name, detectFunc func) {
    if (name == NULL || func == NULL) {
        return -1;
    }
    
    // 查找同名（覆盖）
    for (int i = 0; i < MAX_DETECTORS; i++) {
        if (detectTable[i].active && strcmp(detectTable[i].name, name) == 0) {
            detectTable[i].func = func;
            ESP_LOGW(TAG, "覆盖已注册的检测: %s", name);
            return i + 1;  // 句柄 = 索引 + 1
        }
    }
    
    // 查找空闲位置
    for (int i = 0; i < MAX_DETECTORS; i++) {
        if (!detectTable[i].active) {
            strncpy(detectTable[i].name, name, NAME_LEN - 1);
            detectTable[i].name[NAME_LEN - 1] = '\0';
            detectTable[i].func = func;
            detectTable[i].active = true;
            ESP_LOGI(TAG, "注册检测: %s (句柄=%d)", name, i + 1);
            return i + 1;
        }
    }
    
    ESP_LOGE(TAG, "注册表已满，无法注册: %s", name);
    return -1;
}

// ============ 4. 注销 ============
bool FaultDetectUnregister(int handle) {
    int idx = handle - 1;
    if (idx < 0 || idx >= MAX_DETECTORS) {
        ESP_LOGE(TAG, "无效句柄: %d", handle);
        return false;
    }
    
    if (!detectTable[idx].active) {
        ESP_LOGW(TAG, "句柄 %d 已注销或无效", handle);
        return false;
    }
    
    detectTable[idx].active = false;
    detectTable[idx].name[0] = '\0';
    detectTable[idx].func = NULL;
    ESP_LOGI(TAG, "注销检测: 句柄=%d", handle);
    return true;
}

// ============ 5. 执行所有检测 ============
faultResult_t FaultDetectExecute(void) {
    faultResult_t result;
    result.errCode = 0;
    result.faultLevel = FAULT_NONE;
    
    for (int i = 0; i < MAX_DETECTORS; i++) {
        if (detectTable[i].active) {
            result = detectTable[i].func();
            if (result.faultLevel != FAULT_NONE) {
                ESP_LOGE(TAG, "❌ 检测失败: %s, 严重性: %d", 
                         detectTable[i].name, result.faultLevel);
                return result;
            }
        }
    }
    return result;
}

// ============ 6. 调试：打印注册表 ============
void FaultDetectDump(void) 
{
    ESP_LOGI(TAG, "========== 注册表状态 ==========");
    int count = 0;
    for (int i = 0; i < MAX_DETECTORS; i++) {
        if (detectTable[i].active) {
            ESP_LOGI(TAG, "  [%d] %s", i + 1, detectTable[i].name);
            count++;
        }
    }
    ESP_LOGI(TAG, "总计: %d 个检测项", count);
    ESP_LOGI(TAG, "=================================");
}

// ============ 7. FreeRTOS 任务 ============
void FaultDealTask(void *pvParameters) 
{
    FaultDetectInit();
    
    ESP_LOGI(TAG, "错误检测已创建");
    while (1) {
        faultResult_t err = FaultDetectExecute();
        if (err.faultLevel == FAULT_NONE) {
            // 无错误，正常运行
        } else {
            // 错误处理：触发报警、记录日志、进入安全模式等
            // 这里可以发送错误码到其他任务
        }
        vTaskDelay(pdMS_TO_TICKS(20));  // 每20ms检测一次
    }
}

