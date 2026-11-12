# 链接内存分解

输入：`/mnt/d/workspace/Code/Keil/NULLVE_V2/ESP32S3/agent-tp/tools/memory/../../../build/EVT_ESP32-S3.map`。只统计实际输出段，不含丢弃段。单位为字节。

归属按地址去重；跨组件重叠的合并常量单列为 shared/merged，未被输入条目覆盖的地址列为 padding/unattributed。对象表为输入条目声明大小，可能包含重叠，不能直接相加。

## .iram0.vectors

起始 `0x40374000`，大小 1027 B（1.003 KiB）。

| 组件 | B | KiB |
| --- | ---: | ---: |
| padding/unattributed | 600 | 0.586 |
| libxtensa.a | 424 | 0.414 |
| C:/Espressif/tools/xtensa-esp-elf/esp-13.2.0_20240530/xtensa-esp-elf/bin/../lib/gcc/xtensa-esp-elf/13.2.0/esp32s3/no-rtti/crti.o | 3 | 0.003 |

最大输入条目：

| 条目 | 声明大小 B | 来源 |
| --- | ---: | --- |
| `.WindowVectors.text` | 362 | `esp-idf/xtensa/libxtensa.a(xtensa_vectors.S.obj)` |
| `.DoubleExceptionVector.text` | 14 | `esp-idf/xtensa/libxtensa.a(xtensa_vectors.S.obj)` |
| `.Level2InterruptVector.text` | 6 | `esp-idf/xtensa/libxtensa.a(xtensa_vectors.S.obj)` |
| `.Level3InterruptVector.text` | 6 | `esp-idf/xtensa/libxtensa.a(xtensa_vectors.S.obj)` |
| `.Level4InterruptVector.text` | 6 | `esp-idf/xtensa/libxtensa.a(xtensa_vectors.S.obj)` |
| `.Level5InterruptVector.text` | 6 | `esp-idf/xtensa/libxtensa.a(xtensa_vectors.S.obj)` |
| `.DebugExceptionVector.text` | 6 | `esp-idf/xtensa/libxtensa.a(xtensa_vectors.S.obj)` |
| `.NMIExceptionVector.text` | 6 | `esp-idf/xtensa/libxtensa.a(xtensa_vectors.S.obj)` |
| `.KernelExceptionVector.text` | 6 | `esp-idf/xtensa/libxtensa.a(xtensa_vectors.S.obj)` |
| `.UserExceptionVector.text` | 6 | `esp-idf/xtensa/libxtensa.a(xtensa_vectors.S.obj)` |
| `.init` | 3 | `C:/Espressif/tools/xtensa-esp-elf/esp-13.2.0_20240530/xtensa-esp-elf/bin/../lib/gcc/xtensa-esp-elf/13.2.0/esp32s3/no-rtti/crti.o` |

## .iram0.text

起始 `0x40374404`，大小 113023 B（110.374 KiB）。

| 组件 | B | KiB |
| --- | ---: | ---: |
| libfreertos.a | 18708 | 18.270 |
| libbtdm_app.a | 14292 | 13.957 |
| libpp.a | 11813 | 11.536 |
| libspi_flash.a | 11345 | 11.079 |
| libhal.a | 8776 | 8.570 |
| libheap.a | 7814 | 7.631 |
| libesp_hw_support.a | 7660 | 7.480 |
| libphy.a | 5073 | 4.954 |
| libesp_ringbuf.a | 4686 | 4.576 |
| libesp_system.a | 4509 | 4.403 |
| libnet80211.a | 3852 | 3.762 |
| libxtensa.a | 2057 | 2.009 |
| libnewlib.a | 1677 | 1.638 |
| libesp_psram.a | 1469 | 1.435 |
| libesp_mm.a | 1339 | 1.308 |
| libesp_timer.a | 1298 | 1.268 |
| padding/unattributed | 1192 | 1.164 |
| libbootloader_support.a | 821 | 0.802 |
| libbt.a | 745 | 0.728 |
| libesp_rom.a | 714 | 0.697 |
| libMyEncoder.a | 427 | 0.417 |
| libesp_wifi.a | 423 | 0.413 |
| libxt_hal.a | 405 | 0.396 |
| libesp_lcd.a | 352 | 0.344 |
| libesp_driver_gpio.a | 329 | 0.321 |
| libbtbb.a | 289 | 0.282 |
| liblog.a | 276 | 0.270 |
| libesp_phy.a | 191 | 0.187 |
| libesp_coex.a | 173 | 0.169 |
| libesp_driver_usb_serial_jtag.a | 123 | 0.120 |
| libesp_driver_ledc.a | 99 | 0.097 |
| libmbedcrypto.a | 80 | 0.078 |
| libesp_pm.a | 16 | 0.016 |

最大输入条目：

| 条目 | 声明大小 B | 来源 |
| --- | ---: | --- |
| `.iram1` | 2775 | `C:/Espressif/frameworks/esp-idf-v5.3.1/components/esp_phy/lib/esp32s3\libphy.a(phy_reg.o)` |
| `.text.tlsf_memalign_offs` | 1674 | `esp-idf/heap/libheap.a(tlsf.c.obj)` |
| `.iram1` | 1506 | `esp-idf/xtensa/libxtensa.a(xtensa_vectors.S.obj)` |
| `.text.tlsf_realloc` | 1454 | `esp-idf/heap/libheap.a(tlsf.c.obj)` |
| `.wifirxiram.45` | 1323 | `C:/Espressif/frameworks/esp-idf-v5.3.1/components/esp_wifi/lib/esp32s3/libnet80211.a(ieee80211_sta.o)` |
| `.wifi0iram.20` | 1227 | `C:/Espressif/frameworks/esp-idf-v5.3.1/components/esp_wifi/lib/esp32s3/libnet80211.a(ieee80211_output.o)` |
| `.iram1.8` | 1040 | `C:/Espressif/frameworks/esp-idf-v5.3.1/components/bt/controller/lib_esp32c3_family/esp32s3\libbtdm_app.a(lld_con.o)` |
| `.text.tlsf_free` | 1028 | `esp-idf/heap/libheap.a(tlsf.c.obj)` |
| `.iram1.9` | 1007 | `C:/Espressif/frameworks/esp-idf-v5.3.1/components/bt/controller/lib_esp32c3_family/esp32s3\libbtdm_app.a(lld_scan.o)` |
| `.text.tlsf_malloc` | 991 | `esp-idf/heap/libheap.a(tlsf.c.obj)` |
| `.iram1.15` | 983 | `C:/Espressif/frameworks/esp-idf-v5.3.1/components/bt/controller/lib_esp32c3_family/esp32s3\libbtdm_app.a(lld_con.o)` |
| `.wifirxiram.26` | 966 | `C:/Espressif/frameworks/esp-idf-v5.3.1/components/esp_wifi/lib/esp32s3/libpp.a(pp.o)` |
| `.iram1` | 902 | `C:/Espressif/frameworks/esp-idf-v5.3.1/components/esp_phy/lib/esp32s3\libphy.a(phy_i2c.o)` |
| `.iram1.2` | 819 | `esp-idf/esp_system/libesp_system.a(cpu_start.c.obj)` |
| `.iram1.3` | 810 | `C:/Espressif/frameworks/esp-idf-v5.3.1/components/bt/controller/lib_esp32c3_family/esp32s3\libbtdm_app.a(rwble.o)` |

## .dram0.data

起始 `0x3FC9FE00`，大小 24616 B（24.039 KiB）。

| 组件 | B | KiB |
| --- | ---: | ---: |
| libhal.a | 3185 | 3.110 |
| libfreertos.a | 3108 | 3.035 |
| libphy.a | 3094 | 3.021 |
| libpp.a | 2604 | 2.543 |
| libspi_flash.a | 2091 | 2.042 |
| libcoexist.a | 1436 | 1.402 |
| libDataCenter.a | 1360 | 1.328 |
| libnet80211.a | 1150 | 1.123 |
| libxtensa.a | 1060 | 1.035 |
| shared/merged | 709 | 0.692 |
| libesp_system.a | 581 | 0.567 |
| libesp_wifi.a | 476 | 0.465 |
| libesp_hw_support.a | 437 | 0.427 |
| libbtdm_app.a | 435 | 0.425 |
| libbt.a | 373 | 0.364 |
| libc.a | 360 | 0.352 |
| libBluetooch.a | 331 | 0.323 |
| libFunctionSelection.a | 288 | 0.281 |
| libesp_driver_uart.a | 196 | 0.191 |
| libvfs.a | 192 | 0.188 |
| libnewlib.a | 172 | 0.168 |
| padding/unattributed | 154 | 0.150 |
| liblvgl.a | 146 | 0.143 |
| libmbedcrypto.a | 116 | 0.113 |
| libesp_coex.a | 72 | 0.070 |
| libesp_mm.a | 49 | 0.048 |
| libesp_lcd_st7796.a | 45 | 0.044 |
| libesp_driver_usb_serial_jtag.a | 40 | 0.039 |
| libefuse.a | 36 | 0.035 |
| libesp_driver_gpio.a | 36 | 0.035 |
| libesp_timer.a | 32 | 0.031 |
| libMyEncoder.a | 32 | 0.031 |
| libSystemMoniter.a | 28 | 0.027 |
| libmbedtls.a | 26 | 0.025 |
| libOTA.a | 24 | 0.023 |
| libmyLvgl.a | 22 | 0.021 |
| libjson.a | 12 | 0.012 |
| libheap.a | 12 | 0.012 |
| libesp_lcd.a | 12 | 0.012 |
| liblwip.a | 12 | 0.012 |
| libesp_psram.a | 11 | 0.011 |
| libesp_phy.a | 9 | 0.009 |
| libOTAService.a | 8 | 0.008 |
| liblog.a | 8 | 0.008 |
| libwpa_supplicant.a | 8 | 0.008 |
| libesp_driver_ledc.a | 8 | 0.008 |
| libpthread.a | 8 | 0.008 |
| libesp_netif.a | 4 | 0.004 |
| libstdc++.a | 4 | 0.004 |
| libBlueDataDeal.a | 4 | 0.004 |

最大输入条目：

| 条目 | 声明大小 B | 来源 |
| --- | ---: | --- |
| `.rodata.cache_hal_disable.str1.4` | 3292 | `esp-idf/hal/libhal.a(cache_hal.c.obj)` |
| `.dram1.3` | 3072 | `esp-idf/freertos/libfreertos.a(port.c.obj)` |
| `.rodata.str1.1` | 1904 | `C:/Espressif/frameworks/esp-idf-v5.3.1/components/esp_phy/lib/esp32s3\libphy.a(phy_debug.o)` |
| `.data` | 1024 | `esp-idf/xtensa/libxtensa.a(xtensa_intr_asm.S.obj)` |
| `.data.TxRxCxt` | 960 | `C:/Espressif/frameworks/esp-idf-v5.3.1/components/esp_wifi/lib/esp32s3/libpp.a(pp.o)` |
| `.data.command_queue` | 788 | `esp-idf/DataCenter/libDataCenter.a(DataCenter.c.obj)` |
| `.data.phy_param` | 712 | `C:/Espressif/frameworks/esp-idf-v5.3.1/components/esp_phy/lib/esp32s3\libphy.a(phy_init.o)` |
| `.rodata.esp_cache_msync.str1.4` | 627 | `esp-idf/esp_mm/libesp_mm.a(esp_cache.c.obj)` |
| `.data.gateway_transaction` | 540 | `esp-idf/DataCenter/libDataCenter.a(DataCenter.c.obj)` |
| `.data.g_wifi_osi_funcs` | 476 | `esp-idf/esp_wifi/libesp_wifi.a(esp_adapter.c.obj)` |
| `.rodata.str1.1` | 340 | `C:/Espressif/frameworks/esp-idf-v5.3.1/components/esp_phy/lib/esp32s3\libphy.a(phy_rx_cal.o)` |
| `.data` | 244 | `C:/Espressif/tools/xtensa-esp-elf/esp-13.2.0_20240530/xtensa-esp-elf/bin/../lib/gcc/xtensa-esp-elf/13.2.0/../../../../xtensa-esp-elf/lib/esp32s3/no-rtti\libc.a(libc_a-impure.o)` |
| `.data.country_info_24ghz` | 230 | `C:/Espressif/frameworks/esp-idf-v5.3.1/components/esp_wifi/lib/esp32s3/libnet80211.a(ieee80211_reg_db.o)` |
| `.data.g_eb_list_desc` | 220 | `C:/Espressif/frameworks/esp-idf-v5.3.1/components/esp_wifi/lib/esp32s3/libpp.a(esf_buf.o)` |
| `.rodata.mmu_hal_map_region.str1.4` | 213 | `esp-idf/hal/libhal.a(mmu_hal.c.obj)` |

## .dram0.bss

起始 `0x3FCA5E28`，大小 31200 B（30.469 KiB）。

| 组件 | B | KiB |
| --- | ---: | ---: |
| libmyLvgl.a | 12996 | 12.691 |
| libOTAService.a | 9001 | 8.790 |
| liblvgl.a | 1119 | 1.093 |
| libfreertos.a | 1069 | 1.044 |
| libDataCenter.a | 718 | 0.701 |
| libbtdm_app.a | 710 | 0.693 |
| libUI.a | 687 | 0.671 |
| libntcTemp.a | 555 | 0.542 |
| libc.a | 360 | 0.352 |
| libwifi.a | 322 | 0.314 |
| padding/unattributed | 313 | 0.306 |
| libesp_system.a | 301 | 0.294 |
| libDebugConsole.a | 295 | 0.288 |
| libmbedcrypto.a | 284 | 0.277 |
| libFaultDeal.a | 280 | 0.273 |
| liblog.a | 272 | 0.266 |
| libBlueDataDeal.a | 269 | 0.263 |
| libmbedtls.a | 240 | 0.234 |
| libnewlib.a | 216 | 0.211 |
| libBluetooch.a | 170 | 0.166 |
| libesp_hw_support.a | 133 | 0.130 |
| libesp_psram.a | 60 | 0.059 |
| libesp_phy.a | 51 | 0.050 |
| libMilk.a | 45 | 0.044 |
| libesp_mm.a | 44 | 0.043 |
| libesp_driver_ledc.a | 44 | 0.043 |
| libesp_wifi.a | 43 | 0.042 |
| libvfs.a | 40 | 0.039 |
| libesp_timer.a | 36 | 0.035 |
| libSoyMilkMake.a | 36 | 0.035 |
| libMyEncoder.a | 33 | 0.032 |
| libesp_driver_uart.a | 33 | 0.032 |
| libOTA.a | 32 | 0.031 |
| libspi_flash.a | 32 | 0.031 |
| libmain.a | 29 | 0.028 |
| libesp_netif.a | 29 | 0.028 |
| libToufuMake.a | 28 | 0.027 |
| libFunctionSelection.a | 28 | 0.027 |
| libnvs_flash.a | 24 | 0.023 |
| libusart.a | 24 | 0.023 |
| libWiFiConnect.a | 19 | 0.019 |
| libSystemMoniter.a | 17 | 0.017 |
| libstdc++.a | 17 | 0.017 |
| libesp_vfs_console.a | 16 | 0.016 |
| libapp_update.a | 12 | 0.012 |
| libpthread.a | 12 | 0.012 |
| libesp_app_format.a | 10 | 0.010 |
| libcore.a | 9 | 0.009 |
| libjson.a | 8 | 0.008 |
| libesp_partition.a | 8 | 0.008 |
| libRGB.a | 8 | 0.008 |
| libheap.a | 8 | 0.008 |
| libcoexist.a | 6 | 0.006 |
| libesp_driver_usb_serial_jtag.a | 5 | 0.005 |
| libbootloader_support.a | 4 | 0.004 |
| libbrine.a | 4 | 0.004 |
| libSelfClean.a | 4 | 0.004 |
| libesp_event.a | 4 | 0.004 |
| libesp-tls.a | 4 | 0.004 |
| libhal.a | 4 | 0.004 |
| libphy.a | 4 | 0.004 |
| libefuse.a | 4 | 0.004 |
| libusrdug.a | 4 | 0.004 |
| libBeep.a | 4 | 0.004 |
| libPress.a | 4 | 0.004 |

最大输入条目：

| 条目 | 声明大小 B | 来源 |
| --- | ---: | --- |
| `.bss.fallback_buf2$3` | 6400 | `esp-idf/myLvgl/libmyLvgl.a(lv_port_disp.c.obj)` |
| `.bss.fallback_buf1$4` | 6400 | `esp-idf/myLvgl/libmyLvgl.a(lv_port_disp.c.obj)` |
| `.bss.s_info_buf` | 4096 | `esp-idf/OTAService/libOTAService.a(OTAInfo.c.obj)` |
| `.bss.s_upg_rx_buf` | 4096 | `esp-idf/OTAService/libOTAService.a(OTANetUpgrade.c.obj)` |
| `.bss.override_src` | 576 | `esp-idf/UI/libUI.a(ui.c.obj)` |
| `.bss.pxReadyTasksLists` | 500 | `esp-idf/freertos/libfreertos.a(tasks.c.obj)` |
| `.bss.s_upg` | 420 | `esp-idf/OTAService/libOTAService.a(OTANetUpgrade.c.obj)` |
| `.bss.s_ver_mgr` | 384 | `esp-idf/OTAService/libOTAService.a(OTAVerManager.c.obj)` |
| `.bss` | 316 | `C:/Espressif/tools/xtensa-esp-elf/esp-13.2.0_20240530/xtensa-esp-elf/bin/../lib/gcc/xtensa-esp-elf/13.2.0/../../../../xtensa-esp-elf/lib/esp32s3/no-rtti\libc.a(libc_a-findfp.o)` |
| `.bss.detectTable` | 280 | `esp-idf/FaultDeal/libFaultDeal.a(FaultDeal.c.obj)` |
| `.bss.message$1` | 260 | `esp-idf/BlueDataDeal/libBlueDataDeal.a(BlueDataDeal.c.obj)` |
| `.bss.opa_table$0` | 256 | `esp-idf/lvgl/liblvgl.a(lv_draw_sw_letter.c.obj)` |
| `.bss.debug_frame` | 256 | `esp-idf/DebugConsole/libDebugConsole.a(DebugConsole.c.obj)` |
| `.bss.s_log_cache` | 248 | `esp-idf/log/liblog.a(log.c.obj)` |
| `.bss.ntc_adc_windows` | 240 | `esp-idf/ntcTemp/libntcTemp.a(ntcTemp.c.obj)` |

## .ext_ram.bss

起始 `0x3C340000`，大小 55192 B（53.898 KiB）。

| 组件 | B | KiB |
| --- | ---: | ---: |
| liblvgl.a | 32768 | 32.000 |
| libnet80211.a | 7518 | 7.342 |
| libbt.a | 4788 | 4.676 |
| libntcTemp.a | 3816 | 3.727 |
| liblwip.a | 3709 | 3.622 |
| libwpa_supplicant.a | 1321 | 1.290 |
| libpp.a | 1145 | 1.118 |
| padding/unattributed | 127 | 0.124 |

最大输入条目：

| 条目 | 声明大小 B | 来源 |
| --- | ---: | --- |
| `.ext_ram.bss.0` | 32768 | `esp-idf/lvgl/liblvgl.a(lv_mem.c.obj)` |
| `.bss.g_cnxMgr` | 3832 | `C:/Espressif/frameworks/esp-idf-v5.3.1/components/esp_wifi/lib/esp32s3/libnet80211.a(wl_cnx.o)` |
| `.ext_ram.bss.0` | 3816 | `esp-idf/ntcTemp/libntcTemp.a(ntcTemp.c.obj)` |
| `.bss.s_wifi_nvs` | 1284 | `C:/Espressif/frameworks/esp-idf-v5.3.1/components/esp_wifi/lib/esp32s3/libnet80211.a(ieee80211_nvs.o)` |
| `.bss.dns_table` | 1184 | `esp-idf/lwip/liblwip.a(dns.c.obj)` |
| `.bss.gWpaSm` | 824 | `esp-idf/wpa_supplicant/libwpa_supplicant.a(wpa.c.obj)` |
| `.bss.ble_att_svr_prep_entry_mem` | 768 | `esp-idf/bt/libbt.a(ble_att_svr.c.obj)` |
| `.bss.g_ic` | 668 | `C:/Espressif/frameworks/esp-idf-v5.3.1/components/esp_wifi/lib/esp32s3/libnet80211.a(ieee80211.o)` |
| `.bss.ble_freertos_ev_buf` | 660 | `esp-idf/bt/libbt.a(npl_os_freertos.c.obj)` |
| `.bss.gChmCxt` | 552 | `C:/Espressif/frameworks/esp-idf-v5.3.1/components/esp_wifi/lib/esp32s3/libnet80211.a(wl_chm.o)` |
| `.bss.ble_freertos_co_buf` | 480 | `esp-idf/bt/libbt.a(npl_os_freertos.c.obj)` |
| `.bss.destination_cache` | 480 | `esp-idf/lwip/liblwip.a(nd6.c.obj)` |
| `.bss.g_pm` | 464 | `C:/Espressif/frameworks/esp-idf-v5.3.1/components/esp_wifi/lib/esp32s3/libpp.a(pm.o)` |
| `.bss.ble_sm_proc_mem` | 400 | `esp-idf/bt/libbt.a(ble_sm.c.obj)` |
| `.bss.ble_hs_hci_frag_data` | 300 | `esp-idf/bt/libbt.a(ble_hs_hci.c.obj)` |
