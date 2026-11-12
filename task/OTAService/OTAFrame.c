#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "OTAFrame.h"

/* OTAFrame：下挂芯片 bootloader 帧的编解码实现，纯函数、无状态、无 IO。
 * AT32 与 ST32 除 Modbus 站号外帧格式完全一致，站号由调用方按入参传入。
 * 全部字段偏移、CRC 参数集中在本文件，OTAUart 只按语义取字段。 */

#define OTA_FRAME_CRC16_INIT             0xFFFFU
#define OTA_FRAME_CRC16_POLY_REFLECTED   0xA001U
#define OTA_FRAME_CRC16_SIZE             2U
#define OTA_FRAME_CRC32_INIT             0xFFFFFFFFU
#define OTA_FRAME_CRC32_POLY_REFLECTED   0xEDB88320U
#define OTA_FRAME_CRC32_XOROUT           0xFFFFFFFFU
#define OTA_FRAME_CRC32_SIZE             4U

/* 最短可校验帧：2 字节 CRC16 之外还必须有帧头，否则无意义。 */
#define OTA_FRAME_MIN_CRC_SIZE           4U

/* 0xAA 帧内的字段偏移。 */
#define OTA_FRAME_OFFSET_LENGTH          2U
#define OTA_FRAME_OFFSET_PROTOCOL        4U
#define OTA_FRAME_OFFSET_KEY             6U
#define OTA_FRAME_OFFSET_PAYLOAD_LENGTH  10U
#define OTA_FRAME_OFFSET_TOTAL           12U
#define OTA_FRAME_OFFSET_PACKET          14U
#define OTA_FRAME_OFFSET_PAYLOAD         16U

/* 0x10 帧内的字段偏移。 */
#define OTA_FRAME_OFFSET_REGISTER        2U
#define OTA_FRAME_OFFSET_COUNT           4U
#define OTA_FRAME_OFFSET_QUERY_STATUS    6U
#define OTA_FRAME_OFFSET_QUERY_KEY       8U
#define OTA_FRAME_OFFSET_CONTROL_MAGIC   6U
#define OTA_FRAME_OFFSET_CONTROL_STATUS  10U
#define OTA_FRAME_OFFSET_CONTROL_KEY     12U
#define OTA_FRAME_OFFSET_CONTROL_OP      16U

/* 0xAA 应答帧内的字段偏移：ack 紧跟在功能码之后。 */
#define OTA_FRAME_OFFSET_ACK             2U
#define OTA_FRAME_OFFSET_ACK_PACKET      3U

/* ---------- 基础编解码 ---------- */

/* 多字节字段除 CRC16 外全部大端，与 bootloader 的 read_value/write_value 一致。 */
static void ota_frame_put_u16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)(value & 0xFFU);
}

static void ota_frame_put_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)((value >> 16) & 0xFFU);
    out[2] = (uint8_t)((value >> 8) & 0xFFU);
    out[3] = (uint8_t)(value & 0xFFU);
}

static uint16_t ota_frame_get_u16(const uint8_t *in)
{
    return (uint16_t)(((uint16_t)in[0] << 8) | (uint16_t)in[1]);
}

static uint32_t ota_frame_get_u32(const uint8_t *in)
{
    return ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) | ((uint32_t)in[2] << 8) | (uint32_t)in[3];
}

/* ---------- 校验 ---------- */

/* CRC16/MODBUS：初值 0xFFFF、反射多项式 0xA001、无末尾异或，与
 * bootloader 的 add_crc() 和 DebugConsole 的 modbus_crc16() 等价。 */
uint16_t ota_frame_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = OTA_FRAME_CRC16_INIT;

    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8U; ++bit) {
            crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ OTA_FRAME_CRC16_POLY_REFLECTED) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

/* CRC32/ISO-HDLC，等价 binascii.crc32；只覆盖本包载荷，线上大端。 */
uint32_t ota_frame_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = OTA_FRAME_CRC32_INIT;

    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8U; ++bit) {
            crc = (crc & 1U) ? ((crc >> 1) ^ OTA_FRAME_CRC32_POLY_REFLECTED) : (crc >> 1);
        }
    }
    return crc ^ OTA_FRAME_CRC32_XOROUT;
}

/* CRC16 低字节在前，与上位机的 little_crc16=True 一致。 */
static void ota_frame_append_crc16(uint8_t *frame, size_t crc_offset)
{
    uint16_t crc = ota_frame_crc16(frame, crc_offset);

    frame[crc_offset] = (uint8_t)(crc & 0xFFU);
    frame[crc_offset + 1U] = (uint8_t)(crc >> 8);
}

static bool ota_frame_crc16_valid(const uint8_t *frame, size_t length)
{
    if (length < OTA_FRAME_MIN_CRC_SIZE) {
        return false;
    }

    uint16_t received = (uint16_t)(((uint16_t)frame[length - 1U] << 8) | frame[length - OTA_FRAME_CRC16_SIZE]);
    return (ota_frame_crc16(frame, length - OTA_FRAME_CRC16_SIZE) == received) ? true : false;
}

/* 站号校验的公共判定：入参为 OTA_FRAME_SLAVE_ANY 时跳过（仅日志路径）。 */
static bool ota_frame_slave_matches(uint8_t actual, uint8_t expected)
{
    return (expected == OTA_FRAME_SLAVE_ANY || actual == expected) ? true : false;
}

/* ---------- 帧构造 ---------- */

/* 0x10 查询帧：status/key 字段由主机填写，bootloader 只读 count 决定应答格式，
 * 状态与 key 一律来自设备本地，所以这里固定填 0（与上位机 query() 一致）。 */
size_t ota_frame_build_query(uint8_t *frame, size_t capacity, uint8_t slave_address)
{
    if (frame == NULL || capacity < OTA_FRAME_QUERY_SIZE) {
        return 0U;
    }

    frame[0] = slave_address;
    frame[1] = OTA_FRAME_FUNC_CONTROL;
    ota_frame_put_u16(frame + OTA_FRAME_OFFSET_REGISTER, OTA_FRAME_REGISTER_START);
    ota_frame_put_u16(frame + OTA_FRAME_OFFSET_COUNT, OTA_FRAME_REG_COUNT_QUERY);
    ota_frame_put_u16(frame + OTA_FRAME_OFFSET_QUERY_STATUS, 0U);
    ota_frame_put_u32(frame + OTA_FRAME_OFFSET_QUERY_KEY, 0U);
    ota_frame_append_crc16(frame, OTA_FRAME_QUERY_SIZE - OTA_FRAME_CRC16_SIZE);
    return OTA_FRAME_QUERY_SIZE;
}

/* 0x10 控制帧：offset 6..9 必须是大端 u32 的 8，否则 bootloader 直接丢弃。 */
size_t ota_frame_build_control(uint8_t *frame, size_t capacity, uint8_t slave_address, uint16_t operation, uint32_t key)
{
    if (frame == NULL || capacity < OTA_FRAME_CONTROL_SIZE) {
        return 0U;
    }

    frame[0] = slave_address;
    frame[1] = OTA_FRAME_FUNC_CONTROL;
    ota_frame_put_u16(frame + OTA_FRAME_OFFSET_REGISTER, OTA_FRAME_REGISTER_START);
    ota_frame_put_u16(frame + OTA_FRAME_OFFSET_COUNT, OTA_FRAME_REG_COUNT_CONTROL);
    ota_frame_put_u32(frame + OTA_FRAME_OFFSET_CONTROL_MAGIC, OTA_FRAME_CONTROL_MAGIC);
    ota_frame_put_u16(frame + OTA_FRAME_OFFSET_CONTROL_STATUS, OTA_FRAME_STATUS_IDLE);
    ota_frame_put_u32(frame + OTA_FRAME_OFFSET_CONTROL_KEY, key);
    ota_frame_put_u16(frame + OTA_FRAME_OFFSET_CONTROL_OP, operation);
    ota_frame_append_crc16(frame, OTA_FRAME_CONTROL_SIZE - OTA_FRAME_CRC16_SIZE);
    return OTA_FRAME_CONTROL_SIZE;
}

/* 0xAA 数据帧：L = 载荷 + 22，L1 = 载荷长度，total = 末包包号（包数 - 1），
 * packet 从 0 开始；bootloader 以 currentPackage > total 判定收完。 */
size_t ota_frame_build_data(uint8_t *frame, size_t capacity, uint8_t slave_address, uint32_t key, uint16_t total, uint16_t packet, const uint8_t *payload, size_t payload_length)
{
    if (frame == NULL || payload == NULL || payload_length == 0U) {
        return 0U;
    }
    if (payload_length > (size_t)0xFFFFU) {
        return 0U;
    }

    const size_t frame_length = payload_length + OTA_FRAME_OVERHEAD;
    if (capacity < frame_length) {
        return 0U;
    }

    frame[0] = slave_address;
    frame[1] = OTA_FRAME_FUNC_DATA;
    ota_frame_put_u16(frame + OTA_FRAME_OFFSET_LENGTH, (uint16_t)frame_length);
    ota_frame_put_u16(frame + OTA_FRAME_OFFSET_PROTOCOL, OTA_FRAME_PROTOCOL_VERSION);
    ota_frame_put_u32(frame + OTA_FRAME_OFFSET_KEY, key);
    ota_frame_put_u16(frame + OTA_FRAME_OFFSET_PAYLOAD_LENGTH, (uint16_t)payload_length);
    ota_frame_put_u16(frame + OTA_FRAME_OFFSET_TOTAL, total);
    ota_frame_put_u16(frame + OTA_FRAME_OFFSET_PACKET, packet);
    memcpy(frame + OTA_FRAME_OFFSET_PAYLOAD, payload, payload_length);
    ota_frame_put_u32(frame + OTA_FRAME_OFFSET_PAYLOAD + payload_length, ota_frame_crc32(payload, payload_length));
    ota_frame_append_crc16(frame, frame_length - OTA_FRAME_CRC16_SIZE);
    return frame_length;
}

/* ---------- 帧解析 ---------- */

/* 解析 0x10 应答。开始 OTA 失败时 bootloader 回的是 14 字节状态帧而不是
 * 8 字节确认，所以两种 count 都要能认出来。 */
bool ota_frame_parse_control_reply(const uint8_t *reply, size_t length, uint8_t slave_address, ota_frame_reply_t *kind, uint16_t *status)
{
    if (reply == NULL || kind == NULL || status == NULL) {
        return false;
    }

    *kind = OTA_FRAME_REPLY_NONE;

    if (length < OTA_FRAME_CONTROL_REPLY_SIZE) {
        return false;
    }
    if (!ota_frame_slave_matches(reply[0], slave_address) || reply[1] != OTA_FRAME_FUNC_CONTROL) {
        return false;
    }
    if (ota_frame_get_u16(reply + OTA_FRAME_OFFSET_REGISTER) != OTA_FRAME_REGISTER_START) {
        return false;
    }

    uint16_t count = ota_frame_get_u16(reply + OTA_FRAME_OFFSET_COUNT);

    if (count == OTA_FRAME_REG_COUNT_QUERY) {
        if (length < OTA_FRAME_QUERY_REPLY_SIZE || !ota_frame_crc16_valid(reply, OTA_FRAME_QUERY_REPLY_SIZE)) {
            return false;
        }
        *status = ota_frame_get_u16(reply + OTA_FRAME_OFFSET_QUERY_STATUS);
        *kind = OTA_FRAME_REPLY_STATUS;
        return true;
    }

    if (count == OTA_FRAME_REG_COUNT_CONTROL) {
        if (!ota_frame_crc16_valid(reply, OTA_FRAME_CONTROL_REPLY_SIZE)) {
            return false;
        }
        *kind = OTA_FRAME_REPLY_ACK;
        return true;
    }

    return false;
}

/* 解析 0xAA 应答：SA AA | ack | packet u16 | CRC16。 */
bool ota_frame_parse_ack_reply(const uint8_t *reply, size_t length, uint8_t slave_address, uint8_t *ack, uint16_t *packet)
{
    if (reply == NULL || ack == NULL || packet == NULL) {
        return false;
    }
    if (length < OTA_FRAME_ACK_REPLY_SIZE) {
        return false;
    }
    if (!ota_frame_slave_matches(reply[0], slave_address) || reply[1] != OTA_FRAME_FUNC_DATA) {
        return false;
    }
    if (!ota_frame_crc16_valid(reply, OTA_FRAME_ACK_REPLY_SIZE)) {
        return false;
    }

    *ack = reply[OTA_FRAME_OFFSET_ACK];
    *packet = ota_frame_get_u16(reply + OTA_FRAME_OFFSET_ACK_PACKET);
    return true;
}

/* 帧头字段解码，只校验站号/功能码与长度，不校验 CRC：日志路径要能打出
 * 残缺帧的关键字段。 */
bool ota_frame_parse_data_header(const uint8_t *frame, size_t length, uint8_t slave_address, ota_frame_data_header_t *header)
{
    if (frame == NULL || header == NULL) {
        return false;
    }
    if (length < OTA_FRAME_DATA_HEADER_SIZE) {
        return false;
    }
    if (!ota_frame_slave_matches(frame[0], slave_address) || frame[1] != OTA_FRAME_FUNC_DATA) {
        return false;
    }

    header->frame_length = ota_frame_get_u16(frame + OTA_FRAME_OFFSET_LENGTH);
    header->protocol = ota_frame_get_u16(frame + OTA_FRAME_OFFSET_PROTOCOL);
    header->key = ota_frame_get_u32(frame + OTA_FRAME_OFFSET_KEY);
    header->payload_length = ota_frame_get_u16(frame + OTA_FRAME_OFFSET_PAYLOAD_LENGTH);
    header->total = ota_frame_get_u16(frame + OTA_FRAME_OFFSET_TOTAL);
    header->packet = ota_frame_get_u16(frame + OTA_FRAME_OFFSET_PACKET);
    header->payload_crc32 = 0U;

    const size_t crc32_offset = OTA_FRAME_OFFSET_PAYLOAD + (size_t)header->payload_length;
    if (length >= crc32_offset + OTA_FRAME_CRC32_SIZE) {
        header->payload_crc32 = ota_frame_get_u32(frame + crc32_offset);
    }
    return true;
}

/* 0x10 帧的寄存器地址与 count，仅用于日志分类。 */
bool ota_frame_parse_control_header(const uint8_t *frame, size_t length, uint8_t slave_address, uint16_t *register_address, uint16_t *count)
{
    if (frame == NULL || register_address == NULL || count == NULL) {
        return false;
    }
    if (length < OTA_FRAME_OFFSET_COUNT + OTA_FRAME_CRC16_SIZE) {
        return false;
    }
    if (!ota_frame_slave_matches(frame[0], slave_address) || frame[1] != OTA_FRAME_FUNC_CONTROL) {
        return false;
    }

    *register_address = ota_frame_get_u16(frame + OTA_FRAME_OFFSET_REGISTER);
    *count = ota_frame_get_u16(frame + OTA_FRAME_OFFSET_COUNT);
    return true;
}