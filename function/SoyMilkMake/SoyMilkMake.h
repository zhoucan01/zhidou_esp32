#ifndef SOYMILKMAKE_H
#define SOYMILKMAKE_H

#include "stdbool.h"
#include "stdint.h"
typedef struct {
    bool SoyMilk_Contain;//豆浆浓度
    bool SoyMilk_Clean;//是否自清洁
    bool SoyMilk_KeepWarm;//是否保温
    bool SoyMilk_Decrease;//是否降温
    int  SoyMilk_Temp;//保温或者降温的温度
}SoyMilkFuncDrtail_t;

void SoyMilkMake_Task(void *pvParameters);
void SoyMilkLightContian(void);

void SoyMilkResume(void);
void SoyMilkPause(void);
void SoyMilkEnd(void);

void SoyMilkKeppWarm(uint8_t  KeepWarmTemp);
void SoyMilkDecTemp(uint8_t DecTemp);

void HeatSoyMilk(void);
void TouFuSoyMilk(void);

/* 通用搅拌：0.5s 一拍，onHalf 拍开、offHalf 拍停；每次调用跑满 1 秒(2 拍)。
   开3停0.5 => stirOneSecond(&cnt, 6, 1);   开2停0.5 => stirOneSecond(&cnt, 4, 1); */
void stirOneSecond(int *cnt, int onHalf, int offHalf);

#endif