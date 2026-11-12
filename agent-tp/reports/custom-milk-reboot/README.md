# Custom Milk 切换重启修复（2026-11-06）

## 原因与修复

用户提供的故障栈为 `lvUIshowTask → LVGL refresh → lv_draw_sw_layer_create → lv_memset_00`，异常地址为 0，清零长度 14,592 字节。

- `components/lvgl/src/draw/sw/lv_draw_sw_layer.c` 的不可分片图层分支，在 `lv_mem_alloc()` 后先判断 NULL，再清零及修改刷新上下文。分配失败交回上层已有的跳过绘制路径。
- 实际固件配置为 `sdkconfig` 的 `CONFIG_LV_CONF_SKIP=y`，因此 `main/lv_conf.h` 中的 64 KiB 不是固件实际池大小。本次将实际 `CONFIG_LV_MEM_SIZE_KILOBYTES` 从 32 改为 128；沿用 `lv_mem.c` 的外部 BSS/PSRAM 池。透明图层支持在实际 Kconfig 中原已启用。
- 主机 UI 回归原用系统 malloc，无法覆盖设备固定池不足。现在从 `sdkconfig` 导入 LVGL 选项，使用相同大小的 TLSF 池；仅改变主机诊断、断言处理及性能浮层。未调整文字内容、字号、旋转方向或配方业务。

## 已完成验证

1. 用真实 LVGL/TLSF 人为占用内存，在图层上下文仍能分配、像素缓冲无法分配的情况下，分别进行带透明和不透明图层的 50 次创建，共 100 次失败。全部返回 NULL，内存可用量和最大连续块不变，绘制指针及透明状态不变，`lv_mem_test()` 通过。
2. 释放占用后重新创建 14,592 字节透明图层，逐字节验证已清零；销毁后内存和上下文恢复。
3. 全部现有 UI 流程回归通过；额外连续切换 200 次配方，共完成 2,880 次图层创建，所有正常图层分配成功。最大单图层 14,592 字节；池 131,072 字节，记录峰值 116,610 字节，结束时最大连续空闲块 112,592 字节。峰值属于主机测试序列，不能作为 ESP32 实测内存峰值。
4. `git diff --check` 通过。

完整结果：[host-validation.log](host-validation.log)。主机渲染：[Custom Milk](host-custom-milk.png)。

复现主机测试（需要支持当前平台的 GCC/兼容编译器）：

```text
python -B agent-tp/tests/firmware/test_plantmilk_ui.py --cc <host-compiler>
```

本机用下载到 `build/host-deps/` 的 Zig C 编译器执行，缓存位于 `build/ccache/zig*`。临时编译器包装关闭 Zig 默认启用的 UB sanitizer，以保持已有 LVGL 字体位运算在普通 GCC 下的执行方式；测试保留 C 断言、内存完整性及图层成功断言。最初带 sanitizer 的运行在 LVGL 原有字体解压代码的负数左移处中止，与本次空指针修复无关。

## 实机画面及验收边界

已按 DirectShow 设备名称精确选择 SHL-500W（枚举索引 1），抓取 1920×1080 画面：[当前 Oat Milk 页面](device-before.png)。采集工具：[capture.py](../../tools/camera_capture/capture.py)。COM12 读取的短日志在 `agent-tp/data/captures/2026-11-06/custom-milk-before-serial.txt`，未向设备发送控制指令。

用户明确允许整片擦除及烧录后，已通过系统入口 `nv_flash --port COM12` 完成擦除、烧录、各段哈希校验和自动复位。[烧录日志](flash.log)。

用户操作实机旋钮，完成反复切换并回复“已切换”。SHL-500W 已拍到实机停在 [Custom Milk 页面](device-custom-milk.png)，文字正常显示。同步读取的 55 秒串口记录在 `agent-tp/data/captures/2026-11-06/custom-milk-after-serial.txt`，共 2,564 字节，心跳依次为 30590、40590、50590、60590、70590，没有 StoreProhibited、Guru Meditation、Backtrace、Rebooting、ESP-ROM、复位原因或 watchdog 标记。本次实机回归未复现原故障；不据此声称已完成长期运行验收。

主机渲染图片为 `host-custom-milk.png`；实机图片由 SHL-500W 直接采集，未用主机渲染替代实机结果。

## 固件构建

使用项目系统入口 `nv_build`，本机显式指定 IDF 安装及其已有 Python 环境，最终构建成功。应用大小 `0x1aee60` 字节。最终链接映射确认：`lv_mem.c.obj` 的池位于 `.ext_ram.bss`，地址 `0x3c1a0000`，大小 `0x20000`（128 KiB）。

应用 SHA256：`50ca187e6eb8e6301476726d274badae69c68817b56beba17ac07afcb61913ee`。便携固件包：`build/firmware-jcbaj5za/`；构建日志：`build/custom-milk-build.log`。烧录清单的应用地址为 `0x20000`，包含 bootloader、分区表及 OTA 初始化数据。

工作区存在其他未提交变更，本次未提交或推送；构建包含构建时的整个工作区，不是只含本补丁的独立镜像。
