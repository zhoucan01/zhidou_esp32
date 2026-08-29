idf_component_register(SRCS "Press.c" 
                    INCLUDE_DIRS "."  
                    REQUIRES driver
                    REQUIRES sys
                    REQUIRES ToufuMake
                    REQUIRES FunctionSelection
                    REQUIRES SystemMoniter
                    REQUIRES UsartExchange
                    REQUIRES Music
                    REQUIRES DataCenter
                    )
