#ifndef __BEEP_H
#define __BEEP_H

#include <stdint.h> // 引入标准整数类型

#include "driver/ledc.h" // 引入 LEDC 相关的类型定义

#ifdef __cplusplus
extern "C" {
#endif

// C 语言没有类，我们用 void* 来代表“对象句柄”
// 就像 Windows 编程里的 HANDLE 一样，C 语言不需要知道里面是什么
typedef void* BuzzerHandle;

// 声明 C 语言风格的函数
BuzzerHandle Buzzer_Create(ledc_timer_config_t* timer_config, 
                           ledc_channel_config_t* channel_config, 
                           uint32_t idle_level);
void Buzzer_Beep(uint32_t freq, uint32_t duration);
void Buzzer_Destroy();
void BeepInit();
#ifdef __cplusplus
}
#endif

#endif