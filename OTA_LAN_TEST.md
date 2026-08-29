# ESP32-S3 局域网 OTA 测试

电脑和 ESP32 必须连接同一个局域网。当前电脑 WLAN 地址为 `192.168.1.3`，OTA 服务端口为 `8070`。

## 1. 首次有线烧录 1.0.0

确认 `version.txt` 内容是 `1.0.0`，然后执行：

```powershell
idf.py fullclean
idf.py build
idf.py flash monitor
```

这次完整烧录会同时更新支持回滚的 Bootloader。串口中应看到：

```text
[OTA BOOT] version=1.0.0 partition=ota_0
```

如果本地 OTA 服务器尚未启动，版本检查连接失败是正常现象。

## 2. 构建 1.0.1 OTA 固件

把 `version.txt` 改为 `1.0.1`，然后只构建，不要再次执行 `idf.py flash`：

```powershell
idf.py fullclean
idf.py build
```

待提供的文件是：

```text
build\EVT_ESP32-S3.bin
```

## 3. 启动电脑端 OTA 服务

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\start_ota_server.ps1 -Version 1.0.1
```

浏览器打开 `http://192.168.1.3:8070/health`，应看到 `firmware_exists:true`。

如果 Windows 防火墙弹窗，允许 Python 在专用网络通信。

## 4. 重启 ESP32 并观察日志

ESP32 获得 IP 后会自动检查一次。成功链路的关键日志为：

```text
[OTA CHECK] UPDATE FOUND: 1.0.0 -> 1.0.1
[OTA DOWNLOAD] progress=100%
[OTA VERIFY] Image validation passed
[OTA SWITCH] Next boot partition=ota_1
[OTA RESULT] DOWNLOAD SUCCESS; rebooting in 2 seconds
[OTA BOOT] version=1.0.1 partition=ota_1
[OTA BOOT] image state=PENDING_VERIFY
[OTA RESULT] SUCCESS: version=1.0.1 confirmed valid
```

最后一行出现才代表下载、分区切换、重启和新固件确认全部成功。

## 注意

- 这是局域网联调配置，使用 HTTP，不可直接用于公网量产。
- 电脑 IP 改变后，需要同步修改 `components/OTA/OTA.c` 中的 `OTA_CHECK_URL`，并用新的 `-HostIp` 启动服务器。
- 当前实现每次开机只触发一次 OTA 检查，避免 Wi-Fi 重连导致并发升级。
