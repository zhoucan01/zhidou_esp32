# Read the required version file and rerun CMake whenever it changes.
set(FIRMWARE_VERSION_FILE "${CMAKE_CURRENT_LIST_DIR}/firmware_version.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${FIRMWARE_VERSION_FILE}")
if(NOT EXISTS "${FIRMWARE_VERSION_FILE}")
    message(FATAL_ERROR "Required firmware version file is missing: ${FIRMWARE_VERSION_FILE}")
endif()

file(READ "${FIRMWARE_VERSION_FILE}" ESP32S3_VERSION)
string(STRIP "${ESP32S3_VERSION}" ESP32S3_VERSION)
if(NOT ESP32S3_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    message(FATAL_ERROR "${FIRMWARE_VERSION_FILE}: expected major.minor.patch.build, e.g. 1.1.0.5")
endif()

set(ESP32S3_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(ESP32S3_VERSION_MINOR "${CMAKE_MATCH_2}")
set(ESP32S3_VERSION_PATCH "${CMAKE_MATCH_3}")
set(ESP32S3_VERSION_BUILD "${CMAKE_MATCH_4}")

foreach(_segment
        ESP32S3_VERSION_MAJOR ESP32S3_VERSION_MINOR
        ESP32S3_VERSION_PATCH ESP32S3_VERSION_BUILD)
    if(${_segment} GREATER 255)
        message(FATAL_ERROR
            "${FIRMWARE_VERSION_FILE}: version segment ${_segment} must be 0..255")
    endif()
endforeach()
