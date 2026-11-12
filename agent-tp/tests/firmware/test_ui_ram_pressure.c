/* Real UI/LVGL audit. Board I/O comes from the existing host regression stubs. */
#define main ui_regression_main
#include "test_plantmilk_ui.c"
#undef main
#include <stdlib.h>

static bool audit_active;
static size_t decoded_live, decoded_peak, decoded_allocations, decoded_failures;
static size_t decoded_largest_request;
static size_t pool_peak, scene_pool_peak, scene_decoded_peak;
static unsigned normal_alloc_failures;
static unsigned peak_page, peak_selection, current_page_id, current_selection;
static size_t pool_budget = LV_MEM_SIZE;

typedef struct { size_t size; unsigned magic; } allocation_header;
void *heap_caps_malloc(size_t bytes, unsigned caps)
{
    (void)caps;
    allocation_header *header = malloc(sizeof(*header) + bytes);
    if (!header) { ++decoded_failures; return NULL; }
    header->size = bytes;
    header->magic = 0xabbacafe;
    decoded_live += bytes;
    if (decoded_live > decoded_peak) {
        decoded_peak = decoded_live;
        peak_page = current_page_id;
        peak_selection = current_selection;
    }
    if (decoded_live > scene_decoded_peak) scene_decoded_peak = decoded_live;
    ++decoded_allocations;
    if (bytes > decoded_largest_request) decoded_largest_request = bytes;
    return header + 1;
}

void heap_caps_free(void *ptr)
{
    if (!ptr) return;
    allocation_header *header = (allocation_header *)ptr - 1;
    assert(header->magic == 0xabbacafe);
    assert(decoded_live >= header->size);
    decoded_live -= header->size;
    header->magic = 0;
    free(header);
}

void audit_observe_pool(void *result)
{
    if (!audit_active) return;
    if (!result) ++normal_alloc_failures;
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    size_t used = mon.total_size - mon.free_size;
    if (used > pool_peak) pool_peak = used;
    if (used > scene_pool_peak) scene_pool_peak = used;
}

static void measure_scene(FILE *file, unsigned pass, unsigned page, unsigned selection)
{
    current_page_id = page;
    current_selection = selection;
    scene_pool_peak = 0;
    scene_decoded_peak = decoded_live;
    ui_show_page((ui_page_id_t)page);
    ui_select_image((ui_page_id_t)page, selection);
    lv_tick_inc(200);
    lv_refr_now(NULL);
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    assert(lv_mem_test() == LV_RES_OK);
    assert(normal_alloc_failures == 0 && decoded_failures == 0);
    fprintf(file, "%u,%u,%u,%zu,%zu,%u,%u,%zu,%u\n", pass, page, selection,
            decoded_live, scene_decoded_peak, (unsigned)(mon.total_size - mon.free_size),
            (unsigned)mon.free_biggest_size, scene_pool_peak, rendered_layers);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    /* Baseline tests include artificial exhaustion; exclude them from normal peaks. */
    assert(ui_regression_main(2, argv) == 0);
    lv_img_cache_invalidate_src(NULL);
    assert(decoded_live == 0);
    decoded_peak = decoded_allocations = decoded_failures = 0;
    decoded_largest_request = 0;
    audit_active = true;
    FILE *file = fopen(argv[2], "w");
    assert(file);
    fprintf(file, "pass,page,selection,decoded_live,decoded_scene_peak,pool_used,pool_largest_free,pool_scene_peak,layer_count\n");
    for (unsigned pass = 0; pass < 6; ++pass) {
        for (unsigned page = 0; page < UI_PAGE_COUNT; ++page) {
            unsigned count = ui_page_configs[page].image_count;
            for (unsigned selection = 0; selection < count; ++selection) {
                if (page == UI_PAGE_PLANTMILK_SELECT && selection >= UI_PM_SELECT_OAT &&
                    selection < UI_PM_SELECT_OAT + UI_PM_RECIPE_COUNT)
                    ui_plantmilk_select_recipe(selection - UI_PM_SELECT_OAT);
                measure_scene(file, pass, page, selection);
            }
        }
        ui_show_page(UI_PAGE_HOME);
        lv_tick_inc(200);
        lv_refr_now(NULL);
        lv_img_cache_invalidate_src(NULL);
        assert(decoded_live == 0);
        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        printf("AUDIT_PASS %u: pool_used=%u free=%u largest=%u children=%u decoded_live=%zu\n",
               pass, (unsigned)(mon.total_size-mon.free_size), (unsigned)mon.free_size,
               (unsigned)mon.free_biggest_size, (unsigned)lv_obj_get_child_cnt(ui_Screen), decoded_live);
    }
    fclose(file);
    printf("AUDIT_SUMMARY pages=%u passes=6 pool_budget=%zu normal_pool_peak=%zu decoded_peak=%zu decoded_peak_page=%u decoded_peak_selection=%u decoded_allocations=%zu largest_decoded_request=%zu failures=%u/%zu\n",
           UI_PAGE_COUNT, pool_budget, pool_peak, decoded_peak, peak_page, peak_selection,
           decoded_allocations, decoded_largest_request, normal_alloc_failures, decoded_failures);
    return 0;
}
