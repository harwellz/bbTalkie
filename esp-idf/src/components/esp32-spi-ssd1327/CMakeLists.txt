idf_component_register(SRCS "esp32-spi-ssd1327.c" "amoled_co5300.c"
                       REQUIRES driver esp_lcd hardware_driver
                       INCLUDE_DIRS ".")
