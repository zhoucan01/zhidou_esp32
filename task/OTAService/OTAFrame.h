#ifndef OTA_FRAME_H
#define OTA_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* OTAFrame: 下挂芯片 bootloader 的 0x10 / 0xAA 帧组帧、解帧与校验，纯函数组件。
 *
 * 只由 OTAUart 调用：不持会话状态、不做 IO、不记日志、不依赖 ESP-IDF，因此可以
 * 在主机侧直接单测。帧格式与参考工程 C:\at32f415bootloader（bootloader/project/app
 * 的 modbus.c、upgrade.c）以及上位机 AT32_OTA_PC/protocol.py 逐字节一致；下挂
 * ST 芯片（C:\stm32g4bootloader）除站号外帧格式完全相同，因此站号做成入参：
 * AT32 用 0x02，ST 用 0x03，其余字节不变。
 *
 *   0x10 查询帧（14B）：SA 10 | reg 00E6 | count 3 | ota_status u16 | key u32 | CRC16
 *   0x10 控制帧（20B）：SA 10 | reg 00E6 | count 4 | u32=8 | ota_status u16 |
 *                       key u32 | operation u16 | CRC16
 *   0xAA 数据帧（22+N）：SA AA | L u16 | protocol u16 | key u32 | L1 u16 |
 *                       total u16 | packet u16 | payload | CRC32(payload) u32 | CRC16
 *   0xAA 应答帧（7B）  ：SA AA | ack u8 | packet u16 | CRC16
 *   0x10 查询应答（14B）：SA 10 | reg 00E6 | count 3 | status u16 | key u32 | CRC16
 *   0x10 控制应答（8B） ：SA 10 | reg 00E6 | count 4 | CRC16
 *
 * SA 为目标芯片站号，由调用方按目标传入，本组件不假设固定站号。
 *
 * 多字节字段除 CRC16 外全部大端；CRC16 为 Modbus 变体（反射多项式 0xA001、
 * 初值 0xFFFF、无末尾异或、附加时低字节在前）；CRC32 为 ISO-HDLC（等价
 * binascii.crc32），只覆盖本包载荷、线上大端。
 *
 * 组帧函数一律写入调用方提供的缓冲并返回实际帧长，返回 0 表示容量不足；解帧
 * 函数只判定格式与 CRC，不解读业务语义，因此重发、超时与状态迁移全部留在
 * OTAUart 里。单包载荷上限属于传输约束（受网关单帧 256 字节限制），定义在
 * OTAUart.h 的 OTA_UART_PAYLOAD_SIZE，本组件只按入参计算帧长。 */

/* 0xAA 帧除载荷外的固定开销：帧头 16 + CRC32 4 + CRC16 2。 */
#define OTA_FRAME_OVERHEAD          22U

/* 0xAA 帧头长度：02 AA | L | protocol | key | L1 | total | packet。 */
#define OTA_FRAME_DATA_HEADER_SIZE  16U

/* 站号：AT32 为 0x02，ST 为 0x03（见 C:\stm32g4bootloader\README.md），
 * 由调用方按目标传入。OTA_FRAME_SLAVE_ADDRESS 只作为 AT32 的默认值保留，
 * 组件内部不再直接使用。 */
#define OTA_FRAME_SLAVE_ADDRESS     0x02U
#define OTA_FRAME_SLAVE_ANY         0x00U   /* 日志等只判格式的场景：不校验站号 */
#define OTA_FRAME_REGISTER_START    0x00E6U
#define OTA_FRAME_PROTOCOL_VERSION  1U
#define OTA_FRAME_FUNC_CONTROL      0x10U
#define OTA_FRAME_FUNC_DATA         0xAAU

/* 0x10 帧的 count 字段：查询为 3、控制为 4，bootloader 据此决定应答格式。 */
#define OTA_FRAME_REG_COUNT_QUERY   3U
#define OTA_FRAME_REG_COUNT_CONTROL 4U

/* 控制帧 offset 6..9 的固定 u32 值，bootloader 用它区分自定义 0x10 帧。 */
#define OTA_FRAME_CONTROL_MAGIC     8U

#define OTA_FRAME_QUERY_SIZE        14U
#define OTA_FRAME_CONTROL_SIZE      20U
#define OTA_FRAME_QUERY_REPLY_SIZE  14U
#define OTA_FRAME_CONTROL_REPLY_SIZE 8U
#define OTA_FRAME_ACK_REPLY_SIZE    7U

/* 控制操作码。 */
#define OTA_FRAME_OPERATION_START   1U
#define OTA_FRAME_OPERATION_RESET   2U

/* bootloader 的 OTA 状态（MODBUS_STATUS_*）。 */
#define OTA_FRAME_STATUS_FORBIDDEN   0x0000U
#define OTA_FRAME_STATUS_IDLE        0x0001U
#define OTA_FRAME_STATUS_DOWNLOADING 0x0002U
#define OTA_FRAME_STATUS_FLASHING    0x0003U
#define OTA_FRAME_STATUS_DAMAGED     0x0055U

/* 0xAA 应答码：1 表示本包写入成功。 */
#define OTA_FRAME_ACK_OK            0x01U

/* 0x10 应答类型，按 count 字段区分。 */
typedef enum {
    OTA_FRAME_REPLY_NONE = 0,
    OTA_FRAME_REPLY_STATUS,
    OTA_FRAME_REPLY_ACK,
} ota_frame_reply_t;

/* 0xAA 帧头解码结果，供调用方取字段，避免外部重复计算偏移。 */
typedef struct {
    uint16_t frame_length;
    uint16_t protocol;
    uint32_t key;
    uint16_t payload_length;
    uint16_t total;
    uint16_t packet;
    uint32_t payload_crc32;     /* 载荷 CRC32，帧长不足以容纳时为 0 */
} ota_frame_data_header_t;

/* CRC16/MODBUS，线上低字节在前。 */
uint16_t ota_frame_crc16(const uint8_t *data, size_t length);

/* CRC32/ISO-HDLC，线上大端，只覆盖本包载荷。 */
uint32_t ota_frame_crc32(const uint8_t *data, size_t length);

/* 0x10 查询帧：status/key 由主机填 0，bootloader 只读 count 决定应答格式。
 * slave_address 为目标芯片站号（AT32 0x02 / ST 0x03）。 */
size_t ota_frame_build_query(uint8_t *frame, size_t capacity, uint8_t slave_address);

/* 0x10 控制帧：operation 取 OTA_FRAME_OPERATION_*，key 为会话 key。 */
size_t ota_frame_build_control(uint8_t *frame, size_t capacity, uint8_t slave_address, uint16_t operation, uint32_t key);

/* 0xAA 数据帧：total 为末包包号（包数 - 1），packet 从 0 开始。 */
size_t ota_frame_build_data(uint8_t *frame, size_t capacity, uint8_t slave_address, uint32_t key, uint16_t total, uint16_t packet, const uint8_t *payload, size_t payload_length);

/* 解析 0x10 应答。查询应答 14 字节（带 status），控制应答 8 字节；开始 OTA
 * 失败时 bootloader 回的是 14 字节状态帧而不是 8 字节确认，两种都要能认出。
 * slave_address 传 OTA_FRAME_SLAVE_ANY 时跳过站号校验（仅用于日志分类）。 */
bool ota_frame_parse_control_reply(const uint8_t *reply, size_t length, uint8_t slave_address, ota_frame_reply_t *kind, uint16_t *status);

/* 解析 0xAA 应答：SA AA | ack | packet u16 | CRC16。 */
bool ota_frame_parse_ack_reply(const uint8_t *reply, size_t length, uint8_t slave_address, uint8_t *ack, uint16_t *packet);

/* 解码 0xAA 帧头字段，只校验帧头自身，不校验载荷与 CRC。 */
bool ota_frame_parse_data_header(const uint8_t *frame, size_t length, uint8_t slave_address, ota_frame_data_header_t *header);

/* 解码 0x10 帧的寄存器地址与 count 字段，只用于日志分类。 */
bool ota_frame_parse_control_header(const uint8_t *frame, size_t length, uint8_t slave_address, uint16_t *register_address, uint16_t *count);

#ifdef __cplusplus
}
#endif

#endif /* OTA_FRAME_H */