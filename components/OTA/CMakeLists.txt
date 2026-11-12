idf_component_register(SRCS "OTA.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_http_client esp_https_ota nvs_flash cjson
                    REQUIRES app_update)
