### 一号样机代码

OTA 设计、测试、回滚及故障排查请参阅 [OTA_README.md](OTA_README.md)。

## 使用说明
代码分为5个部分，分别为 **function**、**hardware**、**components**、**task**、**main**。
**components** 文件夹包含各种片内外设的初始化，**sys** 负责管理各种外设的使用，**sysconfig.h** 包含各种头文件，**sysdepecated.h** 包含已经不使用的**外设**，**sysiicconfig.h** 包含 **iic** 的管理，**syspin.h** 管理**引脚**的使用，**syspwmconfig.h** 管理 **pwm** 的使用。
