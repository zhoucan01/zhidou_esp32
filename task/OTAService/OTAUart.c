#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"

#include "DataCenter.h"
#include "OTAFrame.h"
#include "OTAUart.h"
#include "OTAVerManager.h"

/* OTAUart：把下挂芯片分区里的镜像按对应 bootloader 协议推给下挂芯片。
 *
 * AT32 与 ST32 共用同一条 MBUS485 总线、同一套 0x10/0xAA 帧格式与 ACK 语义，
 * 只有 Modbus 站号（AT32 0x02、ST32 0x03）和镜像所在分区（AT_Firmware /
 * ST_Firmware）不同。站号与分区都由 ota_uart_start() 的 target 入参决定，
 * 因此这里只有一份实现，不存在 ST32 专用通道。
 *
 * 一拍的完整动作是"发一帧 + 等一帧"：ModbusGatewayTransaction() 本身是同步
 * 事务，因此每个状态最多发起一次该调用，不 delay，靠版本管理器 100 ms 的
 * 调度周期推进。发送与接收在这一层按职责拆成 ota_uart_send() 与
 * ota_uart_receive()：前者负责把请求交给网关并记录发送侧结果，后者负责判定
 * 并记录应答；ota_uart_transact() 只做串联，保证"每拍最多一次网关事务、
 * 网关忙不算失败"的语义不变。
 *
 * 帧的字节布局、CRC 与字段偏移全部在 OTAFrame 组件内，本文件不出现偏移量。
 *
 * 与上位机 AT32_OTA_PC/serial_ota.py 的差异只有一处：它用 15 s 超时等
 * "开始 OTA"的控制应答，因为设备要先复制备份再擦除 A 区。网关的首字节超时
 * 固定 500 ms，所以这里发出操作码 1 后不依赖那条控制应答，改为轮询 0x10
 * 查询，直到状态变成 0x0002（下载中）才认为开始成功。
 *
 * 会话 key 由 esp_timer_get_time() 派生，只在本轮内固定：bootloader 用它校验
 * 后续每个数据包，复用旧 key 没有意义。
 *
 * 时序预算（见下方 OTA_UART_*_WINDOW_US）：bootloader 复位后只有约 2 s 的
 * 握手窗口（main.c 的 bootTick > 2000 就跳 APP），所以复位后必须逐拍查询；
 * 单次查询失败要等满网关 500 ms 首字节超时，窗口内只够试两三次，因此按
 * "查回 APP 状态就重新复位一次"的方式补足，而不是直接把整轮判失败。 */

static const char *TAG = "OTAUart";

/* 0xAA 数据帧整帧长度：载荷 + 帧头 16 + CRC32 4 + CRC16 2。 */
#define OTA_UART_FRAME_SIZE         (OTA_UART_PAYLOAD_SIZE + OTA_FRAME_OVERHEAD)

/* 应答缓冲：实际应答最长 14 字节，留出余量容纳残留帧拼接。 */
#define OTA_UART_REPLY_SIZE         64U

/* ---------- 时序预算 ---------- */

/* 复位后等待 bootloader 应答：窗口本身约 2 s，单次查询最长 500 ms 超时，
 * 加上重新复位的重试，给 8 s。 */
#define OTA_UART_RESET_WINDOW_US    (8 * 1000 * 1000LL)

/* 发出操作码 1 后等待状态变成 DOWNLOADING：设备要先复制备份并擦除 A 区。 */
#define OTA_UART_START_WINDOW_US    (60 * 1000 * 1000LL)

/* 末包 ACK 后设备做 SHA-256 校验并跳转 APP，版本寄存器由 DataCenter 下一次
 * 轮询刷新，给 15 s 余量。 */
#define OTA_UART_VERIFY_WINDOW_US   (15 * 1000 * 1000LL)

/* 握手窗口错过后重新复位的次数上限。 */
#define OTA_UART_RESET_ATTEMPTS     3U

/* 进度日志的打印步长：每推进 10% 打一行。 */
#define OTA_UART_LOG_PROGRESS_STEP  10U

/* ModbusGatewayTransaction() 在"网关被占用/队列已满"时会直接返回 false 且
 * 不写 response_length，用这个哨兵值把"没发出去"和"发了但没回"区分开：
 * 前者下一拍原样重发，后者按协议失败处理（不重传数据包，避免重复写 Flash）。 */
#define OTA_UART_LEN_UNSET          ((size_t)-1)

/* ---------- 内部类型 ---------- */

typedef enum {
    OTA_UART_STATE_IDLE = 0,        /* 未启动 */
    OTA_UART_STATE_QUERY,           /* 查询 OTA 状态，决定是否需要复位 */
    OTA_UART_STATE_RESET,           /* 发操作码 2 复位设备 */
    OTA_UART_STATE_WAIT_BOOTLOADER, /* 复位后轮询握手，直到状态允许开始 */
    OTA_UART_STATE_START,           /* 发操作码 1 开始 OTA */
    OTA_UART_STATE_WAIT_START,      /* 轮询状态，直到进入下载中 */
    OTA_UART_STATE_SEND_PACKET,     /* 逐包发送 0xAA 数据帧 */
    OTA_UART_STATE_VERIFY,          /* 全部包 ACK 后核对目标芯片版本号 */
    OTA_UART_STATE_DONE,            /* 升级成功 */
    OTA_UART_STATE_FAIL,            /* 升级失败 */
} ota_uart_state_t;

/* 单次串口事务的结果。 */
typedef enum {
    OTA_UART_XFER_OK = 0,           /* 收到应答 */
    OTA_UART_XFER_NO_REPLY,         /* 已发出但没收到（或应答不合法） */
    OTA_UART_XFER_BUSY,             /* 网关占用，请求未发出，下一拍重试 */
} ota_uart_xfer_t;

typedef struct {
    ota_uart_state_t      state;
    const esp_partition_t *partition;                           /* AT_Firmware / ST_Firmware */
    ota_fw_target_t       target;                               /* 目标芯片，决定站号与版本行 */
    uint8_t               slave_address;                        /* Modbus 站号：AT32 0x02 / ST32 0x03 */
    int                   version_index;                        /* getVersion() 行号，见 ota_fw_target_version_index() */
    char                  expected_version[OTA_VERSION_STRING_MAX_LEN];
    uint32_t              image_size;                           /* 镜像字节数 */
    uint16_t              total;                                /* 总包数 */
    uint16_t              sent;                                 /* 已确认包数 */
    uint32_t              key;                                  /* 会话 key */
    uint16_t              device_status;                        /* 最近一次查询到的状态 */
    uint8_t               reset_attempts;                       /* 已发出的复位次数 */
    uint8_t               last_log_pct;                         /* 进度日志节流 */
    int64_t               deadline_us;                          /* 当前阶段的到期时刻 */
} ota_uart_session_t;

/* 网关一次调用的原始结果，由发送阶段写入、接收阶段解读。 */
typedef struct {
    size_t length;      /* 网关写入的应答长度，OTA_UART_LEN_UNSET 表示未写 */
    bool   ok;          /* 网关返回值 */
} ota_uart_xfer_result_t;

/* 帧日志节流：同一帧 1 s 内只打印一次。 */
typedef struct {
    uint8_t  frame[OTA_UART_FRAME_SIZE];
    size_t   length;
    unsigned repeat;        /* 自上次打印后被抑制的次数 */
    int64_t  last_us;
    bool     valid;
} ota_uart_log_throttle_t;

static ota_uart_session_t s_uart;

/* 收发缓冲全部静态分配：升级期间不碰堆，避免碎片影响 esp_ota_write()。 */
static uint8_t s_uart_frame[OTA_UART_FRAME_SIZE];
static uint8_t s_uart_payload[OTA_UART_PAYLOAD_SIZE];
static uint8_t s_uart_reply[OTA_UART_REPLY_SIZE];

static ota_uart_xfer_result_t s_uart_xfer;

static ota_uart_log_throttle_t s_log_tx;
static ota_uart_log_throttle_t s_log_rx;

/* ---------- 帧日志 ---------- */

/* 0xAA 数据帧逐包全量 hex 会淹没串口日志，默认只打印 16 字节帧头（长度/包号/
 * key），CRC32 在摘要行里单独给出；把 OTA_UART_LOG_DATA_FRAME_FULL 置 1 可
 * 打印整帧，用于对照上位机报文。 */
#ifndef OTA_UART_LOG_DATA_FRAME_FULL
#define OTA_UART_LOG_DATA_FRAME_FULL 0
#endif

/* 轮询阶段同一条查询帧以 100 ms 周期重复发出，60 s 的等待窗口会刷出上千行，
 * 因此同一帧在 1 s 内只打印一次，被抑制的次数在下一次打印时一并给出。 */
#define OTA_UART_LOG_REPEAT_US      (1000 * 1000LL)

/* 返回 true 表示本次需要打印，*repeat 为期间被抑制的重复帧数。 */
static bool ota_uart_log_allowed(ota_uart_log_throttle_t *t, const uint8_t *frame, size_t length, unsigned *repeat)
{
    const int64_t now = esp_timer_get_time();

    *repeat = 0U;
    if (length > sizeof(t->frame)) {
        return true;
    }

    if (t->valid && t->length == length && memcmp(t->frame, frame, length) == 0 && now - t->last_us < OTA_UART_LOG_REPEAT_US) {
        ++t->repeat;
        return false;
    }

    *repeat = t->repeat;
    t->repeat = 0U;
    memcpy(t->frame, frame, length);
    t->length = length;
    t->last_us = now;
    t->valid = true;
    return true;
}

/* 按功能码分类打印：0xAA 数据帧给出包号/key/载荷长度/CRC32，0xAA 应答给出
 * ack 与包号，0x10 帧给出 count。字段提取全部走 OTAFrame，本文件不碰偏移。 */
static void ota_uart_log_frame(const char *direction, const uint8_t *frame, size_t length, unsigned repeat)
{
    size_t dump_length = length;
    ota_frame_data_header_t header;
    uint16_t register_address = 0U;
    uint16_t count = 0U;
        uint16_t ack_packet = 0U;
        uint8_t ack = 0U;

        if (repeat > 0U) {
        ESP_LOGI(TAG, "%s: previous identical frame repeated %u times", direction, repeat);
    }
    if (frame == NULL || length < 2U) {
        ESP_LOGI(TAG, "%s: no frame (%u bytes)", direction, (unsigned)length);
        return;
    }

    if (ota_frame_parse_data_header(frame, length, OTA_FRAME_SLAVE_ANY, &header)) {
        ESP_LOGI(TAG, "%s 0xAA %u bytes: packet %u/%u key 0x%08X payload %u crc32 0x%08X", direction, (unsigned)length, (unsigned)header.packet, (unsigned)header.total, (unsigned)header.key, (unsigned)header.payload_length, (unsigned)header.payload_crc32);
        if (OTA_UART_LOG_DATA_FRAME_FULL == 0 && dump_length > OTA_FRAME_DATA_HEADER_SIZE) {
            dump_length = OTA_FRAME_DATA_HEADER_SIZE;
        }
        } else if (ota_frame_parse_ack_reply(frame, length, OTA_FRAME_SLAVE_ANY, &ack, &ack_packet)) {
            ESP_LOGI(TAG, "%s 0xAA ack %u bytes: ack 0x%02X packet %u", direction, (unsigned)length, (unsigned)ack, (unsigned)ack_packet);
        } else if (ota_frame_parse_control_header(frame, length, OTA_FRAME_SLAVE_ANY, &register_address, &count)) {
        ESP_LOGI(TAG, "%s 0x10 %u bytes: count %u", direction, (unsigned)length, (unsigned)count);
    } else {
        ESP_LOGI(TAG, "%s unknown %u bytes", direction, (unsigned)length);
    }

    ESP_LOG_BUFFER_HEX_LEVEL(TAG, frame, dump_length, ESP_LOG_INFO);
    if (dump_length < length) {
        ESP_LOGI(TAG, "... %u more bytes not dumped", (unsigned)(length - dump_length));
    }
}

/* ---------- 串口收发 ---------- */

/* 发送阶段：把已组好的帧交给网关，只负责"请求有没有发出去"。
 * 网关忙碌/队列满时 DataCenter 直接返回、不写 response_length，这一拍什么都
 * 没发出，保持状态不变等下一拍即可，不算协议失败。 */
static ota_uart_xfer_t ota_uart_send(size_t request_length)
{
    unsigned repeat = 0U;

    if (ota_uart_log_allowed(&s_log_tx, s_uart_frame, request_length, &repeat)) {
        ota_uart_log_frame("TX", s_uart_frame, request_length, repeat);
    }

    s_uart_xfer.length = OTA_UART_LEN_UNSET;
    s_uart_xfer.ok = ModbusGatewayTransaction(s_uart_frame, request_length, s_uart_reply, sizeof(s_uart_reply), &s_uart_xfer.length);

    if (!s_uart_xfer.ok && s_uart_xfer.length == OTA_UART_LEN_UNSET) {
        ESP_LOGI(TAG, "TX %u bytes dropped, gateway busy, retrying next tick", (unsigned)request_length);
        return OTA_UART_XFER_BUSY;
    }
    return OTA_UART_XFER_OK;
}

/* 接收阶段：解读发送阶段留下的结果，只负责"应答有没有按协议回来"。 */
static ota_uart_xfer_t ota_uart_receive(size_t request_length, size_t *reply_length)
{
    unsigned repeat = 0U;
    size_t length = s_uart_xfer.length;
    const bool ok = s_uart_xfer.ok;

    /* 网关返回失败却写了长度：按"没有可用应答"处理。 */
    if (length == OTA_UART_LEN_UNSET) {
        length = 0U;
    }

    if (ok && length > 0U) {
        const size_t received = (length > sizeof(s_uart_reply)) ? sizeof(s_uart_reply) : length;
        if (ota_uart_log_allowed(&s_log_rx, s_uart_reply, received, &repeat)) {
            ota_uart_log_frame("RX", s_uart_reply, received, repeat);
        }
    } else {
        ESP_LOGW(TAG, "RX timeout after %u bytes sent (ok=%d)", (unsigned)request_length, (int)ok);
    }

    *reply_length = length;
    return (ok && length > 0U) ? OTA_UART_XFER_OK : OTA_UART_XFER_NO_REPLY;
}

/* 一拍一次网关事务：发送与接收串联，任一侧失败立即返回。 */
static ota_uart_xfer_t ota_uart_transact(size_t request_length, size_t *reply_length)
{
    ota_uart_xfer_t xfer = ota_uart_send(request_length);

    if (xfer != OTA_UART_XFER_OK) {
        *reply_length = 0U;
        return xfer;
    }
    return ota_uart_receive(request_length, reply_length);
}

/* 发一帧 0x10 查询并取出设备状态。 */
static ota_uart_xfer_t ota_uart_query(uint16_t *status)
{
    size_t length = 0U;
    ota_frame_reply_t kind = OTA_FRAME_REPLY_NONE;
    uint16_t value = 0U;

    if (ota_frame_build_query(s_uart_frame, sizeof(s_uart_frame), s_uart.slave_address) == 0U) {
        return OTA_UART_XFER_NO_REPLY;
    }

    ota_uart_xfer_t xfer = ota_uart_transact(OTA_FRAME_QUERY_SIZE, &length);
    if (xfer != OTA_UART_XFER_OK) {
        return xfer;
    }

        if (!ota_frame_parse_control_reply(s_uart_reply, length, s_uart.slave_address, &kind, &value) || kind != OTA_FRAME_REPLY_STATUS) {
        return OTA_UART_XFER_NO_REPLY;
    }

    *status = value;
    return OTA_UART_XFER_OK;
}

/* ---------- 版本号 ---------- */

/* 把 "1.0.0.2" 这样的字符串逐段解析成 4 个字节，不足 4 段按 0 补齐。
 * AT32 的版本寄存器固定 4 字节，按数值比较可以容忍记录里少写末尾的 0。 */
static bool ota_uart_parse_version(const char *text, uint8_t out[4])
{
    unsigned index = 0U;
    unsigned value = 0U;
    bool has_digit = false;

    memset(out, 0, 4U);
    if (text == NULL) {
        return false;
    }

    for (const char *p = text; ; ++p) {
        char c = *p;

        if (c >= '0' && c <= '9') {
            value = value * 10U + (unsigned)(c - '0');
            if (value > 255U) {
                return false;
            }
            has_digit = true;
        } else if (c == '.' || c == '\0') {
            if (!has_digit || index >= 4U) {
                return false;
            }
            out[index++] = (uint8_t)value;
            value = 0U;
            has_digit = false;
            if (c == '\0') {
                return true;
            }
        } else {
            return false;
        }
    }
}

/* 读 DataCenter 维护的目标芯片实时版本（对应站号那 4 个原始字节），
 * 拼法与 ota_ver_mgr_load_target_version() 完全一致。 */
static bool ota_uart_read_device_version(char *out, size_t out_len)
{
    char (*versions)[4] = getVersion();
    char snapshot[4];
    int n = 0;

    if (out == NULL || out_len < OTA_VERSION_STRING_MAX_LEN) {
        return false;
    }
    out[0] = '\0';
    if (versions == NULL || s_uart.version_index < 0) {
        return false;
    }

    memcpy(snapshot, versions[s_uart.version_index], sizeof(snapshot));

    n = snprintf(out, out_len, "%u.%u.%u.%u", (unsigned)(uint8_t)snapshot[0], (unsigned)(uint8_t)snapshot[1], (unsigned)(uint8_t)snapshot[2], (unsigned)(uint8_t)snapshot[3]);
    if (n < 0 || n >= (int)out_len) {
        out[0] = '\0';
        return false;
    }
    return true;
}

/* 目标芯片是否在线：版本寄存器只在该站被成功轮询后才刷新，因此这两个
    * 判据等价（DataCenter 里分别对应 ModbusActuatorOnline() 与
    * ModbusPowerOnline()），按目标站号选择，避免把 AT32 的在线状态误当成 ST32。 */
static bool ota_uart_device_online(void)
{
    if (s_uart.target == OTA_FW_TARGET_ST32) {
        return ModbusPowerOnline();
    }
    return ModbusActuatorOnline();
}

static ota_uart_ret_t ota_uart_fail(const char *reason)
{
    ESP_LOGE(TAG, "chip iap failed (slave 0x%02X): %s", (unsigned)s_uart.slave_address, reason);
    s_uart.state = OTA_UART_STATE_FAIL;
    return OTA_UART_RET_FAIL;
}

/* ---------- 状态推进 ---------- */

/* 首次查询：APP 运行中（状态 0）时设备拒绝升级，必须先用操作码 2 复位到
 * bootloader；空闲(1)/下载中(2)/双坏(0x55)都允许直接开始，与上位机一致。 */
static ota_uart_ret_t ota_uart_step_query(void)
{
    uint16_t status = 0U;
    ota_uart_xfer_t xfer = ota_uart_query(&status);

    if (xfer == OTA_UART_XFER_BUSY) {
        return OTA_UART_RET_RUNNING;
    }
    if (xfer == OTA_UART_XFER_NO_REPLY) {
        return ota_uart_fail("no answer to the OTA status query");
    }

    s_uart.device_status = status;
    ESP_LOGI(TAG, "slave 0x%02X OTA status %u (image %u bytes, %u packets)", (unsigned)s_uart.slave_address, (unsigned)status, (unsigned)s_uart.image_size, (unsigned)s_uart.total);

    switch (status) {
    case OTA_FRAME_STATUS_IDLE:
    case OTA_FRAME_STATUS_DOWNLOADING:
    case OTA_FRAME_STATUS_DAMAGED:
        s_uart.state = OTA_UART_STATE_START;
        return OTA_UART_RET_RUNNING;

    case OTA_FRAME_STATUS_FORBIDDEN:
        ESP_LOGI(TAG, "slave 0x%02X runs the application, resetting it into the bootloader", (unsigned)s_uart.slave_address);
        s_uart.state = OTA_UART_STATE_RESET;
        return OTA_UART_RET_RUNNING;

    default:
        return ota_uart_fail("the device is busy with a flash operation");
    }
}

/* 操作码 2：APP 侧先回控制确认再复位。复位确认丢失不影响后续判断，
 * 真正的证据是复位后能否查到 bootloader 的允许升级状态。 */
static ota_uart_ret_t ota_uart_step_reset(void)
{
    size_t length = 0U;
    ota_uart_xfer_t xfer = OTA_UART_XFER_NO_REPLY;

    if (ota_frame_build_control(s_uart_frame, sizeof(s_uart_frame), s_uart.slave_address, OTA_FRAME_OPERATION_RESET, 0U) == 0U) {
        return ota_uart_fail("reset frame does not fit the transmit buffer");
    }

    xfer = ota_uart_transact(OTA_FRAME_CONTROL_SIZE, &length);
    if (xfer == OTA_UART_XFER_BUSY) {
        return OTA_UART_RET_RUNNING;
    }

    ++s_uart.reset_attempts;
    s_uart.deadline_us = esp_timer_get_time() + OTA_UART_RESET_WINDOW_US;
    s_uart.state = OTA_UART_STATE_WAIT_BOOTLOADER;

    if (xfer == OTA_UART_XFER_NO_REPLY) {
        ESP_LOGI(TAG, "reset ack not received, polling the bootloader anyway");
    } else {
        ESP_LOGI(TAG, "slave 0x%02X reset ack received, polling the handshake window", (unsigned)s_uart.slave_address);
    }
    return OTA_UART_RET_RUNNING;
}

/* 复位后逐拍查询。bootloader 只在约 2 s 内响应握手，错过之后就跳回 APP，
 * 此时查回状态 0，重新复位一次而不是直接放弃。 */
static ota_uart_ret_t ota_uart_step_wait_bootloader(void)
{
    uint16_t status = 0U;
    ota_uart_xfer_t xfer = ota_uart_query(&status);

    if (xfer == OTA_UART_XFER_BUSY) {
        return OTA_UART_RET_RUNNING;
    }

    if (xfer == OTA_UART_XFER_OK) {
        s_uart.device_status = status;

        if (status == OTA_FRAME_STATUS_IDLE || status == OTA_FRAME_STATUS_DOWNLOADING || status == OTA_FRAME_STATUS_DAMAGED) {
            ESP_LOGI(TAG, "slave 0x%02X bootloader ready, status %u", (unsigned)s_uart.slave_address, (unsigned)status);
            s_uart.state = OTA_UART_STATE_START;
            return OTA_UART_RET_RUNNING;
        }

        if (status == OTA_FRAME_STATUS_FORBIDDEN) {
            if (s_uart.reset_attempts >= OTA_UART_RESET_ATTEMPTS) {
                return ota_uart_fail("the device keeps running the application after reset");
            }
            ESP_LOGW(TAG, "handshake window missed, resetting again (%u/%u)", (unsigned)s_uart.reset_attempts, (unsigned)OTA_UART_RESET_ATTEMPTS);
            s_uart.state = OTA_UART_STATE_RESET;
            return OTA_UART_RET_RUNNING;
        }
        /* 其它状态（Flash 操作中）继续等。 */
    }

    if (esp_timer_get_time() >= s_uart.deadline_us) {
        return ota_uart_fail("bootloader handshake window missed");
    }
    return OTA_UART_RET_RUNNING;
}

/* 操作码 1：设备先校验备份、复制 A->B、擦除 A 区，耗时数秒，因此这条
 * 控制应答可能超时；无论应答是否收到，都转到轮询用状态确认是否真的开始。 */
static ota_uart_ret_t ota_uart_step_start(void)
{
    size_t length = 0U;
    ota_uart_xfer_t xfer = OTA_UART_XFER_NO_REPLY;

    if (ota_frame_build_control(s_uart_frame, sizeof(s_uart_frame), s_uart.slave_address, OTA_FRAME_OPERATION_START, s_uart.key) == 0U) {
        return ota_uart_fail("start frame does not fit the transmit buffer");
    }

    xfer = ota_uart_transact(OTA_FRAME_CONTROL_SIZE, &length);
    if (xfer == OTA_UART_XFER_BUSY) {
        return OTA_UART_RET_RUNNING;
    }

    s_uart.deadline_us = esp_timer_get_time() + OTA_UART_START_WINDOW_US;
    s_uart.state = OTA_UART_STATE_WAIT_START;

    if (xfer == OTA_UART_XFER_NO_REPLY) {
        ESP_LOGI(TAG, "start ack not received (erase in progress), polling status");
    } else {
        ESP_LOGI(TAG, "start ack received, waiting for the device to enter download mode");
    }
    return OTA_UART_RET_RUNNING;
}

static ota_uart_ret_t ota_uart_step_wait_start(void)
{
    uint16_t status = 0U;
    ota_uart_xfer_t xfer = ota_uart_query(&status);

    if (xfer == OTA_UART_XFER_BUSY) {
        return OTA_UART_RET_RUNNING;
    }

    if (xfer == OTA_UART_XFER_OK) {
        s_uart.device_status = status;

        if (status == OTA_FRAME_STATUS_DOWNLOADING) {
            ESP_LOGI(TAG, "slave 0x%02X entered download mode, sending %u packets of %u bytes", (unsigned)s_uart.slave_address, (unsigned)s_uart.total, (unsigned)OTA_UART_PAYLOAD_SIZE);
            s_uart.sent = 0U;
            s_uart.last_log_pct = 0U;
            s_uart.state = OTA_UART_STATE_SEND_PACKET;
            return OTA_UART_RET_RUNNING;
        }

        if (status == OTA_FRAME_STATUS_FORBIDDEN) {
            /* bootloader 用 14 字节状态帧回开始失败（擦除或复制备份失败）。 */
            return ota_uart_fail("the device rejected the start operation");
        }
        /* 空闲/Flash 操作中/双坏：擦除仍在进行，继续等。 */
    }

    if (esp_timer_get_time() >= s_uart.deadline_us) {
        return ota_uart_fail("the device did not enter download mode");
    }
    return OTA_UART_RET_RUNNING;
}

/* 逐包发送：读一包 -> 发 0xAA -> 等 7 字节应答。busy 之外的任何失败都不
 * 重传，重复写同一包会让设备再次编程 Flash。 */
static ota_uart_ret_t ota_uart_step_send_packet(void)
{
    uint32_t offset = (uint32_t)s_uart.sent * OTA_UART_PAYLOAD_SIZE;
    uint32_t remaining = s_uart.image_size - offset;
    size_t chunk = (remaining > OTA_UART_PAYLOAD_SIZE) ? OTA_UART_PAYLOAD_SIZE : (size_t)remaining;
    size_t frame_length = 0U;
    size_t reply_length = 0U;
    uint8_t ack = 0U;
    uint16_t packet = 0U;
    unsigned pct = 0U;

    if (chunk == 0U) {
        return ota_uart_fail("image offset out of range");
    }

    esp_err_t err = esp_partition_read(s_uart.partition, offset, s_uart_payload, chunk);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read %s at %u failed: %s", s_uart.partition->label, (unsigned)offset, esp_err_to_name(err));
        return ota_uart_fail("image read failed");
    }

    frame_length = ota_frame_build_data(s_uart_frame, sizeof(s_uart_frame), s_uart.slave_address, s_uart.key, (uint16_t)(s_uart.total - 1U), s_uart.sent, s_uart_payload, chunk);
    if (frame_length == 0U) {
        return ota_uart_fail("data frame does not fit the transmit buffer");
    }

    ota_uart_xfer_t xfer = ota_uart_transact(frame_length, &reply_length);

    if (xfer == OTA_UART_XFER_BUSY) {
        return OTA_UART_RET_RUNNING;
    }
    if (xfer == OTA_UART_XFER_NO_REPLY) {
        ESP_LOGE(TAG, "packet %u/%u ack timeout", (unsigned)s_uart.sent, (unsigned)(s_uart.total - 1U));
        return ota_uart_fail("data packet ack timeout");
    }

    if (!ota_frame_parse_ack_reply(s_uart_reply, reply_length, s_uart.slave_address, &ack, &packet)) {
        return ota_uart_fail("invalid data packet ack frame");
    }
    if (ack != OTA_FRAME_ACK_OK) {
        ESP_LOGE(TAG, "packet %u rejected, ack 0x%02X", (unsigned)packet, (unsigned)ack);
        return ota_uart_fail("data packet rejected by the device");
    }
    if (packet != s_uart.sent) {
        ESP_LOGE(TAG, "ack packet %u does not match sent packet %u", (unsigned)packet, (unsigned)s_uart.sent);
        return ota_uart_fail("data packet ack mismatch");
    }

    ++s_uart.sent;

    pct = (unsigned)(((uint32_t)s_uart.sent * 100U) / s_uart.total);
    if (pct >= (unsigned)s_uart.last_log_pct + OTA_UART_LOG_PROGRESS_STEP || s_uart.sent >= s_uart.total) {
        s_uart.last_log_pct = (uint8_t)pct;
        ESP_LOGI(TAG, "progress %u%% (%u/%u packets)", pct, (unsigned)s_uart.sent, (unsigned)s_uart.total);
    }

    if (s_uart.sent >= s_uart.total) {
        ESP_LOGI(TAG, "all packets acked, waiting for the new application version");
        s_uart.deadline_us = esp_timer_get_time() + OTA_UART_VERIFY_WINDOW_US;
        s_uart.state = OTA_UART_STATE_VERIFY;
    }
    return OTA_UART_RET_RUNNING;
}

/* 成功判定只看目标芯片实读版本：全部包 ACK 只代表写进去了，末包的 SHA-256
 * 校验和跳转结果协议里没有应答，必须等 APP 起来后版本寄存器刷新再比对。 */
static ota_uart_ret_t ota_uart_step_verify(void)
{
    uint8_t expected[4];
    uint8_t actual_bytes[4];
    char actual[OTA_VERSION_STRING_MAX_LEN];

    if (!ota_uart_parse_version(s_uart.expected_version, expected)) {
        return ota_uart_fail("expected version string is invalid");
    }

    if (ota_uart_device_online() && ota_uart_read_device_version(actual, sizeof(actual)) && ota_uart_parse_version(actual, actual_bytes) && memcmp(expected, actual_bytes, sizeof(expected)) == 0) {
        ESP_LOGI(TAG, "slave 0x%02X version %s matches the record %s, upgrade done", (unsigned)s_uart.slave_address, actual, s_uart.expected_version);
        s_uart.state = OTA_UART_STATE_DONE;
        return OTA_UART_RET_DONE;
    }

    if (esp_timer_get_time() >= s_uart.deadline_us) {
        char current[OTA_VERSION_STRING_MAX_LEN] = {0};
        (void)ota_uart_read_device_version(current, sizeof(current));
        ESP_LOGE(TAG, "version mismatch: expected %s, slave 0x%02X reports %s", s_uart.expected_version, (unsigned)s_uart.slave_address, current);
        return ota_uart_fail("the device version does not match the record");
    }
    return OTA_UART_RET_RUNNING;
}

/* ---------- 对外接口 ---------- */

void ota_uart_reset(void)
{
    memset(&s_uart, 0, sizeof(s_uart));
    memset(&s_uart_xfer, 0, sizeof(s_uart_xfer));
    memset(&s_log_tx, 0, sizeof(s_log_tx));
    memset(&s_log_rx, 0, sizeof(s_log_rx));
    s_uart.state = OTA_UART_STATE_IDLE;
}

bool ota_uart_start(const char *expected_version, uint32_t image_size, ota_fw_target_t target)
{
    ota_uart_reset();

    if (expected_version == NULL || expected_version[0] == '\0') {
        ESP_LOGE(TAG, "start: empty expected version");
        return false;
    }
    if (strlen(expected_version) >= sizeof(s_uart.expected_version)) {
        ESP_LOGE(TAG, "start: version %s too long", expected_version);
        return false;
    }
    if (image_size < OTA_UART_IMAGE_MIN_SIZE || image_size > OTA_UART_IMAGE_MAX_SIZE) {
        ESP_LOGE(TAG, "start: image size %u out of range", (unsigned)image_size);
        return false;
    }
    if (!ota_fw_target_has_iap(target)) {
        ESP_LOGE(TAG, "start: target %d has no iap channel", (int)target);
        return false;
    }

    const char *label = ota_fw_target_partition_label(target);
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
    if (part == NULL) {
        ESP_LOGE(TAG, "start: partition %s not found", label);
        return false;
    }
    if (image_size > part->size) {
        ESP_LOGE(TAG, "start: image %u exceeds partition %s (%u)", (unsigned)image_size, part->label, (unsigned)part->size);
        return false;
    }

    uint32_t total = (image_size + OTA_UART_PAYLOAD_SIZE - 1U) / OTA_UART_PAYLOAD_SIZE;
    if (total == 0U || total > 0xFFFFU) {
        ESP_LOGE(TAG, "start: invalid packet count %u", (unsigned)total);
        return false;
    }

    strncpy(s_uart.expected_version, expected_version, sizeof(s_uart.expected_version) - 1U);
    s_uart.partition = part;
    s_uart.target = target;
    s_uart.slave_address = ota_fw_target_slave_address(target);
    s_uart.version_index = ota_fw_target_version_index(target);
    s_uart.image_size = image_size;
    s_uart.total = (uint16_t)total;
    s_uart.key = (uint32_t)(esp_timer_get_time() & 0xFFFFFFFFLL);
    if (s_uart.key == 0U) {
        s_uart.key = 1U;
    }
    s_uart.state = OTA_UART_STATE_QUERY;

    ESP_LOGI(TAG, "start: slave 0x%02X, image %u bytes from %s, %u packets, key 0x%08X, expect version %s",
             (unsigned)s_uart.slave_address, (unsigned)image_size, part->label, (unsigned)s_uart.total,
             (unsigned)s_uart.key, s_uart.expected_version);
    return true;
}

ota_uart_ret_t ota_uart_process(void)
{
    switch (s_uart.state) {
    case OTA_UART_STATE_IDLE:
        return OTA_UART_RET_IDLE;

    case OTA_UART_STATE_QUERY:
        return ota_uart_step_query();

    case OTA_UART_STATE_RESET:
        return ota_uart_step_reset();

    case OTA_UART_STATE_WAIT_BOOTLOADER:
        return ota_uart_step_wait_bootloader();

    case OTA_UART_STATE_START:
        return ota_uart_step_start();

    case OTA_UART_STATE_WAIT_START:
        return ota_uart_step_wait_start();

    case OTA_UART_STATE_SEND_PACKET:
        return ota_uart_step_send_packet();

    case OTA_UART_STATE_VERIFY:
        return ota_uart_step_verify();

    case OTA_UART_STATE_DONE:
        return OTA_UART_RET_DONE;

    case OTA_UART_STATE_FAIL:
        return OTA_UART_RET_FAIL;

    default:
        return ota_uart_fail("unknown internal state");
    }
}

void ota_uart_get_progress(uint16_t *sent, uint16_t *total)
{
    if (sent != NULL) {
        *sent = s_uart.sent;
    }
    if (total != NULL) {
        *total = s_uart.total;
    }
}