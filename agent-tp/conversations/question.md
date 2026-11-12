# RS485 通信自检与修复记录

> 日期：2026-09-22
> 范围：ESP32-S3 双串口 RS485 通信链路（站号 2 低压板 + 站号 3 高压板）

## 一、改动背景

工程原有两路 UART 通信：
- **UART2（ScreenUart）**：TX=GPIO1、RX=GPIO2、115200，控制站号 2（低压板）。
- **UART1（PowerUart）**：TX=GPIO39、RX=GPIO38、原 9600，控制站号 3（高压板）。

两路串口的 DE 引脚此前均定义为 `UART_PIN_NO_CHANGE`，即未启用 RS485 半双工模式，`usart.c` 中通过 `if (de != UART_PIN_NO_CHANGE)` 条件判断是否切换到 `UART_MODE_RS485_HALF_DUPLEX`。

本次改动将两路串口统一改为 RS485 通信：
- 为 UART2 配置 DE = `GPIO_NUM_37`（`ScreenDe`）。
- 为 UART1 配置 DE = `GPIO_NUM_36`（`PowerDe`）。
- `usart.c` 中无条件启用 `UART_MODE_RS485_HALF_DUPLEX`，由 ESP32 UART 硬件自动控制 DE 引脚收发切换（发送时拉高，接收时拉低）。

## 二、自检发现的问题

对 `components/usart/`、`components/sys/include/sys.h`、`task/DataCenter/` 全链路扫描后，发现以下问题：

### 问题 1：波特率配置不一致（严重）

- `components/sys/include/sys.h` 中 `PowerBaud` 已由 9600 改为 115200。
- `task/DataCenter/ModbusMap.h` 中 `MODBUS_POWER_BAUD` 仍为 9600。
- `DataCenter.c` 启动日志使用 `MODBUS_POWER_BAUD` 输出，会打印错误的波特率，误导调试。
- 实际波特率由 `sys.h` 中的 `PowerBaud` 决定，`MODBUS_POWER_BAUD` 仅用于日志，不影响通信，但属于配置漂移隐患。

**解决**：将 `ModbusMap.h` 中 `MODBUS_POWER_BAUD` 同步改为 115200，与 `sys.h` 保持一致。

### 问题 2：`ScrUartGetData` 声明未实现（中等）

- `components/usart/include/usart.h` 声明了 `ScrUartGetData`，但 `usart.c` 中无实现。
- 若有调用方，链接阶段会报 `undefined reference to ScrUartGetData`。
- 当前工程内无调用（`DataCenter.c` 直接使用 `uart_read_bytes`），但属于接口契约违约。

**解决**：在 `usart.c` 中补全 `ScrUartGetData` 实现，作为 `Uart_GetData` 的别名（均读 `ScreenUart`），保持头文件契约完整。

### 问题 3：`Uart_GetData` 写死 ScreenUart（中等）

- `Uart_GetData` 命名是通用接口，但实现写死读 `ScreenUart`，无法读取 `PowerUart`。
- 当前无调用方，但接口语义不完整，后续扩展会踩坑。

**解决**：保留 `Uart_GetData` 读 `ScreenUart`（向后兼容），`ScrUartGetData` 作为别名。若后续需要读 PowerUart，应新增 `PowerUartGetData` 接口而非复用 `Uart_GetData`。

### 问题 4：sys.h 注释与实际不符（轻微）

- 修改后注释仍写 "GPIO_NUM_37 is currently unused on the board"，但实际已作为 DE 引脚使用。
- GPIO_NUM_36 曾在弃用注释中作为 `PowerRx`，注释未说明当前已改作 DE 引脚。

**解决**：更新注释为 "RS485 transceiver DE/RTS pin. Driven high by UART hardware during TX."，准确反映用途。

### 问题 5：RS485 发送前未 flush 接收缓冲（已确认无需修改）

- `DataCenter.c` 中 `modbus_uart_write` 发送前未调用 `uart_flush_input`。
- 但 nanomodbus 平台层已注册 `modbus_uart_flush` 回调，nanomodbus 在每次事务开始前会调用 flush，逻辑正确。
- `execute_raw_gateway` 中也显式调用了 `uart_flush_input(uart)`。
- **结论**：无需修改，现有 flush 策略符合 RS485 半双工要求。

### 问题 6：RS485 帧间隔参数偏大（已确认无需修改）

- `MODBUS_GATEWAY_GAP_TIMEOUT_MS = 30U`，在 115200 波特率下远大于 Modbus RTU 规范的 3.5 字符时间（约 0.4ms）。
- 该参数用于网关响应帧的分段读取超时，偏大只会导致读取略慢，不影响功能正确性。
- `MODBUS_RESPONSE_TIMEOUT_MS = 100` 在 115200 下足够覆盖最长响应帧。
- **结论**：无需修改，保留余量以兼容总线延迟。

## 三、修改文件清单

| 文件 | 改动内容 |
|------|----------|
| `components/sys/include/sys.h` | `ScreenDe` 改为 `GPIO_NUM_37`；`PowerDe` 改为 `GPIO_NUM_36`；更新两处注释 |
| `components/usart/usart.c` | 移除 `if (de != UART_PIN_NO_CHANGE)` 条件判断，无条件启用 `UART_MODE_RS485_HALF_DUPLEX`；补全 `ScrUartGetData` 实现 |
| `task/DataCenter/ModbusMap.h` | `MODBUS_POWER_BAUD` 由 9600 改为 115200，与 `sys.h` 一致 |

## 四、未修改但已确认正确的部分

- `DataCenter.c` 的 `modbus_uart_write` 已调用 `uart_wait_tx_done` 等待发送完成，RS485 半双工下确保 DE 引脚在发送完成后才切换为接收，逻辑正确。
- `execute_raw_gateway` 中发送前 `uart_flush_input`、发送后 `uart_wait_tx_done`，流程完整。
- `modbus_uart_flush` 已注册到 nanomodbus 平台层，事务前自动清空接收缓冲。
- `select_slave` 根据从站地址切换 `modbus_transport.uart`，两路串口路由正确。

## 五、验证

- 所有修改文件 IntelliSense 无编译错误。
- 建议执行 `idf.py build` 完整编译验证，并实测两路 RS485 与从站 2/3 的 Modbus 通信。

## 六、构建验证结果（2026-09-22）

执行 `idf.py build` 完整构建，**构建成功**。

### 构建环境
- ESP-IDF：v5.3.1
- 目标芯片：esp32s3
- Python：3.11.2（`idf5.3_py3.11_env`）
- 构建工具：cmake 3.24.0 + ninja 1.11.1

### 构建产物
| 产物 | 大小 | 说明 |
|------|------|------|
| `build/EVT_ESP32-S3.bin` | 0x31e200 字节（约 3.26 MB） | 主应用固件 |
| `build/bootloader/bootloader.bin` | 0x5260 字节 | bootloader，剩余 36% 空间 |
| `build/partition-table.bin` | - | 分区表 |

### 空间占用
- 最小 app 分区大小：0x3b0000 字节（约 3.69 MB）
- 剩余空间：0x91e00 字节（约 584 KB，15% free）
- 编译进度：`[169/169]` 全部完成，无失败目标

### 结论
- ✅ CMake 配置阶段通过（5 个依赖组件全部处理）
- ✅ Ninja 编译阶段通过（169 个目标全部成功）
- ✅ 链接阶段通过（ELF 转 bin 成功）
- ✅ 无编译错误、无链接错误
- ⚠️ 固件已占用 85% 分区空间，后续新增功能需关注分区容量

### 烧录命令
```powershell
idf.py flash
# 或
python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0xf000 build\ota_data_initial.bin 0x20000 build\EVT_ESP32-S3.bin
```

## 七、蓝牙 Modbus 请求超时问题排查与修复（2026-09-22）

### 现象

烧录后通过蓝牙上位机发送 Modbus 请求，串口监视器输出：
```
I (60073) gatt: Bluetooth gateway text received: 20 bytes
I (60253) gatt: Bluetooth gateway text received: 15 bytes
W (60772) BlueDataDeal: Bluetooth Modbus request rejected: result=3
```
每隔约 10 秒重复一次，`result=3` 持续出现。

### 根因分析

`result=3` 对应 `control_modbus_result_t` 枚举中的 `CONTROL_MODBUS_TIMEOUT`（枚举值从 0 起：OK=0, BAD_CRC=1, BAD_SLAVE=2, TIMEOUT=3, BAD_RESPONSE_CRC=4）。

调用链：`BlueDataDealTask` → `ControlModbusProcess` → `ModbusGatewayTransaction` → `execute_raw_gateway`。

`execute_raw_gateway` 中：
```c
int received = uart_read_bytes(
    uart, gateway_transaction.response, 1,
    pdMS_TO_TICKS(MODBUS_GATEWAY_RESPONSE_TIMEOUT_MS));  // 500ms
if (received != 1) {
    return false;  // ← 从站 500ms 内未应答，超时
}
```

**根本原因：波特率不匹配。**

- `docs/ESP32S3_Modbus主站协议.md` 明确规定：站号 3（动力板）波特率为 **9600**。
- 但 `sys.h` 中 `PowerBaud` 被改为 115200（与站号 2 相同），`ModbusMap.h` 中 `MODBUS_POWER_BAUD` 也被同步改为 115200。
- ESP32 以 115200 向实际只有 9600 的动力板发送请求，从站无法解析帧，自然不应答，导致 `CONTROL_MODBUS_TIMEOUT`。

### 修复

| 文件 | 修改 |
|------|------|
| `components/sys/include/sys.h` | `PowerBaud` 由 115200 改回 **9600** |
| `task/DataCenter/ModbusMap.h` | `MODBUS_POWER_BAUD` 由 115200 改回 **9600** |

### 验证

- 重新执行 `idf.py build`，`[49/49]` 全部成功，`Project build complete`。
- 固件大小不变：`EVT_ESP32-S3.bin` = 0x31e200 字节。
- 烧录后蓝牙 Modbus 请求应能正常收到从站 3 的应答（需实测确认）。

### 教训

- **波特率必须与从站实际配置一致**，不能为统一性而随意修改。
- 两路独立串口的波特率可以不同：站号 2 = 115200，站号 3 = 9600，这是协议规定，不是配置错误。
- 修改通信参数后，必须用上位机或调试终端实测从站应答，不能只看编译通过。

---

# 2026-10-08 UI 问题与处理记录

## 1. 本次工作的目标

本次工作的主要目标是将 `function/UI/UI_V11` 中的新 UI 接入当前工程的 UI 架构，移除旧 UI 的实际编译依赖，并检查显示缓冲、Flash 映射、刷新率、交互延迟和字体替换方案。

最终完成了以下工作：

- 将 UI_V11 首页转换为当前工程使用的固定槽位、单 screen 架构。
- 图片资源从应用固件中拆出，打包到 `storage` 分区并通过 MMAP 使用。
- 确认显示驱动使用双缓冲，但属于局部行缓冲，不是双全屏缓冲。
- 增加并检查可配置的 1～20 FPS 刷新率逻辑。
- 分析 10 FPS、20 FPS 下的理论响应等待时间。
- 为 `ui.h`、`ui.c` 中的结构体和函数补充注释。
- 导入 Manrope 22 号压缩字体，并解决替换字体后无法显示的问题。
- 再导入六种 Manrope 字重，并增加统一字体别名，方便后续切换。
- 修正 `Tofu` 第一行文字的水平位置及旋钮切换后位置被覆盖的问题。

## 2. 新 UI 接入当前架构

### 2.1 遇到的问题

UI_V11 是由新 UI 工具导出的代码，原始组织方式与工程现有 UI 架构不同。如果直接把导出文件全部加入编译，会同时存在两套对象管理和页面切换逻辑，后续旋钮、业务状态以及图片内存管理不容易统一。

### 2.2 处理方式

没有直接运行 UI_V11 的 screen 代码，而是将其页面内容转换成当前架构的数据配置：

- 整个 UI 只创建一个 LVGL screen。
- 页面中的图片和文字由 `ui_image_config_t`、`ui_label_config_t` 和 `ui_page_config_t` 描述。
- 图片对象和 label 对象在初始化时按固定槽位创建，运行中复用对象。
- 旋钮选中项由 `ui_select_home_item()` 统一更新按钮高亮、中心预览图和标题文字。
- 旧 UI 图片源文件不再逐个加入 `function/UI/CMakeLists.txt`。

这样可以保留现有业务层调用方式，同时使用 UI_V11 的视觉资源和布局。

### 2.3 注释整理

为 `ui.h` 和 `ui.c` 中的结构体、枚举、公开函数及主要内部函数补充了 Doxygen 风格注释，说明参数、返回值、生命周期和页面槽位行为，便于后续继续扩展页面。

## 3. 当前显示缓冲方式

### 3.1 结论

当前是**局部双缓冲**，不是单缓冲，也不是双全屏缓冲。

`components/myLvgl/lv_port_disp.c` 中申请了两个 DMA 内部内存缓冲：

```c
uint32_t rows = 40;
uint32_t buf_pixel_count = MY_DISP_HOR_RES * rows;

lv_color_t *buf1 = heap_caps_malloc(buf_size,
                                    MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
lv_color_t *buf2 = heap_caps_malloc(buf_size,
                                    MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
```

屏幕为 320×480、RGB565 时：

- 单个 40 行缓冲：`320 × 40 × 2 = 25,600` 字节。
- 两个缓冲合计：`51,200` 字节。
- 一个完整屏幕缓冲：`320 × 480 × 2 = 307,200` 字节。
- 两个完整屏幕缓冲需要约 `614,400` 字节，当前并未这样分配。

若 40 行 DMA 缓冲申请失败，代码会回退到两个 10 行静态缓冲，合计 `12,800` 字节。

双局部缓冲允许 LVGL 在一个缓冲通过 DMA 发送时准备另一个缓冲，在内部 RAM 占用、刷新效率和稳定性之间取得平衡。

## 4. MMAP 和 16MB Flash 使用情况

### 4.1 是否实现 MMAP

已经实现。

构建时，`function/UI/tools/generate_ui_assets.py` 会执行以下操作：

1. 收集 UI 图片像素数据。
2. 生成 `build/ui_assets.bin`。
3. 为每张图片生成只包含尺寸、格式和 Flash 偏移的 `lv_img_dsc_t`。
4. 生成运行时 `ui_assets_init()` 和 `ui_assets_deinit()`。

运行时使用：

```c
esp_partition_mmap(partition, 0, UI_ASSET_TOTAL_SIZE,
                   ESP_PARTITION_MMAP_DATA,
                   &mapped_address, &mapping_handle);
```

映射成功后，每个图片描述符的 `data` 指针会指向 MMAP 地址对应的图片偏移。这样图片像素不需要复制到 PSRAM，也不会全部塞进应用程序固件。

初始化时还会校验：

- magic；
- 资源包总大小；
- 图片数量；
- CRC32。

固件与 `ui_assets.bin` 不匹配时，UI 初始化会失败并输出日志，避免使用错误图片数据。

### 4.2 当前图片资源占用

本次构建共打包 17 张 UI 图片：

- `ui_assets.bin`：491,044 字节，约 479.5 KiB。
- CRC32：`0x9A5B09DC`。
- 存放位置：`storage` 分区，起始地址 `0x780000`。

当前 `storage` 虽然在分区表中标记为 LittleFS，但本方案将它作为 UI 原始资源分区使用，因此不能同时将该分区挂载为普通 LittleFS 文件系统。

### 4.3 16MB Flash 分区

| 分区 | 起始地址 | 大小 | 当前用途 |
| --- | ---: | ---: | --- |
| `nvs` | `0x9000` | 24 KiB | NVS 参数 |
| `otadata` | `0xF000` | 8 KiB | OTA 状态 |
| `phy_init` | `0x11000` | 4 KiB | PHY 数据 |
| `ota_0` | `0x20000` | `0x3B0000`，约 3.69 MiB | 当前/候选应用 |
| `ota_1` | `0x3D0000` | `0x3B0000`，约 3.69 MiB | OTA 备用应用 |
| `storage` | `0x780000` | `0x864000`，约 8.39 MiB | UI 图片资源 |
| `User_Data` | `0xFF2000` | 56 KiB | 用户数据 |

最后一次构建结果：

- `EVT_ESP32-S3.bin`：`0x131BA0` 字节。
- 最小应用分区：`0x3B0000` 字节。
- 应用分区剩余：`0x27E460` 字节，约 68%。
- UI 图片仅占 `storage` 分区约 5.6%，后续仍可加入较多图片。

烧录时除了应用固件，还必须烧录：

```text
0x780000 build/ui_assets.bin
```

否则运行时 MMAP 校验会失败，图片不能显示。

## 5. 刷新率和画面响应延迟

### 5.1 当前刷新率配置

刷新率范围定义在 `task/DataCenter/ModbusMap.h`：

```c
#define MODBUS_DISPLAY_FPS_DEFAULT 10U
#define MODBUS_DISPLAY_FPS_MIN      1U
#define MODBUS_DISPLAY_FPS_MAX     20U
```

默认是 10 FPS，最大允许设置为 20 FPS。运行时可调用：

```c
DisplayFrameRateSet(20);
```

也可以通过本地 Holding Register `0x0010` 修改刷新率。

### 5.2 `1000 / FPS` 中的 1000 是什么

`1000` 表示一秒包含 1000 毫秒。因此：

```text
刷新周期（ms） = 1000 ms / 每秒帧数
```

代码位于 `components/myLvgl/myLvgl.c`：

```c
uint32_t frame_period_ms = (1000U + fps - 1U) / fps;
```

这里使用的是整数向上取整：

- 10 FPS：`(1000 + 10 - 1) / 10 = 100 ms`。
- 20 FPS：`(1000 + 20 - 1) / 20 = 50 ms`。

计算结果被设置到 LVGL 显示刷新定时器：

```c
lv_timer_set_period(_lv_disp_get_refr_timer(display), frame_period_ms);
```

### 5.3 10 FPS 时延迟如何估算

旋钮事件可以发生在一个刷新周期中的任意位置，因此仅等待下一次刷新所产生的理论延迟是：

- 最好情况：接近 0 ms；
- 最坏情况：接近 100 ms；
- 均匀随机发生时的平均等待：约 50 ms。

实际可见延迟还要加上：

- 编码器扫描和累计三个计数的时间；
- `lvUIshowTask` 的 5 ms 循环等待；
- LVGL 对象更新和局部渲染时间；
- SPI/DMA 将像素传到屏幕的时间；
- LCD 面板自身响应时间。

因此 `100 ms` 只是 10 FPS 的刷新周期，不等于所有情况下测得的完整旋钮到画面延迟。

### 5.4 20 FPS 的优势和风险

切换到 20 FPS 后，刷新等待变为：

- 最坏约 50 ms；
- 平均约 25 ms。

优势：

- 旋钮选项切换更跟手；
- 动画和高亮变化更平滑；
- 快速连续旋转时视觉停顿更少。

代价：

- LVGL 刷新和 SPI 传输调用频率提高；
- CPU、SPI 带宽和功耗增加；
- 大面积重绘时更容易出现帧时间超过 50 ms 的情况。

图片数量本身不会直接造成卡顿；真正影响速度的是每帧发生变化、需要重绘和传输的像素面积。静态图片即使数量较多，只要没有反复失效重绘，影响通常较小。建议保持局部更新，不要每帧执行全屏 invalidate。

### 5.5 延迟测试支持

显示驱动中已经加入渲染计时：

- `render_start_cb` 记录渲染开始时间；
- LCD DMA 完成回调记录最后一次 flush 完成时间；
- `display_stats_record_frame()` 统计帧数、最小、最大和平均渲染时间。

该统计测量的是 LVGL 开始渲染到最后一次 DMA 完成的帧耗时。若要得到完整的“旋钮物理动作到屏幕显示完成”延迟，还需要在编码器事件产生时记录时间戳，并在对应帧 DMA 完成时计算差值。目前没有为了该测试继续修改交互逻辑。

## 6. 自定义字体导入和显示问题

### 6.1 第一次导入 Manrope Bold 22

原始字体文件：

```text
function/UI/fonts/Manrope-Bold.ttf
```

转换参数：

- 字号：22 px；
- 位深：4 bpp；
- 字符范围：`0x20-0x7E`，即基本 ASCII；
- LVGL C 格式；
- 默认 RLE 压缩。

生成符号：

```c
ui_font_manrope_bold_22
```

### 6.2 替换字体后编译或显示异常

主要问题不是 `LABEL_FONT()` 调用格式，而是压缩字体支持没有在实际 LVGL 配置中打开。

生成字体的描述符包含：

```c
.bitmap_format = 1
```

这表示字体位图为压缩格式。如果 `LV_USE_FONT_COMPRESSED` 为 0，LVGL 无法解码字形，label 对象仍存在，但文字不会显示。

另外工程启用了：

```text
CONFIG_LV_CONF_SKIP=y
```

这意味着当前构建主要采用 `sdkconfig` 生成的 LVGL 配置，不能只修改 `main/lv_conf.h`。

最终处理：

```text
sdkconfig:      CONFIG_LV_USE_FONT_COMPRESSED=y
main/lv_conf.h: #define LV_USE_FONT_COMPRESSED 1
```

两处保持一致，其中 `sdkconfig` 是当前构建真正生效的配置来源。

### 6.3 自定义字体与系统字体切换

自定义字体：

```c
ui_font_manrope_bold_22
```

LVGL 自带 Montserrat 22：

```c
lv_font_montserrat_22
```

两者都可以传入当前的 `LABEL_FONT()`。切换到 Montserrat 22 时，需要确保 `CONFIG_LV_FONT_MONTSERRAT_22=y`。

## 7. 新增六种 Manrope 22 号字体

新增的六个 TTF 文件均已转换为 22 号、4 bpp、ASCII、压缩 LVGL 字体：

| 字体文件 | LVGL 字体符号 |
| --- | --- |
| `Manrope-ExtraBold.ttf` | `ui_font_manrope_extrabold_22` |
| `Manrope-ExtraLight.ttf` | `ui_font_manrope_extralight_22` |
| `Manrope-Light.ttf` | `ui_font_manrope_light_22` |
| `Manrope-Medium.ttf` | `ui_font_manrope_medium_22` |
| `Manrope-Regular.ttf` | `ui_font_manrope_regular_22` |
| `Manrope-SemiBold.ttf` | `ui_font_manrope_semibold_22` |

连同原来的 Bold，共有七种 Manrope 字重可用。

### 7.1 为后续替换做的调整

`function/UI/CMakeLists.txt` 改为自动收集字体目录中的 `.c` 文件：

```cmake
file(GLOB UI_FONT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/fonts/*.c")
```

以后把转换好的字体 `.c` 放入 `fonts` 目录，不需要再次逐条修改 CMake。

`ui.h` 中统一声明全部项目字体，并增加语义别名：

```c
#define UI_FONT_TITLE ui_font_manrope_bold_22
#define UI_FONT_BODY  lv_font_montserrat_14
```

页面只使用 `UI_FONT_TITLE`、`UI_FONT_BODY`，不直接使用具体字体符号。以后更换标题字体只需要改一行，例如：

```c
#define UI_FONT_TITLE ui_font_manrope_medium_22
```

未被别名或其他代码引用的字体对象不会进入最终链接结果，因此保存多种字体源不会让所有字体同时占用最终固件空间。

当前自定义字体只包含基本 ASCII。如果后续需要中文或其他字符，必须在转换时增加相应 Unicode 范围或明确字符列表，否则对应字符仍然不会显示。

## 8. `Tofu` 文字向右调整

标题由两行组成：

```text
Tofu
Studio
```

需求是只让第一行 `Tofu` 向右移动，第二行 `Studio` 保持不动。当前通过在第一行前增加空格实现：

```c
"     Tofu\nStudio"
```

最初只修改了页面初始文字，但初始化或旋钮重新选中 Tofu 时，`home_selections` 中的另一份标题会覆盖它，导致偏移恢复。

解决方法是同步修改两处：

- `home_labels` 中的初始标题；
- `home_selections[UI_HOME_ITEM_TOFU]` 中的运行时标题。

这样开机显示、切换到其他菜单后再切回 Tofu，都会保持相同位置。

如果后续需要像素级精确控制，建议把 `Tofu` 和 `Studio` 拆成两个独立 label，分别设置坐标，避免使用空格宽度进行布局。

## 9. 构建验证

本次字体接入和文字位置调整后均执行了完整 `idf.py build`，结果：

- CMake 配置成功；
- 七个 Manrope 字体源均可编译；
- UI 静态库链接成功；
- ELF 和 BIN 生成成功；
- 分区大小检查通过；
- 最终应用分区剩余 68%。

本次只完成了代码和构建验证，尚未代替用户烧录设备。烧录后应重点确认：

1. `ui_assets.bin` 与应用固件一起烧录。
2. 开机时没有 `ui_assets` MMAP 或 CRC 错误日志。
3. Manrope 压缩字体能够正常显示。
4. 旋钮切换四个入口时高亮、预览图和标题同步。
5. 切回 Tofu 后第一行仍保持右移。
6. 10 FPS 和 20 FPS 下的实际旋钮手感及最大帧耗时。

