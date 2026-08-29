idf_component_register(SRCS "FaultDeal.c" 
                    INCLUDE_DIRS "."  # 添加这一行确保头文件路径被包含
                    REQUIRES driver
                    REQUIRES DataCenter
                    REQUIRES UsartExchange
)
