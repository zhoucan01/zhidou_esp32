idf_component_register(SRCS "sys.c"
                    INCLUDE_DIRS "include"
                    
                    REQUIRES wifi
                    REQUIRES OTA

                    REQUIRES SelfClean
                    REQUIRES SoyMilkMake

                    REQUIRES ToufuMake
                    
                    REQUIRES Press)
