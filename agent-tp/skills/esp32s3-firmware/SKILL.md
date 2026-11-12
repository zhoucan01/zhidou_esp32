---
name: esp32s3-firmware
description: 修改或分析本仓库ESP32-S3固件、Modbus控制、LVGL界面、温度标定及测试数据时使用；规定所有工程外辅助产物归档到agent-tp。
---

# ESP32-S3项目开发与分析

这是Codex和GitHub Copilot共用的技能正文。工程路径均相对当前仓库根目录，不依赖Windows盘符或路径大小写。

## 第一条：所有工程外产物必须进入agent-tp

**除ESP32工程代码、工程必需的构建配置和固件资源外，Agent新增的所有报告、分析文档、测试程序、测试桩、脚本、上位机工具、图表、导出文件及对话记录，都必须保存在`agent-tp/`。** 不以“与固件相关”“为了验证代码”“以前放这里”为由在其他目录新增这些文件。

创建文件之前先按用途归类：

| 用途 | 新文件位置 |
|---|---|
| ESP32实际编译、链接、运行的源代码及固件资源 | 对应的`main/`、`components/`、`hardware/`、`function/`、`task/`等工程目录 |
| 分析结论、测试报告、图表、PDF/HTML | `agent-tp/reports/<主题>/`，配图随报告保存 |
| 固件逻辑的主机回归测试、C测试驱动、测试样例 | `agent-tp/tests/firmware/`及其专属子目录 |
| 可复用的分析、标定、测试辅助脚本 | `agent-tp/tools/<工具名>/` |
| 串口上位机及其直接模块测试 | `agent-tp/tools/soy_host/` |
| 对话记录、交接说明、改动总结 | `agent-tp/conversations/` |
| 原始CSV与同名串口日志 | `agent-tp/data/captures/YYYY-MM-DD/` |
| 按样机、电压或流程分组的原始数据 | `agent-tp/data/testdata/` |
| 技能正文及必要的技能参考材料 | `agent-tp/skills/<技能名>/` |

不要在根`docs/`、`tests/`、`tools/`、`script/`、`output/`或固件组件内新建辅助报告、测试、分析脚本。C/C++文件若只供主机验证，仍属于测试。实际参与固件编译的生产`.inc`表属于工程资源，但建表脚本及验证报告属于`agent-tp/`。

必要的`AGENTS.md`、Copilot指令和技能发现入口保留在工具规定位置，不能放报告或辅助程序。已有工程代码、协议、脚本可原位维护，不自动迁移历史文件；用户明确指定不同路径时按该次指令执行。

可交付结果写入`agent-tp/`；构建、临时编译桩/可执行文件、依赖及绘图库缓存写入根`build/`并忽略。设计脚本默认输出目录时区分报告与临时文件，最终报告不能只留在`build/`。

## 项目定位与所有权

- ESP-IDF 5.3.1、FreeRTOS、LVGL、ESP32-S3。`main/main.c`是启动入口，根及组件`CMakeLists.txt`装配工程；先读本次涉及的实际源码，不将技能描述当成实时配置。
- `task/DataCenter/ModbusMap.h`集中定义站号、寄存器、范围和轮询常量，业务代码不新增裸地址。用“站号＋功能码＋PDU地址”区分寄存器。
- `DataCenter.c/.h`管理串行命令队列、下游站2/3事务、状态缓存及执行器API；业务、轮询、上位机透传共用队列，不另开并行UART写通道绕过它。
- `DebugConsole`负责站1本机寄存器及UART0网关；蓝牙入口在`BlueDataDeal`、`Bluetooch`。调电机或加热时检查目标与反馈、方向、队列失败和旧命令不能覆盖新停止命令。
- 制作流程主要在`function/`，调度在`FunctionSelection`。区分固件自动流程与`agent-tp/tools/soy_host/`上位机流程，不用一套流程的档位和节奏解释另一套日志。
- `function/UI`采用单个LVGL screen和复用对象；输入逻辑在`components/myLvgl/`。新增页面先确认逻辑页面配置机制，避免重复创建整套screen和标签。

## 按任务读取与验证

### 协议和控制

修改寄存器或控制行为前，读取`docs/modbus/`中的对应站专用协议及主站协议，再核对代码。保持已发布地址兼容：新字段追加，停用字段保留。修改常量、编解码、范围校验、CRC示例或现有接口文档时同步检查消费者。新增差异分析、联调报告写`agent-tp/reports/communication/`，不另建根目录报告。

### 温度与数据分析

- 读取`function/ntcTemp/ntcTemp.h`、`ntcTemp.c`及实际数据表，确认机型宏、主通道和工况选择；上位机“本地MACHINE”不等于固件机型。
- CSV是事件式快照，使用本通道事件及接收时间去重，按时间对齐参考温度，公开允许插值间隔、剔除量与覆盖率，不把重复旧值当新采样。
- 分开原始ADC、固件温度、主温度和上位机本地拟合；区分平滑、准确度、冻结和有效采样率。同批拟合/留出不等于独立批次验证。
- 三路探头高度不同：NTC1底部、NTC2中底部、NTC3中部。考虑参考位置、搅拌、分层和隔热；不要只凭通道差异认定损坏。ADC平稳但换算突变时优先检查切表及滤波。
- 分析请求不隐含覆盖生产表；更新表按实际授权执行。报告记录输入、工况、方法、误差、限制及复现命令。保留原始数据，不为改善指标静默删点。

### 测试与构建

从工程根目录运行相关检查，新测试源码必须放`agent-tp/`。现有入口：

```bash
python3 agent-tp/tests/firmware/test_display_diagnostics.py
python3 agent-tp/tests/firmware/test_encoder_interrupts.py
python3 agent-tp/tests/firmware/test_temperature_stop_windows.py
python3 agent-tp/tests/firmware/test_power_recovery.py
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s agent-tp/tools/soy_host -p 'test_*.py'
```

完整上位机测试需要Tkinter和pyserial，缺依赖时说明未运行部分，不假报通过。

固件构建和烧录使用用户已配置的系统命令，在工程根目录执行：

- `nv_build`：构建、编译固件。
- `nv_flash`：烧录固件。

两者都是系统命令，不是工程内脚本；先用当前 shell 的 `type nv_build`、`type nv_flash` 检查解析结果，不因工程中找不到同名文件就判断命令不存在。若当前 shell 无法解析，说明实际环境问题，不擅自用其他构建或烧录入口替代。以命令实际成功输出为准。任务不包含烧录时不自动执行 `nv_flash`；主机桩不证明电气、机械或实时行为。

用户指定的固件编译入口（从工程根目录执行，一次选择一个）：

```bash
/home/wsl/.local/bin/nv_build
```

首选入口不可用时，使用备用路径：

```bash
/home/wsl/workspace/tools/nv_build
```

这两个路径是同一构建用途的入口，不需要连续执行两次。需要时可将 `/home/wsl/.local/bin` 与 `/home/wsl/workspace/tools` 加到本次构建终端的 `PATH`。若沙箱中路径显示不存在、命令不可见，按正常权限流程在系统环境执行；本项目已验证系统环境可解析 `/home/wsl/.local/bin/nv_build`。不向工程复制系统工具，也不因为沙箱不可见就断言宿主机未安装。构建失败时检查实际输出；只有入口不可用时才切换备用路径，不通过重复编译掩盖源码错误。

使用`pathlib`等方式按仓库结构定位，搬动工具后修正工程根、数据和配置路径。测试生成物放根`build/`，不要误写入`agent-tp/build/`。

## 收尾

1. 检查新增持久化文件的位置，报告、测试和脚本不得漏在工程外侧旧目录。
2. 编译缓存固定`build/ccache/`，临时缓存固定`build/ccache/tmp/`；原始数据、工具缓存同步检查`.gitignore`，不得`git add -f`。
3. 保留用户未提交修改，不擅自批量迁移或删除文件。提交前检查`git status --short`及`git diff --cached --name-only`；已跟踪缓存仅移出索引，保留本地。
4. 运行适合变更的验证及`git diff --check`，说明实际通过项、未完成项及`agent-tp/`产物链接。

工作区入口：工程根目录`agent-tp/README.md`。

## 技能发现与维护

共享正文只维护此文件。Codex入口为`.agents/skills/esp32s3-firmware/SKILL.md`，Copilot入口为`.github/skills/esp32s3-firmware/SKILL.md`，两者只负责加载正文。常驻归档规则还写在根`AGENTS.md`及`.github/copilot-instructions.md`，不依赖技能是否被自动选中。

需要同步发现入口时，在工程根目录运行`python3 agent-tp/tools/skills/install_project_skill.py`；只检查时加`--check`。安装脚本仅将Copilot发现入口同步到本仓库的Codex目录，不改用户级配置；若环境保护该目录，使用正常权限审批，不绕过限制。

发现目录依据：[Codex官方技能文档](https://developers.openai.com/codex/skills/)、[GitHub Copilot官方技能文档](https://docs.github.com/en/copilot/how-tos/copilot-on-github/customize-copilot/customize-cloud-agent/add-skills)。本技能不修改工具权限或自动批准设置。
