# 合并 feature_add_ota

- 目标：将 origin/feature_add_ota 合并到当前 main。
- 已从 origin 拉取远程分支，分支顶端 b256caf。
- 合并提交：5825f01，使用 Git ort 策略完成。
- 引入 OTAService、WiFiConnect、OTA 实现及分区配置修改。
- 合并前全部已跟踪和未跟踪的本地改动已通过 stash 备份，名称 agent-before-merge-feature_add_ota-20261030；恢复使用 apply，备份保留。
- 恢复时 main/main.c 的任务创建宏出现冲突，已保留本地内存采样宏并保留远端 OTA/Wi-Fi 任务创建逻辑。
- 本地 RAM 诊断、PSRAM 迁移、PowerRecovery 1024 B 栈及其他原有修改保留为未提交修改，未并入合并提交。
- 已检查 main/main.c 编辑器诊断和差异格式。当前 shell 无法解析 nv_build，未执行固件构建；未烧录、未推送。
