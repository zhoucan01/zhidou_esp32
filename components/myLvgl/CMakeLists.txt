idf_component_register(SRCS "myLvgl.c" "lv_port_disp.c" "display_stats.c" "plantmilk_ui_flow.c" "clean_ui_flow.c"
                    INCLUDE_DIRS "include"
                    REQUIRES lvgl
                    REQUIRES sys
                    REQUIRES FunctionSelection
                    REQUIRES DataCenter
                    REQUIRES MyEncoder
                    REQUIRES UI
                    REQUIRES RGB esp_timer)
