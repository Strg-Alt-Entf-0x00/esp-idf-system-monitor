cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS 
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    "${CMAKE_CURRENT_LIST_DIR}/../../../../projects-released/esp-idf-uart-utf8-console"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(console_system_monitor_dev)
