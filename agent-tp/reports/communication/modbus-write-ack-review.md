# 命令重试与应答误判检查

本次将 `task/DataCenter/ModbusMap.h` 中 `MODBUS_COMMAND_RETRIES` 从2改为3，表示首次发送加两次重试。作用于 DataCenter 普通排队命令；原始网关透传不走该重试循环，轮询也不走该循环。复合命令失败时会重新执行整个命令，不能将3次理解为整条复合命令最多只发3帧。

## 校验结论

`function/nanoModbus/nanomodbus.c` 对FC06应答校验完整读取、站号、功能码、寄存器地址、写入值及CRC。没有任何接收字节时返回超时，不会仅因发送完成而成功。每次请求会重置消息状态并清理已有RX数据。普通命令目的站限定为2或3，不会误走广播免应答分支。

仍有以下边界：

1. FC06正常应答与请求完全相同。如果接线或收发器造成自身发送回显，解析器无法区分回显和从站ACK。当前使用硬件RS485半双工，但是否存在物理回显需要实机抓包验证。
2. RTU帧没有本次事务序号。上一条相同命令的迟到ACK若在本次清空RX后到达，会被认为本次成功。已有数据清空不能消除未来才到达的迟到帧。
3. `components/usart/usart.c` 的 `uart_modbus_wait_bus_idle` 清理数据后固定等待t3.5，没有因等待期间新收到字节而重新计时。存在迟到帧进入下次接收窗口的时序风险。
4. 收到合法ACK不等于加热实际工作。PowerRecoveryTask依据有效功率反馈决定是否执行0到目标档位的恢复。其恢复判断依赖总功率，不能单凭总功率证明每一路加热均启动。
5. 过期恢复/保护停止命令会主动跳过并返回NONE，这是取消语义，不是实际收到ACK。原始网关内部只做最低长度判断，属于不同路径，不能套用普通写命令的严格ACK校验结论。

本次保留现有传输和控制行为，仅调整重试次数；未声称上述边界已消除。进一步增强可考虑真正连续空闲检测，以及写后状态读取；相同迟到应答的严格事务归属需要从站协议支持序号，单靠RTU ACK内容无法解决。

## 验证

新增测试：[应答测试](../../tests/firmware/test_modbus_write_ack.c)，直接链接工程实际nanomodbus实现。

```sh
cc -std=c99 -Wall -Wextra -I function/nanoModbus agent-tp/tests/firmware/test_modbus_write_ack.c function/nanoModbus/nanomodbus.c -o build/test_modbus_write_ack
build/test_modbus_write_ack
git diff --check
```

10项场景通过：无响应、截断、错站号、错功能码、错地址、错值、错CRC均拒绝；合法ACK、自身回显模拟、相同迟到ACK模拟均接受。后两项模拟的是在发送后提供相同字节流，不证明实机已出现该情况。`git diff --check`通过。未执行完整固件构建或硬件联调。
