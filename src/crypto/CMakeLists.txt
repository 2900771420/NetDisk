# ===== DAY 33A MODIFIED BEGIN crypto成为具有实现的静态模块 =====
add_library(netdisk_crypto STATIC
    sha256.cpp
)

target_include_directories(netdisk_crypto PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(netdisk_crypto PUBLIC
    OpenSSL::Crypto
)
# ===== DAY 33A MODIFIED END =====
