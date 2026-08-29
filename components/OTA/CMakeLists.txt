idf_component_register(SRCS "OTA.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_http_client nvs_flash cjson app_update esp_app_format)
