---
name: esp32s3-firmware
description: 修改本项目ESP32-S3固件、Modbus、LVGL、温度标定或分析测试数据时使用；所有工程外辅助产物必须归档到agent-tp。
---

# ESP32-S3项目技能入口

先读取并应用[共享技能正文](../../../agent-tp/skills/esp32s3-firmware/SKILL.md)，按本次任务只读取所需源码与资料。

**硬性规则：除ESP32工程代码及其必需配置和固件资源外，所有新增报告、测试程序、测试桩、脚本、图表和对话记录都必须放到`agent-tp/`。** 不在根`docs/`、`tests/`、`tools/`、`script/`或固件组件中新增辅助产物。构建及临时可执行文件、缓存放根`build/`；最终报告放`agent-tp/reports/`。

该文件仅供工具发现；具体规则和工作流统一维护在共享正文。遵守根目录`AGENTS.md`，用户明确指定其他输出位置时按该次指令执行。
