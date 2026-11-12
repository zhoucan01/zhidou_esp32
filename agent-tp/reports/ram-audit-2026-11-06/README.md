# 当前工程 RAM 与 UI 并发压力审查

日期：2026-11-06。分支：`ESP32_NEW_UI`；提交：`37d7dd32cdd0caffc3e6e00c04518ef0c4b53d16`。本次阅读 UI 历史问题，重新扫描源码和实际配置，解析现有最终 ELF/MAP，运行真实 LVGL 主机压力测试，并通过 COM9 只读采集运行堆数据。未修改固件、未构建或烧录新固件，未开启配网、蓝牙或工艺；用户明确选择本次先不进入配网。

## 结论

**当前页面集的 UI 私有池压力较低，未发现持续增长；不能据此保证蓝牙、Wi-Fi、UI、工艺和升级全部叠加时绝不会 RAM 不足。主要待验证点是内部 RAM 余量、最大连续块和任务栈水位。**

- 正常 UI 测到的固定池占用峰值为 **34,104 B（33.30 KiB，128 KiB 池的 26.0%）**，不接近容量上限。
- 图片解压缓存峰值为 **415,776 B（406.03 KiB）PSRAM**；最大单笔图片申请 **244,608 B（238.88 KiB）**。即使总空闲较多，最大连续块不足仍可能导致图片打开失败。
- 当前设备两段待机采样，内部 8 位堆空闲均为 **94,111 B（91.91 KiB）**，PSRAM 8 位堆空闲均为 **1,704,064 B（1.625 MiB）**。这不代表无线传输峰值，也不是 largest/min-free 或栈余量。
- UI 的内部 RAM 固定负担主要是 **75 KiB 动态 DMA 双缓冲**，还有常驻 **12.5 KiB 静态备用缓冲**。蓝牙 NimBLE 和普通 FreeRTOS 任务仍使用内部内存，网络开启时需要额外预算。
- UI-13 原来的“NULL 清零”路径已经修复；当前其他分配失败路径并非全部能够降级恢复，仍有 assert/abort 或页面绘制失败的可能。

## 扫描与证据口径

扫描了工程目录中 **740 个非图片/字体源码及头文件**，生成 **529 条分配/释放/任务或协议栈创建候选位置**；结合构建记录中的 **734 个工程内编译单元**、当前 `sdkconfig`、SDK v5.3.1 实现及链接结果筛选活动路径。候选包含库实现、声明和条件编译分支，不能把每一条都当作运行中的分配，更不能仅靠搜索认定泄漏。

- [源码和配置清单](inventory.json)、[分配位置索引](allocation-sites.csv)
- [SDK 链接大小结果](link-size.json)、[链接组件明细](link-archives.json)
- [汇总指标](summary.json)、[UI 压力原始结果](ui-audit.log)、[逐场景数据](ui-scenes.csv)
- [UI 历史问题](../../../docs/UI历史问题.md)：重点复核 UI-06/07/08/12/13。

现有 MAP/ELF 修改时间为 2026-11-06 19:20:19；本次应用 SHA256 为 `6b03497ebc30ea7244e868d6fd2f57ca8937655546347c721e4a6523ab3c6d9b`。进入审查时已有的 UI 历史问题文档修改保留。

## 三类内存必须分别看

| 区域 | 当前用途 | 无线/工艺的影响 |
| --- | --- | --- |
| PSRAM 内的 LVGL 固定 TLSF 池 | 对象、样式、文字、字体临时缓冲、旋转图层等；固定 131,072 B | Wi-Fi/BLE 不直接从这个池申请；任务增多不会直接吃掉池内空闲 |
| 系统 PSRAM 堆 | LZ4 解压像素、UI 8 KiB 栈、部分 Wi-Fi/LwIP 分配等 | 图片缓存与支持外部内存的网络分配共用，需看 free 和 largest |
| 内部 RAM 堆 | LCD DMA 双缓冲、普通任务栈/TCB、NimBLE、驱动与队列等 | 是高负载并发时的主要竞争区域；PSRAM 空闲不能保证这些申请成功 |

当前 `CONFIG_LV_CONF_SKIP=y`，库的实际配置来自 `sdkconfig`。`LV_MEM_CUSTOM=0` 的池通过 `EXT_RAM_BSS_ATTR` 放到 PSRAM；不是从内部 RAM 申请 128 KiB。[池实现](../../../components/lvgl/src/misc/lv_mem.c)、[实际配置](../../../sdkconfig)。

链接结果：内部 data 24,664 B、bss 40,360 B，合计 **65,024 B（63.5 KiB）**；IRAM vectors/text 合计 **113,682 B**；外部 BSS **153,512 B（149.91 KiB）**，其中 LVGL 池 131,072 B。不要把 `.dram0.dummy`、`.ext_ram.dummy` 的映射避让范围再当成真实 RAM 数据相加；静态 BSS 也不能再从运行时 free 中重复扣除。

## UI 正常运行压力与泄漏检查

在现有真实 LVGL/UI 主机回归基础上，额外遍历 **30 个逻辑页面、各图片焦点状态、6 轮，共 924 个渲染场景**。共发生 5,730 次图片解压分配；正常阶段无图层或图片分配失败，内存完整性检查通过。

| 指标 | 本次结果 | 说明 |
| --- | ---: | --- |
| LVGL 池容量 | 131,072 B | 来自实际固件 Kconfig |
| 正常分配/重分配后的池占用采样峰值 | 34,104 B | 最大场景为植物奶配方选择页 |
| 峰值对应容量余量 | 96,968 B | 主机观测值，不是设备实测 largest |
| 图片缓存/打开过程的峰值 | 415,776 B | 最大场景为深度清洗准备页，包含缓存替换过程 |
| 最大单笔解压像素分配 | 244,608 B | 实机 PSRAM largest 至少需能容纳此类申请及相应开销 |
| 每轮回到首页并清空图片缓存后的池占用 | 18,272 B，6 轮一致 | largest 112,592 B；解压像素剩余 0 B |
| 复用对象数量 | 25，保持不变 | 11 个图片槽及 14 个文字槽，未随切页新增 screen |

128 KiB 固定池、已测图片峰值和 UI 8 KiB 栈合计约 **542 KiB PSRAM**，其中池是提前保留的容量，不能再把池内 34,104 B 叠加一次。这不是全工程 PSRAM 占用，也不是目标硬件上同时刻的精确峰值。

**此前约 116 KiB 的测试峰值不能用来认定正常 UI 接近满池。** 原回归先故意申请大块占满 TLSF 池，再制造 100 次失败；`lv_mem_monitor().max_used` 保留了该失败注入的历史。另一个口径限制是当前 LVGL 的 `lv_mem_realloc()` 不更新同一套 `max_used` 记账。本次排除失败注入阶段，在 allocator 的分配/重分配返回处读取 `total_size - free_size`。

主机使用 Windows 64 位 ABI，ESP32-S3 为 32 位；对象大小、分配器布局及缓存碎片可能不同。本次保留实际色深、字体、页面和 Kconfig，测量结果证明这些场景下的容量关系及回收行为，不能替代设备端长期运行、所有参数组合和碎片测试。像素通过主机 malloc 承载并统计字节数，未模拟其他任务竞争 PSRAM。

实现与复现：[压力用例](../../tests/firmware/test_ui_ram_pressure.c)、[运行入口](../../tools/memory/run_ui_ram_audit.py)。只在 `build/ccache/ui-ram-audit/` 生成分配器观测副本，没有改动工程内 `lv_mem.c`。

## 当前实机数据及并发预算

COM9 只读发送站1 FC04，读取 `0x0012～0x001B`：工艺、动作、PSRAM/内部堆 free 和显示统计；未写保持寄存器，未发送制作、加热、电机或网络切换命令。

| 采样 | 有效回复 | 内部 free | PSRAM free | 工艺号 |
| --- | ---: | ---: | ---: | ---: |
| [35 秒](../../data/captures/2026-11-06/212315-ram-audit.csv) | 35 | 94,111 B，最小/最大一致 | 1,704,064 B，最小/最大一致 | 0 |
| [90 秒](../../data/captures/2026-11-06/213046-ram-audit.csv) | 89 | 94,111 B，最小/最大一致 | 1,704,064 B，最小/最大一致 | 0 |

这里只能确定当时待机、所有已创建后台任务和当时页面状态的余量稳定。**本次按用户要求不进入配网；没有完成 BLE 连接＋SoftAP 网页＋STA 传输＋UI 切页的实机组合，也没有启动真实工艺或 OTA。**

内部 RAM 的主要预算：

| 项目 | 可确认的请求量 | 限制 |
| --- | ---: | --- |
| LCD 60 行双 DMA 缓冲 | 76,800 B（75 KiB） | 内部专用；启动分配成功后不会因后续无线压力自动退回 40 行 |
| LCD 静态 10 行备用双缓冲 | 12,800 B（12.5 KiB） | 已包含在内部 bss，正常分配成功时仍保留 |
| main 创建的常驻业务内部任务栈 | 45,568 B（44.5 KiB） | 不含 TCB、系统任务和可选关闭的调试任务 |
| usrdug 与蜂鸣器任务栈 | 3,072 + 2,048 B | 分别在相关初始化入口创建；不含队列与 TCB |
| 单个实际工艺任务 | 4,096 B | 六种任务均为此值，运行时才分配 |
| Collect Pulp 收尾任务 | 4,096 B | 点击后分配，结束后删除；创建失败可留在完成页重试 |
| 配网 HTTP server 栈 | 6,144 B | SoftAP 启动后创建，不含 socket、请求及 JSON 临时内存 |
| NimBLE host 栈 | 4,096 B | BLE 初始化后创建；另有协议池和控制器内存 |

这些名义请求不能从上述 94,111 B 全部再扣一遍：已经初始化的 UI DMA、后台任务和 Wi-Fi 驱动占用已体现在当前 free 中。系统任务、驱动结构、队列、协议状态、TCB/线程本地存储和对齐开销不能仅凭源码栈数字精确配平。

## 无线、网络与其他任务的具体风险

**1. 蓝牙初始化对内部 RAM 有新增需求。** 当前 `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL=y`，SDK 的 `bt_osi_mem_malloc/calloc` 明确使用 `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`。连接数上限为 1；两组 MSYS 配置净数据块容量为 `12×256 + 12×320 = 6,912 B`，这还不包括块头、对齐、连接状态、NPL、host 栈或控制器，不能把它当成全部 BLE 内存。`BluetoothInit()` 的 `nimble_port_init()` 等失败仍使用 `ESP_ERROR_CHECK`，不足时可能导致 abort。[蓝牙入口](../../../components/Bluetooch/Bluetooch.c)。

蓝牙并非当前默认一直启动：主入口只准备名称，配网任务在开机按键触发后才初始化 BLE；未连接客户端时还有 120 秒关闭逻辑。因此普通待机数据不能当作蓝牙已连接的数据。[配网任务](../../../task/WiFiPortal/WiFiPortal.c)。

**2. Wi-Fi 只有部分分配可用 PSRAM。** 已启用 `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`；静态 RX=6、静态 TX=8、动态 RX 上限=16、TX cache 上限=16。按官方每个静态 DMA RX/TX 缓冲约 1,600 B 的说明，RX/TX 净容量约 **22,400 B**，但这不是 Wi-Fi 全部内存，也不是 AP 开启后的新增量。[ESP-IDF 5.3.1 Wi-Fi 缓冲说明](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-guides/wifi.html#wi-fi-buffer-configure)

SoftAP 允许 4 个客户端，配网页会创建 HTTP 服务、socket、JSON 对象与序列化字符串；扫描结果上限 16 条，临时记录显式放 PSRAM 并释放。大小有限不等于传输峰值已测量，连续 JSON/网络分配还需观察内部 largest。

**3. 内部小块分配与连续块仍需监控。** 当前 malloc 的内部优先阈值为 16,384 B，许多 JSON、协议对象先竞争内部堆；保留内部额度为 32,768 B，这不是额外可用内存或绝不会不足的保证。内部与 DMA 能力集合重叠，不能相加成总 free。[ESP-IDF 外部 RAM 分配规则](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-guides/external-ram.html#provide-external-ram-via-malloc)

**4. 不同工艺可以被远程入口叠加启动。** `wrapper_xTaskCreate()` 只检查对应任务自己的 handle，没有统一拒绝其他正在运行的工艺；`FuncEvent()` 也没有全局互斥门禁。正常 UI 的 idle 检查减少了此情况，但不能替代串口/蓝牙入口的调度约束。六种工艺同时存在时仅名义栈就可达到 **24 KiB**，而 `CurrentFunc` 只记录最后一个。这是代码可确认的预算缺口；本次没有为了测试而启动这些实际执行器任务。[调度源码](../../../task/FunctionSelection/FunctionSelection.c)。

Collect Pulp 的输入锁仅阻止 UI 重复操作，没有将收尾任务注册为工艺调度器中的运行任务；收尾期间远程启动其他工艺仍需纳入互斥设计。

**5. HTTPS 是条件性额外压力，不是当前已测常驻开销。** 配置将 mbedTLS 内存强制放内部 RAM，TLS 输入/输出内容上限为 16,384/4,096 B，仅这两项合计约 20 KiB，另有握手、证书和连接对象。当前活动 OTAService 查询入口使用 HTTP，不能把完整 TLS 开销算成每次普通待机都已存在；下载地址若使用 HTTPS 或启用其他 HTTPS 客户端，需单独测峰值。活动下载按 4 KiB 接收后写分区，不把整包固件放 RAM，连接失败/结束已有清理路径。[升级下载](../../../task/OTAService/OTANetUpgrade.c)、[版本查询](../../../task/OTAService/OTAVerManager.c)。

**6. 任务栈是否足够尚不能盖章。** PowerRecovery 仅 1,536 B，通信任务多数 4 KiB；heap free 充足也不能证明某个调用链不会耗尽自己的栈。本次没有各任务最坏工况的 high-water mark。`MAIN_RUNTIME_LOG_ENABLE=0`、`SYSTEM_MEMORY_LOG_ENABLE=0`，当前只读寄存器也没有提供 largest、min-free 或任务栈水位。

## 与 UI 历史问题相比，还会不会出现类似现象

| 历史类别 | 当前情况 | 剩余边界 |
| --- | --- | --- |
| UI-13 空指针清零 | 该旋转图层分支已先判空，当前页面集固定池余量充足 | 只修复该路径，不等于全部分配失败都能恢复 |
| UI-06/07 图片和缓存 | 解压缓冲显式 PSRAM，换页/换图失效缓存，6 轮未见持续增长 | 实机长期碎片未测；单笔最大约 239 KiB，需要足够 largest；不足会返回图片打开失败，可能缺图 |
| UI-08 显示缓冲 | 有 60→40→静态10行的启动分配回退 | 60 行成功后已占用 75 KiB，后续 BLE/AP 开启不会触发重新缩小 |
| UI-12 旋钮日志队列 | 原可选日志队列路径已移除，未发现再次启用 | 蜂鸣器、Wi-Fi/BLE 等其他创建路径仍有 assert/ESP_ERROR_CHECK，不能由此推定全工程耐受 OOM |

LVGL 的图层上下文、标签及临时缓冲分配仍有 `LV_ASSERT_MALLOC`/`LV_ASSERT_MSG`；当前默认断言处理为 `while(1)`，日志默认关闭，严重 OOM 可能表现为 UI 卡住或看门狗现象。[图层上下文](../../../components/lvgl/src/draw/lv_draw_layer.c)、[内存临时缓冲](../../../components/lvgl/src/misc/lv_mem.c)、[断言配置](../../../components/lvgl/src/lv_conf_internal.h)。

还找到同类潜伏缺陷：旧 `components/OTA/OTA.c` 的 `_http_event_handler()` 首次 malloc 后未判空即 memcpy，且响应累计增长没有明确上限。但该旧检查入口/回调在当前 MAP 的实际分配输出中未出现，当前配网页使用 OTAService；因此列为**启用旧接口前需修复**，不把它误判为当前每次运行都会触发的问题。

## 建议优先级

1. **先补并发实测口径。** 后续方便配网时，在 BLE 初始化/连接、AP＋网页请求、Wi-Fi 传输、单工艺、UI 最大图片页和升级阶段采集 internal/PSRAM 的 free、largest、min-free 及各任务栈水位；LVGL 池统计放在 UI 线程。当前数据足以判断 UI 页面集压力较低，不足以承诺无线全负载不会 OOM。
2. **若要增加内部余量，优先评估 DMA 双缓冲 60 行改 40 行。** 可明确节省 **25 KiB 内部 RAM**，需复测刷新延迟和 FPS。当前 128 KiB LVGL 池在 PSRAM，缩减它不会释放内部 DMA RAM，不建议为此把池降回 32 KiB。
3. **统一工艺和取料收尾的执行器/任务互斥。** 防止远程命令绕过 UI 的单工艺限制，使内存及动作预算真正有上界。
4. **明确分配失败的恢复行为。** 可选无线/页面资源失败应报告并保留基本控制能力；保留判空，优先处理活动入口，再修复当前未链接的旧 OTA 回调。不要通过关闭断言掩盖分配失败。
5. **按实际调用链评估迁移。** 可研究 NimBLE 外部分配选项或合适任务的外部栈，但不能只改开关后认定所有控制器/DMA内存都迁走。OTA 8 KiB 栈保持内部；涉及 NVS/Flash 的栈迁移需检查缓存关闭限制。[SDK 限制](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-guides/external-ram.html#restrictions)

本次只分析、测试和归档；没有替用户实施上述参数调整，也没有修改 `function/` 的任何固件文件。

## 复现入口

```powershell
python -B agent-tp/tools/memory/scan_ram_sources.py
python -B agent-tp/tools/memory/run_ui_ram_audit.py --cc <与现有主机对象一致的编译器>
python -B agent-tp/tools/memory/sample_heap_readonly.py --port COM9 --seconds 35
```

UI 审查复用 `build/ccache/plantmilk-host/` 的对象，运行前应先用相同编译器运行现有 `agent-tp/tests/firmware/test_plantmilk_ui.py`。本机编译器为位于 `build/` 的 Zig C 包装，缓存均在 `build/ccache/`。报告与脚本均归档 `agent-tp/`。
