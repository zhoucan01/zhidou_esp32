# RAM 占用分析与诊断打印

日期：2026-10-30。依据当前源码、sdkconfig 和已有 build/EVT_ESP32-S3.map（修改时间 2026-10-29）。尚无本次设备串口日志，因此不能确认用户所见 20 KB 的具体时刻和统计口径，也不能直接判定内存泄漏。链接结果属于已有构建，不代表本次修改已编译。

## 已确认的主要占用

| 项目 | 内部 RAM 占用 | 依据 |
| --- | ---: | --- |
| 静态 data + bss | 100000 B，约 97.7 KiB | map：data 0x5fb8，bss 0x126e8 |
| LVGL 静态池（已包含于上行） | 32 KiB | map 中 work_mem_int 为 0x8000 |
| 显示备用双缓冲（已包含于首行） | 12.5 KiB | map 中两个 fallback_buf 各 0x1900；即便正常分配成功也常驻 |
| 正常显示双缓冲 | 50 KiB | 320 像素 × 40 行 × RGB565 两字节 × 两个缓冲，显式分配 INTERNAL + DMA |
| 启动创建的 8 个普通业务任务栈 | 29 KiB | 3072×3 + 4096×5；不含 TCB、系统任务和后续动态流程任务 |

UI 任务的 8 KiB 栈已显式放 PSRAM。另有 Wi-Fi、NimBLE、UART、队列、系统任务等占用，需靠启动阶段打印实测，不能由上述表推算完整剩余量。

CONFIG_LV_CONF_SKIP=y，实际 LVGL 构建使用 sdkconfig 的 32 KiB 池；main/lv_conf.h 中写的 64 KiB 不能作为实际占用依据。

PSRAM 已启用，但 CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384 使小块 malloc 优先使用内部 RAM，CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP 未启用。内部任务栈、显式 DMA 缓冲等仍消耗内部 RAM。CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768 是为内部专用分配保留的额度，不能作为额外已用 32 KiB 再次相加。

## 新增打印

启动阶段：boot、NVS、BLE、UART、WiFi、LCD、lv_init、显示缓冲、UI、业务任务创建完成。

复用 MachineControl 现有任务，每约 30 秒输出一次，不新增任务或诊断堆分配。每阶段分别输出 internal8、psram8、internalDMA 的 free、used、min、largest，单位均为字节；另输出 MachineControl 自身栈历史最小空闲字节。未启用全任务快照配置，当前打印不包含其他任务的栈余量。

- free：当前空闲；used：对应能力堆内已分配量，不含链接时静态 data/bss。
- min：历史最低空闲，可能低于当前值；多堆汇总最低值不保证同时出现。
- largest：最大连续空闲块，大块分配要看这个数。
- internalDMA 与 internal8 重叠，不能相加；初始化阶段有并发任务，阶段差值不是单一模块精确账本。

观察方式：搜索串口标签 RAM，比较相邻启动阶段的 internal8 free；稳定待机后观察是否持续下降，再比较联网、蓝牙连接、进入制作流程时的变化。一次下降后稳定通常是常驻分配，持续下降才需要进一步检查泄漏。20 KB 内部空闲不代表 PSRAM 也仅剩 20 KB。

## 后续优化方向

优先评估将显示正常缓冲由 40 行降到 20 行，预计可少用 25 KiB 内部 RAM，但需要实测刷新性能。备用静态缓冲可另行评估改为按需分配，以避免正常路径仍占 12.5 KiB；需保留低内存时的可靠降级。任务栈应测量各任务最坏工况水位后再裁减。本次仅加诊断，未修改缓冲、任务栈或分配策略。

## 验证

git diff --check 通过，修改的 C 文件编辑器诊断无错误。CMake Tools 构建返回失败且无具体错误，随后 CMake 诊断为空；不能声明编译通过。未烧录、未获得设备运行数据。

## 补充：收到的运行日志与具体对象

用户提供约开机 122 秒时的数据：internal8 free=20551、used=274192、min=15979、largest=7168；PSRAM free=2074616、used=20340；internalDMA free=13019、min=8447、largest=6400。MachineControl 栈最低剩余 1944 B。

这里的 274192 B 是内部堆分配，不能用前面的 100000 B 静态 data/bss 去解释这 274192 B；两者是不同账目。堆中包含任务栈、显示缓冲、驱动和协议栈分配。目前确定的显示正常双缓冲 51200 B 与启动业务内部栈 29696 B，合计 80896 B，其余不能仅凭运行时汇总认定来源。

现有 map 中其他可定位的大对象：

| 对象 | 大小 B | 所属 |
| --- | ---: | --- |
| g_cnxMgr | 3832 | Wi-Fi 连接管理，静态 |
| ntc_temperature_filters | 3816 | ntcTemp 温度滤波，静态 |
| s_wifi_nvs | 1284 | Wi-Fi NVS，静态 |
| TxRxCxt | 960 | Wi-Fi，静态 |
| gWpaSm | 824 | WPA，静态 |
| command_queue | 788 | DataCenter，静态 |
| ble_att_svr_prep_entry_mem | 768 | BLE，静态 |

其他明确的动态来源：buzzer_task 栈 2048 B，NimBLE host 配置栈 4096 B，TCP/IP 配置栈 3072 B；UART 初始化每路 RX 2048 B、TX 1024 B，另有驱动结构和对齐开销；DebugConsole 还安装 RX 1024 B。Wi-Fi 配置静态 RX 6 个、静态 TX 8 个、动态 RX 上限 16 个；驱动实际占用以启动增量为准，这些数量不是当前动态已分配量。

### 第二轮打印补充

- 每次 RAM 打印附上相对上次采样的内部堆净占用变化，正数为占用增加；初始化期间包含其他核并发活动，不是精确模块归属。
- 单独增加 buzzer 初始化采样。
- 每个启动业务任务分别记录创建前后 internal/PSRAM 增量，包含栈和 TCB。采样期间其他核可能分配或释放，日志明确注明这一限制；打印放在恢复调度后。
- sdkconfig 开启 FreeRTOS trace facility，每 30 秒获取所有任务快照，输出任务名称、栈地址、历史最低剩余字节和优先级。最多 48 个任务，容量不足会报提示。临时快照放 PSRAM，打印后释放；不启用 CPU 运行时间统计。栈水位是剩余量，不能直接当作分配大小。

下一步应收集从 boot 开始的 RAM 日志及一组任务快照，定位 Wi-Fi、BLE、UART、LCD、UI 和业务任务各阶段的占用。当前没有启动增量，不能可靠地把剩余约 193296 B 内部堆占用逐笔归属到某一模块。

第二轮仍未完成编译验证：CMake Tools 再次返回失败且 CMake 诊断为空。只读附件路径在当前文件系统中不存在，未修改附件。

## PSRAM 迁移修改

本节为后续修改，取代前文“本次仅加诊断”的状态说明。

后续按用户要求将 PowerRecovery 栈从 3072 B 调整为 1024 B，节省 2048 B 内部 RAM。此前观测最大使用约 716 B，按此历史值新栈仅余约 308 B，需在实际触发加热/电机恢复路径时继续观察栈水位，不能视为最坏工况已验证。当前 shell 的 `type nv_build` 和 `type nv_flash` 均无法解析，因此此修改尚未完成固件构建验证，未烧录。

Function 为 FuncMoniterTask，是流程命令调度任务：每 10 ms 读取功能与操作事件位，针对豆浆、压制、豆腐、植物奶、自清洁、点卤执行启动/暂停/恢复/停止；先调用对应的前置处理，再创建、挂起、恢复或删除实际流程任务，更新当前功能与操作状态。Function 自身栈为 4096 B；启动的具体流程任务另外各分配 4096 B，并不共用它的栈。此次未调整 Function 栈；待机水位不能代表执行暂停/结束回调、日志和任务创建时的最坏用量。

| 项目 | 修改 | 预期内部 RAM 收益 |
| --- | --- | ---: |
| LVGL 固定 TLSF 内存池 | 对静态池添加 EXT_RAM_BSS_ATTR；保留原分配器和 32 KiB 容量 | 32768 B |
| ntc_temperature_filters 温度趋势历史 | 对原数组添加 EXT_RAM_BSS_ATTR，保留自动清零、数组尺寸及滤波算法 | 旧 map 对应 3816 B |
| Wi-Fi/LwIP 支持外部内存的分配 | 开启 CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP | 需实测，不预估 |

sdkconfig 同时开启 CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY。仅显式标记的数据进入外部 BSS，没有整体迁移 Wi-Fi/BLE 静态库。LVGL 与温度源文件的属性均以 ESP_PLATFORM 和该配置为条件，主机编译保持原有存储方式。

前两项合计 36584 B（35.7 KiB），不是设备实测收益。静态外部 BSS 会占用 PSRAM 地址空间，不会计入 PSRAM 堆的 used，所以迁移后不能只看 PSRAM used 增量验证。LVGL 初始化新增打印 `LVGL pool: 32768 bytes at ... (PSRAM)`，直接确认实际指针区域；新 map 中 work_mem_int 和 ntc_temperature_filters 应归入外部 BSS。若配置关闭，打印会显示 internal。

保持内部 RAM 的对象：显示正常与备用 DMA 缓冲、驱动私有静态对象、控制和通信任务栈。显示回调有中断路径，避免将 LVGL 内存池之外的全局数据批量放外部 RAM；任务可能经过 NVS/Flash 操作，暂不迁移。原有 UI PSRAM 栈保留。

验证：宿主 gcc 对 LVGL 内存源文件执行 `-fsyntax-only -std=c99 -DLV_CONF_SKIP -Icomponents/lvgl` 通过；该检查覆盖主机兼容分支，不覆盖 ESP-IDF 的外部 BSS 属性和最终链接。CMake Tools 构建失败且没有可用 CMake 诊断，因此未确认固件编译、链接和设备运行成功。需重新构建后核对新 map、LVGL pool 日志和初始化 RAM 增量，再测试界面、温度采样、Wi-Fi/BLE 连接及 NVS/OTA 场景。
