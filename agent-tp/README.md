# Agent 对话、分析与测试工作区

这里集中保存开发对话、分析资料、原始测试数据和主机工具；固件代码仍在工程的 `main/`、`task/`、`function/`、`components/` 等目录。

```text
agent-tp/
├── conversations/             对话结论、改动说明、处理流程
├── reports/
│   ├── temperature/          温度分析，图片随报告保存，PDF位于exports/
│   └── communication/        BLE、RS485分析及压测报告
├── data/
│   ├── captures/YYYY-MM-DD/  上位机采集CSV与配套串口日志
│   ├── testdata/             按原样机标签保留的温度与流程测试数据
│   └── communication/        通信原始日志
├── tools/soy_host/           串口上位机、配置及自身单元测试
├── skills/                  Codex/Copilot共享技能正文
└── tests/firmware/           在主机运行真实固件逻辑的回归测试
```

## 常用入口

- [对话与结论](conversations/README.md)
- [分析报告索引](reports/README.md)
- [原始数据说明](data/README.md)
- [工具入口](tools/README.md)、[串口上位机使用说明](tools/soy_host/README.md)
- [固件回归测试](tests/README.md)
- [Codex/Copilot项目技能](skills/esp32s3-firmware/SKILL.md)
- [最近一次温度拟合与稳定性分析](../docs/ntc/2026-10-28_232523温度拟合与稳定性分析.md)
- [温度算法与结构隔热分析](../docs/ntc/2026-10-28_温度算法与结构隔热影响分析.md)

## 整理前后路径

| 原位置 | 新位置 |
|---|---|
| `agent-tp/agent-qs/` 内的对话说明 | `conversations/` |
| `agent-tp/agent-qs/report/` | `reports/temperature/` |
| `agent-tp/agent-qs/batter/` 内的报告 | `reports/communication/`，按日期归档 |
| `agent-tp/output/` 内的图片、PDF | `reports/temperature/assets/`、`exports/` |
| `agent-tp/soy_host/` | `tools/soy_host/` |
| `agent-tp/soy_host/logs/` | `data/captures/YYYY-MM-DD/` |
| `agent-tp/testdata/` | `data/testdata/` |
| `agent-tp/tests/*.py` | `tests/firmware/*.py` |

更早的 `tools/soy_host/logs/` 引用也已更新到日期采集目录。已有工程文档保留在根`docs/`供查阅和维护，不自动搬迁；此后Agent新增的报告、测试程序及辅助脚本均必须放到`agent-tp/`，不得继续向根`docs/`、`tests/`、`tools/`、`script/`等目录新增。

## 后续归档约定

1. 原始采集文件保留原名，CSV与同名`-serial.txt`放在同一天目录，不改写内容。跨午夜采集按文件名的开始日期归档。
2. 按样机、电压分类的副本放到`data/testdata/`，保留其来源标签，不直接与原采集合并或覆盖。
3. 新报告建议使用`YYYY-MM-DD_主题.md`，配图放在报告旁边或其专属子目录；对话草稿放`conversations/`。
4. 原始数据目录加入`.gitignore`；历史上已经跟踪的旧路径会在整理后的提交中表现为删除，数据仍在新的本地位置，禁止强制添加。
5. 最终报告、图表和导出文件放`agent-tp/reports/`，工具及测试源码放`agent-tp/tools/`、`agent-tp/tests/`。编译、临时可执行文件及缓存放工程`build/`，编译缓存使用`build/ccache/`及其`tmp/`。本次保留的旧Python缓存已移到`build/agent_tp_legacy_pycache/`。

本次整理保留全部原文件。两份内容相同的BLE报告仍分别保留并注明副本；缺失的采集配对记录见数据说明。
