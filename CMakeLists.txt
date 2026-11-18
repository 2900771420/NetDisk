cmake_minimum_required(VERSION 3.16)

project(netdisk LANGUAGES CXX)

# ===== DAY 33A MODIFIED BEGIN 统一所有子目录的C++语言配置 =====
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# ===== DAY 33A MODIFIED END =====

find_package(nlohmann_json 3.2.0 REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(OpenSSL 1.1.1 REQUIRED COMPONENTS Crypto)

include(CTest)

# ===== ARCHITECTURE GUARD ADDED BEGIN 配置阶段校验第33A架构契约 =====
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(NetdiskArchitecture)
# ===== ARCHITECTURE GUARD ADDED END =====

# ===== DAY 33A DELETED 顶层不再直接声明生产目标和全部测试 =====
# ===== DAY 33A MODIFIED BEGIN 顶层只协调生产源码与可选测试子目录 =====
add_subdirectory(src)

# ===== ARCHITECTURE GUARD ADDED BEGIN 在注册测试前锁定生产目标图 =====
netdisk_verify_architecture()
# ===== ARCHITECTURE GUARD ADDED END =====

if(BUILD_TESTING)
    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
    add_subdirectory(tests)
endif()
# ===== DAY 33A MODIFIED END =====
