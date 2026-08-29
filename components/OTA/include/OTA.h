// OTA.h
#ifndef OTA_H
#define OTA_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 OTA 检查
 */
void start_ota_check(void);

/**
 * @brief 检查 OTA 状态
 */
void check_ota_state(void);

// ========== 设备信息设置接口 ==========
/**
 * @brief 设置产品型号
 */
void set_product_model(const char *model);

/**
 * @brief 设置量产版本代号
 */
void set_version_code(int code);

/**
 * @brief 设置产品编号
 */
void set_serial_number(int sn);

/**
 * @brief 一次性设置所有设备信息
 */
void set_device_info(const char *model, int code, int sn);

/**
 * @brief 获取当前设备ID
 */
const char* get_device_id(void);

/**
 * @brief 获取当前产品型号
 */
const char* get_product_model(void);

/**
 * @brief 获取当前版本代号
 */
int get_version_code(void);

/**
 * @brief 获取当前产品编号
 */
int get_serial_number(void);
// ====================================

#ifdef __cplusplus
}
#endif

#endif /* OTA_H */