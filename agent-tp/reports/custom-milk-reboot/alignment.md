# 配方说明文字居中修复（2026-11-06）

SHL-500W 抓取的[修复前画面](alignment-before.png)停在 Nut Milk，下方说明文字 `Rich and nutty` 未水平居中。

原因：选择页说明标签围绕左上角旋转 90°，原定位使用未旋转文字的尺寸变化补偿，没有固定旋转后的水平中心。不同长度的说明会产生不同偏移。

修改 `function/UI/PlantMilk/ui_plantmilk.c` 的 `ui_plantmilk_active_label_pos()`：仅针对配方选择页说明标签（索引 2），将旋转后的水平中心固定在屏幕中线上，并保留原设计的上下位置。文字内容、字号、配方标题和其他页面布局保持原有实现。

现有真实 LVGL 回归增加四个配方的变换后包围框检查，中心与屏幕中线误差不超过 1 像素。全部流程、100 次图层分配失败及 200 次配方切换回归通过：[验证日志](alignment-validation.log)。

主机渲染：[Oat](aligned-oat.png)、[Nut](aligned-nut.png)、[Soy](aligned-soy.png)、[Custom](aligned-custom.png)。

`nv_build` 构建成功，固件包位于 `build/firmware-fa5w5c0o/`。本次沿用用户此前允许整片擦除及烧录的授权，通过 COM12 使用 `nv_flash` 完成更新、哈希校验与自动复位：[烧录日志](alignment-flash.log)。应用 SHA256 为 `3ac0c70958f7d9c97b9cd031a2e9782c65afd92c0b1bb1f417a7616038bc2fc4`。

用户进入 Nut Milk 后，通过 SHL-500W 重新采集了[修复后的实机画面](alignment-after.png)，确认 `Rich and nutty` 已位于配方圆环下方中线。15 秒短串口记录在 `agent-tp/data/captures/2026-11-06/alignment-after-serial.txt`，含运行心跳，未出现崩溃标记。

本次新增的报告、图片和测试辅助产物均位于 `agent-tp/`；未提交或推送，工作区其他修改保留。
