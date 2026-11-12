# 温度标定模块（temp2map）

本模块以 `TempData/2026-10-09-n1-0` 为拟合目标（外部 TXT 传感器为唯一绝对温度参考）。

## 固件集成（ESP32-S3）

- 组件路径：`function/temp2map`（`temperature_mapping.c` + `temperature_mapping.h` 两个文件即完整算法，替换这两个文件即可完成算法迭代）。
- 调用方：`task/DataCenter/DataCenter.c` 的 `station2_temperature_from_adc()`。站2快照轮询周期 20 ms，历史入队按模块标定节拍 `TEMPERATURE_MAPPING_SAMPLE_PERIOD_MS`（900 ms）门控（算法兼容 0.8～1.0 s），间隔超过 `TEMPERATURE_MAPPING_SAMPLE_RESET_MS`（1200 ms）清空重收；实测采样周期作为 `sample_period_ms` 参数传入，算法内部归一化到标定节拍。
- 数据流：读快照 `0x0000～0x0001`（NTC1 原始 ADC 值，uint32，低字为 16-bit ADC count）→ 换算 → 存入 ESP32 专用字段 `g_DataPool.temperature`（`getTemp()` 访问），不写回快照。所有温度消费方（`getTemp()`、DryBoil 防干烧、站号1摘要 `0x000C`）统一读取标定值。NTC2/NTC3/NTCBack 不做换算，快照直接保存原始值。
- 加热档位反馈：站3 输入寄存器 `0x0003/0x0004/0x0005/0x0006/0x0007`（总开关、下/上加热开关、下/上加热档位）由 `poll_power_registers()` 更新到 `g_DataPool`，`station2_heater_gear_effective()` 据此计算有效档位（总开关关闭或两路加热都关闭时为 0，否则取已开启加热器档位的最大值——标定数据中两路加热始终同档位运行）。档位反馈周期 100 ms，与 20 ms 温度轮询异步，档位变化平缓，该延迟可接受。
- `test_temperature_mapping.c` 为独立 PC 自测程序（gcc 编译运行），不参与固件构建。

## 数据规范（强制）

- **CSV 内部温度列禁止用作绝对温度参考**，只能使用外部 TXT 传感器温度。
- TXT 中 `T=-600.0` 的无效样本必须跳过。
- TXT 参考文件编码 gb18030，CSV 编码 utf-8-sig。

## 推荐接口

```c
/* 方向感知换算（带加热档位 2D 补偿），正常控制阶段使用。 */
float temperature_mapping_from_adc_history_gear(
    const uint16_t adc_history[TEMPERATURE_MAPPING_HISTORY_COUNT],
    uint32_t sample_period_ms, int gear);

/* 单点换算（带加热档位 2D 补偿），仅历史不足 20 点时临时使用。 */
float temperature_mapping_from_adc_gear(uint16_t adc_raw, int gear);
```

数据排列要求：

```text
adc_history[0]  = 最旧的ADC样本
adc_history[TEMPERATURE_MAPPING_HISTORY_COUNT - 1] = 当前最新的ADC样本
```

20个值必须来自同一NTC通道、按时间升序连续排列。采样周期契约由
`TEMPERATURE_MAPPING_SAMPLE_PERIOD_MS`（900ms）和
`TEMPERATURE_MAPPING_SAMPLE_RESET_MS`（1200ms）定义：按约900ms周期喂入样本，
相邻采样间隔超过1200ms时必须清空历史并重新收集。

**周期兼容（800–1000ms）**：`temperature_mapping_from_adc_history_gear()` 接受实际
采样周期参数，内部把每样本斜率归一化到标定节拍后再判定方向，因此均匀采样
周期在 800–1000ms 范围内（含 1.0s）方向判定与 0.9s 一致。历史不足20点时可临时调用：

```c
float temperature_mapping_from_adc_gear(uint16_t adc_raw, int gear);
```

单点接口只能使用缓变表，无法识别升温和降温方向，不应作为正常控制阶段的主要接口。

## 加热功率档位 2D 补偿

**补偿语义**：`result = lookup(adc) − comp(gear, T_indicated)`，结果在 0°C 截断。
`comp(gear, T)` 在 档位×温度 网格（6 个档位锚点 × 27 个温度节点，35～100°C，步长 2.5°C）
上做双线性插值。`gear` 超出 `TEMPERATURE_MAPPING_GEAR_MAX`（19）时内部自动截断到 19，
小于 0 截断到 0。

**根因与来源**：标定曲线按满功率升温数据拟合，NTC 靠近加热器，比液体本体温度偏高；
低档位升温缓慢时 NTC 偏高量同时取决于档位和指示温度，接近沸腾时对流消除梯度、偏差消失
（网格每行右边缘收敛）。网格由合并数据集 26-10-10（1728 对，档位 8/9/10，86～101°C）
与 26-10-10-n1-1（6725 对，档位 0/8/9/10/19，36～100°C）拟合；补偿后 RMSE 0.70 /
0.72°C，未补偿为 5.94 / 12.36°C。档位锚点 {0,8,9,10,18,19}；档位 18 无配对数据，
由档位 10 与 19 列内插填充。35～55°C 行全为 0（57.5°C 以下无配对数据，即低温段无补偿）。

**固件取档位**：`g_DataPool.powerEnabled` 为假或两路加热都未开启时有效档位为 0，
否则取已开启加热器反馈档位（`powerHeater1Gear` / `powerHeater2Gear`）的最大值。

## 方向判断

```text
slope = (adc_history[19] - adc_history[0]) / 19
slope < -0.2：升温表
slope > +0.2：降温表
其他：缓变表
```

ADC值随传感器温度升高而降低。阈值 0.2 经扫描验证最优（0.3-1.0 均更差）。
三套表分方向查表后统一叠加加热档位 2D 补偿（见上节）。

## 表数据来源（2026-10-09-n1-0）

- 校准数据：2026-10-09，机器 n1，数据集 0（832 对时间对齐样本）。
- ADC 来源：站2 NTC1 ADC 高字（16-bit 十进制，Col[47]）。
- 参考：外部 TXT 传感器（1.txt）。
- 方法：对 ln(ADC/(4095-ADC)) 分压变换做 4 阶多项式拟合，锚定端点
  （1613→101.2°C，3655→27.3°C），85 条查表条目，25-ADC 间距，线性插值。
- 范围：ADC 1600～3700，温度 11.1°C～101.1°C。
- 升温/缓变/降温三表当前共享同一标定曲线，待分方向数据就绪后替换。

## 回归结果（2026-10-09-n1-0）

| 指标 | 值 |
|---|---|
| 有效对齐样本 | 832 |
| R² | 0.998 |
| 最大误差 | 1.11°C |
| 端点检查 | ADC=1613→100.8°C（实际 101.2，误差 0.4）；ADC=3655→27.3°C（误差 0.0） |

Run3 独立验证（778 对，未参与标定）：
- 80～100°C：RMSE 0.44～1.08°C，平均偏差 ~0°C
- 40～70°C：RMSE 11～27°C（热滞后，NTC 比参考传感器响应快）

## 验证命令

```text
gcc -std=c11 -Wall -Wextra -pedantic temperature_mapping.c test_temperature_mapping.c -lm -o test_temperature_mapping.exe
.\test_temperature_mapping.exe
```
