# ESP32-S3项目指令

遵守根目录[AGENTS.md](../AGENTS.md)，开发、分析及验证使用[共享项目技能](../agent-tp/skills/esp32s3-firmware/SKILL.md)。

**强制归档：除ESP32工程代码及必需的构建配置、固件资源外，Agent新增的所有报告、测试程序、测试桩、分析/标定脚本、上位机工具、图表及对话记录都必须放到`agent-tp/`。** 不在根`docs/`、`tests/`、`tools/`、`script/`、`output/`或固件组件中新增这些辅助文件。

- 报告和图表：`agent-tp/reports/<主题>/`。
- 固件主机测试：`agent-tp/tests/firmware/`；工具自身测试可与`agent-tp/tools/`中的工具同放。
- 脚本和工具：`agent-tp/tools/<工具名>/`。
- 原始采集：`agent-tp/data/captures/YYYY-MM-DD/`；样机测试集：`agent-tp/data/testdata/`。
- 对话及交接：`agent-tp/conversations/`。

最终报告写入`agent-tp/`；编译产物、临时测试可执行文件及缓存仍放根`build/`，编译缓存为`build/ccache/`、临时缓存为`build/ccache/tmp/`。必要的技能发现入口和本指令是配置例外，技能正文集中在`agent-tp/skills/`。

已有工程文件可原位维护，不据此在旧位置新增同类辅助文件，不擅自搬迁历史资料。用户明确指定其他路径时遵从该次指令。保持原始数据不变，保留未提交修改；完成前检查文件路径与Git差异，给出产物实际链接。
