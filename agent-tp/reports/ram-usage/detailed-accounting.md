# 内部 RAM 详细账目

## 证据与口径

- 链接证据：build/EVT_ESP32-S3.map，修改时间 2026-10-30 18:57，包含 OTA 合并后的布局。
- 旧运行日志：internal free=102455 B、allocated=243356 B（237.652 KiB）。
- 新运行日志：19:10，internal free=80951 B、allocated=255396 B（249.410 KiB）。
- 当前源码还在新日志之后把 DataCenter 栈从 3072 B 增至 4096 B、PowerRecovery 从 1024 B 增至 1536 B。不能把这一版本的任务栈直接套进旧日志。
- 174.294 KiB 是 512 KiB 减去旧日志 free 和 allocated 得到的算术差额，不是某个接口报告的系统保留量。它包含非堆占用，也可能包含堆管理、对齐等统计口径差异。不能将差额全部归因于系统保留或内存泄漏。
- 静态数据与运行堆是两套账；DMA 与 internal8 是重叠能力集合；PSRAM 静态 BSS 不计入 PSRAM 堆 allocated。

## 内部代码与静态数据：可由链接定位

| 项目 | B | KiB |
| --- | ---: | ---: |
| IRAM vectors 输出段 | 1027 | 1.003 |
| IRAM text 输出段 | 113023 | 110.374 |
| IRAM 尾部对齐间隔 | 126 | 0.123 |
| 内部 IRAM 地址范围合计 | 114176 | 111.500 |
| 内部 data 输出段 | 24616 | 24.039 |
| 内部 bss 输出段 | 31200 | 30.469 |
| 内部 data+bss | 55816 | 54.508 |
| 内部代码范围与 data+bss 合计 | 169992 | 166.008 |

这是 18:57 的 map，不与旧运行日志逐字节配平。也不要再额外累计 .dram0.dummy：它用于 IRAM/DRAM 地址别名避让，与已计的物理内部内存重叠。

IRAM text 的主要来源（按实际地址统计，不含下面单列的 data/bss）：

| 组件 | B | KiB |
| --- | ---: | ---: |
| FreeRTOS | 18708 | 18.270 |
| 蓝牙控制器 btdm_app | 14292 | 13.957 |
| Wi-Fi pp | 11813 | 11.536 |
| SPI Flash 驱动 | 11345 | 11.079 |
| HAL | 8776 | 8.570 |
| 内存堆管理 heap | 7814 | 7.631 |
| 芯片硬件支持 esp_hw_support | 7660 | 7.480 |
| 射频 phy | 5073 | 4.954 |
| 环形缓冲实现 esp_ringbuf | 4686 | 4.576 |
| 系统 esp_system | 4509 | 4.403 |
| Wi-Fi net80211 | 3852 | 3.762 |

以上是组件代码，不是缓冲大小；例如环形缓冲实现的 4686 B 不能当 UART 动态数据缓冲再次相减。这些模块包含中断、缓存关闭期间所需路径，不应直接整体搬 PSRAM。

内部静态大对象：

| 对象 | B | 所属/用途 |
| --- | ---: | --- |
| fallback_buf1 | 6400 | LVGL 10 行备用 DMA 缓冲 |
| fallback_buf2 | 6400 | LVGL 10 行备用 DMA 缓冲 |
| s_info_buf | 4096 | OTAInfo 升级信息扇区读改写缓冲 |
| s_upg_rx_buf | 4096 | OTANetUpgrade 下载接收缓冲 |
| FreeRTOS port.c 的 .dram1.3 | 3072 | 与两核各 1536 B ISR 栈配置一致，已计入静态 data，不在任务堆栈表重复累计 |
| Xtensa 汇编 .data | 1024 | 中断相关数据 |
| TxRxCxt | 960 | Wi-Fi 内部状态 |
| command_queue | 788 | DataCenter 静态命令队列 |
| phy_param | 712 | 射频参数 |
| override_src | 576 | UI 状态 |
| gateway_transaction | 540 | DataCenter 透传事务 |
| pxReadyTasksLists | 500 | FreeRTOS 就绪列表 |
| s_upg | 420 | OTA 下载状态 |
| s_ver_mgr | 384 | OTA 版本管理状态 |

内部 bss 按组件合计：myLvgl 12996 B、OTAService 9001 B、LVGL 1119 B、FreeRTOS 1069 B、DataCenter 718 B，其余见 map-breakdown.md。注意已迁出的 LVGL 32 KiB 池和温度历史 3816 B 不在内部 bss 中。

外部静态 BSS 共 55192 B（53.898 KiB），其中 LVGL 池 32768 B、温度历史 3816 B，其余主要为 Wi-Fi、BLE、LwIP 静态数据。这些已经使用 PSRAM。

## 动态堆：目前能定位到什么程度

下表针对用户 19:10 的任务列表，栈大小为源码/配置请求值，不包含 TCB、对齐、分配器或每任务 libc/TLS 数据。

| 业务任务 | 请求栈 B | 所在位置 |
| --- | ---: | --- |
| MachineControl | 4096 | 内部 |
| Function | 4096 | 内部 |
| DebugConsole | 4096 | 内部 |
| OTAServiceTask | 8192 | 内部 |
| PowerRecovery | 1024 | 内部；新源码已改 1536 |
| DataCenterTask | 3072 | 内部；新源码已改 4096 |
| WiFiConnect | 4096 | 内部 |
| FaultDealTask | 3072 | 内部 |
| usrdug | 3072 | 内部 |
| 编码器 test | 4096 | 内部 |
| buzzer_task | 2048 | 内部 |
| 业务内部栈合计 | 45056（44 KiB） | 新源码合计 46592（45.5 KiB） |
| uiTask | 8192 | PSRAM，不计入上行 |

| 系统任务 | 请求栈 B | 配置依据 |
| --- | ---: | --- |
| IDLE0 + IDLE1 | 3072 | 1536 × 2 |
| ipc0 + ipc1 | 2560 | 1280 × 2 |
| sys_evt | 2304 | ESP_SYSTEM_EVENT_TASK_STACK_SIZE |
| esp_timer | 3584 | ESP_TIMER_TASK_STACK_SIZE |
| Tmr Svc | 2048 | FREERTOS_TIMER_TASK_STACK_DEPTH |
| tiT / TCPIP | 3072 | LWIP_TCPIP_TASK_STACK_SIZE |
| nimble_host | 4096 | BT_NIMBLE_HOST_TASK_STACK_SIZE |
| 已知系统内部栈合计 | 20736（20.25 KiB） | 不含 wifi、btController |

wifi 与 btController 的栈由 SDK/预编译控制器内部创建，目前没有本地可验证的准确分配值，不从栈基址间距反推。main 启动任务返回后也不在此运行时常驻列表中，不能把它的 3584 B 再加进来。

其他明确请求：

- 显示正常双缓冲：320 × 40 × 2 × 2 = 51200 B（50 KiB），显式 INTERNAL + DMA。这里不含前面静态表中的备用缓冲。
- UART1/2：每路 RX 2048 B + TX 1024 B，两路总请求 6144 B；DebugConsole UART0 RX 1024 B，共 7168 B（7 KiB）。实际环形缓冲还包含驱动头部、对齐、信号量等。

由此，19:10 日志中 249.410 KiB 内部 allocated 能先明确出：

| 项目 | KiB |
| --- | ---: |
| 已知业务与系统任务请求栈 | 64.25 |
| 正常显示缓冲请求 | 50.00 |
| UART RX/TX 缓冲请求 | 7.00 |
| 上述名义请求小计 | 121.25 |
| 尚未按分配块归属的余额 | 128.160 |

余额不是一个“未知大数组”，包括尚未计入的 wifi/btController 栈、所有任务 TCB/libc/TLS、Wi-Fi 数据/管理帧缓冲、蓝牙控制器及 NimBLE 动态池、LCD I80/GDMA 描述符和事务队列、UART 驱动附加结构、网络接口/事件循环、NVS、队列/信号量/事件组、网络连接及分配开销等。目前没有逐块运行时分配证据，不能把这些项目虚构成精确数字。

Wi-Fi 配置为静态 RX 6 个、静态 TX 8 个、动态 RX 上限 16 个、TX 缓存上限 16 个。这里“静态 RX/TX”指驱动运行期间常驻分配，不是链接 bss；计数上限也不表示此时已经全部分配。部分 Wi-Fi/LwIP 分配可进入 PSRAM，不能把全部配置容量都算进内部堆。

用户最初 237.652 KiB 日志尚未出现 OTAServiceTask/WiFiConnect，PowerRecovery 为 3072 B。按同样方法，旧日志可确认的名义请求约 111.25 KiB，剩余约 126.402 KiB 尚未逐块归属。不能拿当前含 OTA 的任务总量反填旧日志。

## 如何进一步归属动态余额

现有启动打印已覆盖 NVS、BLE、UART、Wi-Fi、蜂鸣器、LCD、显示缓冲、UI 和各业务任务创建。需收集同一次固件从 boot 开始的这些日志，再比较内部/PSRAM 增量。阶段差值仍受其他核并发活动影响，但可以先确定余额主要在哪个初始化阶段产生。

如果还需每个存活分配块的调用点，需要单独构建启用 heap tracing 的诊断固件并覆盖启动/联网/OTA 过程；当前固件只有汇总和任务快照，不能从已有几行运行时日志还原历史调用栈。本次不自动加入大量追踪缓冲或调整生产固件。

## 复现与校验

运行 `perl agent-tp/tools/memory/analyze_map.pl > agent-tp/reports/ram-usage/map-breakdown.md` 生成完整组件分解。脚本对输入条目的地址区间去重，避免链接器合并字符串造成累计大于段大小；每段组件累计都校验等于输出段大小。已执行 Perl 语法检查和实际解析，输出无警告。未修改固件代码、未编译或烧录。

## 继续细分：新增块级诊断

本节为后续修改，替代上一节“未修改固件”的状态。当前仍无新设备日志，因此没有把剩余 128.16 KiB 虚构成完整分配账目。

新增运行时诊断基于已核对的 ESP-IDF v5.3.1 [heap_caps_walk 接口](https://github.com/espressif/esp-idf/blob/v5.3.1/components/heap/include/esp_heap_caps.h)。不启用分配钩子或全程 heap tracing，不改变 Wi-Fi/BLE 参数、任务栈、显示缓冲或分配策略。

### 启动细分日志

- `RAM-BLE`：NimBLE 初始化前、控制器与 host 初始化后、GATT 初始化后；main 原有 BLE 结束采样还包含 host 任务启动。
- `RAM-WIFI`：开始、读取 NVS 配置与事件组后、TCP/IP/netif 初始化后、默认事件循环后、STA 网卡后、Wi-Fi 驱动初始化后、STA 配置后、射频启动后（仅有配置且此路径确实执行时）。
- 显示初始化输出两个实际 DMA 缓冲指针和每个请求大小；如降级则标明静态备用缓冲。

这些输出分别打印内部与外部空闲字节，前后相减可确定阶段净占用。驱动初始化可能还伴随其他核活动，不能解释为精确的某函数独占分配量。固定路由器任务稍后启动射频时仍会发生新分配。

### 约开机 30 秒的 RAM-DETAIL

在已有任务快照上遍历内部 8 位堆，按堆块地址范围匹配任务栈基址和 TCB 句柄，打印：

| 输出 | 含义 |
| --- | --- |
| task=... matched_stack_block | 该任务栈基址落入的已用内部堆块大小，可补全 wifi、btController 等栈大小；包含块对齐等，不等同可用栈字节 |
| matched_tcb_block | 任务句柄落入的已用内部堆块大小 |
| used / stacks / tcbs / other | 遍历时已用块总量及三类拆分，同一块只归入一类 |
| other size_bin | 除已匹配栈/TCB 以外，各大小区间的块数、总字节；最后一个区间为 16385 B 及以上 |
| other largest[0..11] | 最大 12 个其余已用块的地址与实际大小 |

重要：这里的 other **不等于之前 128.16 KiB 的余额**，因为它仍包含显示双缓冲和 UART 缓冲。先用启动时显示指针对照最大块列表，避免再次累计这 50 KiB。块大小不一定等于请求值。

地址匹配只是采样关联，不是分配调用者：任务列表与各堆遍历不是全系统原子快照，期间任务或内存可能变化；0 表示外部栈、静态对象、采样未匹配等可能性，不代表任务没有栈。TCB/栈之外每任务的独立 TLS/libc 分配仍在 other，单靠地址不能可靠识别调用模块。当前未启用 heap task tracking。

每约五分钟重复一次块级诊断，常规 RAM 和任务水位仍每约 30 秒。临时记录工作区在 PSRAM 分配、打印后释放；遍历回调只做地址/计数运算，不日志、不申请内存、不读取已捕获指针指向的数据。遍历仍增加诊断运行开销，应实测通信延迟。

### 验证与下一步输入

新主机测试 `agent-tp/tests/firmware/test_ram_heap_walk.c` 使用从生产源文件提取的原始分类回调，验证空闲/零长块排除、任务地址范围匹配、PSRAM 栈不误计、直方图区间、前 12 块排序及总账闭合，使用 gcc 严格警告编译并执行通过。编译产物与提取片段均在 build/。

提取与验证命令：

```bash
perl -0777 -ne 'if (/(enum \{ MAX_DIAGNOSTIC_TASKS.*?)(?=static void SystemHeapDetailsLog)/s) { print $1 } else { die "callback not found\n" }' task/SystemMoniter/SystemMoniter.c > build/ram_heap_walk.inc
gcc -std=c11 -Wall -Wextra -Werror -Ibuild agent-tp/tests/firmware/test_ram_heap_walk.c -o build/test_ram_heap_walk
build/test_ram_heap_walk
```

编辑器诊断和改动差异格式检查通过。当前 shell 仍无法解析 nv_build，故未确认 ESP-IDF 编译、链接和上板遍历成功。下一次收集从 boot 开始的 RAM-BLE、RAM-WIFI、显示缓冲地址及首轮 RAM-DETAIL，可继续把动态余额按具体块和初始化阶段拆分。

### 构建结果更新

用户提供系统命令位置后，已将 `/home/wsl/.local/bin` 和 `/home/wsl/workspace/tools` 加入构建终端 PATH，并在正常权限审批后进入系统环境运行 nv_build。沙箱内此前显示路径不存在不代表实际系统未安装。

首次真实编译定位到 RAM-DETAIL 直方图范围上界的 `%u` 格式错误：UINT32_MAX 在目标工具链为 unsigned long，条件表达式使结果提升为该类型。已将整个条件表达式显式转换为 unsigned，保持打印参数与格式一致。

再次运行 nv_build 成功，固件完成编译、链接、镜像生成与分区容量检查。固件大小 0x3521f0 B，应用槽 0x7a3000 B，剩余 0x450e10 B（57%）。生成镜像 build/EVT_ESP32-S3.bin，便携包 build/firmware-0m5q1g95。编译结果替代上文“未确认固件编译”的状态；未烧录，块级诊断仍需上板日志确认。

## 19:35 块级日志分析与保留子堆统计修正

实测总账闭合：256948 = 79616 + 8352 + 168980 B。

| 实测分类 | B | KiB |
| --- | ---: | ---: |
| 任务栈对应的内部堆块 | 79616 | 77.750 |
| 24 个任务的 TCB，每个 348 B | 8352 | 8.156 |
| 其他 302 个已用块 | 168980 | 165.020 |
| 原始所有堆累计 allocated | 256948 | 250.926 |

新增明确项：wifi 栈实际块 6656 B，btController 为 4096 B；tiT、sys_evt、esp_timer 实际块分别为 3584、2816、4096 B，均比之前配置名义值多 512 B，应以当前实际块数据更新账目。uiTask 内部栈匹配为 0，TCB 为 348 B，与 UI 栈位于 PSRAM、TCB 仍在内部一致。

### 最大 32 KiB 块不是可以直接认定的业务缓存

配置 CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768。已读取本机 ESP-IDF v5.3.1 的 components/esp_psram/esp_psram.c 中 esp_psram_extram_reserve_dma_pool：该函数先通过 heap_caps_malloc 从内部 DMA 堆申请一块，再通过 heap_caps_add_region_with_caps 将相同地址空间注册为 INTERNAL/DMA/8BIT 子堆。

因此同时遍历父堆与子堆会累计父堆整个 backing block，以及子堆内的实际分配。子堆空闲也会计入 free；不能把原始 allocated 与 free 当作无重叠物理空间直接相加。这也使此前“512 KiB 减去 free+allocated”的非堆差额口径不准确：必须额外处理父子堆重叠、管理元数据和不同版本差异，不能再把该差额当作精确非堆占用。

用户日志最大的 32768 B 块位于 0x3fcb2dc8，与启动保留池的大小和时序高度吻合。不过旧日志没有各堆边界，尚需新增 region 输出对该指针进行最终地址确认；以下按此块为 backing block 作条件去重。

| 去重后的分解 | B | KiB |
| --- | ---: | ---: |
| 任务栈 | 79616 | 77.750 |
| TCB | 8352 | 8.156 |
| 两个 25600 B 块，与正常显示缓冲大小和初始化顺序一致 | 51200 | 50.000 |
| 其余块 | 85012 | 83.020 |
| 去重后已分配合计 | 224180 | 218.926 |
| 父堆 backing block 重复量 | 32768 | 32.000 |

显示缓冲的精确身份还应以启动打印的 buf1/buf2 指针对照，不能仅从块大小作所有权证明。其余 83.020 KiB 中包含之前已确认名义请求 7 KiB UART 缓冲；扣除名义 UART 请求后剩余约 76.020 KiB，实际 UART 块和附加结构仍需核对。

其余最大块依次为：9356 B @0x3fcbd8d0、3840 B @0x3fcc424c、3600 B @0x3fcc57b8、3072 B @0x3fcc3648、2528 B @0x3fcbbdcc、2140 B @0x3fced2c4、2088 B @0x3fcbd0a4、1868 B @0x3fcbfd60、1700 B @0x3fcccb7c。地址与大小本身无法证明调用模块，不将其直接标成 Wi-Fi/BLE。

### 诊断修正

新增两遍 heap_caps_walk：第一遍记录内部堆区间，第二遍排除包含其他已注册内部子堆完整区间的父堆 backing block。输出 heap region、raw_used、excluded_nested_heap_backing；used/stacks/tcbs/other 统计改为去重口径。最多记录 32 个区间，超限显式警告。仍不构成全系统原子快照，也未增加分配调用栈追踪。

主机测试增加父堆已用 32768 B 内注册子堆、子堆分配 2048 B 的案例，验证最终只累计 2048 B、backing 单列为 32768 B；测试通过。

子堆去重修正已通过 nv_build 完整编译、链接和分区容量检查；镜像 0x352410 B，便携包 build/firmware-saf0wshd。未烧录，地址确认仍需下一轮 RAM-DETAIL。

## 19:43 实测确认

本轮输出 excluded_nested_heap_backing=32768，heap region[0] 为 0x3fcb2dc8..0x3fcbadc7，与上一轮 32768 B 大块起始指针完全一致，确认该块是内部保留子堆的父堆 backing，而非业务缓冲。region[1] 结束地址中混入字符 H，不能直接作为有效十六进制地址使用，不影响 region[0] 与去重总数的证据。

最新实测：256924 - 32768 = 224156 B；224156 = 79616 + 8352 + 136188 B。普通 RAM 汇总的 used 仍是未去重口径，只有 RAM-DETAIL 的 used 已去重。free=79415 B（77.554 KiB）没有因改打印而增加；去重是纠正解释，没有释放内存。

| 项目 | B | KiB |
| --- | ---: | ---: |
| 内部任务栈块 | 79616 | 77.750 |
| TCB | 8352 | 8.156 |
| 两块各 25600 B 的显示缓冲候选 | 51200 | 50.000 |
| 其余块 | 84988 | 82.996 |
| 去重已分配块合计 | 224156 | 218.902 |

排除栈/TCB 及两块显示缓冲后的大小分布：

| 块大小 | 块数 | 总 B | KiB |
| --- | ---: | ---: | ---: |
| 1..1024 B 合计 | 271 | 26768 | 26.141 |
| 1025..2048 B | 20 | 31596 | 30.855 |
| 2049..4096 B | 6 | 17268 | 16.863 |
| 8193..16384 B | 1 | 9356 | 9.137 |
| 合计 | 298 | 84988 | 82.996 |

原始其他块 300 个中，两块 25600 B 对应显示缓冲大小，余下 298 块。没有其他单块超过 16 KiB 的未解释业务分配。约 83 KiB 主要由很多中小块构成，不是一个隐藏大数组；最大尚未归属的单块是 9356 B。其具体调用来源不能仅凭地址和大小判定。

后续如需准确归到 Wi-Fi、蓝牙、UART 等模块，应采集 RAM-BLE/RAM-WIFI 开机阶段日志，或使用按阶段启停的 heap tracing 获取存活分配调用栈。此轮未修改固件。

## 诊断开关与打印周期

内存诊断统一由现有 MachineControl（SystemMonitorTask）执行，不新增任务。`task/SystemMoniter/SystemMoniter.h` 中 `SYSTEM_MEMORY_LOG_ENABLE` 默认 0，设为 1 后重新构建，任务启动满 60 秒开始输出，此后每 60 秒输出一次堆、任务栈及块归属统计。默认关闭时诊断实现与周期调度编译剔除，不分配诊断快照。移除了启动各阶段、任务创建、BLE、LVGL 和显示缓冲的分散内存打印；正常故障及配网状态日志保留。
