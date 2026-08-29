#include "Bluetooch.h"
#include "GATT.h"
#include "esp_log.h"
#include "wifi.h"
#include "BlueDataDeal.h"

static const char *TAG = "gatt";

char WiFiPassword[66];
char WiFiName[66];

WiFiInfo_t WiFiInfo;
char cOrder;
//外设控制服务uuid
static const ble_uuid16_t auto_io_svc_uuid = BLE_UUID16_INIT(0x1815);
//外设控制服务句柄
static uint16_t AutoIoSeverHandle;
//外设控制服务回调函数
static int AutoIoSeverCb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);
//外设控制服务特征uuid
static const ble_uuid128_t AutoIo_chr_uuid=BLE_UUID128_INIT(
                                                    0x69,0x6F,0x20,0x73,
                                                    0x65,0x72,0x76,0x65,
                                                    0x72,0x00,0x00,0x00,
                                                    0x00,0x00,0x00,0x00);
 
//wifi连接服务uuid
static const ble_uuid128_t WiFi_conn_svc_uuid = BLE_UUID128_INIT(
                                                    0x77,0x69,0x66,0x69,//wifi
                                                    0x77,0x6F,0x72,0x64,//name
                                                    0x70,0x61,0x72,0x64,//password
                                                    0x00,0x00,0x00,0x00);
//wifi密码特征uuid
static const ble_uuid128_t WiFi_password_chr_uuid = BLE_UUID128_INIT(0x70,0x61,0x73,0x73,0x77,0x6F,0x72,0x64,
                                                                      0x77,0x69,0x66,0x69,0x00,0x00,0x00,0x00);
//wifi名字特征uuid
static const ble_uuid128_t WiFi_name_chr_uuid     = BLE_UUID128_INIT(0x6E,0x61,0x6D,0x65,0x77,0x69,0x66,0x69,                                                                  
                                                                      0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
//wifi ssid以及password句柄
static uint16_t WifiNameSeverHandle; 
static uint16_t WifiPasswordSeverHandle; 
//wifi服务回调函数                                                        
static int WifiConnSeverCb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);

//数据反馈服务uuid
static const ble_uuid128_t Datafd_svc_uuid = BLE_UUID128_INIT(
                                                    0x44,0x61,0x66,0x64,
                                                    0x74,0x65,0x6D,0x70,
                                                    0x70,0x72,0x65,0x73,
                                                    0x74,0x69,0x6D,0x65);
//数据反馈特征uuid
static const ble_uuid128_t Datafd_temp_chr_uuid  = BLE_UUID128_INIT(0x54,0x65,0x6D,0x70,0x74,0x75,0x72,0x64,
                                                                    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
static const ble_uuid128_t Datafd_press_chr_uuid = BLE_UUID128_INIT(0x50,0x72,0x65,0x73,0x73,0x75,0x72,0x65,
                                                                     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
static const ble_uuid128_t Datafd_time_chr_uuid  = BLE_UUID128_INIT(0x54,0x61,0x72,0x43,0x75,0x72,0x54,0x69,
                                                                   0x6D,0X65,0x00,0x00,0x00,0x00,0x00,0x00);
//数据交换句柄
 uint16_t DatafdTempServerHandle;
 uint16_t DatafdPresServerHandle;
 uint16_t DatafdTimeServerHandle;
//数据交换回调函数
static int DataFdSeverCb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);

//系统监控服务uuid
static const ble_uuid128_t SysMoniter_svc_uuid = BLE_UUID128_INIT(
                                                    0x53,0x79,0x73,0x4D,
                                                    0x43,0x75,0x72,0x72,
                                                    0x73,0x61,0x66,0x65,
                                                    0x00,0x00,0x00,0x00);
//系统监控特征uuid
static const ble_uuid128_t SysMoniter_chr_uuid = BLE_UUID128_INIT(0x73,0x79,0x73,0x6D,0x6F,0x6E,0x69,0x74,
                                                                   0x65,0x72,0x00,0x00,0x00,0x00,0x00,0x00);
//系统监控句柄
 uint16_t SysMoniterServerHandle;
//系统监控回调函数
static int SysMoniterSeverCb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    //外设控制服务
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &auto_io_svc_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {/* Heart rate characteristic */
                    .uuid = &AutoIo_chr_uuid.u,
                    .access_cb = AutoIoSeverCb,
                    .flags = BLE_GATT_CHR_F_WRITE ,
                    .val_handle = &AutoIoSeverHandle},
                    {
                        0, /* No more characteristics in this service. */
                    }}    
    },
    //wifi密码账号写入服务
    {
     .type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &WiFi_conn_svc_uuid.u,
     .characteristics = 
        (struct ble_gatt_chr_def[]){
            {
                .uuid = &WiFi_password_chr_uuid.u,
                .access_cb = WifiConnSeverCb,
                .flags = BLE_GATT_CHR_F_WRITE ,
                .val_handle = &WifiPasswordSeverHandle
            },
            {
                .uuid = &WiFi_name_chr_uuid.u,
                .access_cb = WifiConnSeverCb,
                .flags = BLE_GATT_CHR_F_WRITE ,
                .val_handle = &WifiNameSeverHandle
            },
            {
                0
            }
        }
    },
    //数据反馈服务
    {
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = &Datafd_svc_uuid.u,
    .characteristics =
        (struct ble_gatt_chr_def[]){/* temp characteristic */
                {
                    .uuid = &Datafd_temp_chr_uuid.u,
                    .access_cb = DataFdSeverCb,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY |
                             BLE_GATT_CHR_F_INDICATE,
                    .descriptors=NULL,
                    .val_handle = &DatafdTempServerHandle},
                 /* press characteristc*/
                {
                    .uuid = &Datafd_press_chr_uuid.u,
                    .access_cb = DataFdSeverCb,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY |
                             BLE_GATT_CHR_F_INDICATE,
                    .descriptors=NULL,
                    .val_handle = &DatafdPresServerHandle},
                    /* time characteristic*/
                {
                    .uuid = &Datafd_time_chr_uuid.u,
                    .access_cb = DataFdSeverCb,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY |
                             BLE_GATT_CHR_F_INDICATE,
                    .descriptors=NULL,
                    .val_handle = &DatafdTimeServerHandle},
                {0}},
    },
    /*system moniter server*/
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &SysMoniter_svc_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {/* system moniter characteristic */
                    .uuid = &SysMoniter_chr_uuid.u,
                    .access_cb = SysMoniterSeverCb,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY |
                             BLE_GATT_CHR_F_INDICATE|BLE_GATT_CHR_F_WRITE,
                    .descriptors=NULL,
                    .val_handle = &SysMoniterServerHandle},
                {
                    0, /* No more characteristics in this service. */
                }},
    },
    {
        0, /* No more services. */
    },
};                         

int gatt_svr_init(void)
{
    int rc;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    // ble_svc_ans_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to count GATT services: rc=%d", rc);
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to add GATT services: rc=%d", rc);
        return rc;
    }

    return 0;
}

//回调函数处理

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        MODLOG_DFLT(DEBUG, "registered service %s with handle=%d\n",
                    ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                    ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        MODLOG_DFLT(DEBUG, "registering characteristic %s with "
                    "def_handle=%d val_handle=%d\n",
                    ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                    ctxt->chr.def_handle,
                    ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        MODLOG_DFLT(DEBUG, "registering descriptor %s with handle=%d\n",
                    ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                    ctxt->dsc.handle);
        break;

    default:
        assert(0);
        break;
    }
}

static int AutoIoSeverCb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op){
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            if (attr_handle == AutoIoSeverHandle){
                BlueDataMessage_t message = {0};
                message.length = OS_MBUF_PKTLEN(ctxt->om);
                if (message.length == 0 ||
                    message.length > sizeof(message.data)) {
                    ESP_LOGW(TAG, "Invalid AutoIO data length: %u",
                             (unsigned)message.length);
                    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
                }
                if (ble_hs_mbuf_to_flat(ctxt->om, message.data,
                                        sizeof(message.data), NULL) != 0) {
                    ESP_LOGE(TAG, "Failed to read AutoIO data");
                    return BLE_ATT_ERR_UNLIKELY;
                }
                if (BlueDataQueue == NULL ||
                    xQueueSend(BlueDataQueue, &message,
                               pdMS_TO_TICKS(10)) != pdTRUE) {
                    ESP_LOGW(TAG, "Bluetooth command queue unavailable/full");
                    return BLE_ATT_ERR_INSUFFICIENT_RES;
                }
                cOrder=ctxt->om->om_data[0];
                ESP_LOGI(TAG,"AutoIO data:%X,%X,%X,%X,len:%d,order:%c",ctxt->om->om_data[0],ctxt->om->om_data[1],ctxt->om->om_data[2],ctxt->om->om_data[3],ctxt->om->om_len,cOrder);
            }
        break;
    }
    return 0;
}


static int WifiConnSeverCb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch(ctxt->op){
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            if(attr_handle == WifiNameSeverHandle)
            {
                // 直接复制原始数据
                size_t len = ctxt->om->om_len;
                if(len >= sizeof(WiFiName)) {
                    len = sizeof(WiFiName) - 1;
                }
                memcpy(WiFiName, ctxt->om->om_data, len);
                WiFiName[len] = '\0';
                
                // 检查并去掉引号
                char* ssid = WiFiName;
                if(len > 1 && ssid[0] == '"' && ssid[len-1] == '"') {
                    // 去掉首尾引号
                    ssid[len-1] = '\0';  // 去掉尾部引号
                    memmove(ssid, ssid + 1, len - 1);  // 去掉头部引号
                }
                
                printf("WiFi SSID saved: %s\n", WiFiName);
                
            }
            else if(attr_handle == WifiPasswordSeverHandle)
            {
                // 直接复制原始数据
                size_t len = ctxt->om->om_len;
                if(len >= sizeof(WiFiPassword)) {
                    len = sizeof(WiFiPassword) - 1;
                }
                memcpy(WiFiPassword, ctxt->om->om_data, len);
                WiFiPassword[len] = '\0';
                
                // 检查并去掉引号
                char* password = WiFiPassword;
                if(len > 1 && password[0] == '"' && password[len-1] == '"') {
                    // 去掉首尾引号
                    password[len-1] = '\0';  // 去掉尾部引号
                    memmove(password, password + 1, len - 1);  // 去掉头部引号
                }
                printf("WiFi Password saved: %s\n", WiFiPassword);
            }
            break;
    }
    return 0;
}

static int DataFdSeverCb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch(ctxt->op){
        case BLE_GATT_ACCESS_OP_READ_CHR :
            if(attr_handle == DatafdTempServerHandle)
            {
                
            }
    }
    return 0;
}

// static int blTime[16]={0};

int h2i(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

// 8个十六进制字符 → 时间戳
uint32_t hex2timestamp(const char *s) {
    uint32_t val = 0;
    for(int i = 0; i < 8; i++) {
        val = (val << 4) | h2i(s[i]);
    }
    return val;
}

static int SysMoniterSeverCb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch(ctxt->op){
        case BLE_GATT_ACCESS_OP_READ_CHR :
            if(attr_handle == SysMoniterServerHandle)
            {
                 
            }
            break;
        case BLE_GATT_ACCESS_OP_WRITE_CHR :
            if(attr_handle == SysMoniterServerHandle)
            {
                // 收到4个字节（小端序）
                if(ctxt->om->om_len >= 4) {
                    uint32_t ts = (ctxt->om->om_data[3] << 24) |
                                (ctxt->om->om_data[2] << 16) |
                                (ctxt->om->om_data[1] << 8) |
                                ctxt->om->om_data[0];
                    
                    struct timeval tv = {.tv_sec = ts, .tv_usec = 0};
                    settimeofday(&tv, NULL);
                    
                    ESP_LOGI(TAG, "设置时间戳: %lu", ts);
                    time_t now = ts;
                    ESP_LOGI(TAG, "设置时间: %s", ctime(&now));

                    
                }
            }
            break;
    }
    return 0;
}


