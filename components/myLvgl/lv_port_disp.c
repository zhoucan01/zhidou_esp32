/**
 * @file lv_port_disp_templ.c
 *
 */

/*Copy this file as "lv_port_disp.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_disp.h"
#include <stdbool.h>
#include "RGB.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
/*********************
 *      DEFINES
 *********************/
#ifndef MY_DISP_HOR_RES
    #warning Please define or replace the macro MY_DISP_HOR_RES with the actual screen width, default value 320 is used for now.
    #define MY_DISP_HOR_RES    320
#endif

#ifndef MY_DISP_VER_RES
    #warning Please define or replace the macro MY_DISP_HOR_RES with the actual screen height, default value 240 is used for now.
    #define MY_DISP_VER_RES    480
#endif

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_init(void);

static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p);
static bool disp_flush_ready_cb(esp_lcd_panel_io_handle_t panel_io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx);
//static void gpu_fill(lv_disp_drv_t * disp_drv, lv_color_t * dest_buf, lv_coord_t dest_width,
//        const lv_area_t * fill_area, lv_color_t color);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_disp_init(void)
{
    disp_init();

    static lv_disp_draw_buf_t draw_buf_dsc;
    
    // ✅ 像素数 = 行数 * 每行像素数
    uint32_t rows = 40;
    uint32_t buf_pixel_count = MY_DISP_HOR_RES * rows;
    size_t buf_size = buf_pixel_count * sizeof(lv_color_t);
    
    lv_color_t *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    lv_color_t *buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    
    if (buf1 == NULL || buf2 == NULL) {
        static lv_color_t fallback_buf1[MY_DISP_HOR_RES * 10];
        static lv_color_t fallback_buf2[MY_DISP_HOR_RES * 10];
        lv_disp_draw_buf_init(&draw_buf_dsc, fallback_buf1, fallback_buf2, MY_DISP_HOR_RES * 10);
        printf("⚠️ Fallback to 10 rows\n");
    } else {
        lv_disp_draw_buf_init(&draw_buf_dsc, buf1, buf2, buf_pixel_count);
        printf("✅ Buffer: %lu rows, %lu pixels\n", rows, buf_pixel_count);
    }

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = MY_DISP_HOR_RES;
    disp_drv.ver_res = MY_DISP_VER_RES;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf_dsc;
    // ✅ 设置旋转方向
    // disp_drv.sw_rotate = 1;           // 启用软件旋转
    // disp_drv.rotated = LV_DISP_ROT_90;  // 旋转角度
    lv_disp_drv_register(&disp_drv);

    esp_lcd_panel_io_callbacks_t io_callbacks = {
        .on_color_trans_done = disp_flush_ready_cb,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(
        g_panel_io_handle, &io_callbacks, &disp_drv));
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*Initialize your display and the required peripherals.*/
static void disp_init(void)
{
    /*You code here*/
}

volatile bool disp_flush_enabled = true;

/* Enable updating the screen (the flushing process) when disp_flush() is called by LVGL
 */
void disp_enable_update(void)
{
    disp_flush_enabled = true;
}

/* Disable updating the screen (the flushing process) when disp_flush() is called by LVGL
 */
void disp_disable_update(void)
{
    disp_flush_enabled = false;
}

/*Flush the content of the internal buffer the specific area on the display
 *You can use DMA or any hardware acceleration to do this operation in the background but
 *'lv_disp_flush_ready()' has to be called when finished.*/
// static esp_lcd_panel_handle_t g_panel_handle = NULL;
extern esp_lcd_panel_handle_t g_panel_handle;
static bool disp_flush_ready_cb(esp_lcd_panel_io_handle_t panel_io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx)
{
    lv_disp_flush_ready((lv_disp_drv_t *)user_ctx);
    return false;
}

static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    if (g_panel_handle == NULL) {
        lv_disp_flush_ready(disp_drv);
        return;
    }
    
    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2 + 1;
    int32_t y2 = area->y2 + 1;
    
    uint16_t *buf = (uint16_t *)color_p;
    uint32_t pixel_count = (x2 - x1) * (y2 - y1);
    
    // 颜色修正
    for (uint32_t i = 0; i < pixel_count; i++) {
        buf[i] = lcd_color(buf[i]);
    }
    
    esp_err_t err = esp_lcd_panel_draw_bitmap(g_panel_handle, x1, y1, x2, y2, buf);
    if (err != ESP_OK) {
        lv_disp_flush_ready(disp_drv);
        ESP_ERROR_CHECK(err);
    }
}

/*OPTIONAL: GPU INTERFACE*/

/*If your MCU has hardware accelerator (GPU) then you can use it to fill a memory with a color*/
//static void gpu_fill(lv_disp_drv_t * disp_drv, lv_color_t * dest_buf, lv_coord_t dest_width,
//                    const lv_area_t * fill_area, lv_color_t color)
//{
//    /*It's an example code which should be done by your GPU*/
//    int32_t x, y;
//    dest_buf += dest_width * fill_area->y1; /*Go to the first line*/
//
//    for(y = fill_area->y1; y <= fill_area->y2; y++) {
//        for(x = fill_area->x1; x <= fill_area->x2; x++) {
//            dest_buf[x] = color;
//        }
//        dest_buf+=dest_width;    /*Go to the next line*/
//    }
//}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
