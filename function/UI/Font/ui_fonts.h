#ifndef UI_FONTS_H
#define UI_FONTS_H

#include "lvgl.h"

/*
 * UI 字体系列选择。
 *
 * 页面代码仅依赖 UI_FONT_xx。后续更换字体时，
 * 重新生成字体源文件，并只修改此处的映射即可。
 */
LV_FONT_DECLARE(ui_font_manrope_12);
LV_FONT_DECLARE(ui_font_manrope_14);
LV_FONT_DECLARE(ui_font_manrope_16);
LV_FONT_DECLARE(ui_font_manrope_18);
LV_FONT_DECLARE(ui_font_manrope_20);
LV_FONT_DECLARE(ui_font_manrope_22);
LV_FONT_DECLARE(ui_font_manrope_24);
LV_FONT_DECLARE(ui_font_manrope_26);
LV_FONT_DECLARE(ui_font_manrope_28);

#define UI_FONT_12 ui_font_manrope_12
#define UI_FONT_14 ui_font_manrope_14
#define UI_FONT_16 ui_font_manrope_16
#define UI_FONT_18 ui_font_manrope_18
#define UI_FONT_20 ui_font_manrope_20
#define UI_FONT_22 ui_font_manrope_22
#define UI_FONT_24 ui_font_manrope_24
#define UI_FONT_26 ui_font_manrope_26
#define UI_FONT_28 ui_font_manrope_28

#define UI_FONT_DEFAULT UI_FONT_14

/* 保留 Manrope 22 字形，为 MM:SS 等动态时间提供等宽数字。 */
const lv_font_t *ui_font_tabular_22(void);

#endif
