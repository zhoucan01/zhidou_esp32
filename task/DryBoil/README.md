# 防干烧独立任务

防干烧组件集中在本目录，工程根 CMakeLists.txt 已将 `task/` 加入组件搜索路径。**main.c 已和其他任务一起通过 `CREATE_TASK_OR_ABORT(xTaskCreate(...))` 创建下加热检测任务。** 恢复调度后，由防干烧任务自身等待 DataCenter 收到两块板的首帧数据，再每 500 ms 检测一次。等待不会阻塞 main 的后续初始化；创建失败由原有任务创建宏统一处理。DataCenter 增加了新加热指令计数和防止旧停热命令覆盖重启的处理；其他业务流程未修改。

已有 `build/` 的工程首次使用新增组件时，在已激活的 ESP-IDF 环境运行 `idf.py reconfigure build`，使构建系统重新发现本目录。

## 当前工程接入

当前 `main.c` 已直接创建，无需再调用启动包装接口：

```c
#include "DryBoilTask.h"

static TaskHandle_t dry_boil_handle;
CREATE_TASK_OR_ABORT(
    xTaskCreate(DryBoilTask, "DryBoil", DRY_BOIL_TASK_STACK_SIZE, NULL,
                DRY_BOIL_TASK_PRIORITY, &dry_boil_handle),
    "DryBoil");
```

`NULL` 参数默认使用下加热（Heat1）适配。创建可放在现有挂起调度的任务创建区，任务中的等待、日志和检测在恢复调度后执行。等待初始数据时状态为 `started=true`、`sample_valid=false`，不表示数据已可用于检测。`main` 默认可引用本组件；从其他组件调用时，在调用方的 `REQUIRES` 中加入 `DryBoil`。保留的 `DryBoilTaskStart/StartWithPort` 仅用于其他接入方式，不要与直接创建重复使用；包装启动接口需在正常任务上下文、调度启用时调用。

已根据用户确认选择下加热（Heat1）。模式对应关系如下：

| 模式 | 输入算法的实际档位 | 限制 |
| --- | --- | --- |
| `DRY_BOIL_HEATER_1` | 下加热（Heat1）反馈档位 | 上加热可独立运行，其档位不计入算法 |
| `DRY_BOIL_HEATER_2` | 加热器 2 反馈档位 | 加热器 1 必须关闭 |
| `DRY_BOIL_HEATERS_SAME_GEAR` | 两路共同的反馈档位 | 两路实际档位必须相同，并且标定也采用两路同档 |

不将两路档位相加，也不把瓦数当作档位。下加热模式兼容现有上下加热同档或异档运行；只输入下加热档位，不表示已经验证上加热对温升的影响。同档模式下档位不一致、反馈档位超出 0～13 时按输入异常停热。

## 任务行为与复位

- 单实例常驻 FreeRTOS 任务，栈 3072 字节，优先级 17，固定周期 500 ms；重复启动不会创建第二个任务或清除报警。
- 读取 DataCenter 的受锁保护 Modbus 缓存：温度来自执行板，加热使能和实际档位来自功率板。空闲时不消耗算法的启动忽略时间，首次实际加热时开始学习。
- 一个加热周期内的调功、零功率间隔不自动复位。确认干烧、达到原算法绝对温度阈值、输入无效（包括 NaN/Inf）、活动周期发生采样超期都会锁存保护。
- 锁存后每 500 ms 尝试发送两路停热命令；自定义报警回调每次锁存只执行一次。任务不操作电机、UI 或已有 FaultDeal 注册表。
- **收到锁存之后的新非零加热指令，就允许重新加热；任务在下一次检测周期清除旧锁存及历史，重新开始学习。无需先调用复位接口，也不要求反馈档位先变为 0。** 新指令包括 `setPowerHeater1/2()` 的非零档位，以及站号 3 的 Modbus `0x06/0x10` 写上下加热寄存器。重复发送相同的非零档位也算新指令；内部业务指令与外部指令规则相同。
- 只有成功入队才记录新指令。零档位、查询、电机、总开关指令、无效 CRC 帧、锁存前已入队的指令及缓存中的旧非零档位，都不会解锁。正常未锁存时调档也不会重置学习时间。
- 每次保护停热命令都关联锁存时的指令序号。新加热请求到来后，旧停热命令不再入队，已排队的旧停热命令在执行前也会被跳过，避免覆盖新请求。队列中已经执行中的停热仍先完成，新的加热写入随后执行。
- 新指令不会关闭绝对温度或无效输入保护：重新采样仍达到 103 ℃或数据无效，会再次锁存停热。正常结束一轮后若需要重新学习，仍可使用 `DryBoilTaskReset()`；手动复位要求有效温度低于 103 ℃、下加热实际档位为 0。

```c
dry_boil_status_t status;
DryBoilTaskGetStatus(&status);
if (status.alarm_latched) {
    // 当前保护已锁存；如不打算重启，应保持不发送非零加热指令。
}

// 锁存后重新下发加热指令即可恢复，无需调用 Reset：
setPowerHeater1(6); // 需包含 DataCenter.h；例：重新启动下加热 6 档。

// 可选：正常结束一轮、实际停热后，手动准备新学习周期：
esp_err_t err = DryBoilTaskReset();
// err == ESP_OK 后，保持停热，等待下一次检测周期再查询：
DryBoilTaskGetStatus(&status);
// 只有 started && sample_valid && !reset_pending && !cycle_active &&
// !alarm_latched 才表示任务已准备好等待新加热周期。
```

状态查询和复位请求可跨任务调用；移植时回调上下文必须在任务整个生命周期内有效，回调不能长时间阻塞。无需另外调用 `db_simple_reset()` 或 `db_is_dry_boil()`，任务使用独立的 `db_detector_t` 实例。

## 移植

`dry_boil_detector.c/.h` 是原样复制的纯 C 算法。`DryBoilTask.c/.h` 不直接读取 DataCenter；通过 `DryBoilPortInit()` 获取默认适配。当前工程相关操作集中在 `DryBoilPort.c/.h`。

换工程时替换适配层的 `DryBoilPortInit()`，或构造生命周期覆盖任务运行的 `dry_boil_port_t`，将其地址作为 `xTaskCreate` 的参数：

- `read_sample`：输出摄氏温度、温度有效标志、实际 0～13 档位；数据不可用或过期时返回 false。
- `stop_heating`：切断加热，锁存期间反复调用。
- `alarm`：可选的报警通知，只在首次锁存时调用。
- `context`：可选的板级上下文指针。
- `ready`：可选的首帧数据就绪检查；未就绪时任务每 100 ms 等待一次，只在进入检测前检查。没有此回调时直接开始采样，采样失败按异常处理。
- `heating_sequence` / `stop_for_sequence`：可选但必须成对提供。前者返回成功接收的非零加热指令计数，后者只能在指令计数未改变时发送停热，并在真正执行时再次校验。默认适配已接入 DataCenter；自定义适配不提供此对回调时保留手动复位方式。

替换适配层后，同步调整本目录 CMake 的源文件和 `DataCenter` 依赖。非 ESP-IDF 平台还需替换任务、锁和日志接口。

## 当前接口的边界

现有 `ModbusCopyCachedInputRegisters()` 只表示曾经收到数据，没有更新时间或掉线标志，因此本适配层**不能识别通讯中断后残留的旧缓存**。如需通讯失联保护，须由有时效信息的采样回调提供，或后续扩展 DataCenter 的缓存时效接口。

保护停热及加热指令仍经过串口队列执行，入队成功不等于功率板已经响应。新加热请求会按用户要求解除旧锁存，因此持续发送非零档位的业务也可以触发重启。原业务中的独立零档位命令仍正常执行，不属于会被跳过的旧保护命令。

原算法参数保持不变：前 35 秒忽略斜率判定，20 秒回归窗口，默认确认时间 10 秒，斜率判定温区 35～75 ℃、平均档位至少 3，绝对温度阈值 103 ℃。1～2 档不进入斜率判定。目标硬件/加热组合是否适用仍需原标定数据和板上实验确认。

## 文件和验证

- `tools/dry_boil_analyzer.c`：保留原 CSV 回放工具，含桌面 `main()`，不编入固件。
- `examples/dry_boil_loop_example.c`：保留原循环示例，含外部 HAL 占位接口，不编入固件。
- `tests/test_dry_boil.py`：主机行为测试，替代 RTOS/硬件接口，运行真实算法和任务源文件。执行 `python task/DryBoil/tests/test_dry_boil.py --cc <gcc路径>`。测试产物只放在 `build/test_dry_boil/`。
- `tests/test_heating_commands.py`：提取并运行真实 DataCenter 队列、网关入队和指令执行代码，验证新指令识别、队列满、旧停热命令失效、CRC 和批量寄存器写入。执行方式同上，产物位于 `build/test_heating_commands/`。

主机测试无法验证双核调度、实际温升、串口排队延迟和功率板最终关断；这些需在硬件上验证。

本次验证：29 个任务行为场景及 DataCenter 指令测试通过；完整工程编译、链接、固件大小检查通过，生成 `build/EVT_ESP32-S3.bin`。未烧录或进行板上测试。
