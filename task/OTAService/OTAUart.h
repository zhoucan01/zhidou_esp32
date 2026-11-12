#ifndef OTA_UART_H
#define OTA_UART_H

#include <stdbool.h>
#include <stdint.h>

#include "OTAVerManager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* OTAUart: 把下挂芯片分区里的镜像推送给下挂芯片的 bootloader。
 *
 * AT32 与 ST32 共用同一条 MBUS485 通道、同一套 0x10/0xAA 帧格式、同一份
 * ACK 语义，唯一差别是 Modbus 设备站号（AT32 0x02、ST32 0x03）与镜像所在的
 * 分区（AT_Firmware / ST_Firmware）。因此本模块只有一份实现，站号与分区由
 * ota_uart_start() 的 target 入参决定，不另建 ST32 专用通道。
 *
 * 分两层：本模块只管会话状态、串口收发与版本核对，帧的字节布局与 CRC 全部
 * 交给 OTAFrame 组件（OTAFrame.h），此处不出现任何字段偏移。协议严格对齐
 * 参考工程 C:\at32f415bootloader（AT32）与 C:\stm32g4bootloader（ST），
 * 两者除站号外一致，本模块只做"复刻"，不改动参考工程。
 *
 * 模块定位与约定：
 *   - 无独立 task，由版本管理器在 OTA_VER_MGR_STATE_AT32_IAP 中逐拍调用
 *     ota_uart_process()；一次调用最多发起一次 ModbusGatewayTransaction()，
 *     不 delay、不重传；
 *   - 失败即终止：协议要求"超时不重发"，否则会造成重复 Flash 写入，因此任何
 *     应答缺失/非法/ACK 非 1 都直接进 FAIL，由调用方放弃整轮 OTA；
 *   - 成功以目标芯片实读版本为准：全部包 ACK 后读 DataCenter 的 getVersion()
 *     中该站号那一行（AT32->索引 1，ST32->索引 2），与下发的目标版本逐段比较，
 *     一致才算 DONE；
 *   - 单包有效载荷 234 字节，使 0xAA 帧长恰好等于网关单帧上限 256 字节，
 *     不改动 DataCenter 的网关缓冲与 DebugConsole 的帧容量。
 *
 * 命名规范：类型/函数 snake_case，前缀 ota_uart_；宏全大写前缀 OTA_UART_；
 * 内部静态变量 s_ 前缀；C99，只用 stdint/stdbool/stddef。 */

 /* 单包有效载荷。
 * 0xAA 帧长 = 载荷 + OTA_FRAME_OVERHEAD(22)，因此载荷取 234 时帧长为 256，
 * 正好等于 DataCenter 网关的 MODBUS_GATEWAY_FRAME_MAX。 */
#define OTA_UART_PAYLOAD_SIZE       234U

/* 镜像长度范围，与上位机 firmware_pack.py 的校验一致：
 * 版本区 1KB + SHA256 区 1KB + 原始 bin 至少 8 字节 = 2056，上限 100KB。 */
#define OTA_UART_IMAGE_MIN_SIZE     2056U
#define OTA_UART_IMAGE_MAX_SIZE     102400U

/* 推进结果。
 *   IDLE    - 尚未启动或已被复位；
 *   RUNNING - 本拍有进展（或仍在等待窗口内），需继续调用；
 *   DONE    - 目标芯片实读版本已与目标版本一致，升级成功；
 *   FAIL    - 失败，调用方应放弃整轮 OTA。 */
typedef enum {
    OTA_UART_RET_IDLE = 0,
    OTA_UART_RET_RUNNING,
    OTA_UART_RET_DONE,
    OTA_UART_RET_FAIL,
} ota_uart_ret_t;

/* 复位到 IDLE：清空会话状态（含会话 key、包号、分区句柄与收发缓冲状态），
 * 不做任何 IO。升级结束（成功或失败）后必须调用一次，避免残留会话影响下一轮。 */
void ota_uart_reset(void);

/* 启动一次 IAP 会话（只做入参校验与初始化，不发起串口事务）。
 *
 *   expected_version - 期望目标芯片升级后的版本号，即本轮云端下发的新版本，
 *                      与随后写入 OTA_Info 的字符串完全一致，形如 "1.0.0.2"。
 *                      升级结束后用它和 DataCenter 实读的同一目标版本比对。
 *   image_size       - 目标分区中镜像的字节数（即 OTANetUpgrade 落盘的
 *                      fw_size），必须在 OTA_UART_IMAGE_MIN_SIZE 与
 *                      OTA_UART_IMAGE_MAX_SIZE 之间。
 *   target           - 目标芯片，决定镜像分区（AT32 -> AT_Firmware，
 *                      ST32 -> ST_Firmware）、Modbus 站号（0x02 / 0x03）
 *                      与版本号取样行。只接受下挂芯片目标，见
 *                      ota_fw_target_has_iap()。
 *
 * 返回：true = 已进入查询阶段；false = 入参非法、目标不支持或分区缺失，
 *       会话保持 IDLE。
 *
 * 成功后由调用方逐拍调用 ota_uart_process() 推进。 */
bool ota_uart_start(const char *expected_version, uint32_t image_size, ota_fw_target_t target);

/* 推进一步。一次调用最多发起一次串口事务，任何协议违例直接返回 FAIL。 */
ota_uart_ret_t ota_uart_process(void);

/* 读取发送进度（已确认包数 / 总包数），仅用于日志与上报；IDLE 时 total 为 0。 */
void ota_uart_get_progress(uint16_t *sent, uint16_t *total);

#ifdef __cplusplus
}
#endif

#endif /* OTA_UART_H */