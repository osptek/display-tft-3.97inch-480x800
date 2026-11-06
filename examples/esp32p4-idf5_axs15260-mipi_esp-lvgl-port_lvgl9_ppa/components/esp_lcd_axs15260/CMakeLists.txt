idf_component_register(SRCS "esp_lcd_axs15260.c" "esp_lcd_axs15260_mipi.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "priv_include"
                       REQUIRES "driver" "esp_lcd")
