# Modbus RTU 寄存器说明

- 文档标识：从站 02
- 接口：USART3，PB10(TX)/PB11(RX)，经 RS485 收发器，1000000 8N1
- 从站地址：2（广播地址 0 仅执行写操作，不应答）
- 字节序：寄存器高字节在前；CRC 低字节在前
- 支持功能码：01、02、03、04、05、06、0F、10
- 方向控制：PB2（高=发送 / 低=接收），软件 GPIO 翻转，无硬件 DE/RE
- 固件源码：`task/modbusTask.c`、`task/modbusTask.h`
- 版本定义：`project/inc/wk_debug.h`（FW 0.0.0.1 / HW 1.0.0.0 / PROTO 1.0.0.0）

> 📌 本文档描述 AT32F415RBT7（V2 主板）的 Modbus 寄存器布局。0x0000~0x0027（输入寄存器）、0x0000~0x0009（线圈）、0x0000~0x000F（离散输入）、0x0000~0x0003（保持寄存器）与 `ESP32S3/docs/站号2_AT32F415CBT7_Modbus协议.md` 完全一致；0x0028~0x004F 为 V2 追加功能。0x004F 原为保留固定 0，现承载离散输入高位图。

## 常用串口测试命令（快速查看）

串口设置为 **1000000、8N1、HEX 发送、不追加回车换行**。

| 用途 | 发送命令 |
|---|---|
| 读取全部 10 个线圈 | `02 01 00 00 00 0A BC 3E` |
| 读取全部 16 个离散输入 | `02 02 00 00 00 10 79 F5` |
| 读取全部 18 个离散输入 | `02 02 00 00 00 12 F8 34` |
| **一次读取全部状态和信息（文档标准 40 个）** | `02 04 00 00 00 28 F0 27` |
| 一次读取原有 80 个输入寄存器（含 V2 追加） | `02 04 00 00 00 50 F0 05` |
| 读取前 7 个保持寄存器 | `02 03 00 00 00 07 04 3B` |
| 开启 LED | `02 05 00 04 FF 00 CD C8` |
| 关闭 LED | `02 05 00 04 00 00 8C 38` |
| 清水泵速度设为 1000 | `02 06 00 00 03 E8 89 47` |
| 停止清水泵 | `02 06 00 00 00 00 89 F9` |

> 泵和电机命令会实际驱动硬件。测试前请确认设备及机械结构处于安全状态。

## 线圈（01/05/0F）

| 协议地址 | 控制对象 | 0 | 1 |
|---:|---|---|---|
| 0x0000 | 清水泵 | 停止 | 100% 运行 |
| 0x0001 | 点卤泵 | 停止 | 运行 |
| 0x0002 | 蠕动泵 | 停止 | 按保持寄存器 0x0001 的已配置方向/速度运行；若为 0，默认正转 90 档 |
| 0x0003 | 阀门 | 关闭 | 开启 |
| 0x0004 | LED | 关闭 | 开启 |
| 0x0005~0x0007 | 预留执行器 | 仅保存状态 | 仅保存状态 |
| 0x0008 | 压制电机命令状态 | 停止 | 运行（由保持寄存器 0x0002 更新） |
| 0x0009 | 抬升电机命令状态 | 停止 | 运行（由保持寄存器 0x0003 更新） |

压制和抬升电机的方向及停止命令由保持寄存器 `0x0002`、`0x0003` 控制。线圈 `0x0008`、`0x0009` 仅表示对应速度命令是否非零，不用于选择电机方向。

## 离散输入（02）

0x0000~0x0011 依次为：抬升合并高位、抬升合并低位、压制高位、压制低位、压制头在位、水箱在位、水箱高液位、水箱低液位、废水箱在位、阀门开到位、阀门关到位、点卤盒在位、溢出报警、上盖在位、抬升左高实际、抬升右高实际、点卤盒低位（D0 `brineLowPos`）、成型通道在位（D1 `modeAvail > 0`）。

| 协议地址 | 内容 | 信号来源 |
|---:|---|---|
| 0x0000 | 抬升合并高位 | `riseMoterHighPositionGet()`；上行运行时左右均到位（锁存），静止时两侧开关同时触发 |
| 0x0001 | 抬升合并低位 | `riseMoterLowPositionGet()`；编码器下行到位判断 |
| 0x0002 | 压制高位 | `pressMoterHighPositionGet()` |
| 0x0003 | 压制低位 | `pressMoterLowPositionGet()` |
| 0x0004 | 压制头在位 | TMUX D0 `pressaAvail` |
| 0x0005 | 水箱在位 | TMUX D0 `waterTankAvail` |
| 0x0006 | 水箱高液位 | TMUX D0 `waterHighPos` |
| 0x0007 | 水箱低液位 | TMUX D2 `waterPos` > 2800 |
| 0x0008 | 废水箱在位 | TMUX D0 `wasteBoxAvail` |
| 0x0009 | 阀门开到位 | TMUX D0 `valveFault`==0 且电机运行且方向为开 |
| 0x000A | 阀门关到位 | GPIO `valveOFFPos_PIN` |
| 0x000B | 点卤盒在位 | TMUX D0 `brineAvail` |
| 0x000C | 溢出报警 | TMUX D2 `overFlow` |
| 0x000D | 上盖在位 | TMUX D0 `lidAvail` |
| 0x000E | 抬升左高实际状态 | `riseMoterHighLeftPositionGet()`；开关 PF6（原抬升高位开关） |
| 0x000F | 抬升右高实际状态 | `riseMoterHighRightPositionGet()`；开关 PA15（原抬升低位开关） |
| 0x0010 | 点卤盒低位 | TMUX D0 `brineLowPos`；非零为有效 |
| 0x0011 | 成型通道在位 | TMUX D1 `modeAvail`；ADC 原始值大于 0 为有效 |

## 输入寄存器（04）——全部状态快照

使用 `02 04 00 00 00 28 F0 27` 一次读取 `0x0000~0x0027` 共 40 个输入寄存器（文档标准区）。固件在处理该请求时先集中采集数据，再从同一份快照生成应答；主站无需分别读取线圈、离散输入和保持寄存器即可取得设备全部实时状态、底层输入与主要诊断信息。

使用 `02 04 00 00 00 50 F0 05` 可读取全部 80 个输入寄存器（含 V2 追加的 `0x0028~0x004F`）；离散输入高位图位于 `0x004F`，其中 bit0/bit1 分别对应 FC02 地址 `0x0010`/`0x0011`。

### 0x0000~0x0027（文档标准，共 40 个）

| 协议地址 | 内容 |
|---:|---|
| 0x0000~0x0001 | NTC1 原始 ADC 值，uint32，高字在前 |
| 0x0002~0x0003 | 母线电压 float32，高字在前 |
| 0x0004~0x0005 | 电流 float32，高字在前（母线电流） |
| 0x0006 | 抬升电机设定值镜像（保持寄存器 0x0003） |
| 0x0007~0x0008 | 累计圈数 float32，高字在前（57 电机角度/360） |
| 0x0009 | 设备状态字，位定义见下表 |
| 0x000A | 故障码，位定义见下表 |
| 0x000B~0x000C | 固件版本 {主, 次, 修订, 构建}，当前 0.0.0.1 |
| 0x000D | 保留 |
| 0x000E | 离散输入低位图，bit0~bit15 对应离散输入 0x0000~0x000F |
| 0x000F | 全部 10 个线圈命令状态位图，bit0~bit9 对应线圈 0x0000~0x0009 |
| 0x0010 | 清水泵速度命令镜像（保持寄存器 0x0000） |
| 0x0011 | 蠕动泵速度命令镜像（保持寄存器 0x0001） |
| 0x0012 | 压制电机有符号速度命令镜像（保持寄存器 0x0002） |
| 0x0013 | 抬升电机有符号命令镜像（保持寄存器 0x0003） |
| 0x0014 | 队列深度：高字节为旧协议命令队列（V2 固定为 0），低字节为 Modbus 接收队列 |
| 0x0015~0x0016 | Modbus 接收队列溢出累计次数，uint32，高字在前 |
| 0x0017~0x0018 | Modbus CRC 错误累计次数，uint32，高字在前 |
| 0x0019~0x001A | Modbus 帧错误累计次数，uint32，高字在前 |
| 0x001B~0x001C | 旧协议命令队列溢出累计次数，uint32，高字在前（V2 固定为 0） |
| 0x001D | TMUX1308 D0 的 8 路原始数字输入位图，bit0~bit7 对应通道 0~7 |
| 0x001E | TMUX1308 D1 的 8 路原始数字输入位图，bit0~bit7 对应通道 0~7 |
| 0x001F~0x0026 | TMUX1308 通道 0~7 的原始 12 位 ADC 值（D2 组） |
| 0x0027 | 板载电流采样 ADC 原始值 |

设备状态字 `0x0009`：

| 位 | 含义 |
|---:|---|
| bit0 | Modbus 协议已就绪，正常运行时为 1 |
| bit1 | 存在非零执行器命令 |
| bit2 | 至少一个到位、在位、液位或报警输入有效 |
| bit3 | 存在通信诊断异常计数 |
| bit4~bit15 | 保留 |

故障码 `0x000A`：bit0 为 Modbus 接收队列溢出，bit1 为 CRC 错误，bit2 为帧错误，bit3 为旧协议命令队列溢出；对应累计计数非零时置位。

### 0x0028~0x004F（V2 追加，共 40 个）

V2 主板特有功能，追加在文档标准区之后。其他型号（如 ESP32S3）不实现此区间，读取时返回 0。

| 协议地址 | 内容 | 数据类型 |
|---:|---|---|
| 0x0028~0x002F | 多路电流 float32，高字在前：busAmpere / ledAmpere / brakeAmpere / strainGageAmpere | float32 ×4 |
| 0x0030~0x0037 | 抬升双电机四路电流 float32，高字在前：riseRightB / riseRightA / riseLeftB / riseLeftA | float32 ×4 |
| 0x0038 | 42 抬升电机当前电流幅值（回读） | uint16 |
| 0x0039 | 57 压制电机电流幅值 | uint16 |
| 0x003A | 复位按键电平 | uint16（0/1） |
| 0x003B~0x003E | 编码器位置 int32，高字在前：抬升左 / 抬升右 / 压制右 | int32 ×3 |
| 0x0041~0x0046 | NTC2 / NTC3 / NTCBack 原始 ADC 值，uint32，高字在前 | uint32 ×3 |
| 0x0047~0x004A | 流量计脉冲累计值 uint32，高字在前：清水 / 点卤 | uint32 ×2 |
| 0x004B | 57 绕组 A ADC 零点基准 | uint16 |
| 0x004C | 57 绕组 B ADC 零点基准 | uint16 |
| 0x004D | 57 ADC 采集就绪标志 | uint16（0/1） |
| 0x004E | 57 ADC 采样数 | uint16 |
| 0x004F | 离散输入高位图，bit0/bit1 对应离散输入 0x0010/0x0011（原保留固定 0） | uint16 |

### NTC 温度传感器寄存器汇总

全部 NTC 均通过 TMUX1308 D2 组采集，原始 ADC 值为 12 位。NTC1 单独存于文档标准区，NTC2/NTC3/NTCBack 存于 V2 追加区。此外 `0x001F~0x0026` 的 D2 组通道扫描也包含全部 NTC 的原始值。

> ⚠️ 以下寄存器地址以 ESP32 主站 `ModbusMap.h` 为准。从站文档中编码器区域 `0x003B~0x0040` 占 6 个寄存器（3×int32），NTC2/NTC3/NTCBack 的 uint32 起始地址相应为 `0x0041`/`0x0043`/`0x0045`。

| 寄存器地址 | NTC | TMUX通道 | 内容 | 数据类型 |
|---:|---|---:|---|---|
| 0x0000~0x0001 | NTC1（筒壁） | 6 | 原始 ADC 值（ESP32 读取此寄存器换算为温度，存入专用字段，快照保持原始值） | uint32 |
| 0x001F | NTC2（筒壁） | 0 | 原始 12 位 ADC 值 | uint16 |
| 0x0020 | NTC3（筒壁） | 1 | 原始 12 位 ADC 值 | uint16 |
| 0x0021 | NTCBack（预留） | 2 | 原始 12 位 ADC 值 | uint16 |
| 0x0025 | NTC1（筒壁） | 6 | 原始 12 位 ADC 值（TMUX D2 组扫描，与 0x0000~0x0001 同源） | uint16 |
| 0x0041~0x0042 | NTC2（筒壁） | 0 | 原始 ADC 值（不换算，快照保持原始值） | uint32 |
| 0x0043~0x0044 | NTC3（筒壁） | 1 | 原始 ADC 值（不换算，快照保持原始值） | uint32 |
| 0x0045~0x0046 | NTCBack（预留） | 2 | 原始 ADC 值（不换算，快照保持原始值） | uint32 |

> 📌 ESP32 主站读取快照 `0x0000~0x0001`（NTC1 原始 ADC 值，uint32），经 `function/temp2map` 标定模块换算为温度后存入 ESP32 专用字段 `g_DataPool.temperature`（通过 `getTemp()` 访问），不写回快照寄存器。快照 `0x0000~0x0001` 保持从站原始值。NTC2/NTC3/NTCBack 不做换算，快照直接保存和显示原始值。`0x001F~0x0026` 为 D2 组全通道原始扫描，包含全部 NTC 的 12 位 ADC 值。

### FC04 波形窗口（0x1F40~0x33FF，只读，不占用快照）

57 绕组 A/B 的 40 kHz ADC 波形通过 FC04 高地址窗口回传，不塞进 80 个输入寄存器的快照区。窗口逻辑地址 = `8000 + 偏移`。

| 窗口地址 | 内容 |
|---:|---|
| 0x1F40（8000） | 采样数（未启动采集时返回 `STEPPER57_ADC_CAPTURE_MAX_SAMPLES`=1024） |
| 0x1F41（8001） | ADC 采集就绪标志 |
| 0x1F42（8002） | 57 绕组 A ADC 零点基准 |
| 0x1F43（8003） | 57 绕组 B ADC 零点基准 |
| 0x1F44~0x2337（8004~9007） | 57 绕组 A 的 1024 个 ADC 采样点 |
| 0x2338~0x332B（9008~13099） | 57 绕组 B 的 1024 个 ADC 采样点 |

窗口大小 8200（`0x2008`），超出窗口返回异常码 02（非法地址）。

## 保持寄存器（03/06/10）

| 协议地址 | 内容 | 范围/行为 |
|---:|---|---|
| 0x0000 | 清水泵速度 | 0~1000，0 停止；映射到 0~100 档（value/10） |
| 0x0001 | 蠕动泵速度 | `int16_t`；-100~100 档（百分比），正数正转，负数反转，0 停止；越界自动限幅 |
| 0x0002 | 压制电机有符号速度 | `int16_t`；正数调用 Up，负数调用 Down，0 调用 Stop |
| 0x0003 | 抬升电机有符号命令 | `int16_t`；正数调用 Up，负数调用 Down，0 调用 Stop；当前数值大小不参与速度控制 |
| 0x0004~0x0066 | 参数/预留区 | 保存于 RAM，上电清零，不驱动硬件 |

寄存器 `0x0001`、`0x0002`、`0x0003` 均按 16 位二进制补码解释。例如 `+23 = 00 17`、`-23 = FF E9`、停止为 `00 00`。`0x0003` 写 0 当前输出停止命令；停止和驱动器自锁是不同状态，是否具备机械保持力由抬升电机驱动实现决定。

线圈 `0x0002` 置 1 会按保持寄存器 `0x0001` 当前的有符号速度启动蠕动泵（范围限幅为 -100~100）；若该寄存器为 0，则使用默认正转 90 档。置 0 始终停止蠕动泵。

示例：读取从站 2 的前 7 个保持寄存器，请求帧为 `02 03 00 00 00 07 04 3B`。

## 串口测试命令

串口工具请选择"HEX 发送"，不要勾选自动追加回车换行。下列命令均已包含 Modbus CRC。设备上电后，建议先执行只读命令，再执行 LED 测试，最后测试泵和电机。

> ⚠️ V2 波特率为 **1000000**（1 Mbit/s），与 ESP32S3 的 115200 不同。串口工具需相应设置。

### 只读测试

| 测试内容 | 发送帧 | 预期结果 |
|---|---|---|
| 读取全部 10 个线圈 | `02 01 00 00 00 0A BC 3E` | 应答格式：`02 01 02 XX XX CRC_L CRC_H` |
| 读取全部 16 个离散输入 | `02 02 00 00 00 10 79 F5` | 应答格式：`02 02 02 XX XX CRC_L CRC_H` |
| 读取全部 18 个离散输入 | `02 02 00 00 00 12 F8 34` | 应答格式：`02 02 03 XX XX XX CRC_L CRC_H` |
| 一次读取全部状态和信息（文档标准 40 个） | `02 04 00 00 00 28 F0 27` | 应答以 `02 04 50` 开头，后跟 80 字节快照数据和 CRC |
| 一次读取全部 80 个输入寄存器（含 V2 追加） | `02 04 00 00 00 50 F0 05` | 应答以 `02 04 A0` 开头，后跟 160 字节快照数据和 CRC |
| 读取前 7 个保持寄存器 | `02 03 00 00 00 07 04 3B` | 应答以 `02 03 0E` 开头，后跟 14 字节数据和 CRC |

读取线圈时，数据位从低位开始排列。例如线圈 3、4 开启，其余关闭时，前一个数据字节为 `18`，完整应答为：

```text
02 01 02 18 00 F7 FC
```

输入寄存器中的浮点数使用两个寄存器表示。以应答数据 `41 CC 00 00` 为例，按 IEEE754 大端字节序解析结果为 `25.5`。

### LED 安全测试

```text
开启 LED：02 05 00 04 FF 00 CD C8
关闭 LED：02 05 00 04 00 00 8C 38
```

功能码 05 正常时原帧返回，因此开启 LED 的预期应答同样是：

```text
02 05 00 04 FF 00 CD C8
```

### 单个保持寄存器测试

下面两条命令控制清水泵，测试前请确认设备允许泵运行：

```text
清水泵速度设为 1000：02 06 00 00 03 E8 89 47
清水泵停止：          02 06 00 00 00 00 89 F9
```

正常应答与发送帧完全相同。可以随后发送 `02 03 00 00 00 07 04 3B`，确认保持寄存器值是否更新。

### 多线圈测试

以下命令一次写入线圈 0～4，数据 `0x18` 表示开启阀门和 LED，关闭三个泵：

```text
发送：02 0F 00 00 00 05 01 18 2F 49
应答：02 0F 00 00 00 05 95 FB
```

恢复关闭状态可发送：

```text
02 0F 00 00 00 05 01 00 2F 43
```

### 多保持寄存器测试

以下命令同时写入 4 个保持寄存器：清水泵 `1000`、蠕动泵 `80`、压制电机上行 `+500`、抬升电机上升 `+1`。该命令会实际启动多个执行器，请确保机械部分处于安全状态。

```text
发送：02 10 00 00 00 04 08 03 E8 00 50 01 F4 00 01 9C A2
应答：02 10 00 00 00 04 C1 F9
```

停止相关执行器：

```text
02 10 00 00 00 04 08 00 00 00 00 00 00 00 00 F5 7B
```

压制和抬升寄存器写 `0` 均表示停止。

### 异常和通信测试

| 测试内容 | 发送帧 | 预期结果 |
|---|---|---|
| 读取不存在的线圈地址 10 | `02 01 00 0A 00 01 DD FB` | `02 81 02 31 91`，异常码 02：非法地址 |
| 读取超出范围的输入寄存器地址 80 | `02 04 00 50 00 01 31 E8` | `02 84 02 D2 A0`，异常码 02：非法地址 |
| 使用错误从站地址 1 | `01 01 00 00 00 01 FD CA` | 无应答 |
| 故意修改任意 CRC 字节 | 例如将正确帧末字节改为 `00` | 无应答，CRC 错误计数增加 |
| 广播开启 LED | `00 05 00 04 FF 00 CC 2A` | LED 开启，但设备不应答 |

异常码定义：

| 异常码 | 含义 |
|---:|---|
| 01 | 非法功能码（不支持的功能码） |
| 02 | 非法地址（超出寄存器范围） |
| 03 | 非法数值（数量为 0、超限或字节计数不匹配） |

如果串口没有应答，依次检查：PB10/PB11 是否正确连接 RS485 收发器、两端是否共地、波特率是否为 1000000、是否以 HEX 模式发送、CRC 是否按低字节在前发送。

### DMA 数据重同步测试

接收解析器会在一次 DMA 数据中逐字节搜索有效帧。可在正确命令前加入乱码，验证设备仍能找到并响应后面的 Modbus 帧：

```text
AA 55 FF 02 01 00 00 00 0A BC 3E
```

也可以在一次发送中放入两条完整命令，设备应连续返回两帧应答：

```text
02 05 00 04 FF 00 CD C8 02 05 00 04 00 00 8C 38
```

搜索顺序为：检查站号 → 检查功能码 → 计算候选长度 → 校验 CRC。任何检查失败都只后移一个字节；找到有效帧后，跳过整帧并继续搜索后续数据。

---

# 通信协议实现说明

> 以下内容合并自原 `通信协议说明.md`（v2.1.0）：仅保留**现行 Modbus RTU 从站与
> UART5 调试口**的实现说明。旧屏幕协议历史章节与重复的寄存器映射表未收入，
> 寄存器映射一律以本文档上半部分为准。

## 硬件链路总览

固件有两条物理链路，**两条都已启用**，各司其职：

| 链路 | 引脚 | 状态 | 协议 | 实现文件 |
|---|---|---|---|---|
| **USART3**（RS485） | PB10=TX, PB11=RX, **PB2=RE/DE** | ✅ **启用** | **标准 Modbus RTU 从站**（地址 2） | `task/modbusTask.c` / `task/modbusTask.h` |
| **UART5**（调试口） | PC12=TX, PD2=RX | ✅ **启用** | **AT 风格调试指令 + 心跳/版本打印** | `app/usrDebug.c` / `app/usrDebug.h` |

**UART5 调试口当前功能（读代码可复核）：**

- `project/src/main.c` 调用 `wk_uart5_init()` 配置 UART5 外设（PC12/PD2 复用、115200 8N1），随后调用 `usrDebugInit()` 把 DMA1_CHANNEL2 重配为本地接收缓冲区并重启；
- `project/src/at32f415_int.c` 的 `UART5_IRQHandler()` 在 IDLE 标志置位时调用 `usrDebugUart5IdleIsr()`，该函数关 DMA → 读长度 → 清 IDLE → 入帧队列 → 重载重启，与 USART3 的 `modbusUsart3IdleIsr()` 模式一致；
- `app/usrDebug.c` 实现 AT 风格指令解析（`NB+` 前缀，`=` 分隔键值，大小写不敏感），指令清单见 `app/usrDebug.h`；
- `project/src/wk_debug.c` 通过 `fputc` 把 `printf` 重定向到 UART5，输出心跳（`HEARTBEAT:[时间戳]`，每 5 秒）、版本号、`log_d` 日志；
- 旧 `app/modbus.c` / `app/hostif.c` 已删除，符号不再存在。

```
当前固件通信链路：

  PC / 上位机 ──(RS485 A/B)──► [SP3485/MAX3485 收发器] ──► PB11(RX) / PB10(TX)
                                        ▲
                                   PB2 (RE/DE, 高=发 低=收)
                                        │
                                   AT32F415RBT7  ── Modbus RTU 从站（地址 2）

  PC / 串口助手 ──(TTL/USB)──► PC12(RX) / PD2(TX)
                                   AT32F415RBT7  ── UART5 调试口（115200 8N1）
                                     ├─ printf 输出：心跳 / 版本号 / log_d 日志
                                     └─ AT 指令收发：NB+xxx=yyy（见 app/usrDebug.h）
```

> 🖥️ **参考上位机**：仓库内 `tools/motor57_gui.py` 已迁移为标准 Modbus RTU 主站
> （地址 2、CRC `0xA001`、默认 1000000、`|>f` 浮点解析），波特率可在
> 9600 ~ 1000000（含 1M）之间选择，**必须与固件保持一致**，可直接用于联调。

## USART3 RS485 Modbus RTU 从站

### 物理层参数

| 项目 | 值 | 代码位置 |
|---|---|---|
| 串口 | USART3 | `task/modbusTask.c` |
| 引脚 | PB10 = TX，PB11 = RX（`GPIO_MODE_MUX`） | `project/src/wk_usart.c: wk_usart3_init()` |
| 方向控制 | **PB2**（`USART3SEL_PIN` / `USART3SEL_GPIO_PORT`），**高 = 发送，低 = 接收** | `task/modbusTask.c: rs485TransmitEnable(bool)` |
| 波特率 | **1000000**（1 Mbit，AT32 Workbench 生成值） | `project/src/wk_usart.c: usart_init(USART3, 1000000, ...)`；`code.ATWP` 里 `USART3/BaudRate=1000000` |
| 数据格式 | 8 数据位，1 停止位，**无校验**（8N1） | `wk_usart.c` + `usart_parity_selection_config(..., USART_PARITY_NONE)` |
| 流控 | 无 | `wk_usart.c` |
| 从站地址 | **2**（`MODBUS_SLAVE_ADDRESS`）；同时接受广播地址 0 | `task/modbusTask.c` |
| 接收方式 | DMA1_CH1（**循环模式**）+ USART IDLE 中断 | `project/src/wk_dma.c` / `at32f415_int.c` |
| 发送方式 | 阻塞式（轮询 `USART_TDBE_FLAG` / `USART_TDC_FLAG`） | `task/modbusTask.c: modbusRs485Send()` |

> ⚠️ **波特率必须三处同步**：`project/src/wk_usart.c` 的 `usart_init(USART3, ...)`（真正的分频值）、
> `code.ATWP` 的 `USART3/BaudRate`（否则 AT32 Workbench 重新生成工程会覆盖回默认值）、
> 以及 `task/modbusTask.c` 的 `MB_BAUD_RATE`（只用于下面 RS485 方向脚的三字符延时）。
> 三者不一致的典型后果：`MB_BAUD_RATE` 偏大 → 延时不够 → **末字节被收发器截断**。
>
> 上位机下拉框可选 9600 ~ 1000000（含 1M）。在 APB1 = 75 MHz、16 倍过采样下，
> 1 Mbaud 的分频值恰为 75（**误差 0%**），921600 实际为 925926（+0.47%）——
> 想提速可两人一起改到 1M，但固件不改则连不上。

**重要：AT32F415 没有硬件 RS485 DE 自动控制**（SDK 无 `usart_rs485_*` / DEM API），
方向脚必须用普通 GPIO 翻转。发送时序要求：

1. 拉高 PB2；
2. 逐字节写 `USART3->dt`；
3. **等 `USART_TDC_FLAG`（发送完成）置位**；
4. **再额外延时 3 个字符时间**（8N1 → 10 bit/字符）；
5. 才能拉低 PB2。

若省略第 3、4 步，**末字节会被收发器截断**——这是 RS485 半双工最常见的翻车点。
（代码 `rs485DelayThreeCharacters()`：忙等 `(system_core_clock / MB_BAUD_RATE) * 30` 个周期，
当前 1000000 下约为 `150e6 / 1000000 * 30 = 4500` 个周期 ≈ 30 µs。）

### 支持的 Modbus 功能码

| 功能码 | 名称 | 数量上限检查 | 起始地址检查 |
|---|---|---|---|
| `0x01` | 读线圈（Read Coils） | `1 ≤ qty ≤ 2000` | `addr + qty ≤ 16` |
| `0x02` | 读离散输入（Read Discrete Inputs） | `1 ≤ qty ≤ 2000` | `addr + qty ≤ 18` |
| `0x03` | 读保持寄存器（Read Holding Registers） | `1 ≤ qty ≤ 125` | `addr + qty ≤ 128` |
| `0x04` | 读输入寄存器（Read Input Registers） | `1 ≤ qty ≤ 125` | `addr + qty ≤ 80` |
| `0x05` | 写单个线圈（Write Single Coil） | 值须为 `0x0000` 或 `0xFF00` | `addr < 16` |
| `0x06` | 写单个保持寄存器（Write Single Register） | — | `addr < 128` |
| `0x0F` | 写多个线圈（Write Multiple Coils） | `1 ≤ qty ≤ 1968`，且 `byteCount == ceil(qty/8)` | `addr + qty ≤ 16` |
| `0x10` | 写多个保持寄存器（Write Multiple Registers） | `1 ≤ qty ≤ 123`，且 `byteCount == qty*2` | `addr + qty ≤ 128` |

**异常应答**（`0x80 | 功能码` + 异常码）：

| 异常码 | 含义 | 触发条件 |
|---|---|---|
| `0x01` | ILLEGAL FUNCTION | 功能码不在上表 |
| `0x02` | ILLEGAL ADDRESS | 地址 + 数量越界 |
| `0x03` | ILLEGAL VALUE | 数量为 0 / 超上限 / byteCount 不符 / 线圈值非法 |

**广播（地址 0）**：正常执行但**不返回应答**（`processRequest()` 返回 0）。

### CRC

标准 Modbus CRC16：

- 多项式 `0xA001`（反向表示），初值 `0xFFFF`
- 逐字节、每字节 8 次移位
- **低字节在前**（小端传输）

参考实现（`task/modbusTask.c: Modbus_CRC16()`）：

```c
uint16_t Modbus_CRC16(const uint8_t *data, uint16_t length)
{
  uint16_t crc = 0xFFFFU;
  uint8_t bit;
  while(length-- != 0U)
  {
    crc ^= *data++;
    for(bit = 0U; bit < 8U; bit++)
      crc = (crc & 1U) != 0U ? (uint16_t)((crc >> 1) ^ 0xA001U)
                             : (uint16_t)(crc >> 1);
  }
  return crc;
}
```

Python 等价实现（上位机校验用）：

```python
def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc & 0xFFFF
```

CRC 校验范围为**除末尾 2 字节 CRC 外的整帧**（地址 + 功能码 + 数据）：
`Modbus_CRC16(&data[offset], frameLength - 2)`。

### 接收与拆帧机制（**与常规 RTU 从站不同，务必注意**）

固件**不依赖 Modbus t3.5 静默间隔**，理由：USART 的 IDLE 中断只代表 **1 个字符时间**的静默，
不等于 t3.5。因此采用"**按功能码推算长度 + 累积拼帧**"策略：

1. `modbusUsart3IdleIsr()`（由 `USART3_IRQHandler` 调用）：
   - DMA1_CH1 **保持循环（circular）运行，不停止**；
   - 用 `dtcnt` 反算当前写指针 `currentPos = 256 - dtcnt`；
   - 与上次位置 `usart3DmaLastPos` 求差得**本次新增字节数**（处理回绕）；
   - 把新增数据交给 `Modbus_RxIdleCallback()`，然后更新 `usart3DmaLastPos`。
2. `Modbus_RxIdleCallback()`：
   - 把数据追加到 `rxAssembleBuffer`（累积缓冲）；
   - 逐字节搜索：地址不是 2 且不是 0、或功能码不支持 → 左移 1 字节重新同步，`modbusFrameErrorCount++`；
   - 按功能码算期望长度（`requestLength()`）：`0x01/02/03/04/05/06` → **固定 8 字节**；
     `0x0F/0x10` → **`9 + byteCount` 字节**；
   - 长度不足 → 保留，等下一个 IDLE 段继续累积；
   - 长度足够 → 拷贝到 `rxQueue` 的一个槽位，并把**槽位号**（`uint16_t`）投递到
     FreeRTOS 队列 `modbusData_handle`。
3. `my_task01_func()`（`task/modbusTask.c`，**强符号，覆盖 `freertos_app.c` 的 `__WEAK` 版本**）：
   - 阻塞 `xQueueReceive(modbusData_handle, &slot, portMAX_DELAY)`；
   - 取出后调 `processBurst()` → 校验 CRC → `processRequest()` → `modbusRs485Send()` 应答。

> 💡 **为什么传槽位号而不是指针**：`rxQueue` 槽位在 ISR 内已 `memcpy` 完毕，
> IDLE 回调返回后 DMA 可立即继续接收。传槽位索引（而不是指向 DMA 缓冲区的指针）
> 可确保任务看到的帧永远不会被后续 DMA 写入覆盖。

**队列与缓冲区参数：**

| 参数 | 值 | 说明 |
|---|---|---|
| `MODBUS_RX_BUFFER_SIZE` | 256 | DMA 环形缓冲区 |
| `MB_RX_QUEUE_DEPTH` | 4 | 槽位数（每槽约 258B），`slot ≥ 4` 计为帧错误 |
| `MB_RESPONSE_BUFFER_SIZE` | 256 | 应答缓冲 |
| `modbusData_handle` | `xQueueCreate(1000, sizeof(uint16_t))` | 约 2KB；实际只需 4 深度（**见「已知问题与注意事项」**） |
| `modbusCMD_handle` | `xQueueCreate(8, sizeof(uint16_t))` | 已创建但**全工程未使用**（冗余） |

**诊断计数器**（只读）：

| 变量 | 含义 |
|---|---|
| `modbusRxQueueOverflow` | 槽位/队列满而丢弃 |
| `modbusCrcErrorCount` | CRC 校验失败 |
| `modbusFrameErrorCount` | 地址/功能码非法、长度不符、字节重同步 |

三者都已映射到输入寄存器 16~21（见「输入寄存器（04）」节）。

### 任务与刷新周期

| 任务 | 优先级 | 栈 | 实际实现 | 行为 |
|---|---|---|---|---|
| `my_task01_func`（modbusTask） | 10 | 1024 字 | `task/modbusTask.c`（强符号） | 阻塞等 RTU 帧 → 处理并应答 |
| `my_task02_func`（getState） | 8 | 256 字 | `task/getState.c`（强符号） | **每 20ms** 刷新传感器缓存 |
| `my_task03_func`（actionRunTask） | 6 | 256 字 | `freertos_app.c`（`__WEAK`，空循环） | 空转 |

`task/getState.c` 的 20ms 周期内依次执行：

```c
currentUpdateAll();      /* 7 路电流（含 TMUX 切换） */
getBusVoltage();         /* 母线电压 */
getTMUX1308Data();       /* TMUX 三颗全 8 通道扫描（约 32 次 ADC） */
for (pumpTick = 0; pumpTick < 20; pumpTick++) pumpDecSpdTick();  /* 泵 PWM 斜坡 20ms/步 */
```

> ⚠️ **不要**在 Modbus 应答路径里重复扫 ADC。`buildInputSnapshot()` 只对共享缓存做快照，
> 以避免在 RS485 请求/应答路径里插入长时间 ADC 扫描（会拖长应答时间）。

## 请求/应答长度规则（快速参考）

请求总长（含 CRC）：

| 功能码 | 请求总长 |
|---|---|
| `0x01` `0x02` `0x03` `0x04` `0x05` `0x06` | **8 字节** |
| `0x0F` `0x10` | **9 + byteCount**（byteCount 在请求第 7 字节，偏移 6） |

应答总长：

| 功能码 | 应答总长 |
|---|---|
| `0x01` `0x02` | `5 + ceil(qty / 8)` |
| `0x03` `0x04` | `5 + 2 * qty` |
| `0x05` `0x06` `0x0F` `0x10` | 8（回显请求前 6 字节 + CRC） |
| 异常应答 | 5（地址 + `0x80|FC` + 异常码 + CRC） |

## TMUX1308 通道顺序

三颗 TMUX1308 共用地址线 A0/A1/A2（PC11/PB3/PB4），每个地址同时更新三路数据。

**D0（数字，PB13）`tmux1308Data0Values[]`**（顺序与 `app/tmux1308.h` 枚举一致）

| 索引 | 枚举 | 含义 |
|---|---|---|
| 0 | `pressaAvail` | 压制在位 |
| 1 | `brineLowPos` | 点卤盒低水位（离散输入 0x0010 使用此信号） |
| 2 | `brineAvail` | 卤水盒在位（离散输入 0x000B 使用此信号） |
| 3 | `wasteBoxAvail` | 黄水盒在位 |
| 4 | `valveFault` | 阀故障 |
| 5 | `waterHighPos` | 水位高 |
| 6 | `lidAvail` | 盖子在位 |
| 7 | `waterTankAvail` | 水箱在位 |

**D1（模拟，PC3 / ADC1_IN13）`tmux1308Data1Values[]`**

| 索引 | 枚举 | 含义 |
|---|---|---|
| 0 | `pressMoterFault` | 压制电机故障 |
| 1 | `riseMoterRCurrB` | 右抬升线圈 B 电流 |
| 2 | `riseMoterRCurrA` | 右抬升线圈 A 电流 |
| 3 | `modeAvail` | 成型通道在位 |
| 4 | `riseMoterRFaultR` | 右抬升故障 |
| 5 | `riseMoterRFaultL` | 左抬升故障 |
| 6 | `riseMoterLCurrB` | 左抬升线圈 B 电流 |
| 7 | `riseMoterLCurrA` | 左抬升线圈 A 电流 |

**D2（模拟，PC4 / ADC1_IN14）`tmux1308Data2Values[]`**

| 索引 | 枚举 | 含义 |
|---|---|---|
| 0 | `NTC2` | 预留 NTC（AIN8） |
| 1 | `NTC3` | 预留 NTC（AIN7） |
| 2 | `NTCBacK` | 预留 NTC（AIN6） |
| 3 | `NTC1` | **筒壁 NTC1（AIN4）—— 输入寄存器 0~1 原始 ADC 值即取此通道** |
| 4 | `waterPos` | 水位 AIN9 |
| 5 | `Lid1` | 盖子 1 AIN2 |
| 6 | `overFlow` | 溢流 AIN3 |
| 7 | `Lid2` | 盖子 2 AIN5 |
## NTC 温度换算公式（输入寄存器 0~1 为原始 ADC 值）

输入寄存器 0~1 现直接存储 NTC1 的 12 位原始 ADC 值（uint32，高字在前），不再存储 float 温度。
主站如需温度值，可按以下公式自行换算（`getTemp()` 实现见 `app/Tempurature.c`）：

即 Steinhart–Hart 简化式（B 常数法），参考点 25 ℃ / 10 kΩ，$B = 3950$：

$$R = \frac{10000 \times 4095}{ADC} - 10000 \quad (\Omega)$$

$$T = \frac{1}{\frac{1}{298.15} + \frac{1}{3950}\ln\frac{R}{10000}} - 273.15 \quad (°C)$$

- 上拉电阻 10 kΩ 接 3.3 V，NTC 接地；
- `ADC == 0` 时按 $R = 100\,\mathrm{k}\Omega$ 处理（防除零）；
- $R < 100\,\Omega$ 时截断为 $100\,\Omega$。

## 已知问题与注意事项

**现行 Modbus RTU 侧：**

1. **`modbusData_handle` 队列深度 1000，实际只需 4**：`freertos_app.c` 中
   `xQueueCreate(1000, sizeof(uint16_t))` 占用约 2KB RAM，而槽位号只有 0~3。
   建议改为 4 或 8（芯片仅 32KB RAM）。
2. **`modbusCMD_handle` 已创建但全工程未使用**：8 深度队列为冗余开销。
3. **`Modbus_Poll()` 是死代码**：`my_task01_func` 直接读队列，未调用它，链接器已将其移除。
   修改时注意别误以为它在跑。
4. **地址 3~12 的线圈写 0 不产生反向动作**（开/关拆成两个地址，见「线圈」节警告）。
5. **保持寄存器 12/13 写入无任何效果**（预留位，但会被存进 `holdingRegs[]` 并被回读）。
6. **`holdingRegs` 为 128 项但仅 0~20 有语义**：写入 21~127 会静默存值，不报错。
7. **广播地址 0 会被执行但不应答**：上位机无法确认广播是否成功，需靠后续读回校验。
8. **无通信超时看门狗**：若主站停止轮询，执行器会**保持最后状态**，不会自动停止。
   安全上建议上位机做心跳，必要时主动发停止指令。
9. **`rs485DelayThreeCharacters()` 是忙等烧 CPU**：1000000 下约等价于 `150e6/1000000*30 ≈ 4500`
   次空转（≈300 µs）。应答期间会短暂阻塞 `my_task01_func`（不影响其他任务）。
   若把波特率改高，该延时自动缩短；但 `MB_BAUD_RATE` 忘记同步会变成**延时不足 → 末字节被截断**。
10. **`requestLength()` 只支持已知功能码**：若上位机发其他功能码，会因 `supportedFunction()`
    返回 0 而被当作噪声逐字节丢弃（`modbusFrameErrorCount++`），不会回异常码。
11. **接收不校验 t3.5**：靠"长度 + CRC"判定帧边界，上位机须严格保证帧间静默（见「接收与拆帧机制」节）。

### 波形采集的已知限制


1. **时间轴不是绝对时刻**：高地址窗口只回传原始 ADC，**不包含每点的采样时间戳**。
   上位机用固定 `t = n / 40000 s` 反推，前提是 TMR4 在整段采集期间没有停摆/被抢占。
   若要严格时基，需在固件里一并回传 TMR4 计数或起始 tick。
2. **采集缓冲是单份、被覆盖式**：每次写保持寄存器 14 都会从头重采，
   旧数据在下载完成前就可能被新一轮采集覆盖 —— 自动化脚本里必须串行（启动 → 等就绪 → 下载）。
3. **下载期间不要并发读其他寄存器**：上位机对串口加锁（`ModbusRtuMaster.lock`）故不会错帧，
   但每帧都要一次往返，会拖慢采集；界面「自动采集」因此是"下载完再启动下一轮"。

## 固件侧实现索引（按实际函数名）

**现行 Modbus RTU 从站：**

| 功能 | 文件 | 关键函数 / 符号 |
|---|---|---|
| RTU 初始化（方向脚、DMA 指向、循环模式） | `task/modbusTask.c` | `modbusinit()` |
| RTU 空闲中断（`dtcnt` 增量取字节） | `task/modbusTask.c` | `modbusUsart3IdleIsr()` |
| RTU 累积拼帧 + 长度判定 + 入队 | `task/modbusTask.c` | `Modbus_RxIdleCallback()` |
| RTU CRC16（0xA001） | `task/modbusTask.c` | `Modbus_CRC16()` |
| RTU 帧校验与分发 | `task/modbusTask.c` | `processBurst()` → `processRequest()` |
| RS485 方向控制 + 阻塞发送 | `task/modbusTask.c` | `rs485TransmitEnable()` / `modbusRs485Send()` |
| 输入寄存器快照 | `task/modbusTask.c` | `buildInputSnapshot()` |
| 波形窗口取值（FC04 高地址） | `task/modbusTask.c` | `waveRegValue()` / `readWaveWindow()` |
| 离散输入读取 | `task/modbusTask.c` | `readDiscrete()` |
| 线圈写入 | `task/modbusTask.c` | `writeCoil()` |
| 保持寄存器写入 | `task/modbusTask.c` | `writeHoldingRegister()` |
| RTU 任务主体（阻塞等队列） | `task/modbusTask.c` | `my_task01_func()` |
| 传感器 20ms 刷新 | `task/getState.c` | `my_task02_func()` |
| 队列创建 | `project/src/freertos_app.c` | `freertos_queue_create()` |
| 任务创建 | `project/src/freertos_app.c` | `freertos_task_create()` |
| USART3 IDLE 中断挂接 | `project/src/at32f415_int.c` | `USART3_IRQHandler()` → `modbusUsart3IdleIsr()` |
| USART3 引脚/波特率/IDLE 使能 | `project/src/wk_usart.c` | `wk_usart3_init()` |
| DMA1_CH1/CH2 初始配置 | `project/src/wk_dma.c` + `main.c` | `wk_dma1_channel1_init()` / `wk_dma_channel_config()` |
| 引脚宏定义 | `project/inc/at32f415_wk_config.h` | `USART3SEL_PIN`、`riseMoterRPosL` 等 |

**UART5 调试口（在用）：**

| 功能 | 文件 | 关键函数 |
|---|---|---|
| UART5 引脚/波特率/IDLE 使能 | `project/src/wk_usart.c` | `wk_uart5_init()` |
| DMA1_CH2 重配 + 重载 | `app/usrDebug.c` | `usrDebugInit()` |
| UART5 IDLE 中断挂接 | `project/src/at32f415_int.c` | `UART5_IRQHandler()` → `usrDebugUart5IdleIsr()` |
| AT 指令解析与执行 | `app/usrDebug.c` | `usrDebugAtPoll()` |
| Modbus 统计周期打印 | `app/usrDebug.c` | `usrDebugPrintModbusStat()` |
| printf 重定向 | `project/src/wk_debug.c` | `fputc()` → `wk_debug_putc()` |
| 心跳输出 | `project/src/wk_debug.c` | `wk_debug_print_heartbeat()` |
| 版本号输出 | `project/src/wk_debug.c` | `wk_debug_print_version()` |

## 上位机接入检查清单

### USART3 RS485 Modbus RTU

- ☐ 串口参数：**1000000 8N1，无流控**——与 `wk_usart.c` 的 `usart_init(USART3, 1000000, ...)` 逐字对齐
- ☐ 物理层：RS485 A/B 接线，总线两端 120Ω 终端电阻，共地
- ☐ 从站地址填 **2**（或 0 广播）
- ☐ CRC 用**标准 Modbus**（poly `0xA001`，init `0xFFFF`，低字节在前）
- ☐ 帧间保持 ≥ 3.5 字符静默（1000000 下约 35 µs，建议留 1 ms 更稳）
- ☐ 浮点解析：**高字在前**（`struct.unpack(">f", ...)`）
- ☐ 有符号字段：保持寄存器 3/4、输入寄存器 10~14 需按 int16 解释
- ☐ 波形下载：`FC06` 写保持寄存器 14 = 512/1024 → 轮询输入寄存器 77 到 1 → `FC04` 读 8000~16199（见「FC04 波形窗口」节）
- ☐ 超时重发：固件对错误帧静默丢弃，无异常/重发应答，必须自行重试
- ☐ 安全兜底：无通信看门狗，需上位机主动下发停止指令
- ☐ 诊断：读输入寄存器 16~21 可获知溢出 / CRC 错误 / 帧错误计数

### UART5 调试口

- ☐ 串口参数：**115200 8N1，无流控**——与 `wk_usart.c` 的 `usart_init(UART5, 115200, ...)` 逐字对齐
- ☐ 物理层：PC12=TX、PD2=RX，TTL 电平（3.3V），USB-TTL 适配器接线时注意共地
- ☐ 心跳：每 5 秒输出 `HEARTBEAT:[毫秒时间戳]\r\n`，用于确认固件存活
- ☐ 版本号：上电/复位后输出一行 `log_d` 版本信息（FW/HW/PROTO 三组版本号）
- ☐ AT 指令：行以 `\n` 或 `\r` 结束，`NB+` 前缀，`=` 分隔键值，大小写不敏感，多参数逗号分隔
- ☐ 指令清单见 `app/usrDebug.h` 顶部注释，包括电机/阀门/泵/LED/WS2812/诊断查询/Modbus 统计等
- ☐ 应答格式：成功 `NB+<指令>,OK\r\n`；失败 `NB+ERR,<原因>\r\n`；查询类直接回 `NB+<key>=<val>\r\n`
- ☐ Modbus 统计：`NB+stat=on` 开启 10s 周期打印（`NB+stat=off` 关闭，`NB+stat=now` 立即打印一次）
  - 打印格式：`NB+stat,rx=<有效帧>,reply=<应答帧>,crcErr=<CRC错>,frameErr=<帧错>,qOvf=<队列满丢弃>\r\n`

## 寄存器变更记录

| 日期 | 地址 | 变更内容 |
|---|---|---|
| 2026-10-14 | 0x0000、0x0001（离散输入） | 抬升高位按左右拆分上报：0x0000=抬升左高（PF6，原抬升高位开关）、0x0001=抬升右高（PA15，原抬升低位开关）；原 0x0001 抬升低位不再通过离散输入上报 |
| 2026-09-03 | 0x0002、0x0003 | 改为 `int16_t` 有符号控制：正数 Up、负数 Down、0 Stop；压制电机使用绝对值作为速度，抬升电机暂不按数值调速 |
| 2026-09-03 | 0x0008、0x0009（线圈） | 改为电机命令状态镜像，不再用于方向控制 |
| 2026-09-03 | 0x0009~0x0027（输入寄存器） | 实现一次请求读取全部状态和信息；增加输入/线圈位图、命令镜像、状态字、故障码、队列深度、通信诊断计数及全部底层数字量/模拟量输入 |
| 2026-09-28 | 0x0028~0x004F（输入寄存器） | V2 追加功能：多路电流、编码器位置、NTC2/3/Back 温度、流量计脉冲、57 绕组零点及 ADC 采集状态 |
| 2026-09-28 | 波形窗口 0x1F40~0x33FF | FC04 高地址窗口回传 57 绕组 A/B 的 40 kHz ADC 波形，不占用快照区 |
| 2026-10-17 | 0x0000、0x0001、0x000E、0x000F（离散输入） | 0x0000/0x0001 改为抬升合并高位/合并低位（低位=编码器下行到位）；0x000E/0x000F 复用为抬升左高/右高实际状态（左=PF6 原抬升高位开关，右=PA15 原抬升低位开关） |
| 2026-10-17 | 文档 | 合并原 `通信协议说明.md`：硬件链路/物理层、功能码与 CRC、拆帧机制、TMUX 通道顺序、NTC 换算公式、已知问题、固件实现索引、上位机接入清单；旧屏幕协议章节与重复寄存器表未收入，原文件删除 |
| 2026-10-20 | 0x0010~0x0011（离散输入）、0x004F（输入寄存器） | 新增点卤盒低位与成型通道在位；0x004F 原保留固定 0 改为新增离散输入高位图（bit0/bit1 对应地址 0x0010/0x0011） |
