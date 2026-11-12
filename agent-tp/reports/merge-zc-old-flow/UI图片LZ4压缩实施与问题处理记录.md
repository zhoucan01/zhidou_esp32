# UI 图片 LZ4 压缩实施与问题处理记录

## 1. 目的与最终方案

当前 UI 图片由 SquareLine 生成，是编译进固件的 `LV_IMG_CF_TRUE_COLOR_ALPHA` 静态数组。在 `LV_COLOR_DEPTH=16` 时，每个像素占 3 字节（RGB565 2 字节加 Alpha 1 字节），因此源代码文件看起来只有若干 KB，并不代表图片在 Flash 中也只有若干 KB。

本工程采用以下方案：

- 电脑端使用 **LZ4 HC -9** 对图片原始像素数组进行离线压缩。
- ESP32-S3 固件只编译 `lz4.c`，运行时调用 `LZ4_decompress_safe()`。
- 解压缓冲区使用 PSRAM，不占用主要内部 RAM。
- LVGL 图片缓存保存当前页面已经解压的图片，避免分块刷新时重复解压。
- 切换逻辑页面时清空图片缓存，释放上一页占用的 PSRAM。
- 原始图片源文件暂时保留；页面改用压缩描述符后，链接器会移除未被引用的原始数组。

HC -9 只影响电脑端压缩时间和压缩率。LZ4 Fast 与 LZ4 HC 产生的数据都使用普通 LZ4 Block 格式，ESP32-S3 端的解压算法和解压速度相同。因此静态固件资源优先使用 HC -9。

## 2. 相关目录和文件

```text
function/LZ4/
    lz4.c
    lz4.h
    lz4hc.c
    lz4hc.h
    CMakeLists.txt

function/UI/images/          原始图片 C 数组
function/UI/images_lz4/      压缩图片 C 数组
function/UI/ui_lz4_decoder.c LVGL LZ4 解码器
function/UI/ui_lz4_decoder.h
agent-tp/tools/ui_lz4_pack/ui_lz4_pack.py         电脑端单图压缩工具
.venv/lz4/                   电脑端 Python 工具环境，不提交 Git
```

固件只需要编译 `function/LZ4/lz4.c`。`lz4hc.c` 和 `lz4hc.h` 用于 C 语言电脑端压缩程序，本工程实际使用 Python `lz4` 包执行 HC -9，因此不把 `lz4hc.c` 编进 ESP32-S3 固件。

## 3. 电脑端环境安装

### 3.1 最初使用过的路径及问题

最初把虚拟环境创建在：

```text
build/ccache/lz4-venv
```

该环境能够正常安装和运行，但 `build/` 是可删除、可重建的构建产物目录。执行清理、重新生成构建目录或删除 `build/` 后，虚拟环境也会消失，最终导致批量压缩时报错：

```text
Resolve-Path: 找不到路径 ...\build\ccache\lz4-venv\Scripts\python.exe
```

因此，长期使用的工具环境不能放在 `build/` 下。

### 3.2 最终使用的稳定路径

最终将虚拟环境放到工程根目录：

```text
.venv/lz4
```

`.gitignore` 已加入：

```gitignore
.venv/
```

这样环境不会提交到 Git，也不会因为清理固件构建目录而丢失。

创建环境：

```powershell
& 'D:\Python\python.exe' -m venv '.venv\lz4'
& '.venv\lz4\Scripts\python.exe' --version
```

本次使用的版本为 Python 3.12.3。

### 3.3 安装 Python LZ4 包

首次安装时出现过代理错误：

```text
ProxyError('Cannot connect to proxy.')
ConnectionResetError(10054, ...)
```

原因是 PowerShell 会话中的代理环境变量指向了不可用代理。解决方式是对当前命令清除代理变量，并显式使用 PyPI：

```powershell
$env:PIP_CACHE_DIR = "$PWD\build\ccache\pip-cache"

'HTTP_PROXY','HTTPS_PROXY','ALL_PROXY','PIP_PROXY' |
    ForEach-Object {
        Remove-Item "Env:$_" -ErrorAction SilentlyContinue
    }

& '.venv\lz4\Scripts\python.exe' `
    -m pip install `
    --isolated `
    --index-url 'https://pypi.org/simple' `
    --timeout 60 `
    --retries 3 `
    lz4
```

本次最终安装成功的版本为 `lz4 4.4.5`。验证命令：

```powershell
& '.venv\lz4\Scripts\python.exe' `
    -c "import lz4.block; print('LZ4 OK')"
```

预期输出：

```text
LZ4 OK
```

## 4. 电脑端压缩格式

`agent-tp/tools/ui_lz4_pack/ui_lz4_pack.py` 完成以下操作：

1. 从 SquareLine 生成的图片 `.c` 文件读取 `ui_img_xxx_data[]`。
2. 读取图片宽度和高度。
3. 检查格式必须为 `LV_IMG_CF_TRUE_COLOR_ALPHA`。
4. 检查原始数组长度是否等于 `宽 × 高 × 3`。
5. 使用以下参数压缩：

```python
lz4.block.compress(
    raw_data,
    mode="high_compression",
    compression=9,
    store_size=False,
)
```

6. 在电脑端立即解压并逐字节比较，校验压缩数据。
7. 在压缩 Block 前增加四字节标志 `LZ4I`。
8. 生成 `LV_IMG_CF_RAW_ALPHA` 图片描述符，符号名增加 `_lz4` 后缀。

例如：

```text
ui_img_wait_png       原始描述符
ui_img_wait_png_lz4   压缩描述符
```

设备端通过 `LV_IMG_CF_RAW_ALPHA` 和 `LZ4I` 标志识别自定义压缩图片。

## 5. 单张图片验证

单图压缩命令：

```powershell
& '.venv\lz4\Scripts\python.exe' `
    'agent-tp\tools\ui_lz4_pack\ui_lz4_pack.py' `
    'function\UI\images\ui_img_wait_png.c' `
    'function\UI\images_lz4\ui_img_wait_png_lz4.c'
```

`ui_img_wait_png` 的实际结果：

| 项目 | 结果 |
|---|---:|
| 图片尺寸 | 182 × 448 |
| 原始大小 | 244,608 字节 |
| 压缩大小 | 1,147 字节 |
| 压缩占比 | 0.47% |
| 理论节省 | 243,461 字节 |
| 电脑端解压校验 | 通过 |

该图片大面积像素重复，因此压缩率特别高，不代表所有图片都会达到相同比例。

## 6. ESP32-S3 端 LZ4 组件

`function/LZ4/CMakeLists.txt` 只编译普通解压器：

```cmake
idf_component_register(
    SRCS
        "lz4.c"
    INCLUDE_DIRS
        "."
)
```

不要把 `lz4hc.c` 加入固件构建，因为设备端不执行 HC 压缩。

UI 组件的 `function/UI/CMakeLists.txt` 已完成以下接入：

- 增加 `images_lz4/*.c` 文件集合。
- 增加 `ui_lz4_decoder.c`。
- 增加对 `LZ4` 组件的依赖。

## 7. LVGL 自定义解码器

`ui_lz4_decoder.c` 的主要流程是：

1. 只接受变量图片源。
2. 检查描述符格式为 `LV_IMG_CF_RAW_ALPHA`。
3. 检查数据前四字节为 `LZ4I`。
4. 根据 `宽 × 高 × LV_IMG_PX_SIZE_ALPHA_BYTE` 计算解压大小。
5. 使用以下能力标志从 PSRAM 分配缓冲区：

```c
MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
```

6. 调用 `LZ4_decompress_safe()`。
7. 将解码后的格式交给 LVGL 作为 `LV_IMG_CF_TRUE_COLOR_ALPHA`。
8. 缓存失效或图片关闭时释放 PSRAM。

解码器在 `ui_init()` 中注册，注册位置在创建 UI 图片对象之前：

```c
ui_lz4_decoder_init();
```

## 8. 单图替换和 Flash 结果

`ui.h` 中声明压缩描述符：

```c
LV_IMG_DECLARE(ui_img_wait_png_lz4);
```

使用该图片的页面将：

```c
IMG(ui_img_wait_png, ...)
```

替换为：

```c
IMG(ui_img_wait_png_lz4, ...)
```

替换一张图片后的构建结果：

| 项目 | 大小 |
|---|---:|
| 固件 BIN | 4,514,960 字节（4.31 MiB） |
| OTA 分区 | 8,007,680 字节（7.64 MiB） |
| 占用率 | 56.38% |
| 分区剩余 | 3,492,720 字节（3.33 MiB） |
| Flash Data | 3,438,992 字节 |
| Flash Code | 940,022 字节 |

与替换前相比，固件净减少约 241,376 字节。净减少量略小于图片理论节省量，是因为固件新增了 LZ4 解码代码和压缩描述符。

## 9. 刷新明显变慢的问题

### 9.1 现象

只替换一张压缩图片后，界面刷新明显变慢。

### 9.2 根因

当时配置为：

```text
CONFIG_LV_IMG_CACHE_DEF_SIZE=0
```

```c
#define LV_IMG_CACHE_DEF_SIZE 0
```

值为 0 表示完全关闭 LVGL 图片缓存。LVGL 使用较小显示缓冲区分块绘制屏幕时，会多次打开同一图片；自定义解码器因此重复执行“申请约 244 KB PSRAM、解压、绘制、释放”，造成明显卡顿。

### 9.3 解决方法

将缓存项数量设为 16：

```text
CONFIG_LV_IMG_CACHE_DEF_SIZE=16
```

```c
#define LV_IMG_CACHE_DEF_SIZE 16
```

`16` 表示最多保存 16 个图片解码会话，不是 16 KB。当前工程真正控制托管 LVGL 组件编译的是 `sdkconfig` 中的 `CONFIG_LV_IMG_CACHE_DEF_SIZE`；同步修改 `main/lv_conf.h` 是为了保持两套配置一致，避免以后更换 LVGL 配置方式时产生歧义。

在 `ui_show_page()` 切换逻辑页面时清空上一页缓存：

```c
if (page != current_page) {
    lv_img_cache_invalidate_src(NULL);
}
```

最终效果：

- 第一次进入页面时只解压一次。
- 同一页面后续分块刷新直接读取 PSRAM。
- 离开页面时释放上一页解压数据。
- 开启缓存后刷新速度已基本恢复正常。

当前测试图片在缓存期间占用约 244,608 字节 PSRAM。

## 10. 批量压缩失败及最终解决

### 10.1 失败命令和现象

批处理最初使用：

```powershell
Resolve-Path 'build\ccache\lz4-venv\Scripts\python.exe'
```

PowerShell 报告路径不存在。第一条赋值失败后，`$lz4Python` 为空，后面的循环自然不能启动压缩。

### 10.2 根因

虚拟环境保存在可清理的 `build/` 目录下，后续构建目录被重新生成后，该环境已经不存在。这不是图片格式、压缩算法或批处理循环的问题。

### 10.3 最终修复

1. 在稳定路径 `.venv/lz4` 重建 Python 环境。
2. 将 `.venv/` 加入 `.gitignore`。
3. 重新安装并验证 `lz4 4.4.5`。
4. 批处理改用 `.venv/lz4/Scripts/python.exe`。
5. 保留 pip 下载缓存于可清理的 `build/ccache/pip-cache`，但不再把工具本体放入 `build/`。

最终批量压缩命令：

```powershell
$lz4Python = (Resolve-Path '.venv\lz4\Scripts\python.exe').Path
$packer = (Resolve-Path 'agent-tp\tools\ui_lz4_pack\ui_lz4_pack.py').Path
$inputDir = (Resolve-Path 'function\UI\images').Path
$outputDir = Join-Path $PWD 'function\UI\images_lz4'

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$processed = 0

Get-ChildItem -LiteralPath $inputDir -Filter '*.c' -File |
    ForEach-Object {
        $outputFile = Join-Path $outputDir ($_.BaseName + '_lz4.c')

        & $lz4Python $packer $_.FullName $outputFile

        if ($LASTEXITCODE -ne 0) {
            throw "压缩失败：$($_.FullName)"
        }

        $processed++
    }

Write-Output "PROCESSED=$processed"
```

检查输出数量：

```powershell
(Get-ChildItem 'function\UI\images_lz4' -Filter '*.c' -File).Count
```

本次实际结果：

```text
PROCESSED=111
GENERATED=111
```

全部 111 张原始图片都是 `LV_IMG_CF_TRUE_COLOR_ALPHA`，本次批量压缩已全部成功。

## 11. 当前状态与后续步骤

当前已完成：

- 电脑端 HC -9 压缩工具可用。
- 111 张图片已生成对应 LZ4 资源。
- ESP32-S3 LZ4 解码器已经接入。
- PSRAM 解压已经通过单图验证。
- LVGL 缓存和页面切换释放机制已经启用。
- 当前 12 个逻辑页面引用的 69 个不同图片资源已全部映射到 `_lz4` 描述符。
- 同一页面动态更换图片源时会释放旧源缓存，避免 PSRAM 逐渐累积。
- 全量替换已经通过完整编译和链接检查。
- 最终 ELF 中保留 69 个 LZ4 图片数组，原始图片数组数量为 0。

全量替换后的构建结果：

| 项目 | 大小 |
|---|---:|
| 固件 BIN | 1,427,024 字节（1.361 MiB） |
| OTA 分区 | 8,007,680 字节（7.637 MiB） |
| 占用率 | 17.82% |
| 分区剩余 | 6,580,656 字节（6.276 MiB） |
| Flash Data | 350,640 字节 |
| Flash Code | 940,438 字节 |
| Flash 映射段合计 | 1,291,078 字节 |

当前尚未完成：

- 烧录设备并逐页检查图片内容、刷新速度和页面切换。
- 在设备上测量运行时 PSRAM 峰值。
- 全部验证通过后再决定是否从构建中移除原始图片源。

不要在验证完成前删除 `function/UI/images/`。即使保留原始源文件，只要页面不再引用原始描述符，链接器也会自动移除未使用的原始图片数据，不会继续占用最终固件 Flash。
