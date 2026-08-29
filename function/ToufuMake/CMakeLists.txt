idf_component_register(SRCS "ToufuMake.c" 
                    INCLUDE_DIRS "."  
                    REQUIRES driver
                    REQUIRES sys
                    REQUIRES ToufuMake
                    REQUIRES FunctionSelection
                    REQUIRES SystemMoniter
                    REQUIRES UsartExchange
                    REQUIRES Music
                    REQUIRES DataCenter
                    REQUIRES tempurature
                    )
