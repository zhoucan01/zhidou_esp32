#ifndef OTA_INFO_H
#define OTA_INFO_H

#include <stdbool.h>
#include <stdint.h>

#include "OTAVerManager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* OTAInfo: OTA_Info 分区里的升级记录（每个目标芯片一条）。
 *
 * OTA_Info 是 data/undefined 分区（partitions.csv，4K），不参与启动切换，
 * 只用来记录"某颗芯片当前已经落盘到哪个版本"。用途有两个：
 *   1. 下挂芯片（AT32/ST32）没有可读的固件版本寄存器时，用记录里的版本号
 *      作为 current_version 上报云端，避免重复下载同一份镜像；
 *   2. 后续做 IAP 烧写时，用它判断 AT_Firmware/ST_Firmware 里的镜像是否
 *      完整、是否与云端一致（记录里带 md5 与长度）。
 *
 * 布局：分区内按槽位顺序排列，槽位下标即 ota_fw_target_t 的序号。
 * 槽位 0 = AT32（AT_Firmware），槽位 1 = ST32（ST_Firmware，预留）；
 * ESP32 走 app_update/otadata，不占槽位。
 *
 * 单槽位结构以 magic + crc32 做完整性校验：分区被擦除（全 0xFF）或写入
 * 中断时，读取侧按"无记录"处理并退回调用方的默认版本，不返回脏数据。
 *
 * 命名规范：类型/函数 snake_case，前缀 ota_info_；宏全大写前缀 OTAINFO_；
 * 内部静态变量 s_ 前缀；C99，只用 stdint/stdbool/stddef。 */

/* OTA_Info 分区标签，与 partitions.csv 一致。 */
#define OTAINFO_PART_LABEL          "OTA_Info"

/* 记录魔数 "OTAI"（大端书写），用于识别槽位是否被写过。 */
#define OTAINFO_MAGIC               0x4F544149u

/* 单槽位记录格式版本，结构变化时递增；版本不符按"无记录"处理。 */
#define OTAINFO_FORMAT_VERSION      1

/* 槽位数：覆盖 ota_fw_target_t 的 AT32/ST32 两个下挂目标。 */
#define OTAINFO_SLOT_COUNT          2

/* 单个目标芯片的升级记录。
 * reserved 显式补齐到 4 字节边界，避免编译器隐式填充导致结构体大小随
 * 编译选项变化；分区内的布局必须稳定，否则跨固件版本读不出旧记录。 */
typedef struct {
    uint32_t magic;                                 /* OTAINFO_MAGIC */
    uint16_t format_version;                        /* OTAINFO_FORMAT_VERSION */
    uint16_t target;                                /* ota_fw_target_t */
    uint32_t image_size;                            /* 镜像字节数 */
    uint32_t updated_at;                            /* 写入时刻（ms tick） */
    char     version[OTA_VERSION_STRING_MAX_LEN];   /* 已落盘版本，如 "1.0.1" */
    char     md5[OTA_VERSION_MD5_HEX_LEN + 1];      /* 镜像 md5，32 位小写十六进制 */
    char     reserved[3];                           /* 对齐，置 0 */
    uint32_t crc32;                                 /* 覆盖 crc32 之前的全部字段 */
} ota_info_slot_t;

/* 读取某目标的记录。
 *
 *   target - 目标芯片，只有 AT32/ST32 有槽位；
 *   out    - 读取成功时回填完整槽位内容，失败时内容未定义。
 *
 * 返回值：true = 记录有效（magic/格式版本/目标/CRC 全部通过）；
 *         false = 无有效记录（分区不存在、未写过、内容损坏）或入参非法。
 * 纯读操作，不阻塞，不修改分区。 */
bool ota_info_read(ota_fw_target_t target, ota_info_slot_t *out);

/* 写入某目标的记录：擦除整个 OTA_Info 分区后把两个槽位一起写回。
 *
 * OTA_Info 分区只有 1 个 4K 扇区，两个槽位同扇区，所以无法只擦一个槽位；
 * 实现上先把另一个槽位的旧记录读出来，擦除后再一并写回，保证互不影响。
 *
 *   target     - 目标芯片，只有 AT32/ST32 有槽位；
 *   version    - 已落盘的版本字符串，长度必须 < OTA_VERSION_STRING_MAX_LEN；
 *   md5        - 镜像 md5，必须恰好 OTA_VERSION_MD5_HEX_LEN 个字符；
 *   image_size - 镜像字节数。
 *
 * 返回值：true = 写入并通过回读校验；false = 分区缺失、入参非法或写失败。
 * 写 flash 期间关闭 cache，会有毫秒级阻塞，只在下游镜像下载完成后调用一次。 */
bool ota_info_write(ota_fw_target_t target, const char *version, const char *md5, uint32_t image_size);

/* 取某目标的已落盘版本，失败时把 out 置为空串。
 * 入参 out_len 为缓冲区长度，不得小于 OTA_VERSION_STRING_MAX_LEN。 */
bool ota_info_get_version(ota_fw_target_t target, char *out, int out_len);

/* 擦除整个 OTA_Info 分区（全部记录失效）。仅用于产测/恢复场景。 */
bool ota_info_clear_all(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_INFO_H */