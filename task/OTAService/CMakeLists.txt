idf_component_register(SRCS "OTAFrame.c" "OTAUart.c" "OTAInfo.c" "OTANetUpgrade.c" "OTAVerManager.c" "OTAService.c"
                       INCLUDE_DIRS "."
                       REQUIRES OTA freertos WiFiConnect
                       PRIV_REQUIRES esp_http_client cjson app_update esp_partition DataCenter)

include("${CMAKE_CURRENT_LIST_DIR}/../../config/firmware_version.cmake")
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ESP32S3_VERSION_MAJOR=${ESP32S3_VERSION_MAJOR}
    ESP32S3_VERSION_MINOR=${ESP32S3_VERSION_MINOR}
    ESP32S3_VERSION_PATCH=${ESP32S3_VERSION_PATCH}
    ESP32S3_VERSION_BUILD=${ESP32S3_VERSION_BUILD}
)