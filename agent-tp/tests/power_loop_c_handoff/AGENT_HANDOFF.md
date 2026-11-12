# 功率闭环控制算法交接

## 目的与范围

本交接文档和 `power_control.h/.c` 抽取自 `heat_calibrator.py` 的功率控制路径，目标是在嵌入式 C 项目中移植核心计算。代码采用可移植 C99，不包含 Tkinter、串口驱动、仪表协议或具体 MCU HAL；集成方负责取样、计时、下发周波命令和持久化稳定扫描点。

## 输入测量与功率定义

每次控制迭代应使用同一笔有效测量：电压 V（V）、电流 I（A）、有功功率 P（W）。应用侧默认用有功功率 P 作为 PID 反馈；可选以 `I²R` 估算反馈。视在功率 `V×I` 只用于显示，不应替代有功功率控制。

默认周波总数为 100。每次自动扫描开始时重新采集当前电压，作为本次扫描基准电压；不得复用上次满周波电阻标定时的电压。保存扫描点时记录这个启动基准电压，以及该目标点实际读到的电压。历史稳定参数可按最近的基准电压选取，再按电压平方反比修正启动周波：

`cycles = round(saved_cycles × (saved_voltage / current_voltage)²)`

将结果限制在 `[1, total_cycles]`。

## 满周波电阻与最大功率标定

1. 设置加热档位 1、周波数 `total_cycles/total_cycles`，维持 4 秒采样。
2. 至少需要 3 笔 `V > 0, I > 0, P > 0` 的完整测量。
3. 取采样最大功率 `Pmax`，候选平台点为 `P >= 0.9 × Pmax`。
4. 每个候选点计算 `Ri = Pi / Ii²`。计算阻值中位数，只保留与中位数偏差不超过 5% 的样本；有效平台点至少 3 笔。
5. 以保留样本计算加权阻值 `R = ΣP / Σ(I²)`，并分别对 V、I、P 取算术平均。返回平均平台有功功率作为满周波最大功率估计。

启动瞬态或单笔错误读数不应单独决定标定结果。日志应包含有效样本数、平台样本数、平均 V/I/P 和最终 R；平台不足时明确报告失败，不要用配置电阻静默替代测量结果。

本算法假设满周波时加热器近似为电阻负载，且仪表有功功率与测量电流对应同一负载。对明显非线性或随温度显著变阻的负载，R 是本次平台温度下的有效等效值，不是恒定材料参数。

## PID 与输出限制

反馈误差定义为 `error = target - measured_power`。采用全周波估算前馈加比例、积分、微分修正：

- `observed_full_power = measured_power × total_cycles / cycles`
- 满周波估计以 0.5 新值、0.5 旧值平滑。
- `error_fraction = error / estimated_full_power`
- 积分候选值：`clamp(integral + 0.05 × error_fraction × total_cycles, -10, 10)`
- 微分值：本次 `error_fraction - previous_error_fraction`；首笔为 0。
- `raw_cycles = target / estimated_full_power × total_cycles + 0.25 × error_fraction × total_cycles + integral + 0.1 × derivative × total_cycles`
- 四舍五入后限制至 `[0, total_cycles]`。仅当输出未饱和或误差方向有助于退出饱和时，接受积分候选值（anti-windup）。
- 若测量功率 `<= 0.1 W`，从当前周波数增加 1；否则不能建立满周波估算时保持当前周波数。

稳定判定：误差持续在当前容差内达到 4 秒。容差初始为 UI 配置值；仅当误差绝对值不超过 30 W 时统计误差方向反转，超过 3 次后容差增加 10 W，最大为初始容差的 3 倍，并重新开始稳定计时。超出容差即清除稳定计时。

若自动扫描已经达到 `total_cycles`，且反馈功率仍低于 `target - tolerance`，停止后续扫描点，避免继续扫描不可达目标。

## 扫描数据与最大误差

- `扫描最大绝对误差`只在当前目标稳定满 4 秒后更新，使用该稳定确认读数的 `abs(error)`。
- 未稳定过程读数、超时保存的最近点、最大周波不可达点均不得计入该最大值。
- 若产品需要记录未稳定点用于诊断，应与“稳定扫描点”分开标记；它们不能参与稳定配置档案或稳定最大误差。

## C API 集成顺序

1. 调用 `pc_calibrate_resistance()` 处理满周波采样；失败时不得开始 I²R 控制或扫描。
2. 每次扫描调用前读取当前电压，并在应用层将它作为本次扫描固定基准电压。
3. 每笔迭代通过 `pc_pid_step()` 获得下一周波值；测量轮询间隔和时间戳由设备应用负责。
4. 调用 `pc_stability_update()` 更新目标稳定状态；只有返回稳定时才调用 `pc_scan_max_error_update()`。
5. 调用 `pc_full_output_below_target()` 判断是否达到满周波但功率仍不足。
6. 控制退出、异常或手动停止时，应用层必须将周波数清零。

## 自测

在本目录执行：

```sh
cc -std=c99 -Wall -Wextra -Werror -pedantic power_control.c power_control_selftest.c -lm -o power_control_selftest
./power_control_selftest
```

Windows 可使用支持 C99 的 GCC/Clang；对应执行文件名改为 `power_control_selftest.exe`。自测只验证确定性算法，不替代连接实际 NB 控制器与功率仪表的硬件验收。
