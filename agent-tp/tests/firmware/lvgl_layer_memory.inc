/* Real TLSF exhaustion: reproduce the reported 14,592-byte layer failure. */
#include "src/draw/sw/lv_draw_sw.h"
#include "src/core/lv_refr.h"

static unsigned rendered_layers;
static unsigned largest_layer_bytes;
static lv_draw_layer_ctx_t *checked_layer_create(lv_draw_ctx_t *ctx,
                                                lv_draw_layer_ctx_t *layer,
                                                lv_draw_layer_flags_t flags)
{
    lv_draw_layer_ctx_t *result = lv_draw_sw_layer_create(ctx, layer, flags);
    assert(result && "Normal UI rendering must never drop a layer");
    ++rendered_layers;
    unsigned bytes = ((lv_draw_sw_layer_ctx_t *)result)->buf_size_bytes;
    if (bytes > largest_layer_bytes) largest_layer_bytes = bytes;
    return result;
}

static void test_layer_memory_failure(lv_disp_t *disp)
{
    lv_draw_ctx_t *ctx = disp->driver->draw_ctx;
    _lv_refr_set_disp_refreshing(disp);
    const lv_area_t area = {0, 0, 63, 75}; /* 64 * 76 * 3 = 14,592 */
    lv_mem_monitor_t before, blocked, after;
    lv_mem_monitor(&before);
    assert(before.free_biggest_size > 20000U);
    void *blocker = lv_mem_alloc(before.free_biggest_size - 4096U);
    assert(blocker);
    lv_mem_monitor(&blocked);
    assert(blocked.free_biggest_size < 14592U);
    void *saved_buf = ctx->buf;
    const lv_area_t *saved_area = ctx->buf_area;
    const lv_area_t *saved_clip = ctx->clip_area;
    bool saved_transp = disp->driver->screen_transp;
    /* Use the actual backend rather than the normal-render assertion wrapper. */
    ctx->layer_init = lv_draw_sw_layer_create;
    for (unsigned attempt = 0; attempt < 50; ++attempt) {
        assert(lv_draw_layer_create(ctx, &area, LV_DRAW_LAYER_FLAG_HAS_ALPHA) == NULL);
        assert(lv_draw_layer_create(ctx, &area, LV_DRAW_LAYER_FLAG_NONE) == NULL);
        assert(ctx->buf == saved_buf && ctx->buf_area == saved_area);
        assert(ctx->clip_area == saved_clip);
        assert(disp->driver->screen_transp == saved_transp);
        lv_mem_monitor(&after);
        assert(after.free_size == blocked.free_size);
        assert(after.free_biggest_size == blocked.free_biggest_size);
        assert(lv_mem_test() == LV_RES_OK);
    }
    lv_mem_free(blocker);
    lv_draw_layer_ctx_t *layer = lv_draw_layer_create(ctx, &area, LV_DRAW_LAYER_FLAG_HAS_ALPHA);
    assert(layer);
    for (unsigned i = 0; i < 14592; ++i) assert(((unsigned char *)layer->buf)[i] == 0);
    lv_draw_layer_destroy(ctx, layer);
    lv_mem_monitor(&after);
    assert(after.free_size == before.free_size);
    assert(after.free_biggest_size == before.free_biggest_size);
    assert(ctx->buf == saved_buf && ctx->buf_area == saved_area);
    assert(ctx->clip_area == saved_clip && disp->driver->screen_transp == saved_transp);
    ctx->layer_init = checked_layer_create;
    puts("PASS: 100 real layer allocation failures, no crash/leak/context changes; recovery buffer cleared");
}
