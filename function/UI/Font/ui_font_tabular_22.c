#include "ui_fonts.h"

/* 复用原字体的字形位图，为时间字段提供固定数字进位宽度。
 * 不改生成的字体文件，也不影响其他页面的普通文本和数值。 */
static lv_font_t tabular_font;
static uint16_t digit_advance;
static bool initialized;

static bool tabular_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc,
                              uint32_t letter, uint32_t letter_next)
{
    (void)font;
    (void)letter_next;
    /* 时间字段禁用字符对间距，避免冒号或相邻数字改变各位的起点。 */
    if (!UI_FONT_22.get_glyph_dsc(&UI_FONT_22, dsc, letter, 0U)) return false;
    if (letter >= '0' && letter <= '9') {
        dsc->ofs_x += ((int32_t)digit_advance - dsc->adv_w) / 2;
        dsc->adv_w = digit_advance;
    }
    return true;
}

static const uint8_t *tabular_glyph_bitmap(const lv_font_t *font, uint32_t letter)
{
    (void)font;
    return UI_FONT_22.get_glyph_bitmap(&UI_FONT_22, letter);
}

const lv_font_t *ui_font_tabular_22(void)
{
    if (!initialized) {
        for (uint32_t digit = '0'; digit <= '9'; ++digit) {
            uint16_t advance = lv_font_get_glyph_width(&UI_FONT_22, digit, 0U);
            if (advance > digit_advance) digit_advance = advance;
        }
        tabular_font = UI_FONT_22;
        tabular_font.get_glyph_dsc = tabular_glyph_dsc;
        tabular_font.get_glyph_bitmap = tabular_glyph_bitmap;
        initialized = true;
    }
    return &tabular_font;
}
