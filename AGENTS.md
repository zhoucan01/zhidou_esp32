# 项目规范

## Agent产物归档（强制）

- 除ESP32工程代码及其必需的构建配置、固件资源外，Agent生成或新增的**所有报告、分析文档、测试程序、测试桩、脚本、上位机工具、图表和对话记录都必须放到工程根目录的`agent-tp/`内**。按用途判断，不按扩展名判断；供主机验证的C程序也属于测试。
- 不得在根`docs/`、`tests/`、`tools/`、`script/`、`output/`或`function/`、`task/`等固件目录下新增上述辅助产物，已有类似文件不是新增路径的先例。
- 报告及配图放`agent-tp/reports/<主题>/`；固件主机测试放`agent-tp/tests/firmware/`；分析、标定、测试辅助脚本及上位机放`agent-tp/tools/<工具名>/`；对话结论放`agent-tp/conversations/`；原始采集放`agent-tp/data/captures/YYYY-MM-DD/`，样机测试集放`agent-tp/data/testdata/`。
- 脚本默认将可交付结果写入`agent-tp/`；编译产物、临时测试可执行文件及缓存仍按下方规范放工程`build/`，不得混入可提交报告。
- `AGENTS.md`、`.github/copilot-instructions.md`及`.agents/skills/`、`.github/skills/`中的必要发现入口是工具配置例外；技能正文与附带脚本放`agent-tp/skills/`、`agent-tp/tools/`，不能借隐藏配置目录存放报告或测试程序。
- 维护已有工程文件可原位修改，不因本规则擅自搬迁历史文件；新增辅助文件必须归档。用户明确指定其他路径时遵从该次指令。
- 完成前检查新增文件路径，并给出`agent-tp/`内的实际产物链接。

## 项目技能

处理本项目固件、Modbus、温度标定、测试或数据分析时，读取并应用[ESP32-S3项目技能](agent-tp/skills/esp32s3-firmware/SKILL.md)。正文集中维护，Codex与Copilot使用各自的发现入口。

## 持续改动记录

- 从2026-11-09建立改动表的任务起，仅在实际修改工程源码（含头文件、宏定义和源码注释）后，交付前更新[工程改动记录](docs/改动记录.md)，按编号追加改动内容、涉及文件、原因和实际验证结果。
- `docs/改动记录.md`是用户明确指定的持续记录路径例外；其他新增辅助产物仍遵守`agent-tp/`归档规则。
- 代码检查、只读分析、测试运行、未实施的建议，以及独立文档、报告、配图或记录规则的维护均不单独登记；源码改动条目可以附带对应验证结果。用户明确要求补录时，核对已有源码差异并注明补录范围，不能归为当次新改源码。验证不足如实注明，后续补充结果保留可追溯记录。

## Git 提交

- 不提交或推送编译缓存、临时缓存及构建产物，包括 `build/`、`ccache/`、`.ccache/`、`.cache/`、`__pycache__/`。
- 编译缓存统一放在工程的 `build/ccache/`，缓存临时文件放在 `build/ccache/tmp/`。
- 新增工具产生缓存时，同步补充 `.gitignore`，不得使用 `git add -f` 强制提交缓存。
- 提交前检查 `git status --short` 和 `git diff --cached --name-only`，确认暂存区没有缓存或构建产物。
- `.gitignore` 不会自动取消已跟踪文件；若发现缓存已被跟踪，应仅从 Git 索引移除并保留本地文件。
