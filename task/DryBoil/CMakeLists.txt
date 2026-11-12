idf_component_register(
    SRCS "dry_boil_detector.c" "DryBoilTask.c" "DryBoilPort.c"
    INCLUDE_DIRS "."
    REQUIRES esp_common
    PRIV_REQUIRES freertos log DataCenter
)
