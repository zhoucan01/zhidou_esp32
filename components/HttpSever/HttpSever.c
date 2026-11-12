/*
 * 文件: http_server.c
 *
 * 功能说明:
 *   1. 启动 ESP-IDF HTTP Server。
 *   2. 向手机 / 浏览器返回前端控制网页。
 *   3. 接收网页按钮、下拉框、滑动条通过 fetch() 发送来的数据。
 *   4. 本文件不直接控制 GPIO / PWM / 电机。
 *   5. 所有网页操作都会被转换成协议数据并保存到全局数组 arr[]。
 *   6. 每次收到网页操作后，会打印：
 *        - 具体按下了哪个按钮 / 操作了哪个滑动条
 *        - 当前 arr[] 中保存的协议数据
 *
 * 注意:
 *   - arr[] 是全局数组，其他文件可以通过 http_server.h 里的 extern uint8_t arr[16] 访问。
 *   - 本文件只负责“网页数据接收 + 协议转换 + 保存到 arr[] + 打印调试信息”。
 *   - 后续 MCU / 其他任务只需要读取 arr[]，再根据协议执行对应动作。
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_http_server.h"
#include "HttpSever.h"

/* ESP 日志 TAG */
#define TAG "WEB_DATA"

/*
 * 全局协议数组。
 *
 * 每次网页操作都会刷新 arr[]。
 *
 * 例如：
 *   功能界面左侧按钮：
 *      arr[0] = 功能编号
 *      arr[1] = 0x01
 *
 *   功能界面右侧按钮：
 *      arr[0] = 功能编号
 *      arr[1] = 0x01
 *
 *   外设开关：
 *      arr[0] = CLASS_SWITCH
 *      arr[1] = 开关编号
 *      arr[2] = 开关状态
 *
 *   直流控制：
 *      arr[0] = 直流编号
 *      arr[1] = 方向
 *      arr[2] = 数值
 */
uint8_t arr[16] = {0};

/* ========================= 协议分类定义 ========================= */

/* 外设界面左侧 6 个开关这一类 */
#define CLASS_SWITCH      0x10

/* 电机定圈这一类 */
#define CLASS_MOTOR_SET   0x30

/* 无刷、加热、加热1 滑动条这一类 */
#define CLASS_SLIDER      0x50

/* 压制按键类别 */
#define MOTOR_PRESS       0x20

/* 抬升按键类别 */
#define MOTOR_LIFT        0x21

/* 直流方向定义 */
#define DC_STOP           0x00
#define DC_FORWARD        0x01
#define DC_REVERSE        0x02

/*
 * 打印具体操作信息和 arr[] 数据。
 *
 * 参数:
 *   info : 按键或滑动条的具体信息，例如：
 *          "功能界面 -> 文档"
 *          "外设 -> 1号进水泵 -> 开"
 *          "直流1 -> 正转 -> 数值:89"
 *
 *   len  : 本次有效数据长度，仅用于打印。
 *          不作为全局变量保存。
 */
static void print_arr_info(const char *info, uint8_t len)
{
    printf("\n============================\n");
    printf("Button: %s\n", info);
    printf("ARR Data: ");

    for (int i = 0; i < len; i++) {
        printf("%02X ", arr[i]);
    }

    printf("\n============================\n\n");
}

/*
 * 前端网页 HTML。
 *
 * 浏览器访问 ESP32 的 "/" 路径时，ESP32 会返回这个页面。
 *
 * 页面中的按钮 / 滑动条会通过 fetch() 访问以下接口：
 *
 *   /api/function   功能界面按钮
 *   /api/device     外设六个开关
 *   /api/action     压制 / 抬升按钮
 *   /api/motor      电机定圈
 *   /api/dc         直流1 / 直流2
 *   /api/slider     无刷 / 加热 / 加热1
 *   /api/bluetooth  连接蓝牙
 */
static const char html_page[] =
"<!DOCTYPE html>"
"<html>"
"<head>"
"<meta charset=\"UTF-8\">"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">"
"<title>控制器</title>"

"<style>"
"*{margin:0;padding:0;box-sizing:border-box;-webkit-tap-highlight-color:transparent;}"
"body{width:100vw;height:100vh;overflow:hidden;background:#eef2f5;font-family:Arial,Helvetica,sans-serif;touch-action:none;}"
".container{width:100%;height:100%;display:flex;justify-content:center;align-items:center;}"
".card{width:96%;height:94%;background:#fff;border-radius:18px;padding:12px;display:flex;flex-direction:column;}"

".status-bar{height:42px;display:flex;justify-content:space-between;align-items:center;background:#f3f6fa;border-radius:10px;padding:0 10px;margin-bottom:10px;font-size:14px;overflow:hidden;flex-shrink:0;}"
".status-left{display:flex;align-items:center;gap:5px;font-weight:bold;white-space:nowrap;font-size:14px;}"
".status-right{display:flex;gap:8px;white-space:nowrap;font-size:14px;}"
".status-value{color:#1b74ff;font-weight:bold;font-size:14px;}"

".page{display:none;flex:1;width:100%;min-height:0;}"
".home-wrap{height:100%;display:flex;flex-direction:column;justify-content:center;align-items:center;}"
"h1{text-align:center;font-size:38px;margin-bottom:35px;color:#222;}"

".btn-area{width:100%;display:flex;gap:12px;}"
".menu-btn{flex:1;height:110px;border:none;border-radius:16px;font-size:22px;font-weight:bold;color:#fff;}"
".blue{background:#007bff;}.green{background:#0ca678;}.purple{background:#7048e8;}"

".func-layout{display:flex;height:100%;gap:10px;}"
".func-left{width:110px;display:flex;flex-direction:column;gap:8px;}"
".func-center{flex:1;overflow:auto;}"
".func-right{width:130px;display:flex;flex-direction:column;gap:8px;}"
".func-btn{height:42px;border:none;border-radius:10px;background:#edf1f5;font-size:15px;font-weight:bold;}"

".data-table{width:100%;border-collapse:collapse;font-size:13px;}"
".data-table th{background:#1b74ff;color:#fff;padding:5px;}"
".data-table td{border:1px solid #ccc;padding:5px;text-align:center;}"

".peripheral-top{height:40px;display:flex;align-items:center;gap:8px;margin-bottom:8px;}"
".peripheral-home-btn{width:60px;height:30px;border:none;border-radius:8px;background:#666;color:#fff;font-size:13px;font-weight:bold;}"
".peripheral-status{flex:1;height:40px;display:flex;align-items:center;justify-content:space-around;background:#f3f6fa;border-radius:10px;font-size:13px;font-weight:bold;}"

".peripheral-main{height:calc(100% - 48px);display:flex;gap:10px;}"
".peripheral-left{width:330px;background:#f8f9fb;border-radius:14px;padding:8px;display:flex;flex-direction:column;gap:6px;}"
".peripheral-right{flex:1;background:#f8f9fb;border-radius:14px;padding:8px;overflow:hidden;}"

".device-row{height:34px;display:flex;align-items:center;gap:6px;}"
".device-name{flex:1;height:100%;display:flex;align-items:center;padding-left:8px;background:#e9eef5;border-radius:8px;font-size:13px;font-weight:bold;}"
".device-btn{width:42px;height:100%;border:none;border-radius:8px;color:#fff;font-size:12px;font-weight:bold;}"
".on-btn{background:#13b26b;}.off-btn{background:#e04b4b;}"

".dc-row{height:38px;display:flex;align-items:center;gap:8px;}"
".dc-name{width:48px;font-size:13px;font-weight:bold;}"
".dc-track{position:relative;flex:1;height:10px;border-radius:10px;background:linear-gradient(to right,#e53935 0%,#e53935 50%,#167a35 50%,#167a35 100%);touch-action:none;}"
".dc-thumb{position:absolute;left:50%;top:50%;width:24px;height:24px;border-radius:50%;background:#111;transform:translate(-50%,-50%);}"
".dc-value{width:28px;text-align:center;font-size:13px;font-weight:bold;}"

".right-control-row{height:32px;display:flex;align-items:center;gap:6px;margin-bottom:4px;}"
".right-label{width:42px;font-size:12px;font-weight:bold;}"
".small-btn{width:38px;height:26px;border:none;border-radius:6px;background:#e6ebf2;font-size:12px;font-weight:bold;}"

".select-row{height:32px;display:flex;align-items:center;gap:5px;margin-top:4px;font-size:11px;font-weight:bold;}"
".mini-select{width:58px;height:24px;font-size:11px;}"
".send-btn{width:42px;height:24px;border:none;border-radius:6px;background:#7d8da8;color:#fff;font-size:11px;font-weight:bold;}"

".right-slider-row{height:30px;display:flex;align-items:center;gap:5px;margin-top:4px;}"
".right-slider-name{width:38px;font-size:11px;font-weight:bold;}"
".right-track{position:relative;flex:1;height:6px;border-radius:6px;background:#d93b3b;touch-action:none;}"
".right-thumb{position:absolute;left:0%;top:50%;width:18px;height:18px;border-radius:50%;background:#111;transform:translate(-50%,-50%);}"
".right-value{width:20px;text-align:center;font-size:11px;font-weight:bold;}"
"</style>"
"</head>"

"<body>"
"<div class=\"container\"><div class=\"card\">"

"<div class=\"status-bar\" id=\"statusBar\">"
"<div class=\"status-left\">🔷 <span id=\"btStatusBar\">NotConnect</span></div>"
"<div class=\"status-right\">"
"<div>Power:<span class=\"status-value\">YES</span></div>"
"<div>Current:<span class=\"status-value\">1.25A</span></div>"
"<div>Battery:<span class=\"status-value\">85%</span></div>"
"</div></div>"

"<div id=\"homePage\" class=\"page\">"
"<div class=\"home-wrap\">"
"<h1>控制器</h1>"
"<div class=\"btn-area\">"
"<button class=\"menu-btn blue\" onclick=\"switchPage('functionPage')\">功能</button>"
"<button class=\"menu-btn green\" onclick=\"switchPage('peripheralPage')\">外设</button>"
"<button class=\"menu-btn purple\" onclick=\"connectBluetooth()\">连接蓝牙</button>"
"</div></div></div>"

"<div id=\"functionPage\" class=\"page\">"
"<div class=\"func-layout\">"

"<div class=\"func-left\">"
"<button class=\"func-btn\" onclick=\"sendFunction(1)\">文档</button>"
"<button class=\"func-btn\" onclick=\"sendFunction(2)\">制冷</button>"
"<button class=\"func-btn\" onclick=\"sendFunction(3)\">制热</button>"
"<button class=\"func-btn\" onclick=\"sendFunction(4)\">饮水机</button>"
"<button class=\"func-btn\" onclick=\"sendFunction(5)\">洗衣机</button>"
"<button class=\"func-btn\" onclick=\"sendFunction(6)\">吹风机</button>"
"</div>"

"<div class=\"func-center\">"
"<table class=\"data-table\">"
"<thead><tr><th>功能</th><th>目标</th><th>当前</th><th>Tar</th><th>Cur</th></tr></thead>"
"<tbody>"
"<tr><td>NTask1</td><td>1</td><td>0.5</td><td>1</td><td>1</td></tr>"
"<tr><td>NTask2</td><td>2</td><td>1.0</td><td>2</td><td>2</td></tr>"
"<tr><td>NTask3</td><td>3</td><td>1.5</td><td>3</td><td>3</td></tr>"
"<tr><td>NTask4</td><td>4</td><td>2.0</td><td>4</td><td>4</td></tr>"
"<tr><td>NTask5</td><td>5</td><td>2.5</td><td>5</td><td>5</td></tr>"
"<tr><td>NTask6</td><td>6</td><td>3.0</td><td>6</td><td>6</td></tr>"
"</tbody>"
"</table>"
"</div>"

"<div class=\"func-right\">"
"<button class=\"func-btn\" onclick=\"sendFunction(7)\">暂停</button>"
"<button class=\"func-btn\" onclick=\"sendFunction(8)\">恢复</button>"
"<button class=\"func-btn\" onclick=\"sendFunction(9)\">增大</button>"
"<button class=\"func-btn\" onclick=\"sendFunction(10)\">结束</button>"
"<button class=\"func-btn\" onclick=\"switchPage('homePage')\">主页</button>"
"</div>"

"</div></div>"

"<div id=\"peripheralPage\" class=\"page\">"
"<div class=\"peripheral-top\">"
"<button class=\"peripheral-home-btn\" onclick=\"switchPage('homePage')\">主页</button>"
"<div class=\"peripheral-status\">"
"<div>圈数:<span class=\"status-value\">0</span></div>"
"<div>Current:<span class=\"status-value\">1.25A</span></div>"
"<div>Battery:<span class=\"status-value\">85%</span></div>"
"</div></div>"

"<div class=\"peripheral-main\">"

"<div class=\"peripheral-left\">"
"<div class=\"device-row\"><div class=\"device-name\">1号进水泵</div><button class=\"device-btn on-btn\" onclick=\"sendDevice(1,1)\">开</button><button class=\"device-btn off-btn\" onclick=\"sendDevice(1,0)\">关</button></div>"
"<div class=\"device-row\"><div class=\"device-name\">2号进水泵</div><button class=\"device-btn on-btn\" onclick=\"sendDevice(2,1)\">开</button><button class=\"device-btn off-btn\" onclick=\"sendDevice(2,0)\">关</button></div>"
"<div class=\"device-row\"><div class=\"device-name\">抽水泵</div><button class=\"device-btn on-btn\" onclick=\"sendDevice(3,1)\">开</button><button class=\"device-btn off-btn\" onclick=\"sendDevice(3,0)\">关</button></div>"
"<div class=\"device-row\"><div class=\"device-name\">气泵</div><button class=\"device-btn on-btn\" onclick=\"sendDevice(4,1)\">开</button><button class=\"device-btn off-btn\" onclick=\"sendDevice(4,0)\">关</button></div>"
"<div class=\"device-row\"><div class=\"device-name\">3号抽水泵</div><button class=\"device-btn on-btn\" onclick=\"sendDevice(5,1)\">开</button><button class=\"device-btn off-btn\" onclick=\"sendDevice(5,0)\">关</button></div>"
"<div class=\"device-row\"><div class=\"device-name\">转阀开关</div><button class=\"device-btn on-btn\" onclick=\"sendDevice(6,1)\">开</button><button class=\"device-btn off-btn\" onclick=\"sendDevice(6,0)\">关</button></div>"

"<div class=\"dc-row\"><div class=\"dc-name\">直流1</div><div id=\"dc1Track\" class=\"dc-track\"><div id=\"dc1Thumb\" class=\"dc-thumb\"></div></div><div id=\"dc1Value\" class=\"dc-value\">0</div></div>"
"<div class=\"dc-row\"><div class=\"dc-name\">直流2</div><div id=\"dc2Track\" class=\"dc-track\"><div id=\"dc2Thumb\" class=\"dc-thumb\"></div></div><div id=\"dc2Value\" class=\"dc-value\">0</div></div>"
"</div>"

"<div class=\"peripheral-right\">"
"<div class=\"right-control-row\"><div class=\"right-label\">压制</div><button class=\"small-btn\" onclick=\"sendAction('press_down')\">下</button><button class=\"small-btn\" onclick=\"sendAction('press_stop')\">停</button><button class=\"small-btn\" onclick=\"sendAction('press_up')\">上</button></div>"
"<div class=\"right-control-row\"><div class=\"right-label\">抬升</div><button class=\"small-btn\" onclick=\"sendAction('lift_forward')\">正</button><button class=\"small-btn\" onclick=\"sendAction('lift_stop')\">停</button><button class=\"small-btn\" onclick=\"sendAction('lift_reverse')\">反</button></div>"

"<div class=\"select-row\">"
"<span>电机定圈:</span>"
"<select id=\"motorMode\" class=\"mini-select\"><option value=\"press\">压制</option><option value=\"lift\">抬升</option></select>"
"<select id=\"motorCircle\" class=\"mini-select\">"
"<option value=\"\" selected disabled>圈数</option>"
"<option>-20</option><option>-19</option><option>-18</option><option>-17</option><option>-16</option>"
"<option>-15</option><option>-14</option><option>-13</option><option>-12</option><option>-11</option>"
"<option>-10</option><option>-9</option><option>-8</option><option>-7</option><option>-6</option>"
"<option>-5</option><option>-4</option><option>-3</option><option>-2</option><option>-1</option>"
"<option>1</option><option>2</option><option>3</option><option>4</option><option>5</option>"
"<option>6</option><option>7</option><option>8</option><option>9</option><option>10</option>"
"<option>11</option><option>12</option><option>13</option><option>14</option><option>15</option>"
"<option>16</option><option>17</option><option>18</option><option>19</option><option>20</option>"
"</select>"
"<select id=\"motorSpeed\" class=\"mini-select\">"
"<option value=\"\" selected disabled>速度</option>"
"<option>10</option><option>100</option><option>200</option><option>500</option>"
"<option>1000</option><option>1500</option><option>2000</option><option>3000</option>"
"</select>"
"<button class=\"send-btn\" onclick=\"sendMotor()\">发送</button>"
"</div>"

"<div class=\"right-slider-row\"><div class=\"right-slider-name\">无刷</div><div id=\"brushlessTrack\" class=\"right-track\"><div id=\"brushlessThumb\" class=\"right-thumb\"></div></div><div id=\"brushlessValue\" class=\"right-value\">0</div></div>"
"<div class=\"right-slider-row\"><div class=\"right-slider-name\">加热</div><div id=\"heatTrack\" class=\"right-track\"><div id=\"heatThumb\" class=\"right-thumb\"></div></div><div id=\"heatValue\" class=\"right-value\">0</div></div>"
"<div class=\"right-slider-row\"><div class=\"right-slider-name\">加热1</div><div id=\"heat1Track\" class=\"right-track\"><div id=\"heat1Thumb\" class=\"right-thumb\"></div></div><div id=\"heat1Value\" class=\"right-value\">0</div></div>"
"</div>"

"</div></div></div></div></div>"

"<script>"
"function apiGet(url){fetch(url).catch(function(e){});}"

"function switchPage(id){"
"var pages=document.getElementsByClassName('page');"
"for(var i=0;i<pages.length;i++){pages[i].style.display='none';}"
"document.getElementById(id).style.display='block';"
"document.getElementById('statusBar').style.display=(id==='peripheralPage')?'none':'flex';"
"}"

"function connectBluetooth(){"
"document.getElementById('btStatusBar').innerText='Connecting';"
"apiGet('/api/bluetooth?state=1');"
"setTimeout(function(){document.getElementById('btStatusBar').innerText='Connected';},1000);"
"}"

"function sendFunction(id){apiGet('/api/function?id='+id);}"
"function sendDevice(dev,state){apiGet('/api/device?dev='+dev+'&state='+state);}"
"function sendAction(cmd){apiGet('/api/action?cmd='+cmd);}"
"function sendDC(id,value){apiGet('/api/dc?id='+id+'&value='+value);}"
"function sendSlider(name,value){apiGet('/api/slider?name='+name+'&value='+value);}"

"function sendMotor(){"
"var mode=document.getElementById('motorMode').value;"
"var circle=document.getElementById('motorCircle').value;"
"var speed=document.getElementById('motorSpeed').value;"
"if(circle===''||speed==='')return;"
"apiGet('/api/motor?mode='+mode+'&circle='+circle+'&speed='+speed);"
"}"

"var dragType='',dragId='',dragX=0,dragRAF=0,lastSend=0,lastValue=-1;"

"function getPointX(e){if(e.touches&&e.touches.length>0)return e.touches[0].clientX;return e.clientX;}"

"function setSlider(trackId,thumbId,valueId,x){"
"var track=document.getElementById(trackId),thumb=document.getElementById(thumbId),valueEl=document.getElementById(valueId);"
"if(!track||!thumb||!valueEl)return 0;"
"var rect=track.getBoundingClientRect();"
"var percent=(x-rect.left)/rect.width*100;"
"if(percent<0)percent=0;if(percent>100)percent=100;"
"thumb.style.left=percent+'%';"
"var v=Math.round(percent);"
"if(trackId==='heatTrack'||trackId==='heat1Track'){v=Math.round(percent*15/100);}"
"if(trackId==='dc1Track'||trackId==='dc2Track'){if(v===50)v=0;}"
"valueEl.innerText=v;"
"return v;"
"}"

"function refreshDrag(){"
"dragRAF=0;"
"var v=0;"
"if(dragType==='dc'){v=setSlider('dc'+dragId+'Track','dc'+dragId+'Thumb','dc'+dragId+'Value',dragX);}"
"else if(dragType==='right'){v=setSlider(dragId+'Track',dragId+'Thumb',dragId+'Value',dragX);}"
"var now=Date.now();"
"if(v!==lastValue&&now-lastSend>120){"
"lastSend=now;lastValue=v;"
"if(dragType==='dc')sendDC(dragId,v);"
"if(dragType==='right')sendSlider(dragId,v);"
"}"
"}"

"function requestDrag(x){dragX=x;if(!dragRAF){dragRAF=requestAnimationFrame(refreshDrag);}}"
"function startDrag(type,id,e){dragType=type;dragId=id;lastValue=-1;if(e.preventDefault)e.preventDefault();requestDrag(getPointX(e));}"
"function moveDrag(e){if(dragType==='')return;if(e.preventDefault)e.preventDefault();requestDrag(getPointX(e));}"
"function endDrag(){dragType='';dragId='';lastValue=-1;}"

"function initSliders(){"
"var dcIds=['1','2'];"
"for(var i=0;i<dcIds.length;i++){(function(id){"
"var t=document.getElementById('dc'+id+'Track');if(!t)return;"
"t.addEventListener('pointerdown',function(e){startDrag('dc',id,e);});"
"t.addEventListener('touchstart',function(e){startDrag('dc',id,e);},{passive:false});"
"})(dcIds[i]);}"

"var rightIds=['brushless','heat','heat1'];"
"for(var j=0;j<rightIds.length;j++){(function(id){"
"var t=document.getElementById(id+'Track');if(!t)return;"
"t.addEventListener('pointerdown',function(e){startDrag('right',id,e);});"
"t.addEventListener('touchstart',function(e){startDrag('right',id,e);},{passive:false});"
"})(rightIds[j]);}"

"document.addEventListener('pointermove',moveDrag);"
"document.addEventListener('pointerup',endDrag);"
"document.addEventListener('pointercancel',endDrag);"
"document.addEventListener('touchmove',moveDrag,{passive:false});"
"document.addEventListener('touchend',endDrag);"
"document.addEventListener('touchcancel',endDrag);"
"}"

"window.onload=function(){switchPage('homePage');initSliders();};"
"</script>"

"</body>"
"</html>";

/*
 * 首页处理函数。
 *
 * 浏览器访问:
 *   http://ESP32_IP/
 *
 * 返回:
 *   html_page 网页内容。
 */
static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/*
 * 功能界面按钮处理函数。
 *
 * 前端请求格式:
 *   /api/function?id=1
 *
 * 数据格式:
 *   arr[0] = 功能编号
 *   arr[1] = 0x01
 *
 * 功能编号说明:
 *   1  文档
 *   2  制冷
 *   3  制热
 *   4  饮水机
 *   5  洗衣机
 *   6  吹风机
 *
 *   7  暂停
 *   8  恢复
 *   9  增大
 *   10 结束
 *
 * 说明:
 *   左侧功能按钮和右侧功能按钮使用同一个数据格式。
 *   这样 MCU 端只需要根据 arr[0] 判断具体功能即可。
 */
static esp_err_t function_handler(httpd_req_t *req)
{
    char query[64] = {0};
    char id_str[16] = {0};
    const char *func_name = "未知功能";

    httpd_req_get_url_query_str(req, query, sizeof(query));
    httpd_query_key_value(query, "id", id_str, sizeof(id_str));

    arr[0] = atoi(id_str);
    arr[1] = 0x01;

    switch (arr[0]) {
        case 1:  func_name = "文档"; break;
        case 2:  func_name = "制冷"; break;
        case 3:  func_name = "制热"; break;
        case 4:  func_name = "饮水机"; break;
        case 5:  func_name = "洗衣机"; break;
        case 6:  func_name = "吹风机"; break;

        case 7:  func_name = "暂停"; break;
        case 8:  func_name = "恢复"; break;
        case 9:  func_name = "增大"; break;
        case 10: func_name = "结束"; break;

        default: func_name = "未知功能"; break;
    }

    char info[64];
    snprintf(info, sizeof(info), "功能界面 -> %s", func_name);
    print_arr_info(info, 2);

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/*
 * 外设界面左侧 6 个开关处理函数。
 *
 * 前端请求格式:
 *   /api/device?dev=1&state=1
 *
 * 数据格式:
 *   arr[0] = CLASS_SWITCH
 *   arr[1] = 开关编号
 *   arr[2] = 状态
 *
 * 开关编号:
 *   1  1号进水泵
 *   2  2号进水泵
 *   3  抽水泵
 *   4  气泵
 *   5  3号抽水泵
 *   6  转阀开关
 *
 * 状态:
 *   0  关
 *   1  开
 */
static esp_err_t device_handler(httpd_req_t *req)
{
    char query[128] = {0};
    char dev_str[16] = {0};
    char state_str[16] = {0};
    const char *dev_name = "未知开关";

    httpd_req_get_url_query_str(req, query, sizeof(query));
    httpd_query_key_value(query, "dev", dev_str, sizeof(dev_str));
    httpd_query_key_value(query, "state", state_str, sizeof(state_str));

    arr[0] = CLASS_SWITCH;
    arr[1] = atoi(dev_str);
    arr[2] = atoi(state_str);

    switch (arr[1]) {
        case 1: dev_name = "1号进水泵"; break;
        case 2: dev_name = "2号进水泵"; break;
        case 3: dev_name = "抽水泵"; break;
        case 4: dev_name = "气泵"; break;
        case 5: dev_name = "3号抽水泵"; break;
        case 6: dev_name = "转阀开关"; break;
        default: dev_name = "未知开关"; break;
    }

    char info[80];
    snprintf(info, sizeof(info), "外设 -> %s -> %s", dev_name, arr[2] ? "开" : "关");
    print_arr_info(info, 3);

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/*
 * 压制 / 抬升按键处理函数。
 *
 * 前端请求格式:
 *   /api/action?cmd=press_down
 *
 * 数据格式:
 *   压制:
 *      arr[0] = MOTOR_PRESS
 *      arr[1] = 0x01 下
 *      arr[1] = 0x02 停
 *      arr[1] = 0x03 上
 *
 *   抬升:
 *      arr[0] = MOTOR_LIFT
 *      arr[1] = 0x01 正
 *      arr[1] = 0x02 停
 *      arr[1] = 0x03 反
 */
static esp_err_t action_handler(httpd_req_t *req)
{
    char query[128] = {0};
    char cmd[32] = {0};
    const char *action_info = "未知动作";

    httpd_req_get_url_query_str(req, query, sizeof(query));
    httpd_query_key_value(query, "cmd", cmd, sizeof(cmd));

    if (strcmp(cmd, "press_down") == 0) {
        arr[0] = MOTOR_PRESS;
        arr[1] = 0x01;
        action_info = "压制 -> 下";
    } else if (strcmp(cmd, "press_stop") == 0) {
        arr[0] = MOTOR_PRESS;
        arr[1] = 0x02;
        action_info = "压制 -> 停";
    } else if (strcmp(cmd, "press_up") == 0) {
        arr[0] = MOTOR_PRESS;
        arr[1] = 0x03;
        action_info = "压制 -> 上";
    } else if (strcmp(cmd, "lift_forward") == 0) {
        arr[0] = MOTOR_LIFT;
        arr[1] = 0x01;
        action_info = "抬升 -> 正";
    } else if (strcmp(cmd, "lift_stop") == 0) {
        arr[0] = MOTOR_LIFT;
        arr[1] = 0x02;
        action_info = "抬升 -> 停";
    } else if (strcmp(cmd, "lift_reverse") == 0) {
        arr[0] = MOTOR_LIFT;
        arr[1] = 0x03;
        action_info = "抬升 -> 反";
    }

    print_arr_info(action_info, 2);

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/*
 * 电机定圈速度映射函数。
 *
 * 因为速度值 1000 / 3000 超过 1 个字节，
 * 所以 arr[3] 不直接保存速度值，而是保存速度编号。
 *
 * 映射关系:
 *   10   -> 1
 *   100  -> 2
 *   200  -> 3
 *   500  -> 4
 *   1000 -> 5
 *   1500 -> 6
 *   2000 -> 7
 *   3000 -> 8
 */
static uint8_t speed_to_id(int speed)
{
    switch (speed) {
        case 10: return 1;
        case 100: return 2;
        case 200: return 3;
        case 500: return 4;
        case 1000: return 5;
        case 1500: return 6;
        case 2000: return 7;
        case 3000: return 8;
        default: return 0;
    }
}

/*
 * 电机定圈处理函数。
 *
 * 前端请求格式:
 *   /api/motor?mode=press&circle=-5&speed=1000
 *
 * 数据格式:
 *   arr[0] = CLASS_MOTOR_SET
 *   arr[1] = 1 压制，2 抬升
 *   arr[2] = 圈数
 *            -20 ~ -1 表示反转
 *             1  ~ 20 表示正转
 *            负数按 uint8_t 补码保存
 *   arr[3] = 速度编号
 */
static esp_err_t motor_handler(httpd_req_t *req)
{
    char query[128] = {0};
    char mode[16] = {0};
    char circle_str[16] = {0};
    char speed_str[16] = {0};

    httpd_req_get_url_query_str(req, query, sizeof(query));
    httpd_query_key_value(query, "mode", mode, sizeof(mode));
    httpd_query_key_value(query, "circle", circle_str, sizeof(circle_str));
    httpd_query_key_value(query, "speed", speed_str, sizeof(speed_str));

    int8_t circle = atoi(circle_str);
    int speed = atoi(speed_str);

    arr[0] = CLASS_MOTOR_SET;
    arr[1] = strcmp(mode, "press") == 0 ? 1 : 2;
    arr[2] = (uint8_t)circle;
    arr[3] = speed_to_id(speed);

    char info[96];
    snprintf(info, sizeof(info),
             "电机定圈 -> %s -> 圈数:%d -> 速度ID:%d",
             arr[1] == 1 ? "压制" : "抬升",
             (int8_t)arr[2],
             arr[3]);

    print_arr_info(info, 4);

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/*
 * 直流1 / 直流2 滑动控制处理函数。
 *
 * 前端请求格式:
 *   /api/dc?id=1&value=89
 *   /api/dc?id=2&value=20
 *
 * 数据格式:
 *   arr[0] = 直流编号
 *            1 直流1
 *            2 直流2
 *
 *   arr[1] = 方向
 *            0 停止
 *            1 正转
 *            2 反转
 *
 *   arr[2] = 前端滑动条原始数值
 *            0 ~ 100
 *
 * 判断逻辑:
 *   value 45 ~ 55 认为停止
 *   value > 55     正转
 *   value < 45     反转
 */
static esp_err_t dc_handler(httpd_req_t *req)
{
    char query[128] = {0};
    char id_str[16] = {0};
    char value_str[16] = {0};

    httpd_req_get_url_query_str(req, query, sizeof(query));
    httpd_query_key_value(query, "id", id_str, sizeof(id_str));
    httpd_query_key_value(query, "value", value_str, sizeof(value_str));

    uint8_t dc_id = atoi(id_str);
    uint8_t value = atoi(value_str);

    arr[0] = dc_id;

    if (value >= 45 && value <= 55) {
        arr[1] = DC_STOP;
    } else if (value > 55) {
        arr[1] = DC_FORWARD;
    } else {
        arr[1] = DC_REVERSE;
    }

    arr[2] = value;

    char info[96];

    if (arr[1] == DC_STOP) {
        snprintf(info, sizeof(info), "直流%d -> 停止 -> 数值:%d", arr[0], arr[2]);
    } else if (arr[1] == DC_FORWARD) {
        snprintf(info, sizeof(info), "直流%d -> 正转 -> 数值:%d", arr[0], arr[2]);
    } else {
        snprintf(info, sizeof(info), "直流%d -> 反转 -> 数值:%d", arr[0], arr[2]);
    }

    print_arr_info(info, 3);

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/*
 * 无刷 / 加热 / 加热1 滑动条处理函数。
 *
 * 前端请求格式:
 *   /api/slider?name=heat&value=10
 *
 * 数据格式:
 *   arr[0] = CLASS_SLIDER
 *   arr[1] = 0x01 无刷
 *            0x02 加热
 *            0x03 加热1
 *   arr[2] = 数值
 */
static esp_err_t slider_handler(httpd_req_t *req)
{
    char query[128] = {0};
    char name[32] = {0};
    char value_str[16] = {0};
    const char *slider_name = "未知滑动";

    httpd_req_get_url_query_str(req, query, sizeof(query));
    httpd_query_key_value(query, "name", name, sizeof(name));
    httpd_query_key_value(query, "value", value_str, sizeof(value_str));

    arr[0] = CLASS_SLIDER;

    if (strcmp(name, "brushless") == 0) {
        arr[1] = 0x01;
        slider_name = "无刷";
    } else if (strcmp(name, "heat") == 0) {
        arr[1] = 0x02;
        slider_name = "加热";
    } else if (strcmp(name, "heat1") == 0) {
        arr[1] = 0x03;
        slider_name = "加热1";
    } else {
        arr[1] = 0x00;
    }

    arr[2] = atoi(value_str);

    char info[64];
    snprintf(info, sizeof(info), "%s -> 数值:%d", slider_name, arr[2]);
    print_arr_info(info, 3);

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/*
 * 连接蓝牙按钮处理函数。
 *
 * 前端请求格式:
 *   /api/bluetooth?state=1
 *
 * 数据格式:
 *   arr[0] = 0x60
 *   arr[1] = 0x01
 */
static esp_err_t bluetooth_handler(httpd_req_t *req)
{
    arr[0] = 0x60;
    arr[1] = 0x01;

    print_arr_info("首页 -> 连接蓝牙", 2);

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/*
 * 启动 HTTP Server 并注册所有 URI 接口。
 *
 * main.c 中调用 start_webserver() 后，
 * 浏览器即可访问 ESP32 网页。
 */
httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return NULL;
    }

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL
    };

    httpd_uri_t function_uri = {
        .uri = "/api/function",
        .method = HTTP_GET,
        .handler = function_handler,
        .user_ctx = NULL
    };

    httpd_uri_t device_uri = {
        .uri = "/api/device",
        .method = HTTP_GET,
        .handler = device_handler,
        .user_ctx = NULL
    };

    httpd_uri_t action_uri = {
        .uri = "/api/action",
        .method = HTTP_GET,
        .handler = action_handler,
        .user_ctx = NULL
    };

    httpd_uri_t motor_uri = {
        .uri = "/api/motor",
        .method = HTTP_GET,
        .handler = motor_handler,
        .user_ctx = NULL
    };

    httpd_uri_t dc_uri = {
        .uri = "/api/dc",
        .method = HTTP_GET,
        .handler = dc_handler,
        .user_ctx = NULL
    };

    httpd_uri_t slider_uri = {
        .uri = "/api/slider",
        .method = HTTP_GET,
        .handler = slider_handler,
        .user_ctx = NULL
    };

    httpd_uri_t bluetooth_uri = {
        .uri = "/api/bluetooth",
        .method = HTTP_GET,
        .handler = bluetooth_handler,
        .user_ctx = NULL
    };

    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &function_uri);
    httpd_register_uri_handler(server, &device_uri);
    httpd_register_uri_handler(server, &action_uri);
    httpd_register_uri_handler(server, &motor_uri);
    httpd_register_uri_handler(server, &dc_uri);
    httpd_register_uri_handler(server, &slider_uri);
    httpd_register_uri_handler(server, &bluetooth_uri);

    return server;
}