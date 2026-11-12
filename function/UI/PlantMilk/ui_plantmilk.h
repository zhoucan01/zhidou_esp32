#ifndef UI_PLANTMILK_H
#define UI_PLANTMILK_H

#include "../ui.h"

typedef enum {
    UI_PM_OAT,
    UI_PM_NUT,
    UI_PM_SOY,
    UI_PM_CUSTOM,
    UI_PM_RECIPE_COUNT
} ui_plantmilk_recipe_t;

typedef struct {
    uint16_t blending_seconds;
    uint16_t volume_ml;
    uint8_t blade_speed;
    bool cook;
    bool use_filter;
} ui_plantmilk_options_t;

/* UI 参数调整范围，不修改现有 MilkTask 的制作工艺。 */
#define UI_PM_BLEND_MIN_SECONDS 30U
#define UI_PM_BLEND_MAX_SECONDS 180U
#define UI_PM_BLEND_STEP_SECONDS 15U
#define UI_PM_BLADE_MIN 1U
#define UI_PM_BLADE_MAX 5U
#define UI_PM_BLADE_STEP 1U
#define UI_PM_VOLUME_MIN_ML 400U
#define UI_PM_VOLUME_MAX_ML 1200U
#define UI_PM_VOLUME_STEP_ML 100U

enum {
    UI_PM_SELECT_BACKGROUND = 0U,
    UI_PM_SELECT_RING,
    UI_PM_SELECT_OAT,
    UI_PM_SELECT_NUT,
    UI_PM_SELECT_SOY,
    UI_PM_SELECT_CUSTOM,
    UI_PM_SELECT_BACK,
};

enum {
    UI_PM_OPTIONS_BACK = 0U,
    UI_PM_OPTIONS_BLEND,
    UI_PM_OPTIONS_SPEED,
    UI_PM_OPTIONS_COOK,
    UI_PM_OPTIONS_VOLUME,
    UI_PM_OPTIONS_FILTER,
    UI_PM_OPTIONS_RESTORE,
    UI_PM_OPTIONS_CONTINUE,
    UI_PM_OPTIONS_CONTROL_COUNT,
    UI_PM_OPTIONS_PREVIOUS = UI_PM_OPTIONS_CONTROL_COUNT,
    UI_PM_OPTIONS_NEXT,
};

/* 加料和检查页面共用控件编号，中间编号仅用于显示图片。 */
#define UI_PM_STEP_BACK 0U
#define UI_PM_STEP_ACTION 6U
#define UI_PM_READY_COLLECT 1U
#define UI_PM_READY_CLEAN 2U
#define UI_PM_STOP_KEEP 1U
#define UI_PM_STOP_STOP 2U

/* 任务层只报告执行状态，具体跳转目标由各逻辑页面决定。 */
typedef enum {
    UI_PM_FLOW_IDLE,
    UI_PM_FLOW_RUNNING,
    UI_PM_FLOW_STOPPING,
    UI_PM_FLOW_READY,
    UI_PM_FLOW_STOPPED,
} ui_plantmilk_flow_state_t;

/* 通过回调接入传感器和制作任务，页面不直接依赖 RTOS 或硬件组件。 */
typedef struct {
    void (*refresh_sensors)(void);
    bool (*start)(void);
    ui_plantmilk_flow_state_t (*stop)(void);
    ui_plantmilk_flow_state_t (*poll)(void);
    uint16_t (*remaining_minutes)(void);
} ui_plantmilk_flow_ops_t;

void ui_plantmilk_bind_flow(const ui_plantmilk_flow_ops_t *ops);
void ui_plantmilk_select_enter(void);
void ui_plantmilk_handle_short_press(void);
void ui_plantmilk_handle_long_press(void);
void ui_plantmilk_tick(void);

bool ui_plantmilk_is_page(ui_page_id_t page);
void ui_plantmilk_reset(void);
ui_plantmilk_recipe_t ui_plantmilk_get_recipe(void);
void ui_plantmilk_select_recipe(ui_plantmilk_recipe_t recipe);
const ui_plantmilk_options_t *ui_plantmilk_get_options(void);
const char *ui_plantmilk_recipe_name(void);
void ui_plantmilk_restore_defaults(void);
void ui_plantmilk_cancel_editing(void);
bool ui_plantmilk_is_editing(void);
void ui_plantmilk_toggle_editing(void);
void ui_plantmilk_move_selection(int direction);
void ui_plantmilk_check_update(bool lid_present, bool tank_present);
bool ui_plantmilk_can_start(void);
void ui_plantmilk_set_remaining_minutes(uint16_t minutes);

#endif
