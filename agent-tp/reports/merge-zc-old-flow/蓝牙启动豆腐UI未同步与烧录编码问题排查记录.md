# 蓝牙启动豆腐后 UI 未同步与烧录编码问题排查记录

记录日期：2026-10-29。工程：`zc_process`。芯片：ESP32-S3。构建环境：Windows、ESP-IDF 5.3.1、Python 3.11。

## 1. 问题现象与当前结论

通过蓝牙发送豆腐任务启动指令时，启动前 UI 停留在首页。设备打印豆腐任务启动日志，但屏幕没有跳转到豆腐正在制作页。

本次排查涉及三个不同层面，不能把它们当成一个已经完成实机验证的单一原因：

| 层面 | 已确认的情况 | 当前处理结果 |
| --- | --- | --- |
| UI 页面同步 | 旧代码依赖一次性的接管标志，存在接管后无法恢复运行页、旧状态未清除的问题 | 已修复，主机回归测试通过 |
| Windows 构建与烧录参数生成 | UTF-8 中文分区注释被 `parttool.py` 按 GBK 读取，产生解码错误，生成空的应用烧录地址 | 已复现并修复；应用地址恢复为 `0x20000`，烧录预检通过 |
| 设备固件与监视器 ELF | 设备打印的任务入口地址与当前 ELF 的豆腐任务入口不一致 | 已确认地址不匹配；需要重新烧录并使用同次构建的 ELF 做实机核对 |

**截至本记录完成，软件修复、主机测试、固件编译和烧录参数检查已完成；尚未执行实际烧录，也没有取得新固件实机跳转成功的日志。因此，不能将本次用户现象标记为已完成实机闭环。**

## 2. 正常情况下的指令与页面同步链路

当前工程已经有外部任务启动后的页面同步入口，不需要在蓝牙接收回调中直接操作 LVGL。

```text
蓝牙写入数据
    ↓
components/Bluetooch/GATT.c
    ↓ BlueDataSubmit()
task/BlueDataDeal/BlueDataDeal.c
    ↓ ControlModbusProcess()
task/DebugConsole/DebugConsole.c
    ↓ 提交流程类型与操作事件位
task/FunctionSelection/FunctionSelection.c
    ↓ 创建任务并更新当前任务状态
components/myLvgl/myLvgl.c
    ↓ refresh_external_flow_page()
function/UI/ui.c
    ↓ ui_show_page(UI_PAGE_TOFU_MAKING)
LVGL 刷新屏幕
```

`lvUIshowTask()` 在 UI 主循环中调用 `refresh_external_flow_page()`，后者读取 `GetCurTaskState()`：

- `TOUFU_SEC`：同步豆腐正在制作页。
- `MILK_SEC` / `SOYMILK_SEC`：调用 `PlantMilkUiFollowRunningFlow()`。
- `SELFCLEAN_SEC`：调用 `CleanUiFollowRunningFlow()`。

豆腐任务编号为 `2`。本次构建中，首页编号为 `0`，豆腐正在制作页编号为 `7`。这些编号来自当前枚举定义，后续新增或调整枚举时应重新核对。

这里的同步发生在 UI 任务执行到对应逻辑时；页面实际显示还需要经过 LVGL 渲染和 LCD 传输。任务启动日志只能证明启动链路执行到了任务层，不能单独证明页面切换和屏幕刷新成功。

## 3. 设备日志证明了什么

用户提供的关键日志如下：

```text
I (455615) FuncEvent: typeIdx=2, orderIdx=0
I (455615) FuncEvent: Func[2].FuncSecP1 = 0x42015700
--- 0x42015700: BluetoothInit at E:/zhidou/gitlab/V2/ESP32/zc_process/components/Bluetooch/Bluetooch.c:64 (discriminator 1)

I (455615) FuncEvent: Func[2].FuncSecP2 = TouFuTask
I (455620) TouFu: TouFu Make Start
I (455620) FuncCurrent: Current Func:2
```

可以直接确认：

1. 调度器收到了类型为 `2` 的任务请求。
2. `orderIdx=0` 对应启动操作。
3. 任务表中的任务名称为 `TouFuTask`。
4. 豆腐任务已经执行到自身的启动日志。
5. 调度层打印的当前任务状态已经更新为 `2`。

因此，应继续检查任务状态到 UI 的传递、运行固件版本以及实际刷新过程，而不是仅凭首页未跳转就判定蓝牙没有收到命令。

日志中的 `--- 0x42015700: BluetoothInit ...` 是电脑端使用 ELF 解析地址后添加的说明，不是设备主动报告“启动了蓝牙初始化函数”。它与 `TouFu Make Start` 对不上，是核对固件与 ELF 版本的重要线索。

## 4. 原因一：模式开关会关闭外部任务同步

相关配置位于 [myLvgl.h](../components/myLvgl/include/myLvgl.h)：

```c
#ifndef MYLVGL_UI_VALIDATION_MODE
#define MYLVGL_UI_VALIDATION_MODE 0
#endif
```

配置含义如下：

| 值 | UI 行为 | 外部任务页面同步 |
| --- | --- | --- |
| `1` | UI 验证模式，模拟传感器和本地流程 | `refresh_external_flow_page()` 的正式同步逻辑不编译执行 |
| `0` | 读取真实传感器并对接实际工艺任务 | 启用外部任务页面同步 |

早期检查时工程默认值为 `1`，因此它是一个必须排除的配置因素。但用户反馈实机未同步后再次检查，工作区配置已经是 `0`，所以不能继续把测试模式作为该次现象的已确认原因。

该宏是编译期配置。修改头文件后，需要重新编译并让设备实际运行更新后的固件；电脑上的源码改为 `0`，不会自动改变设备中已烧录固件的行为。

## 5. 原因二：一次性接管标志阻断页面恢复

### 5.1 原来的判断条件

[myLvgl.c](../components/myLvgl/myLvgl.c) 原来的豆腐同步逻辑为：

```c
if (flow == TOUFU_SEC && !tofu_flow_requested) {
    tofu_flow_requested = true;
    tofu_flow_seen_running = true;
    ui_show_page(UI_PAGE_TOFU_MAKING);
}
```

第一次接管豆腐任务后，`tofu_flow_requested` 被置为 `true`。后续只要此标志没有清除，代码就不会再次尝试同步豆腐页面，即使当前页面已经回到了首页或其他功能页。

旧逻辑对任务结束的清理也与当前页面有关。例如，豆腐完成页跳转原本主要在豆腐制作页或停止确认页处理。如果任务在其他页面结束，旧接管状态可能残留，并影响下一次接管。

### 5.2 为什么原有测试没有发现

已有的外部任务导航测试会在每轮开始时调用 `UiFlowConfigure()`，清空控制器状态。这能验证首次启动，但无法覆盖“已经接管过任务，之后页面发生变化”及连续任务切换等场景。

补充不重新初始化控制器的测试后，旧实现发生页面断言失败，证明这里确实存在逻辑缺口。

这项缺陷已在主机上复现，但不能仅凭它解释用户提供的那一次首页启动现象：当控制器状态完全干净时，原有首次启动测试本来就能通过。需要结合设备运行版本与 `UIFlow` 日志判断具体触发路径。

### 5.3 本次如何修复

现在以当前任务状态和当前页面共同决定是否同步：

- 当前任务为 `TOUFU_SEC` 时，确认豆腐流程已接管且已观察到运行。
- 如果当前不在豆腐制作页、停止确认页、停止等待页，就进入豆腐制作页。
- 如果已经在上述运行相关页面，不重复切页，保留用户正在进行的停止确认操作。
- 切换到其他任务，或任务在非豆腐运行页结束时，清除旧豆腐接管标志。
- 保留原有豆腐完成和停止完成页面的后续处理。

同步函数只接管页面和 UI 状态，不重新发送启动命令，不创建第二个豆腐任务。

同时增加了以下诊断输出：

```text
UIFlow: validation=0, external_sync=1
UIFlow: task=6 -> 2, page=0, tofu_requested=0
UIFlow: tofu page=0 -> 7, initialized=1
```

这些是正常启动场景下的预期示例，不是本次已经取得的设备实测结果。上一任务编号、当前页面编号和标志值会随设备状态变化。

## 6. 原因三：中文分区注释与 Windows 默认编码不兼容

### 6.1 触发条件

此前按要求将合入代码的英文说明注释改为中文时，[partitions.csv](../partitions.csv) 中的两条预留间隔说明也被改成了 UTF-8 中文，例如：

```csv
# 预留间隔 1： 0xF73000-0xF74000 (4K)
```

分区名称、偏移地址和大小没有因为注释汉化而改变。

但当前使用的 ESP-IDF 5.3.1 中，`components/partition_table/parttool.py` 读取 CSV 文本时使用：

```python
with open(partition_table_file, 'r') as f:
    partition_table = gen.PartitionTable.from_csv(f.read())
```

这里没有显式指定 `encoding='utf-8'`。在本次 Windows 环境中，Python 未启用 UTF-8 模式时，默认文本编码为 GBK。UTF-8 中文字节因此被当成 GBK 解码。

问题发生在读取文件阶段，早于 CSV 解析器忽略 `#` 注释的阶段。因此，虽然内容只是注释，也仍然会导致工具失败。

这项兼容性问题是此前注释汉化时遗漏的工具链检查，不是豆腐任务代码修改了分区布局。

### 6.2 已复现的报错

直接运行分区查询工具时，得到：

```text
File ".../components/partition_table/parttool.py", line 97, in __init__
    partition_table = gen.PartitionTable.from_csv(f.read())
UnicodeDecodeError: 'gbk' codec can't decode byte 0x9a in position 250: illegal multibyte sequence
```

复现条件是本机 GBK 默认编码与未启用 UTF-8 模式的 Python。其他操作系统或已经启用 UTF-8 模式的 Windows 环境，不一定出现同样的错误。

### 6.3 为什么编译成功仍然有问题

ESP-IDF 在 CMake 配置阶段通过 `partition_table_get_partition_info()` 调用 `parttool.py`，查询应用分区、OTA 数据分区等信息。

当前版本的这段 CMake 逻辑只在退出码既不是 `0`、也不是 `1` 时发出额外警告，并没有对查询失败作致命错误处理。本次 Python 解码异常的退出码为 `1`，异常内容写到标准错误，查询结果的标准输出为空；函数仍将空结果传回调用方。因此，本次出现了“固件可以链接、生成，但烧录参数不完整”的情况。

修复前的 `build/flasher_args.json` 中，应用地址为空：

```json
{
  "flash_files": {
    "0x0": "bootloader/bootloader.bin",
    "": "EVT_ESP32-S3.bin",
    "0x8000": "partition_table/partition-table.bin"
  },
  "app": {
    "offset": "",
    "file": "EVT_ESP32-S3.bin"
  }
}
```

对应的 `build/flash_args` 中也缺少应用地址：

```text
0x0 bootloader/bootloader.bin
 EVT_ESP32-S3.bin
0x8000 partition_table/partition-table.bin
```

这份参数不能用于正常烧录应用，错误清单中还没有列出 OTA 初始化数据。编译成功不能替代烧录参数检查，也不能证明设备已经运行最新代码。

### 6.4 本次如何修复

在工程根目录 [CMakeLists.txt](../CMakeLists.txt) 中，位于引入 ESP-IDF 的 `project.cmake` 之前，加入：

```cmake
if(CMAKE_HOST_WIN32)
    set(ENV{PYTHONUTF8} "1")
endif()
```

这样，Windows 主机上由本次 CMake 配置启动的 Python 子进程会启用 UTF-8 模式，`parttool.py` 就能读取带有中文注释的 UTF-8 分区表。

使用 `CMAKE_HOST_WIN32` 是为了判断运行构建工具的电脑系统，而不是 ESP32 交叉编译的目标系统。设置必须早于分区查询，不能放在 `project()` 完成之后。

该修复没有修改 ESP-IDF 安装目录中的脚本，也没有修改系统级编码设置。它解决的是工程配置阶段的 Python 子进程编码；在另外打开的终端中手动运行 `parttool.py` 时，如仍使用 GBK 默认编码，需要在那个终端单独启用 UTF-8 模式。

修复后重新运行 CMake 配置和构建，生成的烧录文件映射恢复为：

```json
{
  "0x0": "bootloader/bootloader.bin",
  "0x20000": "EVT_ESP32-S3.bin",
  "0x8000": "partition_table/partition-table.bin",
  "0xf000": "ota_data_initial.bin"
}
```

应用地址 `0x20000` 对应分区表中的 `ota_0`；`0xf000` 对应 `otadata` 分区。

## 7. 原因四：设备入口地址与当前 ELF 不一致

排查时使用当前 `build/EVT_ESP32-S3.elf` 查询符号，得到：

```text
42015694 T BluetoothInit
42015ab4 T TouFuMakeTask
```

再用该 ELF 解析设备日志中的 `0x42015700`，结果确实位于 `BluetoothInit`，与用户日志中监视器附加的说明一致。

但设备接着打印了 `TouFu Make Start`。这强烈表明电脑当前用于解析的 ELF 与设备实际运行的代码不是同一次构建，不能将错误的地址解析解释成任务表真的调用了 `BluetoothInit`。

可导致这类不一致的情况包括只编译而未成功烧录、烧录了其他构建目录的固件、监视器加载了不同 ELF，或设备启动了另一个 OTA 槽中的旧应用。本次已复现的错误烧录参数会妨碍新应用正常烧录，是需要优先修正的因素；没有设备启动分区与版本信息，不能断言设备具体经历了哪一种情况。

上述地址只是排查时的构建快照，后续编译后可能变化，不应写死为长期判断标准。应使用同一次构建的 `.bin` 和 `.elf`，并核对设备启动日志中的应用版本、编译信息及 ELF SHA256。

## 8. 修改了哪些文件

### 8.1 本轮故障修复与补充记录

| 文件 | 修改内容 | 目的 |
| --- | --- | --- |
| [CMakeLists.txt](../CMakeLists.txt) | 在引入 ESP-IDF 构建逻辑之前，为 Windows CMake 子进程设置 `PYTHONUTF8=1` | 修复中文分区注释导致的 GBK 解码失败和空烧录地址 |
| [components/myLvgl/myLvgl.c](../components/myLvgl/myLvgl.c) | 调整豆腐外部任务同步条件；清除不适用的旧接管状态；保留停止确认和停止等待页；增加 `UIFlow` 日志 | 修复页面恢复缺口，并让任务状态到 UI 的同步过程可观察 |
| [tests/test_plantmilk_ui.c](../tests/test_plantmilk_ui.c) | 新增并调用 `test_external_tofu_reentry()` | 覆盖重复回到首页、跨页面恢复、非运行页结束、任务切换再接管及停止页保留等场景 |
| 本文档 | 记录证据、原因、修复与验证边界 | 便于后续复查和实机验收 |

### 8.2 相关配置与历史触发文件

| 文件 | 本次排查中的状态 | 与问题的关系 |
| --- | --- | --- |
| [components/myLvgl/include/myLvgl.h](../components/myLvgl/include/myLvgl.h) | 当前工作区相对提交版本将 `MYLVGL_UI_VALIDATION_MODE` 从 `1` 调整为 `0`；故障排查开始时已经是 `0`，修复过程未再次修改该值 | 正式外部任务同步的必要配置；不能据此证明设备中固件也已更新 |
| [partitions.csv](../partitions.csv) | 此前注释汉化加入了中文说明；本轮编码修复没有再次修改该文件及分区布局 | 编码不兼容的触发文件 |
| [.vscode/settings.json](../.vscode/settings.json) | 工作区已有串口配置从 `COM10` 调整为 `COM41` 的变化，本轮修复未修改它 | 本机串口选择，与代码层修复不同；实际烧录时仍须确认当前 USB 串口 |

`build/flasher_args.json`、`build/flash_args`、`build/app-flash_args`、固件和 ELF 都是重新配置或构建后生成的产物，不应手工修补后提交。构建产物、测试输出及缓存继续保留在被忽略的 `build/` 目录中。

## 9. 已完成的验证

| 验证项 | 结果与范围 |
| --- | --- |
| 原有正常模式 UI 测试 | 修改前能够通过，说明首次启动测试不足以覆盖重入问题 |
| 新增豆腐重入回归测试 | 修复前页面断言失败，修复后通过 |
| 正常模式主机测试 | `--ui-validation-mode 0` 通过，包括外部豆腐页面恢复和既有 UI 用例 |
| UI 验证模式主机测试 | `--ui-validation-mode 1` 通过，保持测试模式隔离行为 |
| Windows 编码问题复现 | 未启用 UTF-8 模式时直接查询分区表，复现 GBK 解码异常 |
| 工程内编码修复 | 在外层进程设置 `PYTHONUTF8=0` 的条件下重新构建，工程 CMake 设置仍能正确生成烧录参数 |
| 分区查询 | 启用 UTF-8 模式后，默认启动应用分区偏移返回 `0x20000` |
| 固件构建与分区容量检查 | 通过；当时应用大小为 `0x16e120`，最小应用分区为 `0x7a3000`，剩余约 81% |
| 烧录清单检查 | 所有地址非空，应用地址为 `0x20000`，包含 `ota_data_initial.bin` |
| 烧录预检 | `script/flash.py --port COM41 --dry-run` 通过，仅验证文件和生成命令，没有打开串口或烧录 |
| 修改格式检查 | 修复文件的 `git diff --check` 通过 |

主机测试使用真实 LVGL、页面代码和流程桥接代码，但模拟 RTOS、传感器及任务状态，不能替代真实蓝牙、双核任务调度、LCD DMA 和设备烧录验证。

## 10. 后续重建与实机验收方法

在已激活的 ESP-IDF 终端、工程根目录中重新配置并编译：

```powershell
idf.py reconfigure
idf.py build
```

检查 `build/flasher_args.json`：应用偏移必须为 `0x20000`，不能出现空字符串地址，且应包含 `0xf000` 的 OTA 初始化数据。

需要单独查询分区时，可在当前 PowerShell 终端启用 UTF-8 模式后执行：

```powershell
$env:PYTHONUTF8 = '1'
python "$env:IDF_PATH/components/partition_table/parttool.py" -q --partition-table-offset 0x8000 --partition-table-file partitions.csv get_partition_info --partition-boot-default --info offset
```

预期返回：

```text
0x20000
```

烧录前可使用以下命令做文件与参数预检。`COM41` 是本次配置记录中的端口，执行时应改为设备实际 USB 串口：

```powershell
python script/flash.py --port COM41 --dry-run
```

确认设备处于允许重新启动和烧录的状态后，使用实际串口烧录并监视同一次构建：

```powershell
$espPort = 'COM41' # 改为当前 ESP32-S3 实际 USB 串口
idf.py -p $espPort flash monitor
```

本记录中的这条命令是后续操作步骤，本次排查未执行实际烧录。

实机验收应覆盖：

1. 启动日志出现 `UIFlow: validation=0, external_sync=1`，并核对设备版本与当前构建匹配。
2. 从首页通过蓝牙启动豆腐，确认同时出现任务启动日志、`Current Func:2`、`UIFlow` 任务切换与页面切换日志，屏幕实际显示豆腐正在制作页。
3. 打开停止确认页后持续观察，确认周期同步不会把该页面顶回制作页。
4. 任务结束后再次启动，确认旧标志不再阻断页面同步，且不会重复创建任务。

如果任务日志正常，但仍然没有跳转，应保留启动日志和指令前后的完整 `UIFlow` 日志继续定位：

| 观察结果 | 下一步检查方向 |
| --- | --- |
| 找不到启动时的 `UIFlow` 模式日志 | 先核对是否运行了本次固件、日志是否完整、UI 任务是否启动 |
| 模式日志显示 `validation=1` | 运行的是验证模式构建，检查编译配置和实际烧录版本 |
| 有 `Current Func:2`，没有相应的 `UIFlow` 任务变化日志 | 检查日志时段、UI 循环是否仍在运行，以及固件版本是否匹配 |
| 页面日志仍为 `0 -> 0`，且 `initialized=0` | 检查 UI 初始化与对象是否有效 |
| 页面日志为 `0 -> 7`，但屏幕仍显示首页 | 逻辑页面已切换，应继续检查渲染、LCD 刷新与 DMA 完成路径 |

目前仍需补齐的是重新烧录后的设备版本核对与上述实机验收结果。只有这些结果齐全，才能确认用户报告的首页不跳转现象已经解决。
