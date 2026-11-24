set(sys_mon_sources
    "src/system_monitor.c"
    "src/system_monitor_parser.c"
    "src/system_monitor_alarm.c"
    "src/display_hal.cpp"
    "src/system_monitor_display.cpp"
)

set(sys_mon_requires
    esp_timer
    freertos
    log
    driver
    esp_lcd
    esp_lcd_gc9a01
    LovyanGFX
)

if(EXISTS "${IDF_PATH}/components/esp_driver_ledc/CMakeLists.txt")
    list(APPEND sys_mon_requires esp_driver_ledc)
endif()
if(EXISTS "${IDF_PATH}/components/esp_driver_spi/CMakeLists.txt")
    list(APPEND sys_mon_requires esp_driver_spi)
endif()
if(EXISTS "${IDF_PATH}/components/esp_driver_gpio/CMakeLists.txt")
    list(APPEND sys_mon_requires esp_driver_gpio)
endif()

idf_component_register(
    SRCS ${sys_mon_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES ${sys_mon_requires}
)

target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
