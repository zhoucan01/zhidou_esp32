#ifndef UI_H
#define UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "Font/ui_fonts.h"

#include <stdbool.h>
#include <stdint.h>

/* 所有逻辑页面复用同一个 LVGL 屏幕和同一组对象槽位。 */
#define UI_MAX_PAGE_IMAGES 11U
#define UI_MAX_PAGE_LABELS 14U
#define UI_IMAGE_INDEX_NONE 0xFFU

/** @brief 全部功能的逻辑页面编号，用于页面切换、当前页查询及配置表索引。 */
typedef enum {
    UI_PAGE_HOME,                       /**< 公共首页。 */
    UI_PAGE_TOFU_FIRMNESS,              /**< 豆腐软硬度选择页。 */
    UI_PAGE_FIRM_TOFU,                  /**< 豆腐参数设置页，供各软硬度类型共用。 */
    UI_PAGE_MORE_OPTIONS,               /**< 豆腐更多选项页。 */
    UI_PAGE_ADD_INGREDIENTS,            /**< 豆腐添加原料页。 */
    UI_PAGE_MACHINE_CHECK,              /**< 豆腐制作前的机器检查页。 */
    UI_PAGE_PRESS_PARTS_CHECK,          /**< 压制头、模具组件安装检查页。 */
    UI_PAGE_TOFU_MAKING,                /**< 豆腐制作中页面。 */
    UI_PAGE_TOFU_READY,                 /**< 豆腐制作完成页。 */
    UI_PAGE_STOP_TOFU_CONFIRM,          /**< 停止豆腐制作确认页。 */
    UI_PAGE_TOFU_STOPPING,              /**< 豆腐制作正在停止页。 */
    UI_PAGE_TOFU_STOPPED,               /**< 豆腐制作已停止页。 */
    UI_PAGE_PLANTMILK_SELECT,           /**< 植物奶配方选择页。 */
    UI_PAGE_PLANTMILK_OPTIONS,          /**< 植物奶参数设置页。 */
    UI_PAGE_PLANTMILK_ADD_INGREDIENTS,  /**< 植物奶添加原料页。 */
    UI_PAGE_PLANTMILK_MACHINE_CHECK,    /**< 植物奶制作前的机器检查页。 */
    UI_PAGE_PLANTMILK_MAKING,           /**< 植物奶制作中页面。 */
    UI_PAGE_PLANTMILK_READY,            /**< 使用滤网的植物奶制作完成页。 */
    UI_PAGE_PLANTMILK_READY_NO_FILTER,  /**< 不使用滤网的植物奶制作完成页。 */
    UI_PAGE_PLANTMILK_STOP_CONFIRM,     /**< 停止植物奶制作确认页。 */
    UI_PAGE_PLANTMILK_STOPPING,         /**< 植物奶制作正在停止页。 */
    UI_PAGE_PLANTMILK_STOPPED,          /**< 植物奶制作已停止页。 */
    UI_PAGE_CLEAN_SELECT,              /**< 清洗模式选择页。 */
    UI_PAGE_CLEAN_PREPARE_RINSE,        /**< 冲洗准备页。 */
    UI_PAGE_CLEAN_PREPARE_CLEAN,        /**< 深度清洗准备页。 */
    UI_PAGE_CLEAN_RINSING,              /**< 冲洗中页面。 */
    UI_PAGE_CLEAN_CLEANING,             /**< 深度清洗中页面。 */
    UI_PAGE_CLEAN_COMPLETE,            /**< 清洗完成页。 */
    UI_PAGE_CLEAN_STOP_CONFIRM,        /**< 停止清洗确认页。 */
    UI_PAGE_CLEAN_STOPPING,            /**< Screen7：清洗正在停止页。 */
    UI_PAGE_COUNT                      /**< 逻辑页面总数，不代表实际页面。 */
} ui_page_id_t;

/** @brief 公共首页可选择的功能入口。 */
typedef enum {
    UI_HOME_ITEM_TOFU,      /**< 豆腐功能。 */
    UI_HOME_ITEM_MILK,      /**< 植物奶功能。 */
    UI_HOME_ITEM_CLEAN,     /**< 清洗功能。 */
    UI_HOME_ITEM_SETTINGS,  /**< 设置入口。 */
    UI_HOME_ITEM_COUNT      /**< 首页功能入口总数。 */
} ui_home_item_t;

/** @brief 豆腐软硬度选项。 */
typedef enum {
    UI_TOFU_FIRMNESS_SOFT,         /**< 嫩豆腐。 */
    UI_TOFU_FIRMNESS_MEDIUM_FIRM,  /**< 中等硬度豆腐。 */
    UI_TOFU_FIRMNESS_FIRM,         /**< 硬豆腐。 */
    UI_TOFU_FIRMNESS_EXTRA_FIRM,   /**< 特硬豆腐。 */
    UI_TOFU_FIRMNESS_COUNT        /**< 软硬度选项总数。 */
} ui_tofu_firmness_t;

/** @brief 公共首页的图片槽位索引，对应 ui_home_images 数组顺序。 */
enum {
    UI_HOME_PREVIEW_IMAGE = 0U,  /**< 当前功能的预览图片。 */
    UI_HOME_BUTTON_TOFU,         /**< 豆腐入口按钮。 */
    UI_HOME_BUTTON_MILK,         /**< 植物奶入口按钮。 */
    UI_HOME_BUTTON_CLEAN,        /**< 清洗入口按钮。 */
    UI_HOME_BUTTON_SETTINGS,     /**< 设置入口按钮。 */
};

/** @brief 豆腐更多选项页的图片槽位索引。 */
enum {
    UI_MORE_OPTIONS_BACK_BUTTON = 0U,        /**< 返回按钮。 */
    UI_MORE_OPTIONS_INGREDIENT_IMAGE,        /**< 原料选项图片。 */
    UI_MORE_OPTIONS_COAGULANT_IMAGE,         /**< 凝固剂选项图片。 */
    UI_MORE_OPTIONS_AMOUNT_IMAGE,            /**< 用量选项图片。 */
    UI_MORE_OPTIONS_RESTORE_BUTTON,          /**< 恢复默认参数按钮。 */
    UI_MORE_OPTIONS_CONFIRM_BUTTON,          /**< 确认参数按钮。 */
    UI_MORE_OPTIONS_AMOUNT_PREVIOUS_ARROW,  /**< 编辑参数时的上一项或减小箭头。 */
    UI_MORE_OPTIONS_AMOUNT_NEXT_ARROW,      /**< 编辑参数时的下一项或增大箭头。 */
};

/** @brief 豆腐更多选项页可选中的控件，编号与对应图片槽位一致。 */
typedef enum {
    UI_MORE_OPTIONS_CONTROL_BACK = UI_MORE_OPTIONS_BACK_BUTTON,              /**< 返回。 */
    UI_MORE_OPTIONS_CONTROL_INGREDIENT = UI_MORE_OPTIONS_INGREDIENT_IMAGE,    /**< 原料选择。 */
    UI_MORE_OPTIONS_CONTROL_COAGULANT = UI_MORE_OPTIONS_COAGULANT_IMAGE,      /**< 凝固剂选择。 */
    UI_MORE_OPTIONS_CONTROL_AMOUNT = UI_MORE_OPTIONS_AMOUNT_IMAGE,            /**< 用量设置。 */
    UI_MORE_OPTIONS_CONTROL_RESTORE = UI_MORE_OPTIONS_RESTORE_BUTTON,          /**< 恢复默认参数。 */
    UI_MORE_OPTIONS_CONTROL_CONFIRM = UI_MORE_OPTIONS_CONFIRM_BUTTON,          /**< 确认参数。 */
    UI_MORE_OPTIONS_CONTROL_COUNT                                            /**< 可选择控件总数，不含编辑箭头。 */
} ui_more_options_control_t;

/** @brief 豆腐软硬度选择页的图片槽位索引。 */
enum {
    UI_TOFU_FIRMNESS_PREVIEW_IMAGE = 0U,  /**< 当前软硬度的豆腐预览图。 */
    UI_TOFU_FIRMNESS_BACK_BUTTON,         /**< 返回首页按钮。 */
};

/** @brief 豆腐参数设置页的图片槽位索引。 */
enum {
    UI_FIRM_TOFU_BACK_BUTTON = 0U,       /**< 返回软硬度选择页按钮。 */
    UI_FIRM_TOFU_AMOUNT_IMAGE,           /**< 用量选项图片。 */
    UI_FIRM_TOFU_COAGULANT_IMAGE,        /**< 凝固剂选项图片。 */
    UI_FIRM_TOFU_MORE_OPTIONS_BUTTON,    /**< 更多选项按钮。 */
    UI_FIRM_TOFU_CONTINUE_BUTTON,        /**< 继续到添加原料页按钮。 */
    UI_FIRM_TOFU_AMOUNT_PREVIOUS_ARROW,  /**< 用量或凝固剂编辑时的上一项或减小箭头。 */
    UI_FIRM_TOFU_AMOUNT_NEXT_ARROW,      /**< 用量或凝固剂编辑时的下一项或增大箭头。 */
};

/** @brief 豆腐参数设置页可选中的控件，编号与对应图片槽位一致。 */
typedef enum {
    UI_FIRM_TOFU_CONTROL_BACK = UI_FIRM_TOFU_BACK_BUTTON,                  /**< 返回。 */
    UI_FIRM_TOFU_CONTROL_AMOUNT = UI_FIRM_TOFU_AMOUNT_IMAGE,                /**< 用量设置。 */
    UI_FIRM_TOFU_CONTROL_COAGULANT = UI_FIRM_TOFU_COAGULANT_IMAGE,          /**< 凝固剂选择。 */
    UI_FIRM_TOFU_CONTROL_MORE_OPTIONS = UI_FIRM_TOFU_MORE_OPTIONS_BUTTON,  /**< 打开更多选项。 */
    UI_FIRM_TOFU_CONTROL_CONTINUE = UI_FIRM_TOFU_CONTINUE_BUTTON,          /**< 继续下一步。 */
    UI_FIRM_TOFU_CONTROL_COUNT                                            /**< 可选择控件总数，不含编辑箭头。 */
} ui_firm_tofu_control_t;

/** @brief 豆腐用量档位，枚举值是选项索引，不是克数。 */
typedef enum {
    UI_TOFU_AMOUNT_100G,  /**< 100 克档位。 */
    UI_TOFU_AMOUNT_150G,  /**< 150 克档位。 */
    UI_TOFU_AMOUNT_200G,  /**< 200 克档位。 */
    UI_TOFU_AMOUNT_COUNT  /**< 用量档位总数。 */
} ui_tofu_amount_t;

/** @brief 豆腐制作使用的凝固剂类型。 */
typedef enum {
    UI_COAGULANT_MAGNESIUM_CHLORIDE,  /**< 氯化镁。 */
    UI_COAGULANT_CITRIC_ACID,        /**< 柠檬酸。 */
    UI_COAGULANT_COUNT               /**< 凝固剂类型总数。 */
} ui_coagulant_t;

/** @brief 豆腐更多选项页支持的原料类型。 */
typedef enum {
    UI_INGREDIENT_SOYBEANS,    /**< 黄豆。 */
    UI_INGREDIENT_PLANT_MILK,  /**< 植物奶。 */
    UI_INGREDIENT_COUNT        /**< 原料类型总数。 */
} ui_ingredient_t;

/** @brief 豆腐添加原料页的图片槽位索引。 */
enum {
    UI_ADD_INGREDIENTS_BACK_BUTTON = 0U,  /**< 返回豆腐参数设置页按钮。 */
    UI_ADD_INGREDIENTS_FILTER_IMAGE,      /**< 滤网安装步骤的背景图片。 */
    UI_ADD_INGREDIENTS_FILTER_ICON,       /**< 滤网安装图标。 */
    UI_ADD_INGREDIENTS_SOYBEANS_IMAGE,    /**< 添加黄豆步骤的背景图片。 */
    UI_ADD_INGREDIENTS_SOYBEANS_ICON,     /**< 添加黄豆图标。 */
    UI_ADD_INGREDIENTS_DIVIDER_IMAGE,    /**< 步骤分隔图片。 */
    UI_ADD_INGREDIENTS_CONTINUE_BUTTON,  /**< 继续到机器检查页按钮。 */
};

/** @brief 豆腐添加原料页可选中的控件。 */
typedef enum {
    UI_ADD_INGREDIENTS_CONTROL_BACK = UI_ADD_INGREDIENTS_BACK_BUTTON,  /**< 返回。 */
    UI_ADD_INGREDIENTS_CONTROL_CONTINUE =
    UI_ADD_INGREDIENTS_CONTINUE_BUTTON,                               /**< 继续下一步。 */
} ui_add_ingredients_control_t;

/** @brief 豆腐机器检查页的图片槽位索引，状态图片与部件图标分别占用槽位。 */
enum {
    UI_MACHINE_CHECK_BACK_BUTTON = 0U,  /**< 返回添加原料页按钮。 */
    UI_MACHINE_CHECK_SEGMENTS_IMAGE,    /**< 检查项分区背景图片。 */
    UI_MACHINE_CHECK_LID_IMAGE,         /**< 上盖检查状态图片。 */
    UI_MACHINE_CHECK_COAGULANT_IMAGE,  /**< 点卤盒检查状态图片。 */
    UI_MACHINE_CHECK_WATER_IMAGE,       /**< 水箱检查状态图片。 */
    UI_MACHINE_CHECK_WASTE_IMAGE,       /**< 废水箱检查状态图片。 */
    UI_MACHINE_CHECK_CONTINUE_BUTTON,  /**< 继续到压制组件检查页按钮。 */
    UI_MACHINE_CHECK_LID_ICON,          /**< 上盖部件图标。 */
    UI_MACHINE_CHECK_COAGULANT_ICON,   /**< 点卤盒部件图标。 */
    UI_MACHINE_CHECK_WATER_ICON,        /**< 水箱部件图标。 */
    UI_MACHINE_CHECK_WASTE_ICON,        /**< 废水箱部件图标。 */
};

/** @brief 豆腐机器检查页可选中的控件。 */
typedef enum {
    UI_MACHINE_CHECK_CONTROL_BACK = UI_MACHINE_CHECK_BACK_BUTTON,          /**< 返回。 */
    UI_MACHINE_CHECK_CONTROL_CONTINUE = UI_MACHINE_CHECK_CONTINUE_BUTTON,  /**< 检查通过后继续。 */
} ui_machine_check_control_t;

/** @brief 机器检查页使用的逻辑传感器状态，已转换为布尔值，不是原始电平。 */
typedef struct {
    bool lid_present;                  /**< true：上盖在位。 */
    bool coagulant_container_present;  /**< true：点卤盒在位。 */
    bool coagulant_low_level;          /**< true：点卤盒低液位。 */
    bool water_tank_present;           /**< true：水箱在位。 */
    bool water_tank_high_level;        /**< 预留高液位状态，当前忽略，不参与就绪判断。 */
    bool waste_tank_present;           /**< true：废水箱在位。 */
} ui_machine_check_sensors_t;

typedef void (*ui_water_tank_high_level_handler_t)(bool detected);

/** @brief 压制组件安装检查页的图片槽位索引。 */
enum {
    UI_PRESS_PARTS_BACK_BUTTON = 0U,  /**< 返回机器检查页按钮。 */
    UI_PRESS_PARTS_SEGMENT_IMAGE,    /**< 检查项分区背景图片。 */
    UI_PRESS_PARTS_START_BUTTON,     /**< 开始制作按钮。 */
    UI_PRESS_PARTS_MOULD_IMAGE,      /**< 模具组件检查状态图片。 */
    UI_PRESS_PARTS_HEAD_IMAGE,       /**< 压制头检查状态图片。 */
    UI_PRESS_PARTS_HEAD_ICON,        /**< 压制头部件图标。 */
    UI_PRESS_PARTS_MOULD_ICON,       /**< 模具组件部件图标。 */
};

/** @brief 压制组件安装检查页可选中的控件。 */
typedef enum {
    UI_PRESS_PARTS_CONTROL_BACK = UI_PRESS_PARTS_BACK_BUTTON,    /**< 返回。 */
    UI_PRESS_PARTS_CONTROL_START = UI_PRESS_PARTS_START_BUTTON,  /**< 检查通过后开始制作。 */
} ui_press_parts_control_t;

/** @brief 压制组件安装检查页使用的逻辑在位状态。 */
typedef struct {
    bool press_head_present;     /**< true：压制头在位。 */
    bool press_channel_present;  /**< true：压制通道在位。 */
} ui_press_parts_sensors_t;

/** @brief 豆腐制作中页面的图片槽位索引。 */
enum {
    UI_TOFU_MAKING_STOP_BUTTON = 0U,  /**< 打开停止确认页的按钮。 */
    UI_TOFU_MAKING_CONTENT_IMAGE,    /**< 制作中内容图片。 */
    UI_TOFU_MAKING_TOFU_ICON,        /**< 豆腐图标。 */
};

/** @brief 豆腐制作完成页的图片槽位索引。 */
enum {
    UI_TOFU_READY_CONTENT_IMAGE = 0U,  /**< 制作完成内容图片。 */
    UI_TOFU_READY_COLLECT_BUTTON,      /**< 取出豆腐并返回首页按钮。 */
    UI_TOFU_READY_CLEAN_BUTTON,        /**< 进入清洗准备页按钮。 */
};

/** @brief 豆腐制作完成页可选中的控件。 */
typedef enum {
    UI_TOFU_READY_CONTROL_COLLECT = UI_TOFU_READY_COLLECT_BUTTON,  /**< 取出豆腐，返回首页。 */
    UI_TOFU_READY_CONTROL_CLEAN = UI_TOFU_READY_CLEAN_BUTTON,      /**< 进入清洗准备页。 */
} ui_tofu_ready_control_t;

/** @brief 停止豆腐制作确认页的图片槽位索引。 */
enum {
    UI_STOP_TOFU_CONTENT_IMAGE = 0U,  /**< 停止确认内容图片。 */
    UI_STOP_TOFU_STOP_BUTTON,         /**< 确认停止制作按钮。 */
    UI_STOP_TOFU_KEEP_BUTTON,         /**< 保持制作按钮。 */
};

/** @brief 停止豆腐制作确认页可选中的控件。 */
typedef enum {
    UI_STOP_TOFU_CONTROL_STOP = UI_STOP_TOFU_STOP_BUTTON,  /**< 确认停止制作。 */
    UI_STOP_TOFU_CONTROL_KEEP = UI_STOP_TOFU_KEEP_BUTTON,  /**< 继续制作，返回制作中页面。 */
} ui_stop_tofu_control_t;

/** @brief 豆腐制作正在停止页的图片槽位索引。 */
enum {
    UI_TOFU_STOPPING_CONTENT_IMAGE = 0U,  /**< 正在停止的内容图片。 */
    UI_TOFU_STOPPING_ACTION_BUTTON,      /**< 停止过程中显示的禁用清洗按钮。 */
    UI_TOFU_STOPPING_ICON,               /**< 等待停止的沙漏图标。 */
};

/** @brief 豆腐制作已停止页的图片槽位索引。 */
enum {
    UI_TOFU_STOPPED_CONTENT_IMAGE = 0U,  /**< 已停止的内容图片。 */
    UI_TOFU_STOPPED_CLEAN_BUTTON,        /**< 进入清洗准备页按钮。 */
    UI_TOFU_STOPPED_ICON,               /**< 制作已停止的状态图标。 */
};

/** @brief 单个图片槽位的默认资源与布局配置，可由页面逻辑动态调整。 */
typedef struct {
    const void *src;      /**< 普通状态图片源，传给 lv_img_set_src()。 */
    const void *src_alt;  /**< 选中高亮状态图片源；NULL 表示沿用普通状态图片。 */
    int16_t x;           /**< 相对 align 对齐位置的水平偏移，单位：像素。 */
    int16_t y;           /**< 相对 align 对齐位置的垂直偏移，单位：像素。 */
    lv_align_t align;    /**< 图片对象相对父对象的对齐方式。 */
} ui_image_config_t;

/** @brief 单个文字槽位的默认内容、布局与样式，可由页面逻辑动态调整。 */
typedef struct {
    const char *text;       /**< 默认显示文字，使用 UTF-8 编码。 */
    int16_t x;              /**< 相对 align 对齐位置的水平偏移，单位：像素。 */
    int16_t y;              /**< 相对 align 对齐位置的垂直偏移，单位：像素。 */
    lv_align_t align;       /**< 文字对象相对父对象的对齐方式。 */
    uint32_t color_hex;     /**< 文字颜色，格式为 0xRRGGBB。 */
    const lv_font_t *font;  /**< 显示文字使用的 LVGL 字体。 */
    int16_t angle;          /**< 旋转角度，单位：0.1 度，例如 900 表示 90 度。 */
    int16_t pivot_x;        /**< 旋转中心相对文字对象左上角的 X 坐标，单位：像素。 */
    int16_t pivot_y;        /**< 旋转中心相对文字对象左上角的 Y 坐标，单位：像素。 */
    bool hidden;           /**< 默认隐藏状态；true 表示隐藏。 */
} ui_label_config_t;

/** @brief 逻辑页面的默认配置，ui_show_page() 据此更新共用的图片和文字槽位。 */
typedef struct {
    const ui_image_config_t *images;  /**< 图片配置数组，按页面内图片索引排列。 */
    uint8_t image_count;              /**< 图片数量，不超过 UI_MAX_PAGE_IMAGES。 */
    const ui_label_config_t *labels;  /**< 文字配置数组，按页面内文字索引排列。 */
    uint8_t label_count;              /**< 文字数量，不超过 UI_MAX_PAGE_LABELS。 */
} ui_page_config_t;

extern const ui_page_config_t ui_page_configs[UI_PAGE_COUNT];
extern lv_obj_t *ui_Screen;

/* 更新后的第 1～8 屏资源。 */
LV_IMG_DECLARE(ui_img_homepage_png);
LV_IMG_DECLARE(ui_img_ring3_png);
LV_IMG_DECLARE(ui_img_back_high_png);
LV_IMG_DECLARE(ui_img_firm_tofu_200g_png);
LV_IMG_DECLARE(ui_img_firm_tofu__high_200g_png);
LV_IMG_DECLARE(ui_img_795035558);
LV_IMG_DECLARE(ui_img_938874969);
LV_IMG_DECLARE(ui_img_ingredient_png);
LV_IMG_DECLARE(ui_img_ingredient_select_png);
LV_IMG_DECLARE(ui_img_more_tofu_png);
LV_IMG_DECLARE(ui_img_more_tofu_high_png);
LV_IMG_DECLARE(ui_img_more_select_png);
LV_IMG_DECLARE(ui_img_1_png);
LV_IMG_DECLARE(ui_img_2_png);
LV_IMG_DECLARE(ui_img_add_png);
LV_IMG_DECLARE(ui_img_filter_2_png);
LV_IMG_DECLARE(ui_img_filter_png);
LV_IMG_DECLARE(ui_img_normal_png);
LV_IMG_DECLARE(ui_img_high_png);
LV_IMG_DECLARE(ui_img_check1_png);
LV_IMG_DECLARE(ui_img_lid_png);
LV_IMG_DECLARE(ui_img_174602179);
LV_IMG_DECLARE(ui_img_1205051221);
LV_IMG_DECLARE(ui_img_2052261345);
LV_IMG_DECLARE(ui_img_1313577400);
LV_IMG_DECLARE(ui_img_check_png);
LV_IMG_DECLARE(ui_img_machinelid_png);
LV_IMG_DECLARE(ui_img_machinecoagulant_png);
LV_IMG_DECLARE(ui_img_machinewatertank_png);
LV_IMG_DECLARE(ui_img_machinedriptray_png);
LV_IMG_DECLARE(ui_img_grey_png);
LV_IMG_DECLARE(ui_img_444953556);
LV_IMG_DECLARE(ui_img_881831439);
LV_IMG_DECLARE(ui_img_1092992141);
LV_IMG_DECLARE(ui_img_machinepress_2_png);
LV_IMG_DECLARE(ui_img_machinemould_png);
LV_IMG_DECLARE(ui_img_wait_png);
LV_IMG_DECLARE(ui_img_wait_png_lz4);
LV_IMG_DECLARE(ui_img_1036112462);
LV_IMG_DECLARE(ui_img_hourglass_png);

/* 保留旧资源，供 ui_update 中未包含的逻辑页面使用。 */
LV_IMG_DECLARE(ui_img_tofu_png);
LV_IMG_DECLARE(ui_img_milk_png);
LV_IMG_DECLARE(ui_img_clean_png);
LV_IMG_DECLARE(ui_img_setting_png);
LV_IMG_DECLARE(ui_img_2096355461);
LV_IMG_DECLARE(ui_img_1863853840);
LV_IMG_DECLARE(ui_img_1713777493);
LV_IMG_DECLARE(ui_img_1460433628);
LV_IMG_DECLARE(ui_img_322449093);
LV_IMG_DECLARE(ui_img_1272990486);
LV_IMG_DECLARE(ui_img_38353654);
LV_IMG_DECLARE(ui_img_503676461);
LV_IMG_DECLARE(ui_img_bluetooth_png);
LV_IMG_DECLARE(ui_img_clock_png);
LV_IMG_DECLARE(ui_img_countdown_png);
LV_IMG_DECLARE(ui_img_1837598647);

/* 屏幕 2 */
LV_IMG_DECLARE(ui_img_soft_png);
LV_IMG_DECLARE(ui_img_medium_firm_png);
LV_IMG_DECLARE(ui_img_firm_png);
LV_IMG_DECLARE(ui_img_extra_firm_png);
LV_IMG_DECLARE(ui_img_back_png);
LV_IMG_DECLARE(ui_img_1195725596);

/* 屏幕 3 */
LV_IMG_DECLARE(ui_img_firm_tofu_high_150_png);
LV_IMG_DECLARE(ui_img_firm_tofu_150_png);
LV_IMG_DECLARE(ui_img_558999578);
LV_IMG_DECLARE(ui_img_firm_tofu__high200_png);
LV_IMG_DECLARE(ui_img_firm_tofu_high_100_png);
LV_IMG_DECLARE(ui_img_next_png);
LV_IMG_DECLARE(ui_img_previous_png);
LV_IMG_DECLARE(ui_img_disabled_png);
LV_IMG_DECLARE(ui_img_1408384802);
LV_IMG_DECLARE(ui_img_coagulant_png);
LV_IMG_DECLARE(ui_img_61027324);
LV_IMG_DECLARE(ui_img_coagulant_select_png);
LV_IMG_DECLARE(ui_img_coagulant_acid_highlight_png);
LV_IMG_DECLARE(ui_img_acid_coagulant_png);
LV_IMG_DECLARE(ui_img_more_options_high_png);
LV_IMG_DECLARE(ui_img_more_options_png);
LV_IMG_DECLARE(ui_img_firm_continuehigh_png);
LV_IMG_DECLARE(ui_img_firm_continue_png);

/* 屏幕 4 */
LV_IMG_DECLARE(ui_img_add_ingredents_png);
LV_IMG_DECLARE(ui_img_add_soybeans_png);
LV_IMG_DECLARE(ui_img_fit_filter_png);
LV_IMG_DECLARE(ui_img_continue_png);
LV_IMG_DECLARE(ui_img_continue_high_png);

/* 屏幕 5 */
LV_IMG_DECLARE(ui_img_segments_png);
LV_IMG_DECLARE(ui_img_check_lid_png);
LV_IMG_DECLARE(ui_img_check_coagulant_png);
LV_IMG_DECLARE(ui_img_check_water_png);
LV_IMG_DECLARE(ui_img_check_drip_tray_png);
LV_IMG_DECLARE(ui_img_check_continue_png);
LV_IMG_DECLARE(ui_img_check_lid_high_png);
LV_IMG_DECLARE(ui_img_check_coagulant_high_png);
LV_IMG_DECLARE(ui_img_check_water_high_png);
LV_IMG_DECLARE(ui_img_check_drip_tray_high_png);

/* 屏幕 6 */
LV_IMG_DECLARE(ui_img_segment_png);
LV_IMG_DECLARE(ui_img_start_making_high_png);
LV_IMG_DECLARE(ui_img_start_making_png);
LV_IMG_DECLARE(ui_img_fit_mould_assembly_png);
LV_IMG_DECLARE(ui_img_machinepress_png);
LV_IMG_DECLARE(ui_img_machinepress_high_png);
LV_IMG_DECLARE(ui_img_fit_mould_assembly_high_png);

/* 屏幕 7 */
LV_IMG_DECLARE(ui_img_stop_png);
LV_IMG_DECLARE(ui_img_making_tofu_png);

/* 屏幕 8 */
LV_IMG_DECLARE(ui_img_content_png);
LV_IMG_DECLARE(ui_img_collect_pulp_png);
LV_IMG_DECLARE(ui_img_collect_pulp_high_png);
LV_IMG_DECLARE(ui_img_ready_clean_png);
LV_IMG_DECLARE(ui_img_ready_clean_high_png);

/* 屏幕 9 */
LV_IMG_DECLARE(ui_img_stop_making_tofu_png);
LV_IMG_DECLARE(ui_img_keep_making_png);
LV_IMG_DECLARE(ui_img_keep_making_normal_png);
LV_IMG_DECLARE(ui_img_making_stopped_clean_high_png);

/* 屏幕 10 */
LV_IMG_DECLARE(ui_img_making_stoppedclean_png);
LV_IMG_DECLARE(ui_img_stopping_png);
LV_IMG_DECLARE(ui_img_making_png);

void ui_init(void);
void ui_destroy(void);
bool ui_is_initialized(void);

void ui_show_page(ui_page_id_t page);
ui_page_id_t ui_get_current_page(void);

void ui_select_image(ui_page_id_t page, uint8_t index);
uint8_t ui_get_selected_image(ui_page_id_t page);
void ui_set_image_src(ui_page_id_t page, uint8_t index, const void *src);
void ui_set_label_text(ui_page_id_t page, uint8_t index, const char *text);
void ui_set_label_pos(ui_page_id_t page, uint8_t index, int16_t x, int16_t y);

void ui_set_button(ui_page_id_t page, uint8_t index);
void ui_select_button(uint8_t index);
uint8_t ui_get_selected_button(void);
void ui_swap_image(uint8_t index, const void *src);

void ui_select_home_item(ui_home_item_t item);
ui_home_item_t ui_get_selected_home_item(void);

void ui_select_tofu_firmness(ui_tofu_firmness_t firmness);
ui_tofu_firmness_t ui_get_selected_tofu_firmness(void);
void ui_select_tofu_firmness_back(bool selected);
bool ui_is_tofu_firmness_back_selected(void);

void ui_select_firm_tofu_control(ui_firm_tofu_control_t control);
ui_firm_tofu_control_t ui_get_selected_firm_tofu_control(void);
void ui_set_firm_tofu_editing(bool editing);
bool ui_is_firm_tofu_editing(void);
void ui_adjust_firm_tofu_value(int8_t direction);
ui_tofu_amount_t ui_get_selected_tofu_amount(void);
ui_coagulant_t ui_get_selected_coagulant(void);
ui_ingredient_t ui_get_selected_ingredient(void);

void ui_select_more_options_control(ui_more_options_control_t control);
ui_more_options_control_t ui_get_selected_more_options_control(void);
void ui_set_more_options_editing(bool editing);
bool ui_is_more_options_editing(void);
void ui_adjust_more_options_value(int8_t direction);
void ui_restore_default_tofu_options(void);

void ui_select_add_ingredients_control(ui_add_ingredients_control_t control);
ui_add_ingredients_control_t ui_get_selected_add_ingredients_control(void);

void ui_select_machine_check_control(ui_machine_check_control_t control);
ui_machine_check_control_t ui_get_selected_machine_check_control(void);
void ui_machine_check_update(const ui_machine_check_sensors_t *sensors);
bool ui_machine_check_can_continue(void);
void ui_machine_check_set_water_tank_high_level_handler(
    ui_water_tank_high_level_handler_t handler);

void ui_select_press_parts_control(ui_press_parts_control_t control);
ui_press_parts_control_t ui_get_selected_press_parts_control(void);
void ui_press_parts_check_update(const ui_press_parts_sensors_t *sensors);
bool ui_press_parts_check_can_start(void);

void ui_select_tofu_ready_control(ui_tofu_ready_control_t control);
ui_tofu_ready_control_t ui_get_selected_tofu_ready_control(void);
bool ui_tofu_ready_collect(void);
bool ui_tofu_ready_is_collecting(void);

void ui_select_stop_tofu_control(ui_stop_tofu_control_t control);
ui_stop_tofu_control_t ui_get_selected_stop_tofu_control(void);

void ui_tofu_stopping_set_complete(bool complete);
bool ui_tofu_stopping_is_complete(void);

#ifdef __cplusplus
}
#endif

#endif
