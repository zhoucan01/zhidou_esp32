idf_component_register(SRCS "Milk.c" 
                    INCLUDE_DIRS "."  # 添加这一行确保头文件路径被包含
                    REQUIRES driver
                    REQUIRES sys
                    REQUIRES DataCenter           
                    REQUIRES FunctionSelection
                    REQUIRES SystemMoniter
                    REQUIRES UsartExchange
                    )
