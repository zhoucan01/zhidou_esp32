# 主机回归测试

在工程根目录执行；固件测试需要GCC，生成的C桩、可执行文件和编译输出均位于工程`build/`，不写到`agent-tp/`内。

```bash
python3 agent-tp/tests/firmware/test_display_diagnostics.py
python3 agent-tp/tests/firmware/test_encoder_interrupts.py
python3 agent-tp/tests/firmware/test_temperature_stop_windows.py
python3 agent-tp/tests/firmware/test_power_recovery.py
python3 agent-tp/tests/firmware/test_ntc_reaccept.py
```

前四项分别检查显示诊断/Modbus、编码器、短停机测温窗口和站3恢复控制。硬件接口由桩替代，不能代替实机时序验证。

`test_ntc_reaccept.py`检查双向大幅ADC噪声、重复异常不累计、连续状态重新接纳以及真实采样间隔。NTC组件现在使用`esp_timer`；单独编译旧NTC C测试时需要`-Iagent-tp/tests/firmware/ntc_host`及`agent-tp/tests/firmware/ntc_host/clock.c`。实时行为测试会自行提供可控时钟，不使用该默认时钟。

上位机自身测试保留在工具旁，可从工程根目录执行：

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
  -s agent-tp/tools/soy_host -p 'test_*.py'
```

完整上位机测试需要Tkinter和pyserial，导入GUI模块但不打开窗口、不连接串口。仅运行`test_control.py`、`test_telemetry.py`、`test_settings.py`可验证不依赖GUI的部分。

- `firmware/test_temperature_motor_gate_disabled.py`：验证默认关闭停机采样策略后，996 RPM运行时ADC变化仍能更新温度。`test_temperature_stop_windows.py`显式开启宏，验证原策略可恢复。
