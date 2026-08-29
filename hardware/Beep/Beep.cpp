#include <stdio.h>
#include "buzzer.hpp" // 引入原本的 C++ 类
#include "driver/ledc.h"
#include "Beep.h"
#include "sys.h"

BuzzerHandle my_buzzer;

extern "C" {

    /**
     * @brief 创建蜂鸣器对象
     * @param timer_config LEDC 定时器配置指针
     * @param channel_config LEDC 通道配置指针
     * @param idle_level 空闲电平 (0 或 1)
     * @return 返回一个 void* 句柄
     */
    void* Buzzer_Create(ledc_timer_config_t* timer_config, 
                        ledc_channel_config_t* channel_config, 
                        uint32_t idle_level) {
        
        // 这里直接调用 C++ 的构造函数，把参数传进去
        return new Buzzer(timer_config, channel_config, idle_level);
    }

    void Buzzer_Beep(uint32_t freq, uint32_t duration) {
        if (my_buzzer) {
            Buzzer* b = (Buzzer*)my_buzzer;
            b->Beep(freq, duration);
        }
    }

    void Buzzer_Destroy() {
        if (my_buzzer) {
            delete (Buzzer*)my_buzzer;
        }
    }
}

void BeepInit()
{
    ledc_timer_config_t timer_conf;
    ledc_channel_config_t channel_conf;
    Buzzer::MakeConfig(&timer_conf, &channel_conf, BEEP, LEDC_AUTO_CLK, LEDC_LOW_SPEED_MODE, LEDC_TIMER_13_BIT, BeepTimerID, Beepchannal);
    my_buzzer = Buzzer_Create(&timer_conf, &channel_conf, 0);
}