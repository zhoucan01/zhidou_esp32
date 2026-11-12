# ESP32-S3 LVGL UI 开发指南

当前 UI 使用单 screen、固定图片和文字槽位的配置化架构。SquareLine 的多 screen 导出不能直接参与固件编译；其中的对象需要转换为 `ui.c` 的逻辑页面配置。

## 当前结构

- `function/UI/ui.h`：页面 ID、图层配置结构、图片声明与 UI API。
- `function/UI/ui.c`：创建唯一的 `ui_Screen`，管理图片/文字槽位、高亮状态和运行时换图。
- `function/UI/images/`：实际参与编译的图片资源。
- `components/myLvgl/myLvgl.c`：旋钮、短按、长按处理及 LVGL 周期任务。
- SquareLine 导出只作为转换输入，完成转换后从工程中删除，不参与构建。

当前版本包含 `UI_PAGE_HOME`、`UI_PAGE_TOFU_FIRMNESS` 和 `UI_PAGE_FIRM_TOFU` 三个逻辑页面。三个页面共用唯一的 `ui_Screen`、9 个图片槽和 4 个文字槽。SquareLine 中同位置的预览图、普通图和高亮图复用同一个图片槽，因此不会为隐藏状态重复创建 LVGL 对象。

## 配置规则

静态图层使用 `IMG`，可选按钮使用 `IMG_HL`：

```c
static const ui_image_desc_t home[] = {
    IMG(ui_img_background_png, 0, 0, LV_ALIGN_CENTER),
    IMG_HL(ui_img_normal_png, ui_img_highlight_png,
           -37, -124, LV_ALIGN_CENTER),
};
```

坐标和对齐方式由配置表统一维护。`ui_init()` 根据所有配置的最大图层数创建槽位，运行时只换图、移动或隐藏这些槽位，不重复创建 screen。

图片资源加入工程时，需要同时完成以下三项：

1. 把转换后的 `.c` 文件放入 `function/UI/images/`。
2. 在 `ui.h` 中加入对应的 `LV_IMG_DECLARE(...)`。
3. `function/UI/CMakeLists.txt` 会自动收集图片源并直接链接进应用固件，不需要单独烧录图片资源包。

## 交互

- `ui_select_button(index)`：选中当前页按钮并显示高亮图。
- `ui_get_selected_button()`：取得当前高亮按钮索引。
- `ui_swap_image(index, src)`：临时替换当前页指定图层；传 `NULL` 恢复配置图。
- `ui_select_home_item(item)`：按语义选中 Tofu、Milk、Clean 或 Settings，不需要调用方依赖图片槽索引。
- 旋钮切换步数由 `UI_ENCODER_PULSES_PER_SWITCH` 配置，当前每个有效机械步切换一项。物理方向由 `MY_ENCODER_FORWARD_DIRECTION` 校准；所有页面顺转下一项、反转上一项。圆盘选择页首尾循环；普通参数和流程按钮页到首尾后停止，端点外的步数不会积压，反转一步即可离开端点。
- 主页在 Tofu、Milk、Clean、Settings 之间循环。Tofu Firmness 按 Soft、Medium Firm、Firm、Extra Firm、Back 循环，两个方向都可选中 Back，短按 Back 返回主页。植物奶配方和清洁类型的圆盘选择同样循环，且将 Back 纳入循环，保证可退出选择页。
- 豆腐参数页按 Back、Tofu Amount、Coagulant、More Options、Continue 移动，More Options 和植物奶参数页也按控件顺序移动。短按参数进入/退出编辑；编辑时旋钮只调整当前值，退出编辑后才移动焦点。
- 豆腐、植物奶的加料/检查页以及清洁准备页，顺转选 Continue/Start，反转选 Back；同向继续旋转保持当前端点。只移动焦点不会执行制作或清洁任务，短按选中的按钮才执行对应操作。
- 传感器检查未通过时，豆腐机器检查、压制部件检查、植物奶机器检查及漂洗/深度清洁准备页强制高亮 Back，旋钮和直接选择均不能聚焦 Continue/Start。传感器从通过变为未通过时立即退回 Back；全部通过后保留当前 Back 焦点，允许用户再次选择 Continue/Start。
- 双按钮的结果页/停止确认页采用首尾停止规则。装饰图片不参与焦点移动，单按钮页与停止中的页面保持原有操作约束。
- Blending Time 的 `MM:SS` 数值使用原 Manrope 22 字形的等宽数字版本，固定每位数字的进位宽度并关闭字符对间距。切换时间时标签尺寸、分钟和冒号的位置及编辑箭头保持稳定，避免比例数字宽度变化触发重新居中后的跳动。
- 长按优先停止当前运行流程。

## 约束

- 不恢复 SquareLine 的 `ui_Screen1~ui_ScreenN` 多 screen 结构。
- 不把 SquareLine 的 `screens/`、`ui_helpers.c` 或页面源文件加入构建。
- 不在页面切换时创建或销毁 LVGL 图片或文字对象。
- 新增按钮时必须同时提供普通图和高亮图，并确保图层总数不超过 `UI_MAX_PAGE_IMAGES`。
- 修改后至少执行一次完整构建，并检查没有引用已删除的旧图片或旧 UI API。
