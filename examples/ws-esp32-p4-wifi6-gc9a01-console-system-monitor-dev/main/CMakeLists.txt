idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp-idf-system-monitor
        esp-idf-uart-utf8-console
        esp_driver_uart
        esp_timer
        freertos
        log
)
