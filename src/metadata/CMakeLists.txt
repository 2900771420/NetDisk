# ===== DAY 33A MODIFIED BEGIN metadata成为具有实现的SQLite静态模块 =====
add_library(netdisk_metadata STATIC
    database.cpp
    file_metadata_store.cpp
)

target_include_directories(netdisk_metadata PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(netdisk_metadata PUBLIC
    netdisk_api
    netdisk_crypto
    SQLite::SQLite3
)
# ===== DAY 33A MODIFIED END =====
