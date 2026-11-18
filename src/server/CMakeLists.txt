# ===== DAY 33A MODIFIED BEGIN 服务端库不再包含维护模块并保持netdisk目标名 =====
add_library(netdisk_server_lib STATIC
    request_handler.cpp
    file_catalog_handler.cpp
    file_content_handler.cpp
)

target_include_directories(netdisk_server_lib PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(netdisk_server_lib PUBLIC
    netdisk_io
    netdisk_http
    netdisk_metadata
    netdisk_storage
    nlohmann_json::nlohmann_json
)

add_executable(netdisk
    main.cpp
)

target_link_libraries(netdisk PRIVATE
    netdisk_server_lib
    netdisk_maintenance
)
# ===== DAY 33A MODIFIED END =====
