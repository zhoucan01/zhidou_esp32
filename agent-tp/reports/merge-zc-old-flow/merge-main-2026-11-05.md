# 合并最新 main 到当前 UI 工作分支

日期：2026-11-05。

## 合并结果

- 当前分支：`zc_old_flow_4678752`。
- 合并前 HEAD：`93b8b2920ffe92c49261ce8f16318ab5ea6904b7`。
- 本次获取的 `origin/main`：`b9b191b`，提交标题为 `Merge remote-tracking branch 'origin/feature_add_ota_debug'`。
- 合并提交：`6f20104`；父提交为 `93b8b29`、`b9b191b`。
- `git merge-base --is-ancestor origin/main HEAD` 通过，最新 main 已包含在当前分支历史中。
- 未推送任何分支，未烧录设备。

## 冲突与本地改动的保留

1. 先使用 Git 自动暂存保护原有 10 个已跟踪路径的本地修改，再完成分支合并。
2. `.vscode/settings.json`、`.vscode/c_cpp_properties.json` 保留本机配置，避免使用 main 中另一台电脑的路径和串口。
3. 启动入口采用 main 的蓝牙名称准备、WiFiConnect/OTAService/WiFiPortal 等任务接入，同时保留当前分支的首帧背光门控、UI 提前显示和应用就绪握手。
4. `test_display_startup.py` 随上游目录调整放入 `agent-tp/tests/firmware/`，修正工程根目录定位为 `parents[3]`。
5. 自动暂存恢复时，旋钮日志删除与 main 新增的配网回调发生冲突。最终只保留非日志用途的 `my_encoder_task_config_t.boot_hold_callback`，使用 `encoder_config` 接入 `WiFiPortalRequestStart`；日志回调、事件类型、队列及启动解压性能测试继续保持删除。
6. 原旋钮、UI 主机测试修改恢复到上游迁移后的 `agent-tp/tests/firmware/` 路径，修正根目录定位和构建源文件列表。
7. 本地修改恢复为未暂存状态，没有把它们整体自动提交。合并前的自动备份 `2ed0866329b6f15b1a1ad6a63cad6bcf085ec7d5` 仍保留在 stash 中；内容已经恢复，不需再次执行 `stash pop`。
8. 合并前后核对：本机 IDE 设置 JSON 内容一致，`docs/UI历史问题.md`、UI 构建配置及图片声明/别名头文件中的原有本地修改得到保留。

## 随 main 引入的行为与目录变化

- 包含 main 的 OTA、联网配网、温度处理等更新；本报告不将它们记作本次重新实现的功能。
- `UI_ENCODER_PULSES_PER_SWITCH` 采用 main 的值 `3`，即三个有效编码器步切换一个 UI 项。豆腐停止确认页的方向映射与默认 Keep Making 逻辑仍保留。
- main 的开机配网手势为启动窗口内长按 5 秒；普通菜单按键在配网条件不成立时继续按原有短按/2 秒长按规则处理。
- 主机测试迁移到 `agent-tp/tests/firmware/`，相关报告和工具按上游布局归档。
- 上游目录和第三方组件同时调整；没有批量清理上游报告、第三方源码中原有的空白格式差异。

## 验证

### 已执行并通过

- 无未解决的 Git 合并冲突，暂存区已恢复为空；本次本地修改执行 `git diff --check`。
- 使用本机 Clang 编译并运行真实编码器 C 实现的 GPIO/RTOS 模拟测试：四种初始相位、方向、抖动、非法跳变、8000 步连续旋转、计数饱和、任务通知、消抖和短按/长按。
- 配网回调与无日志实现共存的测试：开机窗口内 5 秒触发、仅触发一次、窗口边界和普通长按行为。
- 显示启动模拟测试：提前关闭背光、完整帧 DMA 完成后点亮、ISR 隔离、亮度保存、立即绘制和启动握手。
- 使用实际 `process_button()` 与 `wait_ticks()` 提取体运行上游 `test_boot_button.c`，开机手势和普通按键测试通过。
- 使用系统入口 `nv_build` 完整构建成功。应用 BIN 为 `0x1aeb00` 字节，最小应用分区 `0x7a3000` 字节，剩余 `0x5f4500` 字节，约 78%。
- 生成 `build/EVT_ESP32-S3.bin` 和可移植固件包 `build/firmware-i9v89b9c/`。所有生成的桩、可执行文件、缓存均保留在 `build/`，不进入提交。

### 构建环境说明

首次调用 `nv_build` 时，ESP-IDF 环境导出因 `C:/Espressif/espidf.constraints.v5.3.txt` 缺失而中止，尚未开始源码编译。已使用本机现有 `C:/Espressif/tools/espidf.constraints.v5.3.txt` 验证 Python 依赖满足，并经授权复制到入口要求的位置；未跳过依赖检查。随后使用同一 `nv_build` 入口完成构建。

### 未执行

- 未运行全量 LVGL 页面渲染主机回归及所有 OTA/温度模块测试。
- 未执行 `nv_flash`，未进行真实配网、OTA、电机、加热及上电时序验收。
- 主机模拟和固件构建不证明真实双核调度、电气行为或设备内存峰值满足要求。

## 主要入口

- [启动入口](../../../main/main.c)
- [编码器实现](../../../components/MyEncoder/MyEncoder.c)
- [编码器配置](../../../components/MyEncoder/include/MyEncoder.h)
- [旋钮主机回归](../../tests/firmware/test_encoder_interrupts.py)
- [显示启动回归](../../tests/firmware/test_display_startup.py)
- [开机配网手势回归](../../tests/firmware/test_boot_button.c)
