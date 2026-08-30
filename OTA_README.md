# ESP32-S3 OTA 使用说明

本文档说明本项目当前的局域网 OTA 实现、版本制作方法、测试步骤、回滚流程和常见故障。

当前方案已经完成以下实际验证：

```text
USB 烧录 1.0.0（ota_0）
        ↓
通过局域网下载 1.0.1
        ↓
写入 ota_1 并校验固件
        ↓
切换启动分区并重启
        ↓
1.0.1 自检通过并标记为有效
```

## 1. 当前实现概览

设备启动后会执行以下流程：

1. 检查当前运行分区及 OTA 映像状态。
2. 如果新映像处于 `PENDING_VERIFY`，运行启动自检。
3. 初始化 Wi-Fi。
4. Wi-Fi 获得 IP 后创建一次 OTA 检查任务。
5. 向电脑端服务器提交设备型号、设备 ID 和当前固件版本。
6. 如果服务器存在更高版本，则下载固件到非活动 OTA 分区。
7. 完成 ESP-IDF 映像校验后切换启动分区并重启。
8. 新固件启动自检通过后将映像标记为有效；自检失败则回滚。

相关文件：

- `components/OTA/OTA.c`：版本检查、下载、写入、校验、分区切换和回滚。
- `components/OTA/include/OTA.h`：OTA 对外接口。
- `components/wifi/wifi.c`：Wi-Fi 获得 IP 后触发 OTA 检查。
- `main/main.c`：启动时检查 OTA 状态。
- `version.txt`：写入固件描述信息的项目版本号。
- `partitions.csv`：`ota_0`、`ota_1` 和 `otadata` 分区定义。
- `tools/ota_server.py`：电脑端局域网 OTA 服务器。
- `tools/start_ota_server.ps1`：Windows OTA 服务器启动脚本。

## 2. 分区和回滚

当前分区表包含两个大小相同的应用分区：

| 分区 | 地址 | 大小 | 用途 |
| --- | --- | --- | --- |
| `otadata` | `0xF000` | 8 KiB | 保存 OTA 启动选择和映像状态 |
| `ota_0` | `0x20000` | 3776 KiB | 初始 USB 固件或当前应用 |
| `ota_1` | `0x3D0000` | 3776 KiB | 非活动分区或下一版固件 |

项目已经启用：

```text
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
CONFIG_APP_ROLLBACK_ENABLE=y
```

OTA 后首次启动时，新映像处于 `PENDING_VERIFY`。当前自检包括：

- NVS 是否可以正常打开。
- 内部剩余堆内存是否不低于 30000 字节。

自检通过后调用 `esp_ota_mark_app_valid_cancel_rollback()`；自检失败时调用 `esp_ota_mark_app_invalid_rollback_and_reboot()`，Bootloader 会恢复到上一个有效分区。

首次 USB 烧录建议使用完整的 `idf.py flash`，确保支持回滚的 Bootloader、分区表和 `otadata` 一并更新。

## 3. 版本号和测试标记

项目根目录的 `version.txt` 决定固件版本，例如：

```text
1.0.1
```

程序通过 `esp_app_get_description()` 读取固件内置版本，不再使用独立的硬编码版本号。

`components/OTA/OTA.c` 中的 `OTA_BUILD_MARKER` 只用于串口测试时区分固件来源：

```c
#define OTA_BUILD_MARKER "USB_BASELINE"
```

常用测试标记：

- USB 基线固件：`USB_BASELINE`
- OTA 目标固件：`OTA_UPDATED`

这个标记不参与版本比较，也不影响 OTA 功能。

## 4. 当前可直接复现的测试

当前工程源码和默认 `build` 保持为 1.0.0 USB 基线版，已经制作好的 1.0.1 OTA 固件保存在：

```text
ota_artifacts\1.0.1\EVT_ESP32-S3.bin
```

### 4.1 USB 烧录基线版本

在 ESP-IDF PowerShell 中进入工程目录：

```powershell
cd G:\esp32\zhidou_esp32\zhidou_esp32
idf.py build
idf.py -p COMx flash monitor
```

将 `COMx` 替换成实际串口。启动后应看到：

```text
[VERSION MONITOR] version=1.0.0 partition=ota_0 marker=USB_BASELINE
```

### 4.2 启动 1.0.1 OTA 服务器

电脑和 ESP32 必须位于同一个局域网。当前代码中的服务器地址是：

```text
http://192.168.1.3:8070
```

在项目目录运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\start_ota_server.ps1" -Version "1.0.1" -Firmware ".\ota_artifacts\1.0.1\EVT_ESP32-S3.bin"
```

如果当前目录不在项目中，也可以使用绝对路径：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "G:\esp32\zhidou_esp32\zhidou_esp32\tools\start_ota_server.ps1" -Version "1.0.1" -Firmware "G:\esp32\zhidou_esp32\zhidou_esp32\ota_artifacts\1.0.1\EVT_ESP32-S3.bin"
```

服务器正常启动时会显示：

```text
[OTA_SERVER] Listening on http://192.168.1.3:8070
[OTA_SERVER] Health: http://192.168.1.3:8070/health
[OTA_SERVER] version=1.0.1 size=... sha256=...
```

浏览器打开 `http://192.168.1.3:8070/health`，应返回 `firmware_exists:true`。

如果 Windows 防火墙弹出提示，应允许 Python 在专用网络上通信。服务器窗口在 OTA 完成前不要关闭。

### 4.3 触发升级

服务器启动后按一下 ESP32 复位键。当前固件每次开机、Wi-Fi 获得 IP 后只检查一次 OTA；如果第一次检查时服务器尚未运行，需要再次复位。

下载过程中应看到：

```text
[OTA CHECK] UPDATE FOUND: 1.0.0 -> 1.0.1
[OTA DOWNLOAD] target=ota_1 image=... bytes slot=... bytes
[OTA DOWNLOAD] progress=10%
...
[OTA DOWNLOAD] progress=100%
[OTA VERIFY] Image validation passed
[OTA SWITCH] Next boot partition=ota_1
[OTA RESULT] DOWNLOAD SUCCESS; rebooting in 2 seconds
```

新固件重启后应看到：

```text
[OTA BOOT] version=1.0.1 partition=ota_1
[OTA BOOT] image state=PENDING_VERIFY
[OTA RESULT] SUCCESS: version=1.0.1 confirmed valid
[VERSION MONITOR] version=1.0.1 partition=ota_1 marker=OTA_UPDATED
```

再次检查服务器时应看到：

```text
[OTA RESULT] NO UPDATE: current=1.0.1
```

最后这些日志代表检查、下载、写入、映像校验、分区切换、重启和新版本确认均已成功。

## 5. 制作新的 OTA 版本

下面以 1.0.2 为例。

1. 将 `version.txt` 改为 `1.0.2`。
2. 将 `OTA_BUILD_MARKER` 改为 `OTA_UPDATED`（仅用于测试识别）。
3. 完成代码修改后只构建，不要用 USB 把目标版本烧到测试设备。

```powershell
idf.py fullclean
idf.py build
```

4. 单独保存应用固件：

```powershell
New-Item -ItemType Directory -Force ".\ota_artifacts\1.0.2"
Copy-Item ".\build\EVT_ESP32-S3.bin" ".\ota_artifacts\1.0.2\EVT_ESP32-S3.bin"
```

5. 启动服务器：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\start_ota_server.ps1" -Version "1.0.2" -Firmware ".\ota_artifacts\1.0.2\EVT_ESP32-S3.bin"
```

服务器的 `-Version` 必须与被提供固件的 `version.txt` 版本一致。OTA 文件只需要应用映像 `EVT_ESP32-S3.bin`，不应把合并后的完整 USB 镜像用作 OTA 文件。

本项目为了反复演示，当前特意把源码恢复到 1.0.0，并把 1.0.1 独立保存。正常产品开发时，源码通常应保持在最新版本，不需要每次恢复旧版本。

## 6. 服务器接口

### `GET /health`

用于确认服务器运行状态、目标版本和固件文件是否存在。

### `POST /ota/check-update`

设备请求示例：

```json
{
  "device_id": "F1_0000001",
  "model": "F1",
  "firmware_version": "1.0.0"
}
```

存在更新时的响应示例：

```json
{
  "status": "success",
  "has_update": true,
  "latest_version": "1.0.1",
  "download_url": "http://192.168.1.3:8070/firmware.bin",
  "size": 3467072,
  "sha256": "..."
}
```

服务器使用 `x.y.z` 形式的版本号进行比较，只有服务器版本高于设备版本时才返回更新。

### `GET /firmware.bin`

向 ESP32 发送启动服务器时指定的应用固件。

服务器会计算并返回文件大小和 SHA-256，便于人工核对。目前 ESP32 端没有将响应中的 SHA-256 与下载内容单独比较，而是通过 `esp_ota_end()` 完成 ESP-IDF 映像结构及内置校验。因此当前实现不能视为具有固件来源认证能力。

## 7. 常见问题

### 没有出现 `Got IP`

设备尚未连上 Wi-Fi，因此不会进行 OTA。检查 Wi-Fi 名称、密码、2.4 GHz 网络和信号状态。

### `ESP_ERR_HTTP_CONNECT`

依次检查：

1. OTA 服务器窗口是否仍在运行。
2. 电脑 WLAN IP 是否仍为 `192.168.1.3`。
3. 浏览器能否打开 `http://192.168.1.3:8070/health`。
4. Windows 防火墙是否允许 Python 访问专用网络。
5. 电脑和 ESP32 是否在同一子网。

### 服务器启动了，但设备没有再次检查

当前是每次开机一次性检查。按 ESP32 复位键重新触发。

### 返回 `NO UPDATE`

设备版本已经等于或高于服务器版本。检查 `version.txt`、服务器的 `-Version` 参数以及实际提供的固件文件是否一致。

### 固件尺寸超过分区

当前每个 OTA 应用分区为 3776 KiB。构建输出必须小于应用分区，否则不能进行 OTA。

### `Cannot create static OTA task`

OTA 任务使用 8192 字静态内部 RAM 栈。若仍然创建失败，需要检查内部 RAM 占用、链接布局和启动阶段创建的其他任务。

### OTA 成功后仍然打印旧版本

下载过程中运行的仍是旧分区，打印旧版本是正常的。必须等到下载完成、分区切换并重启后，版本才会变化。

## 8. Wi-Fi 凭据

本地测试凭据保存在：

```text
components/wifi/include/wifi_credentials.local.h
```

该文件已经加入 `.gitignore`，不应提交到 GitHub。固件会优先使用本地配置并覆盖设备中旧的 NVS Wi-Fi 配置，便于更换测试网络。

## 9. 当前限制与量产建议

当前实现用于局域网开发测试，具有以下限制：

- 使用明文 HTTP，没有 TLS 服务器身份验证。
- 没有启用签名固件或 Secure Boot。
- 响应中的 SHA-256 尚未在设备端单独验证，且 HTTP 下该值也不能证明来源可信。
- OTA 服务器 IP 硬编码在固件中，电脑地址变化后需要重新编译。
- 每次开机只检查一次，没有定时轮询、退避重试和断点续传。
- 当前自检只覆盖 NVS 和剩余内存，不覆盖屏幕、串口、传感器及业务任务。
- 版本监视每 5 秒打印一次，仅适合联调阶段。

正式量产前建议增加：

- HTTPS 和服务器证书校验。
- ESP-IDF 签名应用、安全启动和 Flash Encryption。
- 固件摘要或签名的设备端验证。
- 可配置的服务器地址、版本发布策略及设备分组。
- OTA 自动重试、失败统计和升级结果上报。
- 覆盖关键外设和业务任务的启动自检。
- 减少周期调试日志，只保留启动、升级、成功和错误日志。

