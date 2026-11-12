idf_component_register(SRCS "GATT.c" "Bluetooch.c"
                    INCLUDE_DIRS "include"
                    REQUIRES sys
                    REQUIRES wifi
                    REQUIRES nvs_flash
                    REQUIRES BlueDataDeal
                    REQUIRES bt)
