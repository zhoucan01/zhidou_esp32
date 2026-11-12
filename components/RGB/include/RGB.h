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
/* 应用入口尽早关闭背光；首帧完整传输前保持关闭。 */
void ScreenBacklightHoldOff(void);
void ScreenInit(void);
/* 设置目标亮度，首帧就绪前只保存请求，不点亮背光。 */
void ScreenSetBrightness(uint8_t percent);
/* 首帧最后一块 DMA 完成后，从任务上下文释放背光门控。 */
void ScreenNotifyFirstFrameReady(void);
uint16_t lcd_color(uint16_t color);
void lcd_show_color(uint16_t color);
void lcd_show_color_bars(void);
void lcd_test_gradient(void);
void lcd_test_checkerboard(uint16_t color1, uint16_t color2);

extern esp_lcd_panel_handle_t g_panel_handle;
extern esp_lcd_panel_io_handle_t g_panel_io_handle;

#endif

