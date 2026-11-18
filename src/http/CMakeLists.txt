# ===== DAY 33A MODIFIED BEGIN HTTP模块编译请求 范围与响应实现 =====
add_library(netdisk_http STATIC
    request.cpp
    range.cpp
    response.cpp
)

target_include_directories(netdisk_http PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(netdisk_http PUBLIC
    netdisk_api
    netdisk_crypto
)
# ===== DAY 33A MODIFIED END =====
