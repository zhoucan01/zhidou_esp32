#include <stdio.h>
#include <stdlib.h>
#include "RGB.h"
#include "sys.h"  // 引脚定义在这里


#include "esp_err.h"
#include "esp_lcd_st7796.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"  // 在文件顶部添加
esp_lcd_panel_handle_t g_panel_handle = NULL;
esp_lcd_panel_io_handle_t g_panel_io_handle = NULL;

#define LCD_BL_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LCD_BL_LEDC_TIMER      LEDC_TIMER_2
#define LCD_BL_LEDC_CHANNEL    LEDC_CHANNEL_3
#define LCD_BL_LEDC_FREQUENCY  5000
#define LCD_BL_LEDC_RESOLUTION LEDC_TIMER_10_BIT
#define LCD_BL_MAX_DUTY        ((1U << 10) - 1U)

void ScreenSetBrightness(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    uint32_t duty = (LCD_BL_MAX_DUTY * percent + 50U) / 100U;
    ESP_ERROR_CHECK(ledc_set_duty(LCD_BL_LEDC_MODE,
                                  LCD_BL_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LCD_BL_LEDC_MODE,
                                     LCD_BL_LEDC_CHANNEL));
}
// void* g_panel_handle = NULL;
// ===== 屏幕初始化 =====
void ScreenInit(void)
{
    // 1. 初始化背光
    const ledc_timer_config_t bl_timer_config = {
        .speed_mode = LCD_BL_LEDC_MODE,
        .duty_resolution = LCD_BL_LEDC_RESOLUTION,
        .timer_num = LCD_BL_LEDC_TIMER,
        .freq_hz = LCD_BL_LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&bl_timer_config));

    const ledc_channel_config_t bl_channel_config = {
        .gpio_num = lcdBl,
        .speed_mode = LCD_BL_LEDC_MODE,
        .channel = LCD_BL_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LCD_BL_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&bl_channel_config));

    // 2. 创建 I80 总线
    esp_lcd_i80_bus_handle_t i80_bus = NULL;
    esp_lcd_i80_bus_config_t bus_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .dc_gpio_num = lcdRs,
        .wr_gpio_num = lcdWr,
        .data_gpio_nums = {
            lcdD0, lcdD1, lcdD2, lcdD3,
            lcdD4, lcdD5, lcdD6, lcdD7,
            lcdD8, lcdD9, lcdD10, lcdD11,
            lcdD12, lcdD13, lcdD14, lcdD15,
        },
        .bus_width = 16,
        .max_transfer_bytes = LCDH * LCDW * sizeof(uint16_t),
        .dma_burst_size = 64,
    };
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &i80_bus));

    // 3. 创建面板 IO
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = lcdCs,
        .pclk_hz = 8 * 1000 * 1000,
        .trans_queue_depth = 10,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
        .flags = {
            .swap_color_bytes = 0,
        },
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &io_handle));
    g_panel_io_handle = io_handle;

    // 4. 创建面板
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = lcdRst,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(io_handle, &panel_config, &panel_handle));

    g_panel_handle = panel_handle;


    
    // 6. 初始化屏幕
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    vTaskDelay(pdMS_TO_TICKS(100));


    uint8_t data = 0x48;
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle, 0x36, &data, 1));


    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    /* Do not call lcd_show_color() here. esp_lcd_panel_draw_bitmap() queues an
     * asynchronous DMA transfer, while lcd_show_color() frees its large
     * temporary buffer immediately. The DMA can then read freed heap memory
     * and corrupt unrelated subsystems such as NimBLE and UART. LVGL performs
     * the first safe screen flush after its DMA callback is installed. */
}

// ===== 颜色取反 =====
uint16_t lcd_color(uint16_t color)
{
    return ~color & 0xFFFF;
}

// ===== 显示纯色 =====
void lcd_show_color(uint16_t color)
{
    if (g_panel_handle == NULL) return;
    
    uint16_t *buffer = malloc(LCDW * LCDH * sizeof(uint16_t));
    if (buffer == NULL) return;
    
    uint16_t final_color = lcd_color(color);
    
    for (int i = 0; i < LCDW * LCDH; i++) {
        buffer[i] = final_color;
    }
    
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(g_panel_handle, 0, 0, LCDW, LCDH, buffer));
    free(buffer);
}

// ===== 显示彩色条纹 =====
void lcd_show_color_bars(void)
{
    if (g_panel_handle == NULL) return;

    int bar_height = LCDH / 6;
    uint16_t *color_bar = heap_caps_malloc(LCDW * bar_height * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (color_bar == NULL) {
        printf("Memory allocation failed!\n");
        return;
    }

    // 红色条纹
    for (int i = 0; i < LCDW * bar_height; i++) color_bar[i] = lcd_color(COLOR_RED);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(g_panel_handle, 0, 0, LCDW, bar_height, color_bar));
    vTaskDelay(pdMS_TO_TICKS(20));  // 加一点延时

    // 绿色条纹
    for (int i = 0; i < LCDW * bar_height; i++) color_bar[i] = lcd_color(COLOR_GREEN);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(g_panel_handle, 0, bar_height, LCDW, bar_height * 2, color_bar));
    vTaskDelay(pdMS_TO_TICKS(20));

    // 蓝色条纹
    for (int i = 0; i < LCDW * bar_height; i++) color_bar[i] = lcd_color(COLOR_BLUE);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(g_panel_handle, 0, bar_height * 2, LCDW, bar_height * 3, color_bar));
    vTaskDelay(pdMS_TO_TICKS(20));

    // 黄色条纹
    for (int i = 0; i < LCDW * bar_height; i++) color_bar[i] = lcd_color(COLOR_YELLOW);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(g_panel_handle, 0, bar_height * 3, LCDW, bar_height * 4, color_bar));
    vTaskDelay(pdMS_TO_TICKS(20));

    // 品红色条纹
    for (int i = 0; i < LCDW * bar_height; i++) color_bar[i] = lcd_color(COLOR_MAGENTA);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(g_panel_handle, 0, bar_height * 4, LCDW, bar_height * 5, color_bar));
    vTaskDelay(pdMS_TO_TICKS(20));

    // 青色条纹
    for (int i = 0; i < LCDW * bar_height; i++) color_bar[i] = lcd_color(COLOR_CYAN);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(g_panel_handle, 0, bar_height * 5, LCDW, LCDH, color_bar));

    heap_caps_free(color_bar);
    printf("Color bars displayed!\n");
}

void lcd_test_gradient(void)
{
    if (g_panel_handle == NULL) {
        return;
    }
    
    // ✅ 改用 malloc
    uint16_t *buffer = malloc(LCDW * LCDH * sizeof(uint16_t));
    if (buffer == NULL) {
        printf("Gradient malloc failed!\n");
        return;
    }
    
    for (int y = 0; y < LCDH; y++) {
        for (int x = 0; x < LCDW; x++) {
            uint8_t r = (uint8_t)((LCDW - x) * 31 / LCDW);
            uint8_t b = (uint8_t)(x * 31 / LCDW);
            uint8_t g = 0;
            uint16_t color = (r << 11) | (g << 5) | b;
            buffer[y * LCDW + x] = lcd_color(color);
        }
    }
    
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(g_panel_handle, 0, 0, LCDW, LCDH, buffer));
    free(buffer);  // ✅ 用 free
    printf("Gradient displayed!\n");
}

// ===== 棋盘格测试 =====
void lcd_test_checkerboard(uint16_t color1, uint16_t color2)
{
    printf("checkerboard start!\n");
    if (g_panel_handle == NULL) {
        printf("checkerboard failed!\n");
        return;
    }
    
    // ✅ 改用 malloc
    uint16_t *buffer = malloc(LCDW * LCDH * sizeof(uint16_t));
    if (buffer == NULL) {
        printf("checkerboard malloc failed!\n");
        return;
    }
    
    uint16_t c1 = lcd_color(color1);
    uint16_t c2 = lcd_color(color2);
    
    for (int y = 0; y < LCDH; y++) {
        for (int x = 0; x < LCDW; x++) {
            buffer[y * LCDW + x] = ((x + y) % 2 == 0) ? c1 : c2;
        }
    }
    
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(g_panel_handle, 0, 0, LCDW, LCDH, buffer));
    free(buffer);  // ✅ 用 free
    printf("Checkerboard displayed!\n");
}
