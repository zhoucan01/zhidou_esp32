#include "Bluetooch.h"
#include "GATT.h"

 const char *DeviceName = "EVT46";//设备名字
// const char *DeviceName = "nuvell_1";//设备名字
// const char *DeviceName = "nuvell_2";//设备名字
// const char *DeviceName = "nuvell_3";//设备名字
// const char *DeviceName = "nuvell_4";//设备名字
// const char *DeviceName = "nuvell_5";//设备名字
// const char *DeviceName = "nuvell_666";//设备名字
// const char *DeviceName = "nuvell_777";//设备名字
const char *TAG = "bluetooth";

static uint8_t own_addr_type;
static uint8_t addr_val[6] = {0};

static void start_advertising(void);
static void adv_init();
int GAPCallBack(struct ble_gap_event *event, void *arg);

inline void format_addr(char *addr_str, uint8_t addr[]) {
    sprintf(addr_str, "%02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1],
            addr[2], addr[3], addr[4], addr[5]);
}

static void ble_on_reset(int reason)
{
    MODLOG_DFLT(ERROR, "Resetting state; reason=%d\n", reason);
}

static void ble_on_sync(void)
{
    adv_init();
}
//蓝牙初始化
void BluetoothInit(void){
    //初始化nvs flash
    esp_err_t ret;
    //初始化nimble
    ret = nimble_port_init();
    ESP_ERROR_CHECK(ret);
    //初始化gap
    //初始化回调函数
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // ble_hs_cfg.sm_io_cap = CONFIG_EXAMPLE_IO_TYPE;
    ret = gatt_svr_init();
    ESP_ERROR_CHECK(ret);
}

//设置广播参数
static void adv_init(void)
{
    char addr_str[18] = {0};
    /* Make sure we have proper BT identity address set */
    ble_hs_util_ensure_addr(0);
    /* Figure out BT address to use while advertising */
    ble_hs_id_infer_auto(0, &own_addr_type);
    /* Copy device address to addr_val */
    ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);
    format_addr(addr_str, addr_val);
    ESP_LOGI(TAG, "device address: %s", addr_str);
    /* Start advertising. */
    start_advertising();
}

//开始广播
static void start_advertising(void)
{
    /* Local variables */
    int rc = 0;
    const char *name;
    struct ble_hs_adv_fields adv_fields = {0};
    struct ble_gap_adv_params adv_params = {0};

    /* Set advertising flags */
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* Set device name */
    name = DeviceName;
    adv_fields.name = (uint8_t *)name;
    adv_fields.name_len = strlen(name);
    adv_fields.name_is_complete = 1;
    /* Set advertiement fields */
    rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to set advertising data, error code: %d", rc);
        return;
    }

    /* Set non-connetable and general discoverable mode to be a beacon */
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;//设置可连接
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    //设置广播间隔
    /* A 500 ms interval is easy to miss while Wi-Fi coexistence is active.
     * Use 100 ms for responsive discovery on phones and PCs. */
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(100);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(110);

    /* Start advertising */
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                           GAPCallBack, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to start advertising, error code: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "advertising started!"); 
}

void bleprph_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();

    nimble_port_freertos_deinit();
}

extern uint16_t DatafdTempServerHandle; extern uint16_t DatafdPresServerHandle;
extern uint16_t DatafdTimeServerHandle; extern uint16_t SysMoniterServerHandle;

/*回调函数管理*/
/*GAP回调函数*/
uint16_t new_conn = BLE_HS_CONN_HANDLE_NONE;
uint8_t  ConnectState=1;
static bool indication_in_flight;
static bool temp_indicate_enabled;
static bool press_indicate_enabled;
static bool time_indicate_enabled;
static bool monitor_indicate_enabled;
static bool temp_notify_enabled;
static bool press_notify_enabled;
static bool time_notify_enabled;
static bool monitor_notify_enabled;

static bool indication_is_enabled(uint16_t attr_handle)
{
    if (attr_handle == DatafdTempServerHandle) return temp_indicate_enabled;
    if (attr_handle == DatafdPresServerHandle) return press_indicate_enabled;
    if (attr_handle == DatafdTimeServerHandle) return time_indicate_enabled;
    if (attr_handle == SysMoniterServerHandle) return monitor_indicate_enabled;
    return false;
}

static bool notification_is_enabled(uint16_t attr_handle)
{
    if (attr_handle == DatafdTempServerHandle) return temp_notify_enabled;
    if (attr_handle == DatafdPresServerHandle) return press_notify_enabled;
    if (attr_handle == DatafdTimeServerHandle) return time_notify_enabled;
    if (attr_handle == SysMoniterServerHandle) return monitor_notify_enabled;
    return false;
}

static void set_indication_enabled(uint16_t attr_handle, bool enabled)
{
    if (attr_handle == DatafdTempServerHandle) temp_indicate_enabled = enabled;
    else if (attr_handle == DatafdPresServerHandle) press_indicate_enabled = enabled;
    else if (attr_handle == DatafdTimeServerHandle) time_indicate_enabled = enabled;
    else if (attr_handle == SysMoniterServerHandle) monitor_indicate_enabled = enabled;
}

static void set_notification_enabled(uint16_t attr_handle, bool enabled)
{
    if (attr_handle == DatafdTempServerHandle) temp_notify_enabled = enabled;
    else if (attr_handle == DatafdPresServerHandle) press_notify_enabled = enabled;
    else if (attr_handle == DatafdTimeServerHandle) time_notify_enabled = enabled;
    else if (attr_handle == SysMoniterServerHandle) monitor_notify_enabled = enabled;
}

static void clear_indication_state(void)
{
    temp_indicate_enabled = false;
    press_indicate_enabled = false;
    time_indicate_enabled = false;
    monitor_indicate_enabled = false;
    temp_notify_enabled = false;
    press_notify_enabled = false;
    time_notify_enabled = false;
    monitor_notify_enabled = false;
    indication_in_flight = false;
}
int GAPCallBack(struct ble_gap_event *event, void *arg)
{
    switch(event->type){
        case BLE_GAP_EVENT_CONNECT:
            ConnectState = event->connect.status; // 0 表示成功
            
            if (ConnectState == 0) {
                new_conn = event->connect.conn_handle;
                clear_indication_state();
                printf("Adv has been connected! Handle: %d\r\n", new_conn);
                ble_gap_adv_stop(); // 连接成功后停止广播
            } else {
                new_conn = BLE_HS_CONN_HANDLE_NONE;
                printf("Connection failed, status: %d\r\n", ConnectState);
                // 连接失败也重新广播
                start_advertising(); 
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            printf("Disconnected! Reason: %d\r\n", event->disconnect.reason);
            new_conn = BLE_HS_CONN_HANDLE_NONE;
            clear_indication_state();
            ConnectState = 1; // 【重要】先标记为未连接，阻止主循环发送
            
            // 稍微延时一下再开启广播，避免时序冲突（可选，但推荐）
            // 在实际生产中最好用事件标志组，测试代码直接调也可以
            start_advertising(); 
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGW(TAG, "Advertising completed unexpectedly: reason=%d",
                     event->adv_complete.reason);
            if (!BlueToochState()) {
                start_advertising();
            }
            break;
            
        case BLE_GAP_EVENT_SUBSCRIBE:
            set_indication_enabled(event->subscribe.attr_handle,
                                   event->subscribe.cur_indicate != 0);
            set_notification_enabled(event->subscribe.attr_handle,
                                     event->subscribe.cur_notify != 0);
            // 打印订阅状态，确认手机/从机是否真的开启了 Notify
            if (event->subscribe.cur_notify || event->subscribe.cur_indicate) {
                printf("SUBSCRIBE ENABLED on Handle: %d\r\n", event->subscribe.attr_handle);
            } else {
                printf("SUBSCRIBE DISABLED on Handle: %d\r\n", event->subscribe.attr_handle);
            }
            break;
            
        case BLE_GAP_EVENT_NOTIFY_TX:
            /* status 0 only means the indication was transmitted.  Wait for
             * BLE_HS_EDONE (peer confirmation) before sending the next one. */
            if (event->notify_tx.indication && event->notify_tx.status != 0) {
                indication_in_flight = false;
            }
            // 这里可以监控发送结果
            if (event->notify_tx.status != 0 &&
                event->notify_tx.status != BLE_HS_EDONE) {
                 ESP_LOGW(TAG, "Indicate TX failed: %d", event->notify_tx.status);
            }
            break;   
    };
    return 0;
}

bool BlueToochState(void)
{
    if(ConnectState == 0)
    {
        return true;
    }
    else
    {
        return false;
    }
}

void SendMessage(uint8_t *data, size_t len, uint16_t channel)
{
    // 【双重保险】再次检查连接状态
    if (!BlueToochState()) {
        return; // 直接返回，不打印日志，减少串口干扰
    }
    
    // 检查句柄是否有效 (防止 new_conn 是随机值)
    bool use_indication = indication_is_enabled(channel);
    bool use_notification = notification_is_enabled(channel);
    if (new_conn == BLE_HS_CONN_HANDLE_NONE ||
        (!use_indication && !use_notification) ||
        (use_indication && indication_in_flight)) {
        return;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        // 内存不足通常意味着系统负载过高，不要重试，直接返回
        return;
    }

    int rc;
    if (use_indication) {
        rc = ble_gatts_indicate_custom(new_conn, channel, om);
    } else {
        rc = ble_gatts_notify_custom(new_conn, channel, om);
    }
    
    if (rc != 0) {
        // 失败必须释放内存，否则内存泄漏会导致后续崩溃
        os_mbuf_free_chain(om); 
        
        // 如果是 ENOTCONN (7)，说明连接其实已经断了，强制更新状态
        if (rc == 7) {
            ConnectState = 1; 
        }
        // 测试期间可以打印，正式运行建议注释掉以减少串口压力
    //   ESP_LOGW(TAG, "BLE send failed: rc=%d conn=%u handle=%u mode=%s",
    //           rc, new_conn, channel,
    //             use_indication ? "indicate" : "notify");
    } else if (use_indication) {
        indication_in_flight = true;
    }
    // 成功则不释放，由协议栈接管
}
