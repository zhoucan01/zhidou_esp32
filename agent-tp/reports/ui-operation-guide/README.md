# UI 操作指南配图与核对记录

日期：2026-11-09。指南依据当前工作区源码，包括用户已调整的豆腐制作页和完成页坐标。没有改动固件源码或 sdkconfig，没有烧录或控制实机。

## 交付文件

- [图文 HTML 指南](../../../docs/UI操作指南.html)：全部图片内嵌，支持离线打开、目录跳转、点击图片放大及浏览器打印。
- [Markdown 指南](../../../docs/UI操作指南.md)：便于后续维护，配图链接指向本目录 images。
- [配图目录](images)：51 张 480×320 PNG，其中 30 张是逐页主图，21 张是配方、首页入口、编辑态和检查/轮播等补充状态；指南正文使用其中 46 张。
- [页面渲染记录](render.log)、[图片索引](image-index.txt)、[正常模式回归](normal-mode-regression.log)、[演示模式回归](validation-mode-regression.log)。
- 总览图：[1](overview-1.png)、[2](overview-2.png)、[3](overview-3.png)、[4](overview-4.png)，按图片索引顺序排列，供核对完整性。

## 配图方法与边界

使用当前真实 LVGL、UI、字体、图片解码和页面代码，在已有主机测试桩上渲染原生 320×480 帧，再逆时针旋转为用户阅读方向的 480×320 PNG。没有用 AI 生成或重绘界面。

主机桩模拟 RTOS、传感器、工艺状态和显示输出，不能代替设备实拍或电气/机械验收。图层使用真实固定池，渲染包装检查图层申请及内存完整性。渲染结果：30 页均完成，共 818 次图层创建，最大单图层 14,592 B。

主图包括正常检查通过的状态；补图另展示未安装或缺少检查条件的状态。清洗轮播使用已安装的模拟输入。截图不证明机器实际已装好部件、已经完成食品加工或已经联网。

## 已完成核对

- 对照 `ui.h` 的逻辑页面枚举确认 P01～P30 全覆盖，各页均有主图。
- 核对物理方向转换、参数编辑、Back、Continue、Start、完成、停止确认、停止等待以及结束去向。
- 分别用 `--ui-validation-mode 0` 和 `--ui-validation-mode 1` 编译、运行现有 `test_plantmilk_ui.py`；两次均通过，包括真实 LVGL 渲染、传感器门控、页面导航、停止等待、长按屏蔽、自然完成以及结果页清洗跳转。正常模式包含豆腐 Collect Pulp 任务桩验证。
- 四张页面总览图已目视检查；本轮配图包含最新豆腐制作与完成页坐标。
- HTML 包含 30 个页面卡片与 46 张内嵌图片。正文说明 Settings 预留、演示模式、参数未完整接入执行工艺、植物奶 Collect Pulp 仅切页等现有边界。
- Markdown 及归档文档的相对文件链接均存在；P01～P30 与 30 张主图逐项对应，51 张 PNG 的尺寸、CRC 和像素数据长度检查通过；HTML 目录锚点存在且无重复 ID。
- 本次浏览器预览服务无法连接，未完成 HTML 的浏览器交互及打印效果验收；配图总览已目视检查，HTML 已完成静态结构核对。
- 只做主机和文件核对，不将主机图层失败注入产生的历史峰值当作正常 UI 内存峰值。

## 复现

从工程根目录执行，`--cc` 指向可用的主机 C 编译器；主机目标文件、Zig 缓存与临时文件应保持在 `build/ccache/`。

```powershell
python -B agent-tp/tests/firmware/test_plantmilk_ui.py --cc <主机编译器> --ui-validation-mode 0
python -B agent-tp/tests/firmware/test_plantmilk_ui.py --cc <主机编译器> --ui-validation-mode 1
python -B agent-tp/tools/ui-guide/generate_assets.py --cc <同一主机编译器>
python -B agent-tp/tools/ui-guide/build_html.py
```

本机使用已有的 `build/host-cc.cmd`（Zig C 包装）。生成程序位于 [ui-guide 工具目录](../../tools/ui-guide)。主机代码只用于文档渲染，不参与固件构建。
