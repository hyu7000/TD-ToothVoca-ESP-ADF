idf_component_register(SRCS "lcd_touch.c" "parser_char.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_lcd espressif__esp_lcd_ili9341 espressif__esp_lcd_touch esp_timer)
