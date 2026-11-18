# ===== DAY 33A MODIFIED BEGIN maintenance编译恢复 审计与回填实现 =====
add_library(netdisk_maintenance STATIC
    recovery.cpp
    storage_audit.cpp
    digest_backfill.cpp
)

target_include_directories(netdisk_maintenance PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(netdisk_maintenance PUBLIC
    netdisk_io
    netdisk_crypto
    netdisk_metadata
    netdisk_storage
)
# ===== DAY 33A MODIFIED END =====
