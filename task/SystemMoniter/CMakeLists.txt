idf_component_register(SRCS "SystemMoniter.c" 
                    INCLUDE_DIRS "."  # 添加这一行确保头文件路径被包含
                    REQUIRES driver
                    REQUIRES Bluetooch
                    REQUIRES FunctionSelection
                    REQUIRES UsartExchange
                    REQUIRES DataCenter
                    REQUIRES sys)
