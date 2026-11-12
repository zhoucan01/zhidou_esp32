idf_component_register(SRCS "Reservation.c" 
                    INCLUDE_DIRS "."  # 添加这一行确保头文件路径被包含
                    REQUIRES driver
                    REQUIRES sys
                    REQUIRES FunctionSelection
    )
