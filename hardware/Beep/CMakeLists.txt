idf_component_register(SRCS "Beep.cpp" "buzzer.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES driver
                    REQUIRES sys
                    REQUIRES freertos
                    )
