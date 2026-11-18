# ===== DAY 33A ADDED BEGIN 声明跨端文件标识模块 =====
add_library(netdisk_api STATIC
    file_id.cpp
)

target_include_directories(netdisk_api PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
# ===== DAY 33A ADDED END =====
