idf_component_register(SRCS "myLvgl.c""lv_port_disp.c"
                    INCLUDE_DIRS "include"
                    REQUIRES lvgl
                    REQUIRES sys
                    REQUIRES FunctionSelection
                    REQUIRES DataCenter
                    REQUIRES UI
                    REQUIRES RGB)
