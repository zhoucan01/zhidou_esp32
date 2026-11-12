# 本轮改动点汇总（供另一个 agent 合并代码参考）

## 一、UI 框架优化：流程状态驱动的 UI 切换

### 文件：components/myLvgl/myLvgl.c

#### 1. 新增 running_page_for_flow() 流程→运行页映射函数
- 位置：handle_press() 之前
- 作用：流程启动时由 UI 任务统一查询流程状态并切换到对应运行页
- 映射关系（注意历史命名错位）：
  - SOYMILK_SEC → UI_PAGE_RUNNING_PLANT_MILK（豆浆共用植物奶运行页）
  - TOUFU_SEC → UI_PAGE_RUNNING_TOFU
  - MILK_SEC → UI_PAGE_RUNNING_PLANT_MILK
  - SELFCLEAN_SEC → UI_PAGE_RUNNING_CLEAN
  - default → UI_PAGE_COUNT（不切换）

#### 2. handle_press() 重构：只下发事件位，不切换 UI
- 旋钮按下只设置 FuncTypeGroup + FuncOrderGroup(FuncStart_bit)
- UI 切换交给 lvUIshowTask 的流程状态轮询统一处理
- 蓝牙/串口触发同一流程时共用同一套 UI 切换逻辑
- 映射（注意错位）：
  - MODE_PLANT_MILK → SoyMilk_bit（豆浆流程）
  - MODE_TOFU → Milk_bit（植物奶流程，历史命名）
  - CLEAN_DEEP/QUICK → SelfClean

#### 3. lvUIshowTask() 新增流程状态轮询
- 新增变量：last_flow(NOTASK_SEC), last_order(FUNCSEC_STOP)
- 每轮循环查询 GetCurTaskState()/GetCurTaskOrder()
- 流程启动或切换：show_running_page(running_page_for_flow(cur_flow))
- 流程结束/停止：screen_history_depth=0; ui_show_page(UI_PAGE_HOME)

#### 4. 长按退出时下发停止命令
- 位置：isLongPress 分支
- 检测当前是否在运行页（RUNNING_PLANT_MILK/TOFU/CLEAN）
- 若是：xEventGroupSetBits(FuncTypeGroup, 1U << cur_flow) + FuncStop_bit
- 然后 return_to_previous_screen()
- 保证 Modbus 当前流程状态同步变为空闲

### 文件：components/myLvgl/include/myLvgl.h
- 移除了 mode_flag_t 枚举和 currentModeFlag extern（死代码清理）

---

## 二、组件循环依赖修复

### 问题
ToufuMake.c 调用 SoyMilkMake.c 中定义的 TouFuSoyMilk()/HeatSoyMilk()，
但缺少 #include "SoyMilkMake.h" 和 REQUIRES SoyMilkMake，导致隐式声明
（编译器假设返回 int，实际 void），运行时栈布局错误，豆腐流程崩溃。
直接添加 REQUIRES SoyMilkMake 会形成循环依赖（SoyMilkMake REQUIRES ToufuMake）。

### 修复方案：打破循环依赖

#### function/SoyMilkMake/CMakeLists.txt
- 移除 REQUIRES ToufuMake（SoyMilkMake.c 不调用 ToufuMake 中的任何函数）

#### function/Press/CMakeLists.txt
- 移除 REQUIRES ToufuMake（Press.c 不调用 ToufuMake 中的函数）

#### function/Press/Press.c
- 注释掉 #include "ToufuMake.h"（Press.c 不使用 ToufuMake.h 中的声明）

#### function/ToufuMake/CMakeLists.txt
- 新增 REQUIRES SoyMilkMake（ToufuMake.c 调用 TouFuSoyMilk/HeatSoyMilk）
- 新增 REQUIRES Press（ToufuMake.c 包含 press.h 调用 TouFuPress）

#### function/ToufuMake/ToufuMake.c
- 新增 #include "SoyMilkMake.h"（修复 TouFuSoyMilk/HeatSoyMilk 隐式声明）

### 修复后依赖图（DAG，无循环）
- ToufuMake → SoyMilkMake, Press, brine, ...
- Press → FunctionSelection, ...
- SoyMilkMake → Music, FunctionSelection, ...

---

## 三、蓝牙日志优化

### 文件：task/BlueDataDeal/BlueDataDeal.c
- 移除正常路径的 ESP_LOGI("Bluetooth Modbus request received: %u bytes") 打印
- 移除只服务于该打印的死代码 is_total_status_read() 函数
- 现在只在解析错误时打印 ESP_LOGW("Bluetooth Modbus request rejected: result=%d")

---

## 四、烧录脚本修复

### 文件：script/flash.py
- 烧录后的复位命令 esptool run 原来用 --after no_reset
  导致芯片停留在 bootloader（Staying in bootloader）
- 改为 --after hard_reset，通过 RTS 引脚硬复位
  让 ESP32 退出 bootloader 并启动用户程序

---

## 五、关键背景知识（合并时必须了解）

### 流程调度机制
- FreeRTOS EventGroups：FuncTypeGroup（流程类型位）+ FuncOrderGroup（控制位）
- 流程类型位：SoyMilk_bit=(1<<0), PRESS_bit=(1<<1), Milk_bit=(1<<2),
  TouFu_bit=(1<<3), SelfClean=(1<<4), TouFuBrain=(1<<5)
- 控制位：FuncStart_bit=(1<<0), FuncPause_bit=(1<<1),
  FuncResume_bit=(1<<2), FuncStop_bit=(1<<3)
- FuncSecType_t 枚举：SOYMILK_SEC=0, PRESSTOUFU_SEC=1, TOUFU_SEC=2,
  MILK_SEC=3, SELFCLEAN_SEC=4, BRINE_SEC=5, NOTASK_SEC=6
- FuncSecType_t 索引与流程位一一对应：1 << flow_type

### 历史命名错位（重要！）
- TOUFU_SEC(2) 对应 Milk_bit(1<<2)  ← 豆腐用 Milk_bit
- MILK_SEC(3) 对应 TouFu_bit(1<<3) ← 植物奶用 TouFu_bit
- 协议功能号：0x51=豆浆, 0x52=压制, 0x53=豆腐, 0x54=植物奶, 0x55=自清洁, 0x56=点卤
- submit_flow_control: BIT(command[0]-0x51) → FuncType bits; ffs(bits)-1 → typeIdx

### UI 页面枚举
- UI_PAGE_HOME, UI_PAGE_MODE_CLEAN..UI_PAGE_MODE_TOFU
- UI_PAGE_CLEAN_DEEP, UI_PAGE_CLEAN_QUICK
- UI_PAGE_RUNNING_PLANT_MILK, UI_PAGE_RUNNING_TOFU, UI_PAGE_RUNNING_CLEAN

### 构建/烧录
- 构建：.\build.bat（激活 ESP-IDF，运行 idf.py build）
- 烧录：.\flash.bat（调用 script/flash.py）
