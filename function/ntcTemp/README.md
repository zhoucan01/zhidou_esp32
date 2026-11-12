# NTC 查表标定

辅助程序按用途存放：

- `scripts/calibration/`：温度标定与查表数据生成。
- `scripts/analysis/`：日志提取、对比分析及报告生成。
- `scripts/tests/`：Python 回归测试与主机 C 测试程序。

以下命令从工程根目录运行；报告仍输出到 `agent-tp/`，临时构建产物放到 `build/`。

## 机器3加热/降温选表与待录制降温库

站3反馈加热器已使能且档位已知时使用加热表，并刷新最近加热时间。加热关闭后的
10秒内仍使用加热表；超过10秒后，每路用自身温度是否达到98℃及自身ADC上升趋势判断，
各自触发降温锁存后使用降温表。总电源关闭会清除最近加热时间和各路降温锁存。
电机运行状态不决定加热/降温选表，只影响启用电机停转采样优化时的采样许可。

电机仍按周期运行的降温数据单独放在
`tables/NTC3/ntcTemp_machine3_motor_cooling_table.inc`，目前刻意为空。
将来按既有运行参数节点格式录入实测数据：`phase=0`、加热档位0、
电机档位1、转3秒/停2秒，并记录实际水量。仅在相应通道有实测节点后
使用新库；在此之前沿用已有降温节点作为临时回退，不将旧数据冒充新标定。
新库某通道有数据后，该通道只在新库选定参数组内插值或有限外插，
范围外保留上次有效值，不跨库拼接节点。空库不能证明电机降温阶段的测温精度。

启动阶段若站3功率/电机快照尚不可用，站2的有效ADC仍可更新温度，默认按降温工况
查表；加热器“已使能”但档位尚为0且无历史有效档位时，也不推断升温工况。
3号机现有降温实测表约从74℃开始；更低温度优先尝试该表，仅当ADC超过其低温端时，
用同机型已测的低温升温工况节点补足（仍受原有5℃边界外插限制），不把0/4095
等无效ADC映射成温度。该补足曲线不是新取得的降温标定数据。

`ntcTemp.h`中的`NTC_TEMP_MOTOR_STOP_OPTIMIZATION_ENABLE`默认**0**：关闭电机运行时的温度门控、停机200 ms等待、5点停机采样豁免和电机启动触发的滤波重置；电机运行期间也持续换算温度并进行范围检查。设为**1**可恢复原策略。此宏不改变电机控制、故障监控、功率恢复、ADC去噪、温度趋势滤波及工况/阶段切换处理。下文停机采样策略仅在宏为1时生效。

## 机器3运行参数数据库（2026-10-26）

水量字段位于ADC/温度节点之前，现有全部记录及建表脚本默认值为 **1150 mL**。
默认查询水量为 **1150 mL**（`NTC_TEMP_MACHINE3_DEFAULT_WATER_VOLUME_ML`）；显式查询通过`NtcTempMachine3OperatingConditions.water_volume_ml`指定。
每个通道在当前升/降温阶段内，依次按功率档位绝对差、水量绝对差、电机档位绝对差选择最近组（逐级优先，不使用加权总分）。
前三项相同时，比较加热和电机四项开停时间的绝对差之和；全部相同则保留表中首组，精确匹配自然优先。
只在选定组内做ADC插值及原有±5℃外插，不跨水量或跨工况拼接节点；该组ADC仍不可用时保持最近有效温度。

机器3的三路NTC先按各自ADC中值滤波和升/降温阶段选择维持既有逻辑，再查询
`tables/NTC3/ntcTemp_machine3_operating_table.inc`。每条节点包含通道、阶段、
加热档位/加热开停时间、电机档位/运行停止时间、水量`water_volume_ml`（mL）、ADC和参考温度；ADC在相邻节点间
线性插值。加热阶段先按站3有效加热档位筛选 JSON 配置，再选择
`temperature_below_c` 与该路自身最近有效温度距离最近的阶段作为时间配比；降温阶段使用
全停加热配置。加热器周期停热时仍保持升温阶段；加热关闭、该路温度达到98℃，且该路20点ADC窗口
后10点均值高于前10点均值时锁定降温阶段（本标定表中降温对应ADC上升）。降温锁存持续到重新加热或总电源关闭；
总电源关闭会清除上次加热档位及锁存。
电机重新转动时只重置温度趋势滤波，不清空已接受的停机ADC窗口；停机后仍等待200 ms，
随后接受采样。这样“转3秒、停0.5秒”时可跨停机周期累积20点ADC窗口，
不会因每次停机可采点数不足而阻止降温阶段切换；运行期间的ADC不进入该窗口。
`ntcTempGetTemperature()` 按当前温度和有效加热档位推导配置档；
需要指定参数时，可调用 `ntcTempGetTemperatureWithConditions()` 并传入功率档位、
电机档位及加热/电机运行和停止时间。仅当站3实时RPM为0时使用该数据库；
当前电机档位非0不代表电机正在转动，
档位仍通过配置组中的`motor_gear`参与匹配。RPM非0时回退现有同通道、同升降温阶段
的ADC表线性插值。配置组/ADC范围不匹配时也回退相应阶段表。现有高ADC尖峰剔除、20点中值、
阶段切换清窗、主通道选择、降温表未覆盖时回退升温表以及高温处理规则不变：
计算温度在[97.9℃, 98.0℃)时返回98℃，达到98℃及以上则保留查表计算值。
匹配到配置组但ADC略超标定边界时，沿端点相邻节点线性外插，最多扩展到边界温度的
±5℃；超过该范围后回退到现有温度表。
每路换算温度按最近100个实际接纳点做鲁棒趋势/残差处理，使用真实微秒时间戳而非固定20 ms点数推算。抽取最多10点拟合Theil-Sen趋势，MAD残差阈值为0.5～2℃。趋势预测斜率按1℃/秒限制，但正常接纳输出不是硬性限速。拒绝值及替代值不再写回趋势历史。

### 2026-10-28 双向异常隔离与重新接纳

- ADC与可信中值相差超过200时，无论上跳或下跳均进入独立候选区，拒绝点不填入可信20点中值窗口。
- ADC新状态至少8个连续点、跨度至少140 ms、相邻间隔不超过2秒、整段极差不超过`40 ADC + 1000 ADC/s × 候选持续时间`，相邻点变化也需满足`40 ADC + 1000 ADC/s × 相邻间隔`，才用候选中值重建窗口。
- 温度异常候选同样至少8点、140 ms，相邻间隔不超过2秒，极差不超过`0.2℃ + 5℃/s × 候选持续时间`，相邻点变化不超过`0.2℃ + 5℃/s × 相邻间隔`；还需满足相对最后真实接纳温度的变化不超过`0.5℃ + 5℃/s × 实际间隔`。趋势预测斜率同步限制在±5℃/s，允许尚在变化的候选通过确认。
- 回到正常值、候选反向或离散、无效数据、时间不递增或采样间隔过大，会打断候选。电机/工况边界清候选，保留真实温度时间锚点；电机重启仍保留ADC中值，不能通过重新初始化绕过去噪。
- 主温度始终来自配置的主通道（默认NTC1），不因超过2秒无新接纳温度而切换到NTC2/NTC3均值。主通道被拒绝时保留该通道自身最近有效值；尚无有效值时返回NaN。三路独立滤波和诊断保留。不能将4090附近的越界ADC硬映射成100℃。
- 拒绝时保持最近真实接纳温度，不用预测值反复训练自身；真实稳定变化可重新接纳，零散错误不会跨间断拼接计数。
- 0或4095及以上ADC视为无效；上层查表及校准范围检查仍保留。升降温阶段切换会完整清窗，不同工况不共享候选证据。
- DataCenter已按停机指令等待200 ms后，不在RPM延迟回零时重复等待另一段200 ms，保留0.5秒停机内的连续确认窗口；无停机指令时仍按零RPM时刻起计。

这些是保守初始参数，不代表能识别所有故障。持续、集中的错误平台可能与真实变化无法仅凭本通道区分；当前未新增数据失效后的自动停机策略。原始约1 Hz CSV不能重建50 Hz波形，测试方法及结果见[ADC去噪验证报告](../../agent-tp/reports/ntc/2026-10-28_ADC双向去噪与持续变化接纳验证.md)。

数据库由上位机采集文件 `20261026-230537-138007.csv`、对应串口日志和
`agent-tp/tools/soy_host/auto_heat_config.json` 拟合。只使用参考温度与NTC采样时间间隔
不超过2秒的记录，并要求站3电机档位0的确认回显后至少停转500 ms；每个通道/参数组按32 ADC
分箱及单调拟合。每5个样本留1个作回代检查（不是独立批次验证），共生成21个
通道/运行参数组。筛选到1680条预热后NTC记录，其中728条未满足500 ms停转条件、
62条无法匹配参考温度，最终纳入890条记录（每条含三路ADC）。日志只有电机档位命令
确认、没有实际RPM回读，因此500 ms是从零档确认起算，并非实测轴速为零。冷却数据跨越
两段运行，离散度较高，运行时仍保留旧表回退。

### 2026-10-27 13:25 下降温表更新

使用 `20261027-125905-853024.csv` 重新拟合三路下降温表；按用户指定的物理 CSV
第10161行作为降温起点（表头为第1行），该行记录时主温度为99.3℃。虽然参考温度
最高值99.5℃出现在其后，但拟合严格从第10161行开始筛选NTC记录。仍要求参考温度与NTC
采样可在不超过2秒区间内插值、局部±30秒参考温度回归斜率低于-0.002℃/s，且站3 RPM
连续为0至少500 ms。起点后1900条NTC记录中，1784条满足条件，温度范围73.5～99.3℃。
按32 ADC分箱并做单调递减PAVA拟合；本次ADC范围之外保留旧表节点。

每5条时间排序样本留1条作同次采集回代检查（不代表独立批次验证）：

| 通道 | 本次样本 ADC 范围 | 合并节点数 | 留出 MAE | P95 | 最大误差 |
|---|---:|---:|---:|---:|---:|
| NTC1 | 1609～2587 | 48 | 0.065℃ | 0.172℃ | 0.311℃ |
| NTC2 | 1608～2559 | 31 | 0.071℃ | 0.175℃ | 0.411℃ |
| NTC3 | 1686～2575 | 30 | 0.068℃ | 0.170℃ | 0.244℃ |

复现拟合报告；添加 `--write` 才会更新三张下降温表：

```bash
python3 function/ntcTemp/scripts/calibration/calibrate_3ntc.py \
  /path/to/20261027-125905-853024.csv \
  --cooling-only --cooling-start-line 10161
```

### 2026-10-27 12:16 下降温表更新（历史）

使用 `20261027-114351-278445.csv` 更新三路下降温表。参考温度峰值为100.2℃
（12:16:26.696）；从峰值后选取下降温样本，要求参考温度与NTC采样能在不超过2秒的
区间内插值、峰值之后±30秒参考温度线性回归斜率低于-0.002℃/s，并且实测站3 RPM
连续为0至少500 ms（相邻RPM采样间隔超过500 ms则重新计时）。513条峰值后已对齐的
NTC记录中，407条满足电机停转条件，最终纳入404条三路样本，参考温度范围82.2～100.0℃。
在32 ADC分箱后做单调递减PAVA拟合；测量ADC区间外保留旧表节点，因此低于本次测量
范围的旧表尾部没有被新数据重新验证。

当前日志的升降温误差按真实温度接收时间线性插值配对，真实温度参考记录间隔须不超过
2秒。日志开头真实温度从94.7℃降至29.6℃，随后才进入连续升温；该段无法仅凭CSV确认
是实际过程还是测量/采集切换，因此不当作升温误差。升温统计从29.6℃最低点后60秒开始，
降温统计从100.2℃峰值后开始。下表为本次重新拟合前日志中固件上报温度相对真实温度的
误差（偏差=NTC−真实温度），包含传感器动态响应，不等同于单独的查表拟合误差：

| 阶段 | 样本数 | NTC1 偏差 / MAE | NTC2 偏差 / MAE | NTC3 偏差 / MAE |
|---|---:|---:|---:|---:|
| 升温（排除起始60秒） | 975 | -0.362 / 0.626℃ | -1.882 / 1.998℃ | -1.853 / 2.144℃ |
| 降温 | 575 | -0.388 / 0.625℃ | -0.472 / 0.733℃ | -0.540 / 0.884℃ |

升温阶段NTC1在70～100℃的MAE为0.18～0.28℃，但30～60℃误差较大，且升温较快时
存在明显负偏差。降温阶段主要误差集中在80～90℃：NTC1/2/3的MAE分别为0.754、
0.884、1.064℃；90～100℃的MAE分别为0.100、0.116、0.147℃。

每5条按时间排序的下降温样本留1条作同批回代检查；此留出集与训练数据来自同一次采集，
不是独立批次验证：

| 通道 | 本次样本 ADC 范围 | 合并节点数 | 留出 MAE | P95 | 最大误差 |
|---|---:|---:|---:|---:|---:|
| NTC1 | 1593～2267 | 31 | 0.083℃ | 0.245℃ | 0.386℃ |
| NTC2 | 1606～2265 | 31 | 0.083℃ | 0.246℃ | 0.387℃ |
| NTC3 | 1679～2297 | 29 | 0.085℃ | 0.211℃ | 0.387℃ |

复现拟合报告；添加 `--write` 才会更新三张下降温表：

```bash
python3 function/ntcTemp/scripts/calibration/calibrate_3ntc.py \
  /path/to/20261027-114351-278445.csv --cooling-only
```

复现和查看拟合误差：

```bash
DATA_DIR=/path/to/captured-files
python3 function/ntcTemp/scripts/calibration/calibrate_machine3_database.py \
  "$DATA_DIR/20261026-230537-138007.csv" \
  --serial "$DATA_DIR/20261026-230537-138007-serial.txt"
```

显式传入 `--write` 才会写入 C 数据表；配合 `--merge-existing` 时只替换本次拟合到的
profile，保留其他运行工况。仅提取指定 CSV 物理行之后的冷却样本示例：

```bash
python3 function/ntcTemp/scripts/calibration/calibrate_machine3_database.py \
  agent-tp/data/captures/2026-10-27/20261027-142003-970824.csv \
  --serial agent-tp/data/captures/2026-10-27/20261027-142003-970824-serial.txt \
  --cooling-start-line 12626 --write --merge-existing
```

机器3三个通道数据库查询测试：

```bash
for channel in 1 2 3; do
  gcc -std=c11 -Wall -Wextra -Wpedantic -DMACHINE=3 \
    -DNTC_TEMP_LOG_ENABLE=0 -DNTC_TEMP_MACHINE3_PRIMARY_CHANNEL=$channel \
    -Itask/DataCenter -Ifunction/ntcTemp \
  -Iagent-tp/tests/firmware/ntc_host agent-tp/tests/firmware/ntc_host/clock.c \
    function/ntcTemp/ntcTemp.c \
    function/ntcTemp/scripts/tests/test_ntcTemp_machine3_database.c -lm \
    -o /tmp/test_ntcTemp_machine3_database
  /tmp/test_ntcTemp_machine3_database
done
```

## 机器3三路NTC运行条件数据库

`MACHINE=3` 分别计算站2 NTC1、NTC2、NTC3 三路ADC。高压板电机运行时，仅接纳符合自身ADC趋势且跳动小的样本，
其余仍等待电机停止满200 ms后恢复采样。每路先将偏离自身20点窗口中值超过
200 ADC的样本先隔离，连续稳定确认后才接纳，再独立维护20点滑动中值窗口；通过运行条件数据库按加热
工况和阶段查表；MACHINE=3 不再回退到通用升温或冷却表。数据库或 ADC 覆盖缺失时保持
本通道最近一次有效温度，DataCenter 仅将本次真实接纳的主温度送入控制滤波。加热关闭后，每路自身温度达到
98℃且本通道20点ADC窗口后半段均值高于前半段均值时，独立锁定降温阶段，直至重新加热或
总电源关闭。通道阶段切换时只清本通道窗口。
应用层主温度使用NTC1；另外两路仍独立滤波并记录日志。
主温度选择宏 `NTC_TEMP_MACHINE3_PRIMARY_CHANNEL` 默认值为1，可用于测试时选择其他通道，
DataCenter 再维护20点温度窗口，排序后取温度较高的第4至第7个值求平均。
电机停转门控不属于滤波，机器3主温度路径不使用单独的NTC2 ADC跳变滤波。

运行条件表位于 `tables/NTC3/ntcTemp_machine3_operating_table.inc`；旧的六张独立相位表不再
参与 MACHINE=3 运行时温度换算。
采样数据为 `agent-tp/data/captures/2026-10-20/20261020-213637-287343.csv` 及同名串口日志；
仅使用串口日志中 `NoBush: 0` 且停止命令后满200 ms的ADC记录，参考温度按NTC采样时间
线性插值，参考点间隔超过2秒的数据不参与。历史串口日志没有实际RPM回读，筛选依据是
`NoBush: 0` 停止命令；因此这是按电机控制状态近似复现停机逻辑，不是RPM实测验证。
上位机CSV每秒采样，无法重建固件20 ms采样周期的20点窗口，因此标定用筛选后的原始
ADC配对点拟合，运行时仍由固件执行20点中值。升温段覆盖约25.9～97.5℃，降温段覆盖
约69.2～98.1℃。按时间每5点留1点的同一批次验证如下；这不是跨批次独立验证：

| 阶段/探头 | 节点数 | MAE (℃) | P95绝对误差 (℃) | 最大绝对误差 (℃) |
| --- | ---: | ---: | ---: | ---: |
| 升温 NTC1 | 65 | 0.311 | 1.075 | 2.473 |
| 升温 NTC2 | 63 | 0.464 | 2.328 | 4.287 |
| 升温 NTC3 | 63 | 0.267 | 1.145 | 2.608 |
| 降温 NTC1 | 32 | 0.107 | 0.360 | 1.186 |
| 降温 NTC2 | 31 | 0.118 | 0.349 | 2.300 |
| 降温 NTC3 | 29 | 0.131 | 0.464 | 1.813 |

只分析不写表：

```bash
python3 function/ntcTemp/scripts/calibration/calibrate_3ntc.py agent-tp/data/captures/2026-10-20/20261020-213637-287343.csv
```

重新生成六张表需显式传入 `--write`。主机逻辑回归测试：

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -DMACHINE=3 \
  -DNTC_TEMP_MACHINE3_PRIMARY_CHANNEL=2 -Itask/DataCenter -Ifunction/ntcTemp \
  -Iagent-tp/tests/firmware/ntc_host agent-tp/tests/firmware/ntc_host/clock.c \
  function/ntcTemp/ntcTemp.c function/ntcTemp/scripts/tests/test_ntcTemp_3channel.c -lm -o /tmp/test_ntcTemp_3channel
/tmp/test_ntcTemp_3channel
```

## 机器6（2026-10-15）

所有标定表统一存放在 `tables/NTC2/`。`MACHINE=6` 使用独立的
`tables/NTC2/ntcTemp_machine6_table.inc`。默认机型由 `ntcTemp.h` 中的 `MACHINE` 宏决定。
机器6数据源为 `Desktop/deta/6`，第1、2组分别1581、1421个同秒配对点，
合计3002点、63个节点；第3组CSV结束15:20，TXT开始15:33，无重合故未参与。
无需按>20℃残差剔除。ADC范围1679.814～3664.776，拟合温度23.6～97.6℃。
高温区按统一规则处理：计算温度在[97.9℃, 98.0℃)时返回98℃，≥98℃时保留计算值；
低温超范围返回23.6℃。
未封顶回代MAE分别0.367、0.441℃，最大误差3.148、5.238℃，不是独立验证精度。
NTC1接口不变；`getTemp()`、主温度字段和上位机本地NTC2拟合均支持机器6。

```powershell
python function/ntcTemp/scripts/calibration/calibrate.py C:/Users/29546/Desktop/deta/6 --channel 2 --groups 1 2 3 --max-error 20 --write --output ntcTemp_machine6_table.inc
```

## 当前 NTC2 标定（2026-10-12 更新）

已使用最新 `1.csv/1.txt` 至 `4.csv/4.txt` 重新生成 NTC2 表，共 6835 个
有效同秒配对点，56 个单调查表节点。按拟合误差 >20℃ 检查，无需剔除。
第4份 TXT 包含较早记录，仅采用其与第4份 CSV 的同秒匹配部分。
NTC2 表覆盖 ADC 1622.628～3371.885，对应拟合温度 99.6～41.7℃。
整数 ADC 有效范围为 1623～3371；本次配对数据不覆盖41.5℃以下温区。

运行时将计算温度在[97.9℃, 98.0℃)的值映射为98℃，≥98℃的计算值不封顶。
低温端超范围返回41.7℃。
`getTemp()` 与站1摘要继续读取更新后的 NTC2 温度，NTC1 标定不变。

复现命令：

```powershell
python function/ntcTemp/scripts/calibration/calibrate.py C:/Users/29546/Desktop --channel 2 --groups 1 2 3 4 --max-error 20 --write
```

以下旧 NTC2 数据来源、节点范围及验证结果为更新前的历史记录，
当前固件以本节和 `tables/NTC2/ntcTemp_table.inc` 为准。
## 机器选择

`ntcTemp.h` 中用 `MACHINE` 选择 NTC2 标定表，**当前默认 1**：

```c
#ifndef MACHINE
#define MACHINE 1 /* 1：1号机；2：2号机；6：6号机 */
#endif
```

| MACHINE | 机型 | 标定表 | 数据来源 |
| ---: | --- | --- | --- |
| 1 | 1号机 | `tables/NTC2/ntcTemp_2d_table.inc`（二维） | 主站日志 `agent-tp/data/testdata/ntcdata/261014/231906_ntc2_adc.csv` + 外部温度 TXT |
| 2 | 2号机 | `tables/NTC2/ntcTemp_machine2_table.inc`（一维） | 上位机日志 `20261014-202806-675654.csv` |
| 3 | 3号机 | `tables/NTC3/ntcTemp_machine3_operating_table.inc`（运行条件数据库；未命中时使用三路升/降温表） | 上位机日志 `20261026-212649-682665.csv` |
| 6 | 6号机 | `tables/NTC2/ntcTemp_machine6_table.inc`（一维） | `Desktop/deta/6` 第1、2组同秒配对数据 |

`MACHINE=1` 时查表键为 **ADC × 有效加热档位**，曲线未覆盖处回退旧一维表
`tables/NTC2/ntcTemp_table.inc`；`MACHINE=2` 和 `MACHINE=6` 分别按 ADC 查各自的一维表，
`MACHINE=3` 使用三路独立升/降温表并融合温度。机器1、2、6读取 NTC2 ADC；机器3使用三路ADC。
`getTemp()` 和站1摘要自动使用所选表，范围检查也随所选表切换。
拟合温度在[97.9℃, 98.0℃)时返回98℃，达到98℃及以上保留计算值。
NTC1 独立接口不受此宏影响。
其他宏值会报编译错误。

### 机器2最新标定（2026-10-14）

机器2现已使用上位机日志 `20261014-202806-675654.csv` 重新拟合，替代此前三组
机器2表。采用20:28:57.807模式1应答之后、21:12:11.517全停发送之前的加热段，
仅保留温度/ADC接收时间差≤1秒的NTC事件，直接读取原始ADC和参考真实温度，
不使用该CSV中错误标为MACHINE=1的本地拟合列。共1715点，58个节点，
无需按>20℃残差剔除。表覆盖ADC1691.304～3523.857，拟合端点97.7～35.4℃。
计算温度在[97.9℃, 98.0℃)时返回98℃，≥98℃时保留查表值；低温超范围夹紧
35.4℃。仍不能描述停机后的降温关系。
保留98℃规则后的回代MAE为0.722℃，最大误差7.246℃；
这些只代表本次建表数据，仍需下一次独立实验验证。

复现：

```powershell
python function/ntcTemp/scripts/calibration/calibrate_host.py agent-tp/data/captures/2026-10-14/20261014-202806-675654.csv --write
```

下面的机器2三组数据说明为历史版本，当前以本节和机器2表文件为准。

机器2数据来源：`Desktop/deta/2/2#` 的 `1.csv/1.TXT` 至 `3.csv/3.TXT`，
按同秒配对、剔除0～150℃外真实温度，生成4826个有效点、64个节点。
没有拟合误差超过20℃的点需要剔除。表端范围：ADC1662.250～3657.812，
拟合温度98.2～25.8℃，整数ADC有效范围1663～3657。
未封顶查表的三组回代MAE分别为1.159、0.905、0.714℃，
最大误差分别5.640、7.331、6.792℃；这是建表数据回代结果，并非独立验证精度。

机器2重新生成命令（不覆盖机器1）：

```powershell
python function/ntcTemp/scripts/calibration/calibrate.py 'C:/Users/29546/Desktop/deta/2/2#' --channel 2 --groups 1 2 3 --max-error 20 --write --output ntcTemp_machine2_table.inc
```

### 机器1（默认）标定（2026-10-14）

NTC2 使用 ADC × 有效加热档位二维查表。ADC 取站2 `0x0041/0x0042` 合并后的
uint32；有效档位按站3总开关和加热开关反馈计算，取已开启加热器档位最大值。
档位锚点为 0、9、10、19；档位7/8/9样本合并拟合到锚点9。相邻 ADC 节点和档位锚点
均线性插值。若当前 ADC 不在插值所需曲线的实测范围内，则回退旧一维表。

标定来源为 `agent-tp/data/testdata/ntcdata/261014/231906_ntc2_adc.csv` 与外部温度 TXT 的同秒配对，采用
2026-10-14 20:12:00～21:17:15 窗口，共428个配对秒、49个表节点。该窗口内按时间
每5点留1点验证：二维表 MAE 0.618℃、RMSE 0.914℃、P95 2.150℃、最大误差3.131℃。
这是同一段时间序列的留出验证，不是跨批次独立验证。档位14在配对窗口内没有直接样本，
其他未锚定档位仅作档位插值；曲线 ADC 覆盖范围不重叠处回退旧一维表。

重新解析主站日志并生成表：

```bash
python3 function/ntcTemp/scripts/analysis/extract_ntc2_adc.py agent-tp/data/testdata/ntcdata/261014/231906.log
python3 function/ntcTemp/scripts/calibration/calibrate_2d.py --write
```

原始采集数据（日志/CSV/TXT）按日期放在 `agent-tp/data/testdata/ntcdata/<YYMMDD>/` 下，体积很大，
已在 `.gitignore` 中忽略，不进 Git。

主机 C 接口自测：

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -Itask/DataCenter -Ifunction/ntcTemp \
  -Iagent-tp/tests/firmware/ntc_host agent-tp/tests/firmware/ntc_host/clock.c \
  function/ntcTemp/ntcTemp.c function/ntcTemp/scripts/tests/test_ntcTemp_2d.c -lm -o /tmp/test_ntcTemp_2d
/tmp/test_ntcTemp_2d
```

当前固件表为 `tables/NTC2/ntcTemp_2d_table.inc`；2026-10-12 的一维表
`tables/NTC2/ntcTemp_table.inc` 仍保留作覆盖不足时的回退。

## NTC1 温度接口

```c
float temperature1 = ntcTempGetNtc1Temperature();
```

无参数，内部读取 `g_DataPool.ntc1Raw`，使用相同两组 CSV 的
`站2_NTC1 ADC高字[0x0000～0x0001](ADC)` 与 TXT 真实温度拟合。
配对、无效温度剔除和单调查表方法与 NTC2 相同，使用独立的 60 点表。
表文件为 `tables/NTC2/ntcTemp_ntc1_table.inc`，标定工具通过 `--channel 1` 更新。
整数 ADC 标定范围为 1590～3459，表端温度为 96.2～38.8℃。
超范围返回最近表端温度，范围检查使用 `ntcTempNtc1IsInRange(getNtc1Raw())`。

NTC1 同一 ADC 的真实温度存在较大差异。按绝对误差 >20℃ 剔除 114 个
标定点并重新拟合后，仅靠 ADC 的回代误差如下，
这些数值是建表数据上的误差，不代表独立验证精度：

| 数据组 | 平均绝对误差 | 均方根误差 | 95%绝对误差 | 最大绝对误差 |
| --- | ---: | ---: | ---: | ---: |
| 1 | 4.238℃ | 6.294℃ | 14.059℃ | 18.759℃ |
| 2 | 5.062℃ | 7.444℃ | 16.159℃ | 19.000℃ |

重新生成 NTC1 独立拟合表（文件统一存放在 `tables/NTC2/`）：

```powershell
python function/ntcTemp/scripts/calibration/calibrate.py C:/Users/29546/Desktop --channel 1 --max-error 20 --write
```

`getTemp()` 返回 DataCenter 最新更新的 NTC2 查表温度。NTC1 接口供调用方按需使用。

含温度折线、误差曲线和跨组验证的完整报告：
[NTC1/NTC2 温度对比报告](../../agent-tp/reports/temperature/NTC1_NTC2温度对比报告.html)。
生成命令：`python function/ntcTemp/scripts/analysis/comparison_report.py C:/Users/29546/Desktop`。

函数无参数，内部读取 DataCenter 中站2 `0x0041～0x0042` 合并后的 **uint32 原始 ADC**，对应 CSV
`站2_NTC2 ADC高字[0x0041～0x0042](ADC)` 列，不是只读取高字。
输出单位为 ℃。组件由工程的 `function` 组件目录自动发现。

```c
#include "ntcTemp.h"

float temperature = ntcTempGetTemperature();
```

每次调用读取最新的 `g_DataPool.ntc2Raw` 并换算，不主动发起 Modbus 采样。
需要检查标定范围时，可调用 `ntcTempIsInRange(getNtc2Raw())`（包含 `DataCenter.h`）。

使用组件的 CMakeLists.txt 中添加 `REQUIRES ntcTemp`。DataCenter 的
每次站2采样成功后，DataCenter 用本模块更新 `g_DataPool.temperature`，
`getTemp()` 与站1摘要均返回该 NTC2 查表温度。各路 NTC 原始值保持不变。

## 数据处理

- 来源：用户提供的 `1.csv/1.txt`、`2.csv/2.txt`，分别配对后合并。
- TXT 的有效真实温度为 **0 ≤ T ≤ 150℃**，范围外数据全部剔除。
- 按已确认的规则，用 CSV 的 `站2_最近接收时间` 与 TXT 的时分秒匹配。
  CSV 其他站更新时重复携带的站2快照按完整采样时间去重。同一秒内
  ADC 与有效真实温度各自取平均，不使用最近邻时间匹配或人为时间偏移。
- 第一组：1274 个有效 TXT 读数，剔除 412 个无效读数，配对 1270 秒。
  第二组：1149 个有效 TXT 读数，剔除 377 个无效读数，配对 1146 秒。
- 按 32 ADC 宽度分箱，每箱使用 ADC 均值和温度中位数，再按箱内样本数量
  加权进行单调递减拟合（PAVA），生成 57 个查表节点。运行时二分查找、线性插值。
- 实际有效真实温度覆盖 38.5～98.2℃。表端为 ADC 1651.066 → 97.900℃、
  ADC 3435.033 → 38.800℃。对整数 ADC，已标定范围为 **1652～3435**。
  NTC2 先按表端计算温度；97.9℃ 映射为 98.0℃，而计算值达到 98℃及以上时保留，
  低温端超范围仍返回 38.8℃；超范围时 `ntcTempIsInRange()` 返回 false。
  0～150℃ 是数据有效性筛选范围，不能代表已完成该全范围标定。

## 验证与更新

两组数据回代结果（建表数据上的拟合误差，不是独立验证精度）：

| 数据组 | 样本数 | 平均绝对误差 | 均方根误差 | 95%绝对误差 | 最大绝对误差 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 1270 | 0.252℃ | 0.360℃ | 0.696℃ | 2.043℃ |
| 2 | 1146 | 0.253℃ | 0.353℃ | 0.700℃ | 3.577℃ |

重新生成查表数据（仅使用 Python 标准库）：

```powershell
python function/ntcTemp/scripts/calibration/calibrate.py C:/Users/29546/Desktop --write
```

默认按秒匹配。`--resolution millisecond` 可用于检查严格毫秒配对。
不带 `--write` 只分析，不覆盖表。补充温区后应重新生成并使用独立测量验证。

## 2026-10-29：1150 mL、7档高温升温补充

从 `20261029-233558-561446.csv` 及同名串口日志补入三路NTC的7档升温工况：
加热开5秒/停5秒、开6秒/停4秒，电机1档开3秒/停3秒，水量1150 mL。
筛选电机停满500 ms且新鲜实测RPM=0、参考温度插值间隔≤2秒，接纳92组，
实测覆盖93.16～97.99℃；90～93℃仍是9档，不混入7档表。新增18个节点，保留既有节点。
复现工具和详细报告见
[导入工具](../../agent-tp/tools/temperature/import_gear7_heating.py)、
[筛选与验证报告](../../agent-tp/reports/ntc/2026-10-29_gear7_heating/report.md)。

## 通道独立性约束与运行中趋势采样

每路NTC的ADC历史、温度历史、候选点、可信值、工况曲线选择、98℃降温锁存及清窗都独立。任何NTC的数值、异常、趋势或阶段改变，不能参与另一NTC的判断、限幅或重置；不使用主温度 `g_DataPool.temperature` 为三路统一选表。加热器档位、使能、电源、转速等设备反馈可共享，显式传入的运行参数也可共享。

启用电机采样优化时，运行中样本需通过自身历史的线性ADC趋势门槛：至少8个可信时间点、历史跨度≥140ms、最近点距当前≤2秒、拟合斜率绝对值≤1000 ADC/s、历史拟合残差及当前预测残差均≤40 ADC、相邻实际ADC差≤200。通过者才进入本通道原ADC中值、查表和温度趋势滤波；未通过者不训练可信历史，无历史时也不直接放行。电机停转的原200ms等待及短停采样窗口保留。

DataCenter用 `ntcTempPrimarySampleAccepted()` 区分新接纳值与保持旧值，拒绝的旧值不再推进外层温度滤波或98℃窗口计数。运行中数据仍依赖原有标定曲线，趋势符合不代表运行中标定误差已经实机验证。
