idf_component_register(SRCS "RGB.c"
                    INCLUDE_DIRS "include"
                    REQUIRES sys
                    REQUIRES driver
                    REQUIRES esp_lcd
                    PRIV_REQUIRES esp_lcd_st7796)
