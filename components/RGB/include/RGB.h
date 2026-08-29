#ifndef RGB__H
#define RGB__H

#include <stdint.h>

// ===== 屏幕尺寸 =====
#define LCDH    480
#define LCDW    320

// ===== 颜色定义（RGB565格式） =====
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_MAGENTA 0xF81F
#define COLOR_CYAN    0x07FF

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

// ===== 函数声明 =====
void ScreenInit(void);
void ScreenSetBrightness(uint8_t percent);
uint16_t lcd_color(uint16_t color);
void lcd_show_color(uint16_t color);
void lcd_show_color_bars(void);
void lcd_test_gradient(void);
void lcd_test_checkerboard(uint16_t color1, uint16_t color2);

extern esp_lcd_panel_handle_t g_panel_handle;
extern esp_lcd_panel_io_handle_t g_panel_io_handle;

#endif

