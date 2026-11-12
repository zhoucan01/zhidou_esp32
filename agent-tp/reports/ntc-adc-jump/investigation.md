# NTC1/2/3 ADC 在正常值与约3000间跳变的检查

## 结论范围

用户确认现象来自本工程上位机实时值；尚无本次异常的原始应答帧或CSV，因此不能证明现场根因。当前工作区的ESP32、相邻AT32从站及上位机源码检查，未发现NTC寄存器越界或互相覆盖；识别到AT32共享TMUX/ADC被高优先级调试采样抢占的竞争风险，并在主机模型中复现。此复现不等于现场实际发生，更不证明真实其他通道恰好为3000。

## 协议/内存核对

| 路径 | 结果 |
|---|---|
| 站2 FC04 NTC1 | 0x0000/0x0001，uint32，高字在前 |
| 站2 FC04 NTC2 | 0x0041/0x0042，与压制编码器0x003F/0x0040相邻但不重叠 |
| 站2 FC04 NTC3 | 0x0043/0x0044，与NTC2不重叠 |
| ESP32轮询 | registers[80]，读取80个，最大使用0x004F=79；memcpy恰好160字节 |
| 从站快照 | inputSnapshot[80]，FC04通过address/quantity边界检查；80寄存器完整RTU应答165字节，小于response[256] |
| nanoModbus | 内部缓冲260字节；校验站号、功能码、byte_count==quantity*2及CRC；轮询失败直接返回，不发布局部数组 |
| ADC写入 | g_DataPool.ntc1Raw/ntc2Raw/ntc3Raw仅在poll_actuator_registers赋值；滤波与温度换算没有写回这三个原始值 |
| 上位机 | telemetry.request_ntcs读取站2起始0的69个寄存器；parse_ntcs要求143字节、02 04 8A、CRC正确；按0/65/67取uint32；app直接显示解析结果 |

nanoModbus会先填调用者的局部数组，随后才校验CRC；因此不能声称库在CRC失败时完全不写输出数组，但当前poll_actuator_registers在失败时不将其复制到缓存/ADC字段。

CRC只能验证传输内容，不能检验从站采样缓存是否已采错。RTU FC04应答也不回带起始地址，因此同站、同功能码、同长度的迟到应答仅凭CRC不能完全排除；当前未有抓包证实此情况。69寄存器与80寄存器的长度不同，不能直接互相冒充通过当前长度校验。

## 发现的从站采样风险

只读检查 ../at32f415rbt7，本地HEAD为8dd5018，未修改该工程。现场烧录版本是否与本地源码一致尚未确认。

- code/app/tmux1308.c:getTMUX1308Data：设置共享A0/A1/A2后等待1ms，再依次读ADC1_IN13、IN14，将IN14保存到原channel下标；没有覆盖完整事务的互斥。
- code/project/src/wk_adc.c:adc_read_channel：配置/触发/等待/读取ADC1也没有互斥保护。
- getState任务优先级8；usrDebug任务优先级9，可以抢占扫描。usrDebug的NB+rise=zero、peak、wave等路径使用同一TMUX地址线及ADC，返回后不恢复被抢占扫描所需地址。波形路径是否编译启用应核对现场配置。
- 1ms忙等仅避免主动yield，不能阻止高优先级任务抢占。
- 如果getState扫描NTC2的S0期间被抢占，调试采样将地址停在S7，getState恢复后可将S7值存入NTC2；下一轮正常扫描又恢复。若其他通道值约3000，就能出现用户描述的形式。
- 这些调试采样并非正常轮询必然执行的路径；若异常时未执行它们，不能据此直接认定根因，还要检查模拟供电、参考地、MUX/ADC稳定时间、干扰及实际固件版本。
- 从站组快照时读取的缓存不是整个扫描周期的原子快照，可能混入相邻采样周期。这不是数组越界，本身也不能无来源地制造3000。

## 文档不一致

站2协议文档一处称NTC1为D2通道6、镜像地址0x0025；实际tmux1308.h枚举NTC1=3，从站快照使用该枚举，因此本地源码的NTC1镜像为0x001F+3=0x0022。文档后部另有通道3说明。排查须以实际烧录枚举为准，不能只看文档中的通道6。

## 已完成验证

- 实际function/nanoModbus/nanomodbus.c编译主机测试：80寄存器正常值/3000值往返一致；前后canary不变；81/40寄存器长度、错误CRC/站号/功能码、截断应答均返回错误。AddressSanitizer与UBSan通过；因当前环境LeakSanitizer不支持ptrace，运行时关闭泄漏检测，测试本身不分配堆内存。
- python3 agent-tp/tests/firmware/test_display_diagnostics.py：通过。
- PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s agent-tp/tools/soy_host -p test_telemetry.py：7项通过。
- python3 agent-tp/tests/firmware/ntc_modbus/reproduce_tmux_preemption.py：使用真实从站tmux1308.c扫描实现与虚拟ADC/GPIO，注入共享地址被抢占事件，复现三路正常→3000→恢复。3000是人为设置的其他通道模拟值，非现场测量。

## 现场确认方法

保留上位机异常前后的MODBUS-RX原始行及CSV，并记录当时是否运行NB+rise调零/波形命令。抓取站2从0读取80寄存器的同帧快照，比较NTC1低字0x0001与D2通道3的0x0022，NTC2低字0x0042与0x001F，NTC3低字0x0044与0x0020，同时查看全部D2通道值。

若同帧原始ADC和镜像均约3000，说明跳变已存在于从站缓存或更早的采样路径；若镜像与专用字段不同，还需考虑构建快照被扫描任务抢占的时序，不能仅凭一帧差异认定内存覆盖。要最终区分采样串通道与硬件干扰，需要从站加互斥后的对照实验及实际波形。

本次未修改固件、未烧录、未提交，未用滤波掩盖原始ADC异常。
