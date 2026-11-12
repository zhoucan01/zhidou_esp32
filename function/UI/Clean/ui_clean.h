#ifndef UI_CLEAN_H
#define UI_CLEAN_H

#include "../ui.h"

typedef enum {
    UI_CLEAN_RINSE,
    UI_CLEAN_DEEP,
} ui_clean_mode_t;

enum {
    UI_CLEAN_PREP_BACK = 0U,
    UI_CLEAN_PREP_START = 7U,
};

typedef enum {
    UI_CLEAN_FLOW_IDLE,
    UI_CLEAN_FLOW_RUNNING,
    UI_CLEAN_FLOW_STOPPING,
    UI_CLEAN_FLOW_READY,
    UI_CLEAN_FLOW_STOPPED,
} ui_clean_flow_state_t;

typedef struct {
    void (*refresh_sensors)(void);
    bool (*start)(ui_clean_mode_t mode);
    ui_clean_flow_state_t (*stop)(void);
    ui_clean_flow_state_t (*poll)(void);
    uint16_t (*remaining_minutes)(ui_clean_mode_t mode);
} ui_clean_flow_ops_t;

void ui_clean_bind_flow(const ui_clean_flow_ops_t *ops);
void ui_clean_enter(void);
/* 打开指定清洗模式的准备页；仅导航，按下 Start 后才申请清洗任务。 */
void ui_clean_prepare(ui_clean_mode_t selected_mode);
/* 显示已在运行的任务，不发送启动命令。 */
void ui_clean_show_running(ui_clean_mode_t selected_mode);
void ui_clean_handle_short_press(void);
void ui_clean_handle_long_press(void);
void ui_clean_move_selection(int direction);
void ui_clean_tick(void);
void ui_clean_sensor_update(bool lid_present, bool tank_present);
/* 供启动门控和公共焦点规则查询同一份盖子/水箱检查结果。 */
bool ui_clean_can_start(void);
bool ui_clean_is_page(ui_page_id_t page);
void ui_clean_reset(void);

#endif
