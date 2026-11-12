#include "MyEncoder.h"
#include "boot_hold.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_struct.h"
#include "sys.h"

#define BUTTON_DEBOUNCE_US       10000
#define BUTTON_LONG_PRESS_US     2000000

/** @brief 短按事件标志，由编码器任务置位，UI 任务读取并清除。 */
volatile bool isPress = false;

/** @brief 长按事件标志，由编码器任务置位，UI 任务读取并清除。 */
volatile bool isLongPress = false;

/** @brief 旋钮累计计数值，每完成一个有效机械步加一或减一。 */
volatile int encCounter = 0;

/** @brief 编码器任务句柄，供 GPIO 中断发送任务通知。 */
static TaskHandle_t encoder_task;

/** @brief 保护中断与任务共享状态的临界区自旋锁。 */
static portMUX_TYPE encoder_lock = portMUX_INITIALIZER_UNLOCKED;

/** @brief 上一次读取到的 A/B 两相信号组合，取值范围为 0～3。 */
static uint8_t previous_ab;

/** @brief 正交编码四分之一步累计值，达到正负 4 时形成一个完整步。 */
static int8_t quarter_steps;

/** @brief 中断最近一次采集到的按键原始电平，低电平表示按下。 */
static int button_level = 1;

/** @brief 按键原始电平最近一次发生变化的时间，单位为微秒。 */
static int64_t button_edge_us;

/** @brief 经过消抖确认的按键稳定电平，低电平表示按下。 */
static int stable_button_level = 1;

/** @brief 当前一次按键按下动作的起始时间，单位为微秒。 */
static int64_t press_started_us;

/** @brief 当前按压周期是否已经上报长按事件，防止重复触发。 */
static bool long_press_sent;
static void (*boot_hold_callback)(void);
static bool provisioning_press;

/* Preserve the old driver's direction and scale (A=bit0, B=bit1):
 * 3 -> 2 -> 0 -> 1 -> 3 is +1; the opposite sequence is -1.
 * Adjacent bounce cancels; a two-bit jump discards the partial step. */
/** @brief A/B 前后状态对应的正交编码增量查找表。 */
static DRAM_ATTR const int8_t transitions[16] = {
     0,  1, -1,  0,
    -1,  0,  0,  1,
     1,  0,  0, -1,
     0, -1,  1,  0,
};

/**
 * @brief 从 GPIO 输入寄存器一次读取旋钮 A、B 两相和按键电平。
 *
 * 中断服务位于 IRAM，而当前配置下 gpio_get_level() 不保证位于 IRAM，
 * 因此直接读取 GPIO 硬件寄存器。返回值 bit0、bit1、bit2 分别对应
 * A 相、B 相和按键。
 *
 * @return 三个输入引脚的打包电平值。
 */
static uint8_t IRAM_ATTR read_pins(void)
{
    /* gpio_get_level() is not IRAM-enabled in the current sdkconfig. */
    uint32_t low = GPIO.in;
    uint32_t high = GPIO.in1.val;
    unsigned a = ((encCntA < 32 ? low : high) >> (encCntA & 31)) & 1u;
    unsigned b = ((encCntB < 32 ? low : high) >> (encCntB & 31)) & 1u;
    unsigned key = ((encKey < 32 ? low : high) >> (encKey & 31)) & 1u;
    return (uint8_t)(a | (b << 1) | (key << 2));
}

/**
 * @brief 处理旋钮 A/B 相及按键任意边沿的 GPIO 中断。
 *
 * 函数在中断上下文中执行。A/B 相通过状态转换表累计四分之一步，达到
 * 正负 4 后更新 encCounter；按键只记录原始边沿和时间，消抖及长按判断
 * 留给编码器任务完成。
 *
 * @param arg GPIO ISR 注册参数，当前未使用。
 */
static void IRAM_ATTR encoder_gpio_isr(void *arg)
{
    (void)arg;
    uint8_t pins = read_pins();
    uint8_t ab = pins & 3u;
    int64_t now = esp_timer_get_time();
    bool button_changed = false;

    portENTER_CRITICAL_ISR(&encoder_lock);
    if (ab != previous_ab) 
    {
        if ((ab ^ previous_ab) == 3u)
        {
            quarter_steps = 0;
        } else 
        {
            quarter_steps += transitions[(previous_ab << 2) | ab];
            if (quarter_steps >= 4 || quarter_steps <= -4) 
            {
                int direction = quarter_steps > 0 ? 1 : -1;
                quarter_steps = 0;
                /* One valid step always changes the count by one,
                 * regardless of speed or previous button presses. */
                if (direction > 0)
                    encCounter = encCounter == INT_MAX ? INT_MAX : encCounter + 1;
                else
                    encCounter = encCounter == INT_MIN ? INT_MIN : encCounter - 1;
            }
        }
        previous_ab = ab;
    }
    int key = (pins >> 2) & 1u;
    if (key != button_level) 
    {
        button_level = key;
        button_edge_us = now;
        button_changed = true;
    }
    portEXIT_CRITICAL_ISR(&encoder_lock);

    BaseType_t woken = pdFALSE;
    if (button_changed)
    {
        vTaskNotifyGiveFromISR(encoder_task, &woken);
    }
    if (woken) portYIELD_FROM_ISR();
}

/**
 * @brief 将微秒等待时间向上取整为 FreeRTOS tick 数。
 * @param us 等待时间，单位为微秒；非正值表示无需等待。
 * @return 对应的 tick 数；正等待时间最少返回 1 tick。
 */
static TickType_t wait_ticks(int64_t us)
{
    if (us <= 0) return 0;
    TickType_t ticks = pdMS_TO_TICKS((uint32_t)((us + 999) / 1000));
    return ticks ? ticks : 1;
}

/**
 * @brief 在任务上下文中完成按键消抖、短按和长按判定。
 *
 * 此函数只处理按键定时逻辑，不参与 A/B 相脉冲采集。按键释放且未达到
 * 长按时间时置位 isPress；持续按下达到阈值时置位 isLongPress。
 *
 * @return 距离下一次按键状态检查的等待 tick；无待处理按键时返回
 *         portMAX_DELAY。
 */
static TickType_t process_button(void)
{
    int level;
    int64_t edge;
    portENTER_CRITICAL(&encoder_lock);
    level = button_level;
    edge = button_edge_us;
    portEXIT_CRITICAL(&encoder_lock);
    int64_t now = esp_timer_get_time();

    if (level != stable_button_level) {
        int64_t remaining = BUTTON_DEBOUNCE_US - (now - edge);
        if (remaining > 0) return wait_ticks(remaining);
        stable_button_level = level;
        if (level == 0) {
            press_started_us = edge;
            long_press_sent = false;
            provisioning_press = boot_hold_callback && boot_hold_eligible(edge);
        } else if (!long_press_sent) {
            if (provisioning_press && boot_hold_ready(press_started_us, edge)) {
                boot_hold_callback();
            } else if (edge - press_started_us >= BUTTON_LONG_PRESS_US) {
                isLongPress = true;
            } else {
                isPress = true;
            }
        }
    }
    if (stable_button_level == 0 && !long_press_sent) {
        int64_t remaining = (provisioning_press ? BOOT_PROVISION_HOLD_US : BUTTON_LONG_PRESS_US) -
                            (now - press_started_us);
        if (remaining > 0) return wait_ticks(remaining);
        long_press_sent = true;
        if (provisioning_press && boot_hold_ready(press_started_us, now)) boot_hold_callback();
        else isLongPress = true;
    }
    return portMAX_DELAY;
}

/**
 * @brief 初始化编码器 GPIO 中断并运行按键处理任务。
 *
 * A、B 两相和按键均配置为上拉输入及任意边沿中断。旋钮计数直接在 ISR
 * 中更新，UI 直接读取累计值；本任务等待按键边沿通知，执行消抖和长按定时。
 *
 * @param arg 可选的上电长按回调配置；NULL 表示仅处理普通按键。
 */
void enctest(void *arg)
{
    const my_encoder_task_config_t *config = arg;
    boot_hold_callback = config ? config->boot_hold_callback : NULL;
    encoder_task = xTaskGetCurrentTaskHandle();

    gpio_config_t gpio = {
        .pin_bit_mask = (1ULL << encCntA) | (1ULL << encCntB) | (1ULL << encKey),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&gpio));
    uint8_t pins = read_pins();
    previous_ab = pins & 3u;
    button_level = (pins >> 2) & 1u;
    button_edge_us = esp_timer_get_time();
    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL1);
    if (err != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(err);
    const gpio_num_t inputs[] = {encCntA, encCntB, encKey};
    for (unsigned i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        ESP_ERROR_CHECK(gpio_set_intr_type(inputs[i], GPIO_INTR_ANYEDGE));
        ESP_ERROR_CHECK(gpio_isr_handler_add(inputs[i], encoder_gpio_isr, NULL));
    }

    TickType_t wait = process_button();
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, wait);
        wait = process_button();
    }
}
