# 开机热点配网与网页 OTA

## 已实现行为

1. 使用现有编码器按钮。开机计时 30 秒内完成持续 5 秒按压，打开配网热点；例如按下时间为第 25 秒时，最晚在第 30 秒触发。第 26 秒才开始按压不会触发配网。触发只发生在编码器任务，不在 GPIO ISR 内初始化网络。
2. 为避免 2 秒普通长按先执行，符合配网窗口的按压会等待 5 秒判定；若在 2～5 秒间松开，仍派发普通长按。窗口外保留原有 2 秒长按；短按正常。配网手势不会同时触发普通点击或长按。
3. AP 名称直接复制蓝牙 DeviceName，当前格式 NUVELL-XXXX（蓝牙 MAC 最后两字节的大写十六进制，与原蓝牙命名完全一致）。热点为开放网络，最多 4 个客户端。
4. 切换 APSTA，AP 默认地址 172.27.231.1。DHCP 显式向客户端下发该地址作为 DNS；本地 DNS 将 A 查询引导到 AP 地址，AAAA 返回空答案。HTTP 未知路径及非本地 Host 重定向到 http://172.27.231.1/，覆盖常见 captive portal 探测。
5. 热点开启时自动请求异步扫描，最多显示 16 条。网页支持下拉选择、重新扫描、手动输入隐藏网络 SSID、密码输入；开放网络允许空密码。SSID 按 32 字节校验，WPA 密码 8～63 字节，64 字节只接受十六进制 PSK。
6. 提交连接通过队列交给唯一 WiFiConnect 任务。网页轮询显示连接中、成功/IP、失败及原因码；单次连接超时 15 秒，配网失败等待用户重试，不切回工厂路由器覆盖本次输入。
7. 成功取得 IP 并确认目标 SSID 后才写入现有 NVSWifi 命名空间的 USRWifissid/USRWifiPassword。提交错误密码不会覆盖上次成功保存的凭据。下次启动优先连接保存网络；没有保存 SSID 才使用既有固定 NUVELL 配置。普通断线继续重试。
8. 网页显示 OTAServiceGetFirmwareVersion() 版本，按钮调用 OTAServiceTriggerCheck()。未联网时显示开始联网并恢复 STA 连接，最多等待 30 秒后失败；有网后由 OTAService 任务执行检查。
9. OTA 对外提供加锁的进度快照：开始联网/检查、下载中与字节进度、下载完成、无新版本、失败。网络 HTTP 和写 Flash 工作仍在 OTAService 上下文执行，网页 handler 不执行下载。下载中拒绝网页扫描和网络切换，重复 OTA 请求返回忙。
10. 保留原 OTA 次序 AT32 → ESP32。AT32 当前仅下载落盘，页面不会宣称已烧写 AT32；ESP32 下载校验提交后沿用原有 2 秒延迟自动重启。

## 主要代码

- [WiFiPortal](../../../task/WiFiPortal/WiFiPortal.c)：HTTP、DNS、等待联网后的 OTA 触发。
- [网页固件资源](../../../task/WiFiPortal/portal.html)：选择、手输、连接结果、版本与 OTA 进度。
- [WiFiConnect](../../../task/WiFiConnect/WiFiConnect.c)：唯一的驱动配置/扫描/连接管理、NVS 成功后保存。
- [wifi 初始化](../../../components/wifi/wifi.c)：只初始化协议栈，移除与任务争抢连接和配置的旧回调；旧公开函数保留并转发。
- [编码器](../../../components/MyEncoder/MyEncoder.c)和[时序判定](../../../components/MyEncoder/include/boot_hold.h)。
- [OTAService](../../../task/OTAService/OTAService.c)、[下载进度](../../../task/OTAService/OTANetUpgrade.c)。

热点与 HTTP 服务仅由物理开机手势开启，平常不启动 HTTP/DNS socket。新增 WiFiPortal 任务的 4 KiB 内部栈开机常驻；启用门户后 HTTP 任务栈为 6 KiB，另有 TCB、socket、AP/扫描/JSON 分配。HTTP 扫描结果临时副本放 PSRAM；扫描缓存和队列仍有内部占用。实际 RAM 与栈余量需要上板日志测量，本次未再调整显示缓冲。

请求体限制 512 字节，HTTP 收发超时 3 秒。POST 只接受 JSON，校验显式 Origin，不反射密码。SSID 通过 DOM textContent/value 生成选项，不作为 HTML 解释。DNS 解析拒绝截断包、压缩问题名和多问题包，响应不超过 512 字节。

## 验证结果

- 使用 nv_build 在实际系统环境完成编译、链接、镜像生成和分区容量检查。包含下述提示音和串口诊断修正的最终固件大小 0x35bca0 B，应用槽 0x7a3000 B，剩余 0x447360 B（56%）；便携包 build/firmware-0xv1vkcz。构建保留已有的 dry_boil_handle 未使用等历史警告，没有编译错误。
- [DNS/时序/凭据边界测试](../../tests/firmware/test_wifi_portal.c)通过；使用 AddressSanitizer 和 UndefinedBehaviorSanitizer。当前运行环境不支持 LeakSanitizer 的 ptrace 模式，测试没有动态分配，执行时仅关闭泄漏检测，地址与未定义行为检测保持开启。
- [实际按钮处理代码测试](../../tests/firmware/test_boot_button.c)通过，验证满 5 秒触发、不会先触发 2 秒长按、不重复点击、短按、2～5 秒松开、窗口外普通长按及 25/26 秒边界。
- 浏览器使用真实 HTML 与模拟 fetch 响应，验证扫描下拉选项、自动填入名称、密码提交、成功/失败提示、版本、OTA 50% 进度和完成；含 `<img ...>` 的 SSID 保持纯文本，未生成图片元素。最初尝试路由拦截导航超时，改为浏览器内模拟 fetch 后完成上述检查；这不代表真实设备网络已验证。
- 编辑器诊断及 git diff --check 通过。未烧录、未实机按键、未连接手机、未执行真实 OTA。

已核对实际 SDK 的 DHCP 状态机：停止 DHCP 修改 DNS 后，显式调用 esp_netif_dhcps_start 使未启动的 AP 网卡恢复 DHCP_INIT；随后 AP_START 默认处理器才能自动启动租约服务，避免热点有信号但不能获取地址。

复现主机测试（从工程根目录）：

```bash
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Itask/WiFiPortal -Itask/WiFiConnect -Icomponents/MyEncoder/include agent-tp/tests/firmware/test_wifi_portal.c -o build/test_wifi_portal
ASAN_OPTIONS=detect_leaks=0 build/test_wifi_portal
perl -0777 -ne 'if (/(static TickType_t wait_ticks.*?)(?=void enctest)/s) {print $1} else {die "button code not found\n"}' components/MyEncoder/MyEncoder.c > build/encoder_button.inc
gcc -std=c11 -Wall -Wextra -Werror -Ibuild -Icomponents/MyEncoder/include agent-tp/tests/firmware/test_boot_button.c -o build/test_boot_button
build/test_boot_button
```

## 上板验收

1. 开机前 25 秒内开始按住，持续满 5 秒，扫描到与蓝牙同名热点；窗口外不误开 AP。
2. Android/iOS/Windows 连接 AP，检查自动弹页；若系统未弹出，在浏览器手动访问 http://172.27.231.1/。HTTPS、私人 DNS、VPN 和系统策略可能阻止 captive portal 自动弹页，不能强制重定向任意 HTTPS 网站。
3. 扫描/手输网络，分别测试正确密码、错误密码、隐藏 SSID 和开放网络。测试路由器信道改变时手机短暂断开后能重新访问门户。
4. 成功联网后重启，确认连接保存网络而非固定 NUVELL；错误配网不覆盖旧凭据。
5. OTA 分别验证无网络、无更新、下载中、校验失败及 ESP32 完成重启。重启会关闭本轮热点；需要重新进入配网窗口才能再次打开。
6. 记录门户开启前后 RAM、HTTP/WiFiConnect/WiFiPortal 栈水位和控制通信延迟。

## 配网提示、地址和串口失效排查补充

- AP 改为 172.27.231.1/24，网关和 DNS 同地址。统一维护在 task/WiFiConnect/portal_network.h，DHCP 地址、DNS A 响应、HTTP 重定向、Host/Origin 检查同步引用。此网段比常见路由器网段少见，但不能保证所有企业网/VPN 环境绝无冲突。
- 增加 MusicPlayProvisioningReady：HTTP 与 DNS 均已启动后仅播放一次三音上行提示，向已有蜂鸣器队列提交 100、100、180 ms 音符。Buzzer_Beep 的底层为零等待 xQueueSend，门户和串口任务不等待整段音乐，不新增音乐任务。蜂鸣器使用原有 GPIO45/LEDC timer3，未配置 UART 引脚。
- 串口增加 PROVISION:ENTER、AP_START、AP_STEP、AP_READY、HTTP_READY、DNS_READY、READY、SCAN_DONE；失败输出 AP_FAILED/FAILED、步骤名和 ESP 错误。AP_START 打印内部余量、任务栈水位；SCAN_DONE 打印扫描数与栈水位，READY 打印热点名及 URL。
- 原实现扫描完成时在 WiFiConnect 的 4096 B 栈上放置 16 条 AP 记录。原目标文件 finish_scan 栈帧为 0x5b0=1456 B，任务入口为 0x170=368 B；修正后扫描临时数组改为 PSRAM 分配并释放，失败清理驱动扫描列表。新 finish_scan 栈帧为 64 B，任务入口为 224 B，AP 初始化辅助函数为 288 B。这是明确的栈压力改善，不证明原现场已经发生栈溢出。
- AP 初始化改为返回错误并清理，不用本模块 ESP_ERROR_CHECK 在 AP/DHCP 失败时主动中止。使用可返回错误的 esp_netif_new/attach/default handlers API，避免默认创建封装内部的断言。基础开机 Wi-Fi 初始化原有错误检查仍保留。
- 已检查现有串口代码：AP 流程没有调用 UART 删除、改波特率或改引脚；DebugConsole 与 DataCenter 任务仍运行。但尚无用户现场异常日志，不能确定是复位、栈/内存异常、射频供电问题还是上位机命令解析/响应问题，不能声称串口故障已实机修复。
- 更新 DNS 地址边界测试并通过（ASan/UBSan，关闭环境不支持的泄漏检查）；nv_build 最终成功。未烧录、未实测声音/新网段 DHCP/串口并发。

建议下一次从按键前开始记录日志，确认最后一条 PROVISION:AP_STEP 及是否有 AP_FAILED、Guru Meditation、stack overflow、brownout、重新启动横幅。若 READY 与 HEARTBEAT 一直存在但命令无响应，应继续查 UART RX/网关协议路径，而非判断整机被 AP 卡死。

## 屏幕与串口掉线排查

当前屏幕 D1=GPIO20、D2=GPIO19（`components/sys/include/sys.h`），与 ESP32-S3 原生 USB D+/D- 复用。若电脑使用原生 USB，不能同时作为 LCD 数据线使用，需调整实际屏幕接线及引脚定义，或使用独立 USB 转串口连接 UART0。若电脑使用独立 USB 转串口而设备整体消失，应检查屏幕及 Wi-Fi 同时运行时的供电、USB 线缆和共地；代码引脚冲突不能单独解释桥接芯片从电脑消失。尚未通过实机确认具体接口与电源波形。

配网页面原因 263 来自 `ESP_ERR_TIMEOUT`（0x107），不是密码错误的直接证据。网页已明确提示连接超时；串口增加断开上一连接及等待 IP 的失败阶段日志。

用户进一步确认串口接 TX0/RX0，并提到 36、37 脚（物理脚号还是 GPIO 号待确认）。当前 DebugConsole 使用 UART0 默认映射，没有把 UART0 重映射到 GPIO36/37；工程 GPIO36/37 分别为 UART2/UART1 的 RS485 DE 输出。因此不能将原生 USB 引脚复用直接认定为本次串口消失原因。独立 USB 转串口若从电脑设备列表消失，优先核对桥接芯片供电/USB 枚举及屏幕接入后的电源扰动；若确为 GPIO36/37 接串口，需要先纠正接线用途。

## 联网失败日志（CONNECT:）

联网诊断独立于 RAM 诊断开关，正常 INFO 日志等级可见。按用户要求，`CONNECT:CREDENTIALS` 在实际发起每次连接时打印 SSID、明文密码和密码字节长度（包括开机自动连接及重试）；不打印原始 JSON。方括号用于辨认空密码及首尾空格。

- `HTTP_REJECTED` / `HTTP_SUBMIT`：网页请求被格式/Origin/OTA 状态拦截，或已提交给联网队列。
- `REQUEST`：目标 SSID 和队列结果；ESP_OK 仅表示接受请求。
- `BOOT` / `BEGIN`：凭据来源（NVS/出厂备用）、SSID、安全阈值、重试次数、AP 状态及超时时间。
- `STOP_SCAN` / `DISCONNECT_PREVIOUS` / `SET_CONFIG` / `DRIVER_START`：驱动各调用的 esp_err_t 名称与数值；上一连接断开事件等待超时单独标记。
- `ASSOCIATED`：已关联路由器，包含信道与认证方式，此后等待 DHCP 地址。
- `GOT_IP`：获得地址、掩码、网关；`LOST_IP` 表示地址丢失。
- `DISCONNECTED`：驱动断线原因码与名称；`local_request=1` 表示处于本程序 disconnect_sta 主动断开等待区间，不能将此时断线直接归为认证失败。其他停止/重试路径的直接断开不设置此标记。
- `TIMEOUT`：区分 `disconnect_previous`、`association_or_auth` 与 `dhcp_wait`，输出耗时、最后错误以及后续是否自动重试。
- `SAVE_CREDENTIALS`：成功联网后 NVS 保存结果。

常见驱动原因：201=NO_AP_FOUND（未找到 AP）、202=AUTH_FAIL、203=ASSOC_FAIL、204=HANDSHAKE_TIMEOUT、200=BEACON_TIMEOUT、210=NO_COMPATIBLE_SECURITY、211=AUTHMODE_BELOW_THRESHOLD、212=RSSI_BELOW_THRESHOLD。驱动原因码不能交给 esp_err_to_name 解释；263 是 esp_err_t 的 ESP_ERR_TIMEOUT，不是 Wi-Fi 驱动断线原因。认证/握手失败也可能与信号或路由器配置有关，不能只凭单一码确定密码错误。

复现时从网页点击连接前开始保存串口，一直记录到 GOT_IP 或 TIMEOUT。已加日志并构建验证，尚需实机联网失败样例确认根因。

## 263 根因定位与修复：等待不存在的断开事件

设备样例在 `DISCONNECT_PREVIOUS result=ESP_OK` 后报告 `TIMEOUT stage=disconnect_previous wait_ms=1000`，从未到达 `SET_CONFIG` / `DRIVER_START`。因此该次 263 在连接路由器之前由程序生成，不能用于判断路由器密码错误。

修复保留原有一秒事件等待；若未收到断开事件，调用 `esp_wifi_sta_get_ap_info` 核实驱动状态。仅当返回 `ESP_ERR_WIFI_NOT_CONNECT` 时，打印 `DISCONNECT_CONFIRMED` 并继续连接；驱动仍关联或状态未知时保留超时保护。断开 API 的其他失败现在原样返回，不再静默忽略。该改动避免已断开的 STA 没有新断线事件而反复阻断配网，不延长原有等待。

注意日志中的第二次请求 `password_len=0` 表示提交了空密码。页面每次提交后会清空密码框，重试受密码保护的网络需要重新填写。

主机回归直接提取生产 `disconnect_sta` 函数到 `build/wifi_disconnect.inc`，用 `agent-tp/tests/firmware/test_wifi_disconnect.c` 验证六种情况：无事件但已断开、正常事件、仍关联、查询失败、断开 API 失败、API 直接返回未连接。六例通过；主机桩不能证明实机射频或事件时序。

## 配网提示旋律与版本 0.0.1.2

按键长按进入配网、HTTP/DNS 就绪后的提示音从三音 380 ms 改为八音 C 大调旋律（C4 E4 G4 C5 B4 G4 A4 C5），总标称时长 2220 ms，结尾延长。保持已有蜂鸣器任务异步播放，不阻塞门户任务。修复蜂鸣器原先将队列接收超时当作音符持续时间的问题：有排队音符时会立即切换、无法按谱播放；现在每个音符实际等待其时长后停止，再取下一个，并支持静音音符。蜂鸣器声音按队列顺序播放，已有消息排队时新旋律会相应延后。

`config/firmware_version.txt` 从 0011 改为 0012，通过统一版本配置让 ESP32 的 Modbus 本机版本上报为 0.0.1.2（站1 FC04 寄存器0=0x0000、寄存器1=0x0102）。OTA 网页和其他使用统一版本源的功能同步更新；站2/3由各自设备上报的版本不改。实际听感仍需烧录后确认。

## 双播放模式与新版配网音乐

按后续要求，播放模式改为每条蜂鸣器消息携带：`BUZZER_PLAY_QUICK` 保留原队列接收超时的快速切换行为；`BUZZER_PLAY_FULL` 按每个音符的时长播放，并在音符末尾留 10% 静音间隔。模式不使用全局开关。原 `Buzzer_Beep` 及 C++ `Beep` 默认 QUICK，既有按键/告警/Play_Music 调用保持快速播放；仅 `MusicPlayProvisioningReady` 使用新增 `Buzzer_BeepWithMode(..., BUZZER_PLAY_FULL)`。

配网使用新旋律 C5 E5 G5 E5 D5 B4 D5 C5，共八音、标称 2700 ms，末音 720 ms 收尾。旧小星星曲谱保留。已有十条消息蜂鸣器队列及任务继续复用，不创建新任务；队列写入仍为零等待，队列忙满时沿用原丢弃策略。完整播放期间后续提示音按队列排队，播放结束后快速模式继续按旧方式工作。固件版本保持 0.0.1.2。

验证：nv_build 编译通过，镜像 0x35b870 字节，未烧录。主机环境无 g++，以 C 桩运行提取自生产蜂鸣器 Task 的函数体（仅去掉 C++ 方法限定符），`agent-tp/tests/firmware/test_buzzer_modes.c` 验证 QUICK、FULL、模式切换和静音四种情况通过。听感尚需实机确认。
