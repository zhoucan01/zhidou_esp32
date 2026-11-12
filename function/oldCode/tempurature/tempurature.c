#include "tempurature.h"
#include "weights_full.h"
#include "weights_ntc.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "DataCenter.h"
#include "esp_log.h"
#include "SystemMoniter.h"
#include "dual_inference.h"

static float power_hist[FULL_WINDOW];
static float time_hist[FULL_WINDOW];
static float ntc_hist[FULL_WINDOW];
static float ambient_hist[FULL_WINDOW];

float tempDataFeedback(float power, float time, float ntc)
{
    static int curDataCnt = 0;
    const float ambient = 25.0f;

    if (curDataCnt < FULL_WINDOW) {
        power_hist[curDataCnt] = power;
        time_hist[curDataCnt] = time;
        ntc_hist[curDataCnt] = ntc;
        ambient_hist[curDataCnt] = ambient;
        curDataCnt++;
        ESP_LOGI("train", "history not ready");
        return -1.0f;
    }

    for (int i = 0; i < FULL_WINDOW - 1; ++i) {
        power_hist[i] = power_hist[i + 1];
        time_hist[i] = time_hist[i + 1];
        ntc_hist[i] = ntc_hist[i + 1];
        ambient_hist[i] = ambient_hist[i + 1];
    }
    power_hist[FULL_WINDOW - 1] = power;
    time_hist[FULL_WINDOW - 1] = time;
    ntc_hist[FULL_WINDOW - 1] = ntc;
    ambient_hist[FULL_WINDOW - 1] = ambient;

    float prediction = predict_full(power_hist, time_hist, ntc_hist, ambient_hist);
    ESP_LOGI("train", "plan a:%f", prediction);
    return prediction;
}

void tempNNTask(void *pvParameters)
{
    static float tempTaskRunTime = 0.0f;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10 * 1000));
        tempDataFeedback(getHeatPower(0) + getHeatPower(1), tempTaskRunTime, getTemp());
        tempTaskRunTime += 0.5f;
    }
}

void tempTrainTask(void *pvParameters)
{
    static float trainTime = 0.0f;
    while (1) {
        ESP_LOGI("TRAIN", "train temp:%f,time %f,power %f",
                 getTemp(), trainTime, getHeatPower(0) + getHeatPower(1));
        trainTime += 0.1f;
        vTaskDelay(pdMS_TO_TICKS(1000 * 10));
    }
}
