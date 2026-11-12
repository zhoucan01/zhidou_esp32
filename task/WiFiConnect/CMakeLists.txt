idf_component_register(SRCS "WiFiConnect.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi
                    REQUIRES esp_netif
                    REQUIRES esp_event
                    REQUIRES freertos nvs_flash esp_timer lwip)
