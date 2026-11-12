#include <stddef.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"

#include "OTAInfo.h"

/* OTA_Info 分区读写：单扇区 4K，按槽位顺序存放各目标芯片的落盘记录。
 * 读写都以整扇区为单位，写入前先把未改动的槽位读出来一起回写。 */

static const char *TAG = "OTAInfo";

/* 单槽位在分区内的字节跨度：结构体大小按 4 字节对齐。 */
#define OTAINFO_SLOT_SIZE       ((uint32_t)sizeof(ota_info_slot_t))

/* 分区容量上限自检：两个槽位必须能装进 4K 扇区。 */
#define OTAINFO_PART_SIZE_MIN   (OTAINFO_SLOT_SIZE * OTAINFO_SLOT_COUNT)

/* 擦除/回写缓冲。静态数组而不是 malloc：写 flash 期间关 cache，
 * 静态数据位于内部 RAM 可直接访问，同时避免升级路径上的堆碎片。 */
static uint8_t s_info_buf[4096];

static const esp_partition_t *ota_info_partition(void)
{
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, OTAINFO_PART_LABEL);
    if (part == NULL) {
        ESP_LOGE(TAG, "partition %s not found", OTAINFO_PART_LABEL);
    }
    return part;
}

/* 槽位下标即 ota_fw_target_t 序号；ESP32 不占槽位。 */
static bool ota_info_slot_index(ota_fw_target_t target, uint32_t *index)
{
    if (target != OTA_FW_TARGET_AT32 && target != OTA_FW_TARGET_ST32) {
        return false;
    }
    *index = (uint32_t)target;
    return (*index < OTAINFO_SLOT_COUNT);
}

static uint32_t ota_info_slot_crc(const ota_info_slot_t *slot)
{
    /* 覆盖 crc32 之前的全部字段，crc32 自身的值不参与计算。 */
    return esp_rom_crc32_le(0, (const uint8_t *)slot, (uint32_t)offsetof(ota_info_slot_t, crc32));
}

static bool ota_info_slot_is_valid(const ota_info_slot_t *slot, ota_fw_target_t target)
{
    if (slot->magic != OTAINFO_MAGIC) {
        return false;
    }
    if (slot->format_version != OTAINFO_FORMAT_VERSION) {
        ESP_LOGW(TAG, "slot format %u != %u", (unsigned)slot->format_version, (unsigned)OTAINFO_FORMAT_VERSION);
        return false;
    }
    if (slot->target != (uint16_t)target) {
        ESP_LOGW(TAG, "slot target %u != %u", (unsigned)slot->target, (unsigned)target);
        return false;
    }
    if (slot->crc32 != ota_info_slot_crc(slot)) {
        ESP_LOGW(TAG, "slot crc mismatch");
        return false;
    }
    /* version 必须是合法 C 字符串才可用，越界说明记录被破坏。 */
    if (memchr(slot->version, '\0', sizeof(slot->version)) == NULL) {
        ESP_LOGW(TAG, "slot version not terminated");
        return false;
    }
    return true;
}

bool ota_info_read(ota_fw_target_t target, ota_info_slot_t *out)
{
    uint32_t index = 0;

    if (out == NULL || !ota_info_slot_index(target, &index)) {
        ESP_LOGE(TAG, "read: invalid target %d", (int)target);
        return false;
    }

    const esp_partition_t *part = ota_info_partition();
    if (part == NULL) {
        return false;
    }
    if (part->size < OTAINFO_PART_SIZE_MIN) {
        ESP_LOGE(TAG, "read: partition too small (%u)", (unsigned)part->size);
        return false;
    }

    /* 只读一个槽位的跨度，不整扇区读。 */
    esp_err_t err = esp_partition_read(part, index * OTAINFO_SLOT_SIZE, out, OTAINFO_SLOT_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read: esp_partition_read failed: %s", esp_err_to_name(err));
        return false;
    }

    if (!ota_info_slot_is_valid(out, target)) {
        return false;
    }
    return true;
}

bool ota_info_get_version(ota_fw_target_t target, char *out, int out_len)
{
    if (out == NULL || out_len < OTA_VERSION_STRING_MAX_LEN) {
        return false;
    }

    out[0] = '\0';

    ota_info_slot_t slot;
    if (!ota_info_read(target, &slot)) {
        return false;
    }

    strncpy(out, slot.version, (size_t)out_len - 1);
    out[out_len - 1] = '\0';
    return true;
}

bool ota_info_write(ota_fw_target_t target, const char *version, const char *md5, uint32_t image_size)
{
    uint32_t index = 0;

    if (!ota_info_slot_index(target, &index)) {
        ESP_LOGE(TAG, "write: invalid target %d", (int)target);
        return false;
    }
    if (version == NULL || md5 == NULL || version[0] == '\0' || strlen(version) >= OTA_VERSION_STRING_MAX_LEN || strlen(md5) != (size_t)OTA_VERSION_MD5_HEX_LEN) {
        ESP_LOGE(TAG, "write: invalid args");
        return false;
    }

    const esp_partition_t *part = ota_info_partition();
    if (part == NULL) {
        return false;
    }
    if (part->size < OTAINFO_PART_SIZE_MIN || part->size > sizeof(s_info_buf)) {
        ESP_LOGE(TAG, "write: unsupported partition size (%u)", (unsigned)part->size);
        return false;
    }

    const uint32_t part_size = part->size;

    /* 分区只有 1 个扇区、两个槽位同扇区，擦除会连带清掉另一个槽位，
     * 所以先把整扇区读出来，只替换本目标的槽位，再整扇区回写。 */
    esp_err_t err = esp_partition_read(part, 0, s_info_buf, part_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write: read back failed: %s", esp_err_to_name(err));
        return false;
    }

    ota_info_slot_t *slot = (ota_info_slot_t *)(void *)(s_info_buf + index * OTAINFO_SLOT_SIZE);

    memset(slot, 0, OTAINFO_SLOT_SIZE);
    slot->magic = OTAINFO_MAGIC;
    slot->format_version = OTAINFO_FORMAT_VERSION;
    slot->target = (uint16_t)target;
    slot->image_size = image_size;
    slot->updated_at = (uint32_t)xTaskGetTickCount();
    strcpy(slot->version, version);
    strcpy(slot->md5, md5);
    slot->crc32 = ota_info_slot_crc(slot);

    err = esp_partition_erase_range(part, 0, part_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write: erase failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_partition_write(part, 0, s_info_buf, part_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write: esp_partition_write failed: %s", esp_err_to_name(err));
        return false;
    }

    /* 回读校验：写 flash 可能因掉电/坏块失败，落盘记录必须可信。 */
    ota_info_slot_t verify;
    if (!ota_info_read(target, &verify)) {
        ESP_LOGE(TAG, "write: verify read failed");
        return false;
    }
    if (strcmp(verify.version, version) != 0 || strcasecmp(verify.md5, md5) != 0 || verify.image_size != image_size) {
        ESP_LOGE(TAG, "write: verify mismatch");
        return false;
    }

    ESP_LOGI(TAG, "target %d saved: version %s, size %u, md5 %s", (int)target, version, (unsigned)image_size, md5);
    return true;
}

bool ota_info_clear_all(void)
{
    const esp_partition_t *part = ota_info_partition();
    if (part == NULL) {
        return false;
    }

    esp_err_t err = esp_partition_erase_range(part, 0, part->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "clear: erase failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "all records cleared");
    return true;
}