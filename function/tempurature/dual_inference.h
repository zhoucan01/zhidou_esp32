/*
 * 双方案推理引擎 (纯 C, 无动态内存, 适合嵌入式):
 *   predict_full(time_min, power_w, ntc_temp_c, ambient_temp_c) -> liquid_temp_c  方案A
 *   predict_ntc(ntc_hist[NTC_INPUT_DIM])                       -> liquid_temp_c  方案B
 * 两个模型各自独立权重, 分别在 weights_full.h / weights_ntc.h。
 *
 * 权重为扁平行主序: W1[i*H1+j] 对应 sklearn coefs_[0] 的 (i,j)。
 */
#ifndef DUAL_INFERENCE_H
#define DUAL_INFERENCE_H
#include "weights_full.h"
#include "weights_ntc.h"

/* 通用 2 隐藏层 ReLU MLP 前向(输出 1 维)。缓冲区上限 64 神经元。 */
static float mlp_forward(const float *x, int n_in,
                         const float *w1, const float *b1, int h1,
                         const float *w2, const float *b2, int h2,
                         const float *w3, float b3) {
    float a1[64], a2[64];
    int i, j;
    for (j = 0; j < h1; ++j) {
        float s = b1[j];
        for (i = 0; i < n_in; ++i) s += x[i] * w1[i * h1 + j];
        a1[j] = s > 0.0f ? s : 0.0f;
    }
    for (j = 0; j < h2; ++j) {
        float s = b2[j];
        for (i = 0; i < h1; ++i) s += a1[i] * w2[i * h2 + j];
        a2[j] = s > 0.0f ? s : 0.0f;
    }
    float o = b3;
    for (i = 0; i < h2; ++i) o += a2[i] * w3[i];
    return o;
}

/* 方案A: [power_win, time_win, ntc_win, ambient_win] -> liquid_temp_c
 * 四路历史各长度 FULL_WINDOW(最旧->最新)。若与 NTC_INPUT_DIM 不一致会报错
 * (因为 ntc 缓冲区在两方案间共享)。 */
#if FULL_WINDOW != NTC_INPUT_DIM
#error "FULL_WINDOW != NTC_INPUT_DIM: ntc 缓冲区共享, 请用相同 window 重训两方案"
#endif
float predict_full(const float *power_hist, const float *time_hist,
                    const float *ntc_hist, const float *ambient_hist) {
    float x[FULL_INPUT_DIM];
    int k = 0, i;
    for (i = 0; i < FULL_WINDOW; ++i) { x[k] = (power_hist[i]    - FULL_IN_MEAN[k]) / FULL_IN_STD[k]; k++; }
    for (i = 0; i < FULL_WINDOW; ++i) { x[k] = (time_hist[i]     - FULL_IN_MEAN[k]) / FULL_IN_STD[k]; k++; }
    for (i = 0; i < FULL_WINDOW; ++i) { x[k] = (ntc_hist[i]      - FULL_IN_MEAN[k]) / FULL_IN_STD[k]; k++; }
    for (i = 0; i < FULL_WINDOW; ++i) { x[k] = (ambient_hist[i] - FULL_IN_MEAN[k]) / FULL_IN_STD[k]; k++; }
    float o = mlp_forward(x, FULL_INPUT_DIM,
                          FULL_W1, FULL_B1, FULL_H1,
                          FULL_W2, FULL_B2, FULL_H2,
                          FULL_W3, FULL_B3);
    return o * FULL_OUT_STD + FULL_OUT_MEAN;
}

/* 方案B: 过去 NTC_INPUT_DIM 条 ntc 历史(最旧 -> 最新) -> liquid_temp_c */
float predict_ntc(const float *ntc_hist) {
    float x[NTC_INPUT_DIM];
    int i;
    for (i = 0; i < NTC_INPUT_DIM; ++i)
        x[i] = (ntc_hist[i] - NTC_IN_MEAN[i]) / NTC_IN_STD[i];
    float o = mlp_forward(x, NTC_INPUT_DIM,
                          NTC_W1, NTC_B1, NTC_H1,
                          NTC_W2, NTC_B2, NTC_H2,
                          NTC_W3, NTC_B3);
    return o * NTC_OUT_STD + NTC_OUT_MEAN;
}

#endif /* DUAL_INFERENCE_H */
