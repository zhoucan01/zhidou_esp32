# 完成页 Collect Pulp 收尾动作（2026-11-06）

## 最终实现范围

按用户要求，Agent 不修改 `function/` 下除 `UI/` 以外的文件。`Press.c` 原第 6、7 步自动收尾由用户手动注释；制作、压制和 10 分钟保压仍由原流程负责。

页面实现放在 `function/UI/Tofu/ui_tofu_ready.c`：点击 `Collect Pulp` 后创建后台任务，按原程序顺序执行：

1. 抬升电机上升，命令值 23。
2. 等待 `riseHighPos == 1`，停止抬升电机。
3. 等待 300 ms。
4. 压制头上升，命令值 800（80 rpm）。
5. 等待 `pressHighPos == 1`，停止压制电机。
6. 播放提示音乐，抬升电流档恢复 0。

原有高位判据、动作先后与时序保持一致。后台任务使用 4096 字节栈、优先级 13，等待限位时不阻塞 LVGL。页面保持在完成页，收尾结束后返回首页；动作期间忽略旋转、重复短按及长按，任务创建失败时留在完成页供重试。UI 验证模式继续使用原页面模拟行为，不运行电机。

`components/myLvgl/myLvgl.c` 只负责按钮接入、输入锁定和结束后的页面返回；UI 组件构建声明增加 DataCenter/Music 依赖。此前的 DataCenter/FunctionSelection 等待门禁方案已撤回，这两个目录没有本次遗留改动。

## 验证

真实 LVGL 主机回归通过，包括：

- 显示完成页和制作任务未结束时，不启动取料任务。
- 只有点击 `Collect Pulp` 才创建后台任务；创建任务不在 UI 回调内执行电机动作。
- 依次模拟两组高位限位，验证抬升停止后恰好等待 300 ms 再启动压制头。
- 验证完整顺序 `rise up → rise stop → press up → press stop → music → current gear 0`。
- 验证任务创建失败可重试，重复操作不会创建第二个收尾任务，结束后回首页。
- 原有页面、旋转文字内存和配方切换回归通过。

普通模式结果见 [ui-validation.log](ui-validation.log)，UI 验证模式也通过：[ui-validation-mode-1.log](ui-validation-mode-1.log)。主机模拟没有实际驱动电机，不代表完整制作与 10 分钟保压的实机验收。

用户已将 `Press.c` 第 6、7 步自动上升、提示音乐及电流档恢复注释保存。核对差异仅涉及这段收尾；压制、10 分钟保压和 `return true` 保持原样，Agent 未修改该文件。

补充执行了真实 `TouFuPress()` 的主机测试：[test_tofu_press_completion.py](../../tests/firmware/test_tofu_press_completion.py)。保留实际的接触、排水、定型、快压、慢压及保压代码，只模拟硬件反馈和时间；结果确认保压恰好执行 600,000 ms，结束时停止压制，未发出最终抬升、压制头上升、提示音乐或恢复低电流档的调用。[完整结果](press-completion-validation.log)。

用户注释保存后的最终 `nv_build` 构建成功，日志在 `build/tofu-ready-collect-final-build.log`，固件包为 `build/firmware-90yn5g1n/`。应用 SHA256：`6b03497ebc30ea7244e868d6fd2f57ca8937655546347c721e4a6523ab3c6d9b`。

USB 重新连接后枚举为 COM9，已通过 `nv_flash --port COM9` 完成擦除、烧录、各段哈希校验及自动复位。[烧录日志](flash.log)。烧录前应用及 `Press.c` 哈希与最终构建记录一致，未重新采用旧固件。

复位后读取了 15 秒串口日志，包含 `HEARTBEAT:[60593]`，未发现 Guru Meditation、StoreProhibited、Backtrace、Rebooting、Brownout 或 watchdog 标记。原始记录在 `agent-tp/data/captures/2026-11-06/tofu-collect-after-flash-serial.txt`。本次只读取运行日志，未启动实机完整制作或取料动作；完整 10 分钟保压及电机上升的实机联动验收尚未进行。
