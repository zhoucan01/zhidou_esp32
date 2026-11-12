#include "ui_lz4_decoder.h"

#include "esp_heap_caps.h"
#include "lvgl.h"
#include "lz4.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#define UI_LZ4_MAGIC_SIZE 4U

static const uint8_t ui_lz4_magic[UI_LZ4_MAGIC_SIZE] = {
    'L', 'Z', '4', 'I'
};

static lv_img_decoder_t *ui_lz4_decoder;

static bool ui_lz4_is_image(const void *src)
{
    if (src == NULL || lv_img_src_get_type(src) != LV_IMG_SRC_VARIABLE) {
        return false;
    }

    const lv_img_dsc_t *image = src;

    if (image->header.cf != LV_IMG_CF_RAW_ALPHA ||
        image->data == NULL ||
        image->data_size <= UI_LZ4_MAGIC_SIZE) {
        return false;
    }

    for (size_t i = 0; i < UI_LZ4_MAGIC_SIZE; ++i) {
        if (image->data[i] != ui_lz4_magic[i]) {
            return false;
        }
    }

    return true;
}

static lv_res_t ui_lz4_info(lv_img_decoder_t *decoder,
                            const void *src,
                            lv_img_header_t *header)
{
    LV_UNUSED(decoder);

    if (!ui_lz4_is_image(src)) {
        return LV_RES_INV;
    }

    const lv_img_dsc_t *image = src;

    header->always_zero = 0;
    header->w = image->header.w;
    header->h = image->header.h;
    header->cf = LV_IMG_CF_TRUE_COLOR_ALPHA;

    return LV_RES_OK;
}

static lv_res_t ui_lz4_open(lv_img_decoder_t *decoder,
                            lv_img_decoder_dsc_t *dsc)
{
    LV_UNUSED(decoder);

    if (!ui_lz4_is_image(dsc->src)) {
        return LV_RES_INV;
    }

    const lv_img_dsc_t *image = dsc->src;
    size_t raw_size =
        (size_t)image->header.w *
        (size_t)image->header.h *
        LV_IMG_PX_SIZE_ALPHA_BYTE;

    size_t compressed_size = image->data_size - UI_LZ4_MAGIC_SIZE;

    if (raw_size == 0U ||
        raw_size > INT_MAX ||
        compressed_size > INT_MAX) {
        return LV_RES_INV;
    }

    uint8_t *decoded = heap_caps_malloc(
        raw_size,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (decoded == NULL) {
        return LV_RES_INV;
    }

    int result = LZ4_decompress_safe(
        (const char *)(image->data + UI_LZ4_MAGIC_SIZE),
        (char *)decoded,
        (int)compressed_size,
        (int)raw_size);

    if (result != (int)raw_size) {
        heap_caps_free(decoded);
        return LV_RES_INV;
    }

    dsc->header.w = image->header.w;
    dsc->header.h = image->header.h;
    dsc->header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    dsc->img_data = decoded;
    dsc->user_data = decoded;

    return LV_RES_OK;
}

static void ui_lz4_close(lv_img_decoder_t *decoder,
                         lv_img_decoder_dsc_t *dsc)
{
    LV_UNUSED(decoder);

    if (dsc->user_data != NULL) {
        heap_caps_free(dsc->user_data);
        dsc->user_data = NULL;
        dsc->img_data = NULL;
    }
}

void ui_lz4_decoder_init(void)
{
    if (ui_lz4_decoder != NULL) {
        return;
    }

    ui_lz4_decoder = lv_img_decoder_create();

    if (ui_lz4_decoder == NULL) {
        return;
    }

    lv_img_decoder_set_info_cb(ui_lz4_decoder, ui_lz4_info);
    lv_img_decoder_set_open_cb(ui_lz4_decoder, ui_lz4_open);
    lv_img_decoder_set_close_cb(ui_lz4_decoder, ui_lz4_close);
}