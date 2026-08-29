idf_component_register(SRCS "wifi.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_wifi
                    REQUIRES OTA
                    REQUIRES nvs_flash)
