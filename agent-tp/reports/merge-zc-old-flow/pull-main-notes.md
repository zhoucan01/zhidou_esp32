# 同步 origin/main

远程更新至 54fdb5a，通过 merge 合入本地 main。仅在本仓库设置 pull.rebase=false。
原未提交修改已恢复；script/build.py 的删除与远程修复发生冲突，保留迁移到 script/build_tools/build.py 的本地布局，并将远程 OTA 导出修复三方合入该文件。Python 语法及空应用偏移从自定义分区表取值的导出验证通过。未推送、未烧录，未重新运行完整固件构建。备份 stash 保留。
