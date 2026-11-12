idf_component_register(SRCS "DataCenter.c" 
                    INCLUDE_DIRS "."  # 添加这一行确保头文件路径被包含
                    REQUIRES driver
                    REQUIRES sys
                    REQUIRES usart
                    REQUIRES CRC
                    REQUIRES nanoModbus
                    REQUIRES ntcTemp
                    REQUIRES FunctionSelection)

include("${CMAKE_CURRENT_LIST_DIR}/../../config/firmware_version.cmake")
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ESP32S3_VERSION_MAJOR=${ESP32S3_VERSION_MAJOR}
    ESP32S3_VERSION_MINOR=${ESP32S3_VERSION_MINOR}
    ESP32S3_VERSION_PATCH=${ESP32S3_VERSION_PATCH}
    ESP32S3_VERSION_BUILD=${ESP32S3_VERSION_BUILD}
)
