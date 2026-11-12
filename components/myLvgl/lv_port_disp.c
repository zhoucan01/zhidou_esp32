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
#include "display_stats.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
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

/* ST7796 配置为竖屏并按行写入，因此每条 DMA 扫描线
 * 沿 320 像素的短边传输。使用水平条带可保持面板原生写入顺序，
 * 避免软件转置。 */
#define UI_DISP_SHORT_EDGE_PIXELS MY_DISP_HOR_RES
/* 60 行可整除 480 像素的长边，整屏刷新需要 8 个 DMA 条带，
 * 原来的 40 行缓冲区则需要 12 个。双缓冲区共占用 75 KiB 内部 DMA RAM；
 * 分配失败时仍可回退到较小的缓冲区。 */
#define UI_DISP_DRAW_ROWS         60U
#define UI_DISP_COMPAT_ROWS       40U
#define UI_DISP_FALLBACK_ROWS     10U

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_init(void);
static void disp_render_start(lv_disp_drv_t *disp_drv);
static void invert_pixels(uint16_t *pixels, uint32_t pixel_count);
static int64_t render_start_us;
static int64_t pending_frame_start_us;
static portMUX_TYPE frame_time_lock = portMUX_INITIALIZER_UNLOCKED;
static bool first_frame_complete;
static bool first_frame_revealed;

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
    display_stats_init();
    portENTER_CRITICAL(&frame_time_lock);
    first_frame_complete = false;
    first_frame_revealed = false;
    portEXIT_CRITICAL(&frame_time_lock);

    static lv_disp_draw_buf_t draw_buf_dsc;
    
    // ✅ 像素数 = 行数 * 每行像素数
    uint32_t rows = UI_DISP_DRAW_ROWS;
    uint32_t buf_pixel_count = UI_DISP_SHORT_EDGE_PIXELS * rows;
    size_t buf_size = buf_pixel_count * sizeof(lv_color_t);
    
    lv_color_t *buf1 = heap_caps_malloc(buf_size,
                                        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    lv_color_t *buf2 = heap_caps_malloc(buf_size,
                                        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    
    if (buf1 == NULL || buf2 == NULL) {
        /* 重试分配前必须释放已成功分配的缓冲区。
         * 此处若保留一个 DMA 缓冲区，可能导致后续任务创建失败。 */
        heap_caps_free(buf1);
        heap_caps_free(buf2);
        rows = UI_DISP_COMPAT_ROWS;
        buf_pixel_count = UI_DISP_SHORT_EDGE_PIXELS * rows;
        buf_size = buf_pixel_count * sizeof(lv_color_t);
        buf1 = heap_caps_malloc(buf_size,
                                MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        buf2 = heap_caps_malloc(buf_size,
                                MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    }

    if (buf1 == NULL || buf2 == NULL) {
        heap_caps_free(buf1);
        heap_caps_free(buf2);
        static lv_color_t fallback_buf1[UI_DISP_SHORT_EDGE_PIXELS *
                                        UI_DISP_FALLBACK_ROWS];
        static lv_color_t fallback_buf2[UI_DISP_SHORT_EDGE_PIXELS *
                                        UI_DISP_FALLBACK_ROWS];
        lv_disp_draw_buf_init(
            &draw_buf_dsc, fallback_buf1, fallback_buf2,
            UI_DISP_SHORT_EDGE_PIXELS * UI_DISP_FALLBACK_ROWS);
        printf("⚠️ Display buffer fallback: %u rows\n",
               UI_DISP_FALLBACK_ROWS);
    } else {
        lv_disp_draw_buf_init(&draw_buf_dsc, buf1, buf2, buf_pixel_count);
        printf("✅ Display buffers: %lu rows, %lu pixels each\n",
               rows, buf_pixel_count);
    }

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = MY_DISP_HOR_RES;
    disp_drv.ver_res = MY_DISP_VER_RES;
    disp_drv.flush_cb = disp_flush;
    disp_drv.render_start_cb = disp_render_start;
    disp_drv.draw_buf = &draw_buf_dsc;
    disp_drv.full_refresh = 0;
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

bool lv_port_disp_first_frame_ready(void)
{
    portENTER_CRITICAL(&frame_time_lock);
    bool ready = first_frame_revealed;
    portEXIT_CRITICAL(&frame_time_lock);
    return ready;
}

void lv_port_disp_service_backlight(void)
{
    bool reveal = false;
    portENTER_CRITICAL(&frame_time_lock);
    if (first_frame_complete && !first_frame_revealed) {
        reveal = true;
    }
    portEXIT_CRITICAL(&frame_time_lock);

    if (reveal) {
        ScreenNotifyFirstFrameReady();
        portENTER_CRITICAL(&frame_time_lock);
        first_frame_revealed = true;
        portEXIT_CRITICAL(&frame_time_lock);
        printf("Display: first frame DMA complete, backlight released\n");
    }
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
static void disp_render_start(lv_disp_drv_t *disp_drv)
{
    (void)disp_drv;
    render_start_us = esp_timer_get_time();
}

static bool disp_flush_ready_cb(esp_lcd_panel_io_handle_t panel_io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    lv_disp_drv_t *disp_drv = (lv_disp_drv_t *)user_ctx;
    if (lv_disp_flush_is_last(disp_drv)) {
        int64_t finished_us = esp_timer_get_time();
        portENTER_CRITICAL_ISR(&frame_time_lock);
        int64_t elapsed_us = finished_us - pending_frame_start_us;
        /* ui_init() 首次加载整个屏幕；最后一个条带传完才允许显示。
         * 中断只发布标志，亮度更新交给 UI 任务。 */
        first_frame_complete = true;
        portEXIT_CRITICAL_ISR(&frame_time_lock);
        display_stats_record_frame(elapsed_us > UINT32_MAX ? UINT32_MAX :
                                   (uint32_t)elapsed_us);
    }
    lv_disp_flush_ready(disp_drv);
    return false;
}

/* LVGL 的 DMA 绘图缓冲区按字对齐。每次操作同时反转两个 RGB565 像素，
 * 可减少刷新关键路径上的 CPU 开销。may_alias 类型保证在 GCC
 * 启用严格别名规则时，32 位访问仍然合法。 */
typedef uint32_t ui_alias_u32_t __attribute__((__may_alias__));

static void invert_pixels(uint16_t *pixels, uint32_t pixel_count)
{
    while (pixel_count != 0U && ((uintptr_t)pixels & 3U) != 0U) {
        *pixels = (uint16_t)~*pixels;
        ++pixels;
        --pixel_count;
    }

    ui_alias_u32_t *words = (ui_alias_u32_t *)pixels;
    uint32_t word_count = pixel_count / 2U;
    for (uint32_t i = 0; i < word_count; ++i) {
        words[i] = ~words[i];
    }

    if ((pixel_count & 1U) != 0U) {
        uint16_t *last = (uint16_t *)(words + word_count);
        *last = (uint16_t)~*last;
    }
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
    invert_pixels(buf, pixel_count);
    
    if (lv_disp_flush_is_last(disp_drv)) {
        /* The next render may start before this DMA completes. Keep its
         * timestamp separate; LVGL serializes flushes until flush_ready. */
        portENTER_CRITICAL(&frame_time_lock);
        pending_frame_start_us = render_start_us;
        portEXIT_CRITICAL(&frame_time_lock);
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
