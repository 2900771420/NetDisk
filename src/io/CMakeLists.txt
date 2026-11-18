# ===== DAY 33A ADDED BEGIN 声明POSIX资源与发送模块 =====
add_library(netdisk_io STATIC
    posix_io.cpp
)

target_include_directories(netdisk_io PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
# ===== DAY 33A ADDED END =====
