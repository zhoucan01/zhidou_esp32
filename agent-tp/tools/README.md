# 测试与分析工具

## 项目技能维护

共享技能正文在`agent-tp/skills/esp32s3-firmware/SKILL.md`，Codex和Copilot入口只负责发现及加载它。

```bash
# 只检查两个发现入口是否一致，不写文件
python3 agent-tp/tools/skills/install_project_skill.py --check
# 更新Copilot入口后，同步到本仓库的Codex入口
python3 agent-tp/tools/skills/install_project_skill.py
```

该脚本不改用户级配置；受保护目录需按环境权限机制正常批准。

## 串口上位机

程序在[soy_host/](soy_host/README.md)，配置文件`auto_heat_config.json`与程序放在一起，单元测试也保留在同目录，便于测试其直接模块导入。

Windows双击`soy_host/start.bat`；或在该目录运行：

```bash
python -m pip install -r requirements.txt
python app.py
```

新采集保存到工程`agent-tp/data/captures/YYYY-MM-DD/`，不会再写入工具目录内的`logs/`。上位机启动不会自动连接设备。

## 工程内的分析入口

NTC双向去噪验证工具位于`agent-tp/tools/temperature/`：`analyze_adc_noise.py`统计原始跳变并将其实测幅值用于明确标注的合成压力测试；`probe_temperature_filter.py`检查生产趋势滤波对已知输入的反应。它们不覆盖生产标定表，CSV采样间隔不足时不声称重建了固件波形。

以下脚本仍保留原来的组件/脚本位置，以免把固件标定工具与工程构建入口混在一起：

| 位置（相对工程根目录） | 用途 |
|---|---|
| `script/analysis/analyze_thermal_run.py` | 分析事件式采集的误差、稳定性、离线试拟合并输出图形 |
| `function/ntcTemp/scripts/calibration/calibrate_machine3_database.py` | 工况数据库拟合；默认读取本目录上位机配置，不带`--write`不改生产表 |
| `function/ntcTemp/scripts/calibration/calibrate_3ntc.py` | 三路升/降温标定分析 |
| `function/ntcTemp/scripts/calibration/calibrate_2d.py` | 历史ADC×档位二维标定 |
| `function/ntcTemp/scripts/analysis/comparison_report.py` | 生成温度对比HTML和配图，输出到`agent-tp/reports/temperature/` |

常用分析示例，在工程根目录执行：

```bash
PYTHONPATH=build/analysis_deps PYTHONDONTWRITEBYTECODE=1 \
python3 script/analysis/analyze_thermal_run.py \
  agent-tp/data/captures/2026-10-27/20261027-232523-940824.csv \
  --out-dir build/thermal_analysis
```

绘图需要Matplotlib；`build/analysis_deps`仅是本机已有依赖的位置，其他环境安装在正常Python环境也可以，不必设置`PYTHONPATH`。

`script/analysis/analyze_board_temp_error.py`和`script/analysis/plot_board_temp_error.py`是针对特定旧数据、固定分段的历史分析脚本，含旧机器路径，未在本次改写分析逻辑；新采集优先使用上面的通用入口。
