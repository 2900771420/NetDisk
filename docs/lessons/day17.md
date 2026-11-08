# 第十七天 下载响应头与建议文件名

目标 GET和HEAD共用成功头部 增加Content-Disposition attachment filename为ID加.bin HEAD继续只发送头部
第16单元已经实现基础HEAD 本单元完善响应头并验证 无固定复盘 无新增Linux系统调用
当前尚无原始名称元数据 不凭ID推断原始扩展名 Content-Type保持application/octet-stream 查询name仍忽略

## 响应示例

```http
HTTP/1.1 200 OK
Content-Type: application/octet-stream
Content-Length: 5
Content-Disposition: attachment; filename="upload-Abc123.bin"
Connection: close
```

GET在头部后发送5字节内容 HEAD在头部结束处结束 Content-Length仍为对应GET正文大小 不是HEAD实际发送正文长度
错误HEAD也不发送正文 长度描述对应GET的JSON错误体 错误不带Content-Disposition
attachment建议客户端保存响应 filename提供建议名称 最终名称由客户端决定 不改变服务器磁盘路径
参考 https://www.rfc-editor.org/rfc/rfc6266.html#section-4.2 与 https://www.rfc-editor.org/rfc/rfc9110.html#section-9.3.2 和#section-8.6

## 变更与接口

新增src/download_headers.h tests/day17_download_headers.cpp tests/day17_download_headers.py 修改src/file_download.h和CMakeLists.txt
main.cpp object_path.h temp_upload.h http_response.h socket_io.h unique_fd.h 请求解析器和已有测试保持不变
make_download_header(id,size,output)的id是同步借用 size为uint64字节数 调用者从已打开文件的fstat取得 output成功拥有完整响应头 非法ID返回false且输出不变 内存分配仍可能抛异常
函数不访问磁盘 不验证文件存在 不保存元数据 先校验ID再生成建议名 防止引号CRLF等字符进入头部
filename局部string拥有名称 output拥有最终头部 局部变量销毁不会留下悬空视图
这里不是任意文件名转义函数 未来引入真实显示名称时须单独实现名称校验与响应头编码 不能取消ID限制后直接拼入filename
文件下载先open fstat再构造头部 HEAD发送头后返回 GET继续原16KiB循环 文件fd由UniqueFd释放 连接fd继续借用 不改变所有权
普通错误仍由已有JSON响应逻辑构造 不把失败返回体作为附件

## 完整C++及CMake

### src\download_headers.h

```cpp
// ===== DAY 17 ADDED BEGIN 统一下载成功响应头与安全建议文件名 =====
#pragma once

#include "object_path.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

inline bool make_download_header(std::string_view id, std::uint64_t size,
                                 std::string& output)
{
    if (!is_valid_file_id(id)) return false;
    const std::string filename = std::string(id) + ".bin";
    std::string header =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: " + std::to_string(size) + "\r\n"
        "Content-Disposition: attachment; filename=\"" + filename + "\"\r\n"
        "Connection: close\r\n\r\n";
    output = std::move(header);
    return true;
}
// ===== DAY 17 ADDED END =====

```

### src\file_download.h

```cpp
#pragma once

#include "http_response.h"
#include "object_path.h"
#include "socket_io.h"
#include "unique_fd.h"
// ===== DAY 17 ADDED BEGIN 使用统一下载响应头 =====
#include "download_headers.h"
// ===== DAY 17 ADDED END =====

#include <algorithm>
#include <cstdint>
#include <fcntl.h>
#include <sys/stat.h>

inline bool match_file_content(std::string_view target, std::string_view& id)
{
    const auto path = target.substr(0, target.find('?'));
    constexpr std::string_view prefix = "/api/files/";
    constexpr std::string_view suffix = "/content";
    if (path.size() < prefix.size() + suffix.size() ||
        path.substr(0, prefix.size()) != prefix ||
        path.substr(path.size() - suffix.size()) != suffix) return false;
    id = path.substr(prefix.size(), path.size() - prefix.size() - suffix.size());
    return true;
}

inline bool send_file_content(int client_fd, const RequestParser& request,
                              std::string_view id)
{
    const auto& method = request.line().method;
    const bool head_only = method == "HEAD";
    const auto send_error = [&](int status, std::string_view allow = {})
    {
        const auto response = status == 404
            ? HttpResponse{404, "Not Found", "application/json",
                R"({"error":{"code":"not_found","message":"File not found"}})"}
            : error_response(status, allow);
        const std::string wire = build_response(response, head_only);
        return send_all(client_fd, wire.data(), wire.size());
    };
    if (method != "GET" && method != "HEAD")
        return send_error(method == "POST" ? 405 : 501, "GET, HEAD");
    if (request.body_size() != 0) return send_error(400);

    std::string path;
    if (!make_object_path(id, path)) return send_error(400);
    int raw_fd;
    do
    {
        raw_fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    } while (raw_fd == -1 && errno == EINTR);
    if (raw_fd == -1)
    {
        const int error = errno;
        if (error == ENOENT || error == ENOTDIR || error == ELOOP)
            return send_error(404);
        print_system_error("open download", error);
        return send_error(500);
    }
    UniqueFd file(raw_fd);
    struct stat info{};
    int result;
    do
    {
        result = ::fstat(file.get(), &info);
    } while (result == -1 && errno == EINTR);
    if (result == -1)
    {
        print_system_error("fstat download", errno);
        return send_error(500);
    }
    if (!S_ISREG(info.st_mode)) return send_error(404);
    if (info.st_size < 0) return send_error(500);

    std::uint64_t remaining = static_cast<std::uint64_t>(info.st_size);
    // ===== DAY 17 DELETED 删除下载函数内部的成功响应头拼接 =====
    // ===== DAY 17 MODIFIED BEGIN GET与HEAD使用同一份下载响应头 =====
    std::string header;
    if (!make_download_header(id, remaining, header)) return send_error(500);
    // ===== DAY 17 MODIFIED END =====
    if (!send_all(client_fd, header.data(), header.size())) return false;
    if (head_only) return true;

    char buffer[16 * 1024];
    while (remaining != 0)
    {
        const auto count = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, sizeof(buffer)));
        const ssize_t n = ::read(file.get(), buffer, count);
        if (n == -1)
        {
            if (errno == EINTR) continue;
            print_system_error("read download", errno);
            return false;
        }
        if (n == 0)
        {
            std::cerr << "download file ended before Content-Length\n";
            return false;
        }
        const auto bytes = static_cast<std::size_t>(n);
        if (!send_all(client_fd, buffer, bytes)) return false;
        remaining -= bytes;
    }
    return true;
}

```

### tests\day17_download_headers.cpp

```cpp
// ===== DAY 17 ADDED BEGIN 验证下载头格式长度与文件名输入边界 =====
#include "download_headers.h"

#include <cstdlib>
#include <iostream>
#include <limits>

void require(bool ok, const char* message)
{
    if (!ok)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    std::string header;
    require(make_download_header("upload-Abc123", 5, header), "build header");
    require(header ==
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: 5\r\n"
        "Content-Disposition: attachment; filename=\"upload-Abc123.bin\"\r\n"
        "Connection: close\r\n\r\n", "exact header framing and filename");
    require(make_download_header("upload-Abc123", 0, header) &&
            header.find("Content-Length: 0\r\n") != std::string::npos, "empty file");
    require(make_download_header("upload-Abc123",
                std::numeric_limits<std::uint64_t>::max(), header) &&
            header.find("Content-Length: 18446744073709551615\r\n") != std::string::npos,
            "64 bit length formatting");
    const std::string invalid[] = {
        "", "../upload-Abc123", "upload-Abc12\"", "upload-Abc123\r\nX-Test: yes",
        "upload-Abc123?name=x", std::string("upload-Ab\0xyz", 13)
    };
    for (const auto& id : invalid)
    {
        header = "unchanged";
        require(!make_download_header(id, 5, header) && header == "unchanged",
                "unsafe ID accepted or output changed");
    }
    std::cout << "PASS: download headers, lengths and safe filenames\n";
}
// ===== DAY 17 ADDED END =====

```

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)

project(netdisk LANGUAGES CXX)

add_executable(netdisk src/main.cpp)
set(NETDISK_TARGETS netdisk)

include(CTest)
if(BUILD_TESTING)
    add_executable(io_tests tests/day06_io.cpp)
    target_include_directories(io_tests PRIVATE src)
    list(APPEND NETDISK_TARGETS io_tests)
    add_test(NAME io_tests COMMAND io_tests)

    add_executable(request_line_tests tests/day08_request_line.cpp)
    target_include_directories(request_line_tests PRIVATE src)
    list(APPEND NETDISK_TARGETS request_line_tests)
    add_test(NAME request_line_tests COMMAND request_line_tests)

    add_executable(request_headers_tests tests/day09_request_headers.cpp)
    target_include_directories(request_headers_tests PRIVATE src)
    list(APPEND NETDISK_TARGETS request_headers_tests)
    add_test(NAME request_headers_tests COMMAND request_headers_tests)

    add_executable(request_body_tests tests/day10_request_body.cpp)
    target_include_directories(request_body_tests PRIVATE src)
    list(APPEND NETDISK_TARGETS request_body_tests)
    add_test(NAME request_body_tests COMMAND request_body_tests)

    add_executable(http_response_tests tests/day11_http_response.cpp)
    target_include_directories(http_response_tests PRIVATE src)
    list(APPEND NETDISK_TARGETS http_response_tests)
    add_test(NAME http_response_tests COMMAND http_response_tests)

    add_executable(stream_upload_tests tests/day12_stream_upload.cpp)
    target_include_directories(stream_upload_tests PRIVATE src)
    list(APPEND NETDISK_TARGETS stream_upload_tests)
    add_test(NAME stream_upload_tests COMMAND stream_upload_tests)

    add_executable(publish_tests tests/day13_publish.cpp)
    target_include_directories(publish_tests PRIVATE src)
    list(APPEND NETDISK_TARGETS publish_tests)
    add_test(NAME publish_tests COMMAND publish_tests)

    # ===== DAY 15 ADDED BEGIN 注册ID与存储路径隔离测试 =====
    add_executable(object_path_tests tests/day15_object_path.cpp)
    target_include_directories(object_path_tests PRIVATE src)
    list(APPEND NETDISK_TARGETS object_path_tests)
    add_test(NAME object_path_tests COMMAND object_path_tests)
    # ===== DAY 15 ADDED END =====

    # ===== DAY 14 ADDED BEGIN 注册自动启动服务的上传集成测试 =====
    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
    add_test(NAME upload_integration_tests
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/day14_upload_integration.py"
            "$<TARGET_FILE:netdisk>"
    )
    set_tests_properties(upload_integration_tests PROPERTIES
        TIMEOUT 30
        RUN_SERIAL TRUE
        SKIP_RETURN_CODE 77
    )
    # ===== DAY 14 ADDED END =====

    # ===== DAY 16 ADDED BEGIN 注册下载端到端测试 =====
    add_test(NAME download_integration_tests
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/day16_download_integration.py"
            "$<TARGET_FILE:netdisk>"
    )
    set_tests_properties(download_integration_tests PROPERTIES
        TIMEOUT 30
        RUN_SERIAL TRUE
        SKIP_RETURN_CODE 77
    )
    # ===== DAY 16 ADDED END =====

    # ===== DAY 17 ADDED BEGIN 注册响应头单测与真实GET HEAD验证 =====
    add_executable(download_headers_tests tests/day17_download_headers.cpp)
    target_include_directories(download_headers_tests PRIVATE src)
    list(APPEND NETDISK_TARGETS download_headers_tests)
    add_test(NAME download_headers_tests COMMAND download_headers_tests)

    add_test(NAME download_headers_integration_tests
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/day17_download_headers.py"
            "$<TARGET_FILE:netdisk>"
    )
    set_tests_properties(download_headers_integration_tests PROPERTIES
        TIMEOUT 30
        RUN_SERIAL TRUE
        SKIP_RETURN_CODE 77
    )
    # ===== DAY 17 ADDED END =====
endif()

foreach(target IN LISTS NETDISK_TARGETS)
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )
endforeach()

```
## Linux操作

同步以上四个文件及tests/day17_download_headers.py 新Python脚本复用原tests/day16_download_integration.py中的exchange和require 保留原文件 不要求阅读Python实现
停止旧手动服务让8080空闲 复用原build目录 如果原来是build-debug则统一替换

```sh
cd ~/projects/netdisk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j2
ctest --test-dir build -R '^download_headers_integration_tests$' -V
```

只要求运行download_headers_integration_tests一项 应通过且输出PASS: download header integration finished, 6 groups Linux无SKIP 没发现测试不算通过
六组 空UTF8与64KiB二进制GET HEAD头一致且GET字节一致 查询name及编码注入不改变响应头 HEAD400404只发头且长度匹配GET错误正文 POST405Allow且无附件 GET HEAD非空请求体400 后续health
测试自动在独立临时目录启动停止自己创建的服务 不使用工程data 不代表优雅退出

真实操作 在工程目录启动新服务

```sh
mkdir -p data/uploads data/objects
./build/netdisk
```

另一Linux终端 将示例ID替换为已有真实ID

```sh
file_id='upload-Abc123'
curl -I "http://127.0.0.1:8080/api/files/${file_id}/content"
```

应200 类型octet-stream 长度为磁盘文件字节数 Content-Disposition建议名为实际ID加.bin 无正文
本次不要求再次上传下载整个文件 自动测试已经包含对应GET比较
400检查ID格式 404检查ID存在和服务cwd 如果没有新Content-Disposition先确认新版本已构建且旧进程停止 ModuleNotFoundError检查tests目录内保留第16单元脚本

## 验证证据

Windows g++ C++17 Wall Wextra pedantic编译运行download_headers_tests通过 覆盖头部边界 零长度 uint64最大值字符串格式 非法路径引号CRLF查询和NUL拒绝且输出不变 最大值不证明真实超大文件下载
Python ast语法检查 导入旧辅助函数及Windows退出77通过
Linux新服务编译 集成测试 curl与用户理解仍待反馈 第15和16单元没有结果反馈不回填通过 第14单元既有通过记录保留
下一单元第18单元JSON列表与详情