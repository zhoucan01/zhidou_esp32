idf_component_register(SRCS "wifi.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_wifi
                    REQUIRES WiFiConnect esp_netif
                    REQUIRES nvs_flash)
