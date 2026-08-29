#include "DataCenter.h"

#include "freertos/FreeRTOS.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "sys.h"
#include "Bluetooch.h"
#include "crc.h"

#include "nanomodbus.h"

#include "string.h"

char version[3][4]={
                    {0,0,0,1},//esp32s3版本
                    {0,0,0,1},//stm32f103版本
                    {0,0,0,1} //stm32g431版本呢
                  };

//发送数据数组 //帧头 cmd par1 par2 par3 par4 par5 CRClow CRChigh 帧尾
uint8_t screenData[10] = {0x73,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x6e};
//发送完数据后 返回0xff和0x00表示数据正确
//接收到0x00和0xff表示重发
bool sensorState[sensorNum];//存储传感器的数据
bool moterState[moterNum];//存储电机状态数据

extern QueueHandle_t uart_queue; 

// 结构体定义已在头文件中，这里只需要定义变量
struct DataPool g_DataPool = {0};
extern uint16_t SysMoniterServerHandle;

static size_t uart_frame_length(uint8_t first, uint8_t second)
{
    if (first == 0x63 && second == 0x70) return 14;
    if (first == 0x61 && second == 0x73) return 16;
    if (first == 0x76 && second == 0x64) return 17;
    /* 0x76 + version[4] + CRC low/high + 0x6e. */
    if (first == 0x76) return 8;
    if (first == 0x6d && second == 0x6f) return 12;
    return 0;
}

static void process_uart_frame(const uint8_t *frame, size_t length)
{
    size_t payload_length = length - 3; /* Exclude CRC16 and 0x6c tail. */
    uint16_t crc = CRC16_Calc((uint8_t *)frame, payload_length);
    if (frame[payload_length] != (uint8_t)(crc & 0xff) ||
        frame[payload_length + 1] != (uint8_t)(crc >> 8)) {
        ESP_LOGW("DataCenter", "Discard UART frame with invalid CRC");
        return;
    }

    SendMessage((uint8_t *)frame, length, SysMoniterServerHandle);

    if (frame[0] == 0x63) {
        sensorState[riseHighPos] = frame[2];
        sensorState[riseLowPos] = frame[3];
        sensorState[pressHighPos] = frame[4];
        sensorState[pressLowPos] = frame[5];
        sensorState[pressHeadAvail] = frame[6];
        memcpy(&g_DataPool.turns, &frame[7], sizeof(float));
    } else if (frame[0] == 0x61) {
        sensorState[waterBoxAvail] = frame[2];
        sensorState[waterBoxHighPos] = frame[3];
        sensorState[waterBoxLowPos] = frame[4];
        sensorState[wasteBoxAvail] = frame[5];
        sensorState[valveOnPos] = frame[6];
        sensorState[valveOffPos] = frame[7];
        sensorState[brineBoxAvail] = frame[8];
        sensorState[overFlow] = frame[9];
        sensorState[lidAvail] = frame[10];
        sensorState[filerAvail] = frame[11];
        sensorState[moldedBoxAvail] = frame[12];
    } else if (frame[0] == 0x76 && length == 8) {
        for (size_t i = 0; i < 4; ++i) {
            version[1][i] = frame[i + 1];
        }
        ESP_LOGI("DataCenter", "controller version: %d.%d.%d.%d",
                 version[1][0], version[1][1],
                 version[1][2], version[1][3]);
    } else if (frame[0] == 0x76) {
        memcpy(&g_DataPool.temperature, &frame[2], sizeof(float));
        memcpy(&g_DataPool.voltage, &frame[6], sizeof(float));
        memcpy(&g_DataPool.current, &frame[10], sizeof(float));
    } else if (frame[0] == 0x6d) {
        moterState[Valve] = frame[2];
        moterState[pressMoter] = frame[3];
        moterState[clearWaterPump] = frame[4];
        moterState[drinePump] = frame[5];
        moterState[hosePump] = frame[6];
        moterState[riseMoter] = frame[7];
        moterState[breakMoter] = frame[8];
    }
}
// 数据池任务
#if 0
static void DataCenterTaskLegacy(void *pvParameters)
{
    uart_event_t event;
    uint16_t usartcrc;
    uint8_t tempCrcHigh;//crc高位
    uint8_t tempCrcLow;//crc低位

    while(1)
    {
        if (xQueueReceive(uart_queue, (void *)&event, pdMS_TO_TICKS(5)) ==
                pdTRUE &&
            event.type == UART_DATA) {

            if (event.size !=0) {
                size_t read_size = event.size;
                if (read_size > sizeof(UsartdataPool)) {
                    read_size = sizeof(UsartdataPool);
                }
                memset(UsartdataPool, 0, sizeof(UsartdataPool));
                int received = uart_read_bytes(ScreenUart, UsartdataPool,
                                               read_size,
                                               pdMS_TO_TICKS(100));
                if (received <= 0) {
                    ESP_LOGW("DataCenter", "UART event without readable data");
                    continue;
                }
                static int dataTaskCount=0;
                dataTaskCount++;
                if(dataTaskCount>100)
                {
                    dataTaskCount =0;
                    ESP_LOGI("DATA","data:%x,%x,%x,%x,%x,%x,%x,%x,%x,%x",UsartdataPool[0],UsartdataPool[1],UsartdataPool[2],UsartdataPool[3],UsartdataPool[4],UsartdataPool[5],UsartdataPool[6],UsartdataPool[7],UsartdataPool[8],UsartdataPool[9]);
                    ESP_LOGI("DATA","data:%x,%x,%x,%x,%x,%x,%x,%x,%x,%x",UsartdataPool[10],UsartdataPool[11],UsartdataPool[12],UsartdataPool[13],UsartdataPool[14],UsartdataPool[15],UsartdataPool[16],UsartdataPool[17],UsartdataPool[18],UsartdataPool[19]);
                }
    
                // ESP_LOGI("Datapool","size:%d\r\n",event.size);
                SendMessage(UsartdataPool, received, SysMoniterServerHandle);
                memcpy(usartDataBuffer,UsartdataPool,20);
                switch(usartDataBuffer[0])
                 {
                    case 0x63:
                        if(usartDataBuffer[1]==0x70)
                        {
                            usartcrc=CRC16_Calc(usartDataBuffer,11);
                            tempCrcLow=(uint8_t)(usartcrc&0xff);
                            tempCrcHigh=(uint8_t)((usartcrc>>8)&0xff);
                           // ESP_LOGI("Datapool1","rec:%x,%x\r\n",tempCrcLow,tempCrcHigh);
                            if((usartDataBuffer[11]==tempCrcLow)&&(usartDataBuffer[12]==tempCrcHigh))
                            {
                                //	type1: beginning:0x63; ending:0x6c;
                                //	pressdate：beginning: 0x70 抬升高位，抬升低位，
                                //  压制高位(pressSwitch1)，压制低位(pressSwitch2),压制头在位(S4)，圈数float
                                sensorState[riseHighPos]    =usartDataBuffer[2];
                                sensorState[riseLowPos]     =usartDataBuffer[3];
                                sensorState[pressHighPos]   =usartDataBuffer[4];
                                sensorState[pressLowPos]    =usartDataBuffer[5];
                                sensorState[pressHeadAvail] =usartDataBuffer[6];
                                memcpy(&g_DataPool.turns, &usartDataBuffer[7], sizeof(float));
                                ESP_LOGI("sensorState and turn","sensorState and turns");
                            }
                        }break;
                    case 0x61:
                       if(usartDataBuffer[1]==0x73)
                        {
                            usartcrc=CRC16_Calc(usartDataBuffer,13);
                            tempCrcLow=(uint8_t)(usartcrc&0xff);
                            tempCrcHigh=(uint8_t)((usartcrc>>8)&0xff);
                            //ESP_LOGI("Datapool2","rec:%x,%x\r\n",tempCrcLow,tempCrcHigh);
                            if((usartDataBuffer[13]==tempCrcLow)&&(usartDataBuffer[14]==tempCrcHigh))
                            {
                                //	type2 ：bejinning:0x61;ending:0x6c;
                                //	station:bejinning: 0x73 水箱在位，高位，低位;
                                //黄水盒在位;开关阀开位、关位;点卤盒在位;溢出;盖子在位;滤网在位;成型盒在位
                                sensorState[waterBoxAvail]      =usartDataBuffer[2];
                                sensorState[waterBoxHighPos]    =usartDataBuffer[3];
                                sensorState[waterBoxLowPos]     =usartDataBuffer[4];
                                sensorState[wasteBoxAvail]      =usartDataBuffer[5];
                                sensorState[valveOnPos]         =usartDataBuffer[6];
                                sensorState[valveOffPos]        =usartDataBuffer[7];
                                sensorState[brineBoxAvail]      =usartDataBuffer[8];
                                sensorState[overFlow]           =usartDataBuffer[9];
                                sensorState[lidAvail]           =usartDataBuffer[10];
                                sensorState[filerAvail]         =usartDataBuffer[11];
                                sensorState[moldedBoxAvail]     =usartDataBuffer[12];
                                ESP_LOGI("sensorState","sensorState");
                            }
                        }break;
                    case 0x76:
                        if(usartDataBuffer[1]==0x64)
                            {
                                usartcrc=CRC16_Calc(usartDataBuffer,14);
                                tempCrcLow=(uint8_t)(usartcrc&0xff);
                                tempCrcHigh=(uint8_t)((usartcrc>>8)&0xff);
                                // ESP_LOGI("Datapool3","rec:%x,%x\r\n",tempCrcLow,tempCrcHigh);
                                // ESP_LOGI("Datapool3","crcdata:%x,%x\r\n",usartDataBuffer[14],usartDataBuffer[15]);

                                if((usartDataBuffer[14]==tempCrcLow)&&(usartDataBuffer[15]==tempCrcHigh))
                                {
                                //	type3 ：bejinning:0x76 ;ending:0x6c;
                                //  value bejing:0x64 温度，电压，电流
                                    memcpy(&g_DataPool.temperature, &usartDataBuffer[2], sizeof(float));
                                    memcpy(&g_DataPool.voltage, &usartDataBuffer[6], sizeof(float));
                                    memcpy(&g_DataPool.current, &usartDataBuffer[10], sizeof(float));
                                    ESP_LOGI("value","value");
                                }
                            }break;
                    case 0x6d:
                        if(usartDataBuffer[1]==0x6f)
                            {
                                usartcrc=CRC16_Calc(usartDataBuffer,7);
                                tempCrcLow=(uint8_t)(usartcrc&0xff);
                                tempCrcHigh=(uint8_t)((usartcrc>>8)&0xff);
                                // ESP_LOGI("Datapool4","rec:%x,%x\r\n",tempCrcLow,tempCrcHigh);
                                // ESP_LOGI("Datapool4","crcdata:%x,%x\r\n",usartDataBuffer[8],usartDataBuffer[9]);

                                if((usartDataBuffer[8]==tempCrcLow)&&(usartDataBuffer[9]==tempCrcHigh))
                                {
                                //	type3 ：bejinning:0x76 0x64;ending:0x6c;
                                //	station:bejinning: 0x6f 开关阀,压制步进,清水泵,卤水泵,蠕动泵,抬升步进,破壁电机
                                    moterState[Valve]           =usartDataBuffer[2];
                                    moterState[pressMoter]      =usartDataBuffer[3];
                                    moterState[clearWaterPump]  =usartDataBuffer[4];
                                    moterState[drinePump]       =usartDataBuffer[5];
                                    moterState[hosePump]        =usartDataBuffer[6];
                                    moterState[riseMoter]       =usartDataBuffer[7];
                                    moterState[breakMoter]      =usartDataBuffer[8];
                                    // ESP_LOGI("moterState","moterState");
                                }
                            }break;
                 }
                event.size =0;
            }
        }
        if(screenData[1]!=0x00)
        {    
            uint16_t crc;
            crc=CRC16_Calc(screenData,7);
            screenData[7]=crc&0xff;
            screenData[8]=crc>>8;
            uart_write_bytes(ScreenUart, (const char*)&screenData,
                             sizeof(screenData));
            for(uint8_t i=1;i<8;i++)
            {
                screenData[i]=0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
#endif

void DataCenterTask(void *pvParameters)
{
    uint8_t stream[128];
    uint8_t chunk[64];
    size_t stream_length = 0;
    uart_event_t event;

    for (;;) {
        if (xQueueReceive(uart_queue, &event, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (event.type == UART_DATA) {
                size_t remaining = event.size;
                while (remaining > 0) {
                    size_t requested = remaining > sizeof(chunk) ?
                                       sizeof(chunk) : remaining;
                    int received = uart_read_bytes(ScreenUart, chunk, requested,
                                                   pdMS_TO_TICKS(20));
                    if (received <= 0) {
                        break;
                    }
                    remaining -= (size_t)received;

                    if (stream_length + (size_t)received > sizeof(stream)) {
                        ESP_LOGW("DataCenter", "UART stream overflow; resyncing");
                        stream_length = 0;
                    }
                    memcpy(stream + stream_length, chunk, (size_t)received);
                    stream_length += (size_t)received;
                }
            } else if (event.type == UART_FIFO_OVF ||
                       event.type == UART_BUFFER_FULL) {
                ESP_LOGW("DataCenter", "UART hardware buffer overflow");
                uart_flush_input(ScreenUart);
                xQueueReset(uart_queue);
                stream_length = 0;
            }
        }

        while (stream_length >= 2) {
            size_t frame_length = uart_frame_length(stream[0], stream[1]);
            if (frame_length == 0) {
                memmove(stream, stream + 1, --stream_length);
                continue;
            }
            if (stream_length < frame_length) {
                break;
            }
            uint8_t expected_tail =
                (stream[0] == 0x76 && frame_length == 8) ? 0x6e : 0x6c;
            if (stream[frame_length - 1] != expected_tail) {
                memmove(stream, stream + 1, --stream_length);
                continue;
            }

            process_uart_frame(stream, frame_length);
            stream_length -= frame_length;
            memmove(stream, stream + frame_length, stream_length);
        }

        if (screenData[1] != 0x00) {
            uint16_t crc = CRC16_Calc(screenData, 7);
            screenData[7] = (uint8_t)(crc & 0xff);
            screenData[8] = (uint8_t)(crc >> 8);
            uart_write_bytes(ScreenUart, (const char *)screenData,
                             sizeof(screenData));
            memset(&screenData[1], 0, 7);
        }
    }
}

#define modBus 1
#if modBus == 1

enum {
    clearWaterPump01=1,//清水泵
    brineWaterPump01,//点卤泵
    hoseWaterPump01, //蠕动泵
    valve01,         //阀门
    LED01,           //LED
    Heat101,          //加热1
    Heat202,          //加热2
    Nobush01,        //无刷电机
}modBus01FuncAddr;


enum{
    riseHighPos02=1,
    riseLowPos02,
    pressHighPos02,
    pressLowPos02,
    pressHeadAvil02,
    waterTankAvail02,
    waterHighPos02,
    waterLowPos02,
    wasteTankAvail02,
    valveOpenPos02,
    valveClosePos02,
    brineBoxPos02,
    overFlow02,
    lidAvail02,
    filterAvail02,
    modeAvail02,
}modBus02FuncAddr;




#endif

typedef enum{
    CMDNONE=0x00,

    CMDwaterPumpOn,			//清水泵开  1
    CMDwaterPumpOff,		//清水泵关  0
    CMDBRINEPUMPON,			//点卤泵开  0
    CMDBRINEPUMPOFF,		//点卤泵关  0
    CMDHOSEPUMPON,			//蠕动泵开  1
    CMDHOSEPUMPOFF,			//蠕动泵关  0
	CMDPUSHMOTORDOWN,		//压制电机下压 1
	CMDPUSHMOTORUP,			//压制电机上升 1
	CMDPUSHMOTORSTOP,		//压制电机停止 0
	CMDRISEMOTORDOWN,		//抬升电机下压 0
	CMDRISEMOTORUP,			//抬升电机上升 0
	CMDRISEMOTORSTOP,		//抬升电机停止 0
	CMDLIGHTSHOW,			//WS2812发光  4
	CMDHIGHVOLBROAD,		//高压板控制  2
	CMDVAVLEON,				//开关阀开    0
	CMDVAVLEOFF,			//开关阀关    0
	CMDLEDON,				//开灯        0
	CMDLEDOFF,				//关灯        0
    CMDGETVERSION,          //获取版本 0
    CMD_MAX
}CommandCmd_t;

void waterPumpOn(uint8_t waterPumpSpeed)
{
    printf("%s\n", __func__);
    screenData[1]=CMDwaterPumpOn;
    screenData[2]=waterPumpSpeed;
}
void waterPumpOff(void)
{
    printf("%s\n", __func__);
    screenData[1]=CMDwaterPumpOff;
}

void brinePumpOn(void)
{
    printf("%s\n", __func__);
    screenData[1]=CMDBRINEPUMPON;
}
void brinePumpOff(void)
{
    printf("%s\n", __func__);
    screenData[1]=CMDBRINEPUMPOFF;
}

void hosePumpOn(uint8_t hosePumpSpeed)
{
    printf("%s\n", __func__);
    screenData[1]=CMDHOSEPUMPON;
    screenData[2]=hosePumpSpeed;
}
void hosePumpOff(void)
{
    printf("%s\n", __func__);
    screenData[1]=CMDHOSEPUMPOFF;
}

void pressMoterDown(uint8_t pressMoterSpeed)
{
    printf("%s\n", __func__);
    screenData[1]=CMDPUSHMOTORDOWN;
    screenData[2]=pressMoterSpeed;
    printf("pressMoterSpeed:%d\n",pressMoterSpeed);
}
void pressMoterUp(uint8_t pressMoterSpeed)
{
    printf("%s\n", __func__);
    screenData[1]=CMDPUSHMOTORUP;
    screenData[2]=pressMoterSpeed;
    printf("pressMoterSpeed:%d\n",pressMoterSpeed);
}

void pressMoterStop(void)
{
    printf("%s\n", __func__);
    screenData[1]=CMDPUSHMOTORSTOP;
}

void riseMoterDown(uint16_t riseMoterSpeed)
{
    printf("%s\n", __func__);
    screenData[1]=CMDRISEMOTORDOWN;
    screenData[2]=(uint8_t)(riseMoterSpeed & 0xff);
    screenData[3]=(uint8_t)(riseMoterSpeed >> 8);
}
void riseMoterUp(uint16_t riseMoterSpeed)
{
    printf("%s\n", __func__);
    screenData[1]=CMDRISEMOTORUP;
    screenData[2]=(uint8_t)(riseMoterSpeed & 0xff);
    screenData[3]=(uint8_t)(riseMoterSpeed >> 8);
}
void riseMoterStop(void)
{
    printf("%s\n", __func__);
    screenData[1]=CMDRISEMOTORSTOP;
}

void ws2812Coler(uint8_t lednum,uint8_t rColer,uint8_t gColer,uint8_t bColer)
{
    printf("%s\n", __func__);
    screenData[1]=CMDLIGHTSHOW;
    screenData[2]=lednum;
    screenData[3]=rColer;
    screenData[4]=gColer;
    screenData[5]=bColer;
}

void setHighBroad(uint8_t highBroadValue)
{
    //
    printf("%s\n", __func__);
    screenData[1]=CMDHIGHVOLBROAD;
    screenData[2]=highBroadValue;
}

void valveOn(void)
{
    printf("%s\n", __func__);
    screenData[1]=CMDVAVLEON;
}
void valveOff(void)
{
    printf("%s\n", __func__);
    screenData[1]=CMDVAVLEOFF;
}

void ledOn(void)
{
    printf("%s\n", __func__);
    screenData[1]=CMDLEDON;
}
void ledOff(void)
{
    printf("%s\n", __func__);
    screenData[1]=CMDLEDOFF;
}
char (*getVersion(void))[4]
{
    return version;
}
