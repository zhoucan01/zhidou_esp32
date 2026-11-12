# 命令行编译与烧录

## Windows 直接运行 BAT 脚本

BAT 脚本位于工程根目录（`script` 的上一级），Python 脚本按用途存放在 `script/build_tools/`（构建与清理）、`script/flash/`（烧录）、`script/analysis/`（数据分析）。
在工程根目录的普通 PowerShell 或 CMD 终端执行，无需手动激活环境：

```powershell
.\build.bat
.\flash.bat --port COM4
# 只检查烧录命令，不写设备
.\flash.bat --port COM4 --dry-run
# 清理旧固件包，只保留最新一个
.\clean_firmware.bat
# 预览将删除哪些包，不实际删除
.\clean_firmware.bat --dry-run
# 保留最近 3 个，删除其余
.\clean_firmware.bat --keep 3
# 清空所有固件包
.\clean_firmware.bat --keep 0
```

`build.bat` 自动初始化 ESP-IDF 环境并编译、打包；`flash.bat` 直接调用下载流程；
`clean_firmware.bat` 清理 `build/` 下的 `firmware-*` 固件包，按修改时间排序，默认保留最新 1 个。
BAT 入口内部复用 Python 脚本，仍需要安装 Python / esptool。
本机默认使用 `C:\Espressif\frameworks\esp-idf-v5.3.1` 和已安装的 IDF Python 3.11 环境。
其他安装路径可通过环境变量 `IDF_PATH`、`IDF_PYTHON_ENV_PATH` 配置；
烧录可用 `ESP_FLASH_PYTHON` 指定 Python 可执行文件的完整路径。
仅烧录的电脑没有 ESP-IDF 时使用 PATH 中的 `python`，需事先安装 esptool。

生成的烧录包也包含 `flash.bat`，拷贝整个包到其他机器后执行 `flash.bat --port COM4` 即可。
在 CMD 自动化脚本中串联编译和烧录：

```bat
call build.bat
if errorlevel 1 exit /b 1
call flash.bat --port COM4
exit /b %errorlevel%
```

以下为 Python 入口的详细说明。

脚本使用 Python 3，支持 Windows / Linux，任何工作目录下均可通过脚本路径调用。
成功返回退出码 0，失败返回非零，便于自动化串联。烧录直接调用乐鑫 esptool，不依赖 IDE 按钮，也不会触发编译。

## 编译

使用工程对应的 ESP-IDF **5.3.1**，先打开 ESP-IDF 终端，或在 PowerShell 中激活已安装的环境：

```powershell
# 按本机安装位置修改；Linux 使用 . /path/to/esp-idf/export.sh
& C:\Espressif\frameworks\esp-idf-v5.3.1\export.ps1
python .\script\build_tools\build.py
```

如果 PowerShell 执行策略阻止 `export.ps1`，可打开 ESP-IDF 自带终端，或在 CMD 中执行：

```bat
rem 本机同时安装了 Python 3.12，需指定实际已有的 IDF Python 环境。
rem 其他机器按实际路径修改；ESP-IDF 自带终端已配置时无需设置。
set "IDF_PYTHON_ENV_PATH=C:\Espressif\python_env\idf5.3_py3.11_env"
call C:\Espressif\frameworks\esp-idf-v5.3.1\export.bat
python script\build_tools\build.py
```

脚本执行 `idf.py -B <工程>/build build`，使用当前 sdkconfig（ESP32-S3）。
所有产物均在 `build/`，编译缓存在 `build/ccache/`，临时缓存在 `build/ccache/tmp/`。
成功后打印独立烧录包目录 `build/firmware-<随机后缀>/`，含烧录脚本、参数清单及全部所需 bin。
只有输出 `Build succeeded` 才表示整个编译和打包完成。换电脑编译时不要复制旧的 CMake 构建目录。

## 本机烧录

```powershell
# 在 ESP-IDF 终端中，先检查文件和命令（不会连接设备）
python .\script\flash\flash.py --port COM4 --dry-run
python .\script\flash\flash.py --port COM4
# 串口通信不稳定时降低速度
python .\script\flash\flash.py --port COM4 --baud 115200
```

串口必须显式传入，按设备实际端口修改；Linux 示例为 `--port /dev/ttyUSB0`。
默认读取工程 `build/flasher_args.json`，也可通过 `--firmware-dir D:\firmware` 指定烧录包。
地址、Flash 参数和复位方式均读取构建清单，不写死应用地址。会烧录清单中的所有镜像，包括 OTA 初始化数据。

## 在其他机器上烧录

将编译成功打印的整个 `firmware-*` 目录拷贝到目标机器，不需要源代码或完整 ESP-IDF。
安装 Python 3 和对应串口驱动后执行：

```powershell
python -m pip install "esptool>=4.8,<6"
cd D:\firmware
python .\flash.py --port COM4
```

脚本兼容 esptool 4.x / 5.x 命令拼写。离线机器需提前准备 Python、esptool 及其依赖。
烧录前关闭占用串口的监视器。支持自动下载复位的硬件可全程命令行操作；若下载器未连接自动复位线路，仍需按住 BOOT 后复位进入下载模式。

## 自动编译后烧录（PowerShell）

```powershell
python .\script\build_tools\build.py
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
python .\script\flash\flash.py --port COM4
exit $LASTEXITCODE
```

参考：[乐鑫 esptool 烧录说明](https://docs.espressif.com/projects/esptool/en/latest/esp32/esptool/flashing-firmware.html)。
