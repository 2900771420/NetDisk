# ===== DAY 33A MODIFIED BEGIN storage成为对象路径与发布静态模块 =====
add_library(netdisk_storage STATIC
    object_path.cpp
    temp_upload.cpp
)

target_include_directories(netdisk_storage PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(netdisk_storage PUBLIC
    netdisk_api
    netdisk_io
    netdisk_crypto
)
# ===== DAY 33A MODIFIED END =====
