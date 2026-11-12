# 制作任务启动功率校准

豆腐、植物奶、自清洁在进入工艺动作前调用一次 runPowerCalibration(60000U)。成功才继续；失败、通信失败或超时则调用各任务停止清理，关闭上下加热、破壁电机和泵，再关闭动力板总开关以中止残留校准。豆腐同时停止压制/抬升。失败后清除流程状态、任务句柄并删除当前任务。

自清洁保留原校准入口，补全第二路加热关闭及流程状态清理。其他工艺参数和温度算法未修改。沿用现有校准接口：60000U 为接口等待参数，并非整个校准过程严格60秒的总时限。

验证：
- python3 agent-tp/tests/firmware/test_task_start_calibration.py：通过。执行生产任务启动片段和实际停止函数，覆盖校准成功/失败、调用一次、失败不进入工艺、停机与状态清理。
- python3 agent-tp/tests/firmware/test_milk_fault_and_uart_frames.py：通过。
- nv_build：通过，固件大小0x1a61b0，应用分区剩余78%。
- 本次三个固件文件及新增测试 git diff --check：通过。

未烧录、未提交；保留用户已有 components/myLvgl/myLvgl.c 和 main/main.c 修改。主机测试与构建不替代实机校准及执行器停机验证。
