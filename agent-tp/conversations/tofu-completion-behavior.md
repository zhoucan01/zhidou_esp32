# 豆腐完成页与机械收尾（2026-11-06）

后续行为已调整：本记录描述修改前的实现。最新版本为压制及 10 分钟保压结束后显示完成页，点击 Collect Pulp 后才按原顺序抬升及抬压制头，见[取料改动记录](../reports/tofu-collect/README.md)。

核对当前源码，未启动实际制作、加热或电机动作，未修改固件。

正常制作路径：

1. `function/Press/Press.c` 的 `TouFuPress()` 完成慢压及 10 分钟保压。
2. 调用 `riseMoterUp(RISE_TO_HIGH_RPM)`，等待 `riseHighPos == 1`，停止抬升，等待 300 ms。
3. 调用 `pressMoterUp(800)`，等待 `pressHighPos == 1`，停止压制电机；播放完成提示，抬升电流档返回低档，函数返回成功。
4. `function/ToufuMake/ToufuMake.c` 的豆腐任务调用 `FunctionSelectionFlowCompleted(TOUFU_SEC)`。
5. `components/myLvgl/myLvgl.c` 的 `refresh_tofu_flow_page()` 观察到曾运行的豆腐任务结束、状态变为 `NOTASK_SEC`，从制作/停止确认页显示 `UI_PAGE_TOFU_READY`，即用户展示的完成页。

因此正常顺序是先让抬升及压制头回到高位，再显示完成页。当前普通模式 `MYLVGL_UI_VALIDATION_MODE` 默认为 0；未发现所查构建入口覆盖该宏。

边界：UI 只根据任务从运行到空闲判定完成，没有读取独立的成功/失败结果。功率校准失败和站2上线等待超时等异常退出也调用相同任务完成接口，存在误显示“豆腐已完成”的可能。此页面本身不能作为全部机械动作成功的独立证据。

完成页的 `Collect Pulp` 当前返回首页；`Clean` 打开深度清洗准备页，两者不负责触发上述完成收尾抬升。
