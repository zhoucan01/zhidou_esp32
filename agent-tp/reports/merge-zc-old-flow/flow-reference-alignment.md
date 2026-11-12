# 制作流程以 zc_old_flow_4678752 为准

参考 origin/zc_old_flow_4678752，提交 0fb84c1。本次仅调整制作流程，未覆盖温度标定、滤波和98℃确认实现。

- function/Milk 与 function/Press：C/H 原本已与参考一致，无需修改。
- function/ToufuMake/ToufuMake.c：按参考恢复任务入口，移除 main 额外增加的任务启动功率校准等待；恢复暂停/停止时 Heat2Run(0)。站2上线门禁保留，与参考一致。
- function/SoyMilkMake/SoyMilkMake.c：此模块提供豆腐调用的 TouFuSoyMilk、HeatSoyMilk，按参考恢复任务入口及 Heat2Run 调用，包括 EVTspecial=1 的备用流程。当前 EVTspecial=0。
- 构建适配：两文件不引入已经迁到 oldCode、未参与工程构建的 tempurature.h；CMake 保持当前可构建依赖。除此以外，四模块 C/H 已逐文件核对与参考一致。
- 与温度相关的流程判据仍调用当前 DataCenter/ntcTemp 接口；本次不将其算法替换为分支版本，因此不宣称整机温度到达时间与该分支完全相同。

## 验证

四模块 C/H 内容对比通过；植物奶故障/UART 并发帧测试通过；本次两文件 git diff --check 通过。
固件构建：nv_build 通过。未提交、未推送、未烧录；用户已有未提交修改保留。
