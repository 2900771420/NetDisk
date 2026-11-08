# 第十八天 JSON文件列表与详情

目标 引入nlohmann/json 提供GET HEAD /api/files列表与GET HEAD /api/files/{id}详情 保持POST上传和下载兼容
响应字段id name size 列表为files数组 详情为单个对象 name目前是ID加.bin 不是原始名称 不保存名称 不实现摘要 创建时间 数据库或分页 查询参数继续忽略
正式目录仅返回名称符合ID规则的普通文件 uploads和非法名称目录符号链接不返回 按ID排序 最多1000个 超限500 不返回静默截断列表 空对象目录返回files空数组 缺少对象目录返回500

## JSON与文件系统接口

make_file_json(id,name,size)复制字符串返回拥有数据的JSON对象 不校验ID 不负责HTTP响应头编码 特殊名称单测只证明JSON序列化正确 生产name仍是ID加.bin
read_file_info(path,id,output)读取对象自身symlink_status 排除链接和非普通对象 file_size获取字节数 缺失或非普通返回false 其他存储错误抛filesystem_error 成功output拥有JSON
list_file_objects用directory_iterator只扫描data/objects 保存合法ID并排序 再读取详情 迭代器拥有系统资源并自动释放 不读取文件正文
is_file_catalog_request判断列表和一层详情路由 POST /api/files交回原上传流程 下载路由在main优先分派
file_catalog_response接收借用的完整RequestParser 返回拥有JSON正文的HttpResponse GET HEAD允许 详情POST405 其他未实现方法501 非空体400 非法ID400 缺失或非普通对象404 目录或序列化异常记服务器日志并返回固定500
nlohmann::json::array创建数组 push_back追加元素 dump返回序列化string 正确转义引号反斜杠控制字符 UTF8默认保留 size是JSON整数 build_response按string字节数计算Content-Length
JSON拥有字符串 HttpResponse拥有dump正文 返回后没有局部字符串视图悬空 无新增裸文件描述符 父目录受服务账号控制 不保证外部并发修改下的快照一致性
参考 https://json.nlohmann.me/integration/cmake/ 和 https://json.nlohmann.me/api/basic_json/dump/

## 变更

新增src/file_catalog.h tests/day18_file_catalog.cpp tests/day18_file_catalog.py 修改src/main.cpp及CMakeLists.txt
其他源码和旧测试保持不变 Python测试脚本实现不在对话展示 保留完整文件供同步

## 完整源码

### src\file_catalog.h

```cpp
// ===== DAY 18 ADDED BEGIN 用成熟JSON库提供对象列表与详情 =====
#pragma once

#include "http_response.h"
#include "object_path.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

inline nlohmann::json make_file_json(std::string_view id, std::string_view name,
                                     std::uintmax_t size)
{
    return {{"id", std::string(id)}, {"name", std::string(name)}, {"size", size}};
}

inline bool read_file_info(const std::filesystem::path& path,
                           std::string_view id, nlohmann::json& output)
{
    namespace fs = std::filesystem;
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory) return false;
    if (error) throw fs::filesystem_error("inspect object", path, error);
    if (!fs::is_regular_file(status)) return false;
    output = make_file_json(id, std::string(id) + ".bin", fs::file_size(path));
    return true;
}

inline nlohmann::json list_file_objects()
{
    namespace fs = std::filesystem;
    std::vector<std::string> ids;
    for (const auto& entry : fs::directory_iterator("data/objects"))
    {
        const std::string id = entry.path().filename().string();
        if (!is_valid_file_id(id) || !fs::is_regular_file(entry.symlink_status())) continue;
        if (ids.size() == 1000) throw std::length_error("catalog exceeds 1000 objects");
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    auto files = nlohmann::json::array();
    for (const auto& id : ids)
    {
        std::string path;
        if (!make_object_path(id, path)) throw std::logic_error("invalid stored ID");
        nlohmann::json info;
        if (read_file_info(path, id, info)) files.push_back(std::move(info));
    }
    return {{"files", std::move(files)}};
}

inline bool is_file_catalog_request(const RequestLine& line)
{
    const std::string_view target(line.target);
    const auto path = target.substr(0, target.find('?'));
    if (path == "/api/files") return line.method != "POST";
    constexpr std::string_view prefix = "/api/files/";
    return path.substr(0, prefix.size()) == prefix &&
           path.find('/', prefix.size()) == std::string_view::npos;
}

inline HttpResponse file_catalog_response(const RequestParser& request)
{
    const auto& line = request.line();
    const std::string_view target(line.target);
    const auto path = target.substr(0, target.find('?'));
    if (line.method != "GET" && line.method != "HEAD")
        return error_response(line.method == "POST" ? 405 : 501, "GET, HEAD");
    if (request.body_size() != 0) return error_response(400);
    try
    {
        nlohmann::json document;
        if (path == "/api/files")
            document = list_file_objects();
        else
        {
            const auto id = path.substr(std::string_view("/api/files/").size());
            std::string object_path;
            if (!make_object_path(id, object_path)) return error_response(400);
            if (!read_file_info(object_path, id, document))
                return {404, "Not Found", "application/json",
                    R"({"error":{"code":"not_found","message":"File not found"}})"};
        }
        return {200, "OK", "application/json", document.dump()};
    }
    catch (const std::exception& error)
    {
        std::cerr << "file catalog failed: " << error.what() << '\n';
        return error_response(500);
    }
}
// ===== DAY 18 ADDED END =====

```

### src\main.cpp

```cpp
#include "unique_fd.h"
#include "socket_io.h"
#include "http_response.h"
#include "temp_upload.h"
#include "file_download.h"
// ===== DAY 18 ADDED BEGIN 接入JSON文件列表与详情 =====
#include "file_catalog.h"
// ===== DAY 18 ADDED END =====

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>

bool send_response(int client_fd, const HttpResponse& response, bool head_only)
{
    const std::string wire = build_response(response, head_only);
    std::cout << "response status=" << response.status << '\n';
    return send_all(client_fd, wire.data(), wire.size());
}

bool is_upload(const RequestLine& line)
{
    const std::string_view target(line.target);
    const auto path = target.substr(0, target.find('?'));
    return line.method == "POST" && (path == "/dev/upload" || path == "/api/files");
}

bool handle_client(int client_fd)
{
    TempUpload upload;
    RequestParser parser([&upload](const RequestLine& line, std::string_view chunk)
    {
        if (!is_upload(line)) return BodyAction::Buffer;
        return upload.append(chunk) ? BodyAction::Written : BodyAction::Failed;
    });
    ParseState state = ParseState::NeedMore;
    bool received_any = false;
    char buffer[1024];

    while (state == ParseState::NeedMore)
    {
        const ssize_t n = ::recv(client_fd, buffer, sizeof(buffer), 0);
        if (n == -1)
        {
            const int error = errno;
            if (error == EINTR)
            {
                continue;
            }
            print_system_error("recv", error);
            return false;
        }
        if (n == 0)
        {
            if (!received_any) return false;
            parser.finish();
            std::cerr << "peer closed before request completed\n";
            return send_response(client_fd, parser_error_response(parser.error()),
                                 parser.line().method == "HEAD");
        }

        received_any = true;
        const std::size_t count = static_cast<std::size_t>(n);
        const auto result = parser.feed(std::string_view(buffer, count));
        state = result.state;
        if (state == ParseState::Error)
        {
            std::cerr << "invalid or unsupported request or size limit exceeded\n";
            if (parser.error() == RequestError::BodyWriteFailed)
                print_system_error("temporary upload", upload.error());
            return send_response(client_fd, parser_error_response(parser.error()),
                                 parser.line().method == "HEAD");
        }
        if (result.consumed < count)
        {
            std::cout << "trailing bytes ignored before connection close\n";
        }
    }

    const RequestLine& line = parser.line();
    std::cout << "method=" << line.method
              << " target=" << line.target
              << " version=" << line.version << '\n';
    std::string_view file_id;
    if (match_file_content(line.target, file_id))
        return send_file_content(client_fd, parser, file_id);
    // ===== DAY 18 ADDED BEGIN POST上传与GET列表分别分派 =====
    if (is_file_catalog_request(line))
        return send_response(client_fd, file_catalog_response(parser), line.method == "HEAD");
    // ===== DAY 18 ADDED END =====
    if (is_upload(line))
    {
        const std::string_view target(line.target);
        const bool permanent = target.substr(0, target.find('?')) == "/api/files";
        const bool stored = permanent ? upload.publish(parser.body_size()) : upload.close_file();
        if (!stored)
        {
            print_system_error("finalize upload", upload.error());
            return send_response(client_fd, error_response(500), false);
        }
        std::cout << "temporary bytes=" << parser.body_size()
                  << " buffered bytes=" << parser.body().size() << '\n';
        if (permanent) std::cout << "published id=" << upload.id() << '\n';
    }
    return send_response(client_fd, route_request(parser, upload.id()), line.method == "HEAD");
}

int main()
{
    constexpr std::uint16_t port = 8080;
    constexpr int backlog = 16;

    const int raw_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (raw_fd == -1)
    {
        const int error = errno;
        print_system_error("socket", error);
        return 1;
    }
    UniqueFd listener(raw_fd);

    const int reuse_address = 1;
    if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR,
                     &reuse_address, sizeof(reuse_address)) == -1)
    {
        const int error = errno;
        print_system_error("setsockopt", error);
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::bind(listener.get(),
               reinterpret_cast<const sockaddr*>(&address),
               sizeof(address)) == -1)
    {
        const int error = errno;
        print_system_error("bind", error);
        return 1;
    }
    if (::listen(listener.get(), backlog) == -1)
    {
        const int error = errno;
        print_system_error("listen", error);
        return 1;
    }

    std::cout << "listening on 127.0.0.1:" << port
              << " fd=" << listener.get() << std::endl;

    while (true)
    {
        const int accepted_fd =
            ::accept(listener.get(), nullptr, nullptr);
        if (accepted_fd == -1)
        {
            const int error = errno;
            if (error == EINTR || error == ECONNABORTED)
            {
                continue;
            }
            print_system_error("accept", error);
            return 1;
        }

        UniqueFd client(accepted_fd);
        std::cout << "accepted fd=" << client.get() << std::endl;
        const bool ok = handle_client(client.get());
        std::cout << (ok ? "response sent" : "connection failed")
                  << std::endl;
    }
}

```

### tests\day18_file_catalog.cpp

```cpp
// ===== DAY 18 ADDED BEGIN 验证JSON转义与真实磁盘目录查询 =====
#include "file_catalog.h"

#include <chrono>
#include <fstream>

void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

struct TestDirectory
{
    std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::path root = previous / ("day18-test-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));

    TestDirectory()
    {
        require(std::filesystem::create_directory(root), "create isolated directory");
        std::filesystem::current_path(root);
    }

    ~TestDirectory()
    {
        std::error_code error;
        std::filesystem::current_path(previous, error);
        std::filesystem::remove_all(root, error);
    }
};

int main()
{
    try
    {
        const std::string special = u8"笔记\"反斜杠\\换行\n.txt";
        const auto original = make_file_json("upload-Abc123", special, 3);
        const std::string text = original.dump();
        const auto parsed = nlohmann::json::parse(text);
        require(parsed.at("name").get<std::string>() == special &&
                parsed.at("size").is_number_unsigned(), "JSON escaping or numeric type");
        require(text.find('\n') == std::string::npos &&
                text.find("\\n") != std::string::npos, "newline was not escaped");
        TestDirectory directory;
        std::filesystem::create_directories("data/objects");
        require(list_file_objects().at("files").empty(), "empty list");
        {
            std::ofstream file("data/objects/upload-Abc123", std::ios::binary);
            file.write("A\0B", 3);
            require(file.good(), "write fixture");
        }
        std::filesystem::create_directory("data/objects/upload-Dir001");
        const auto files = list_file_objects().at("files");
        require(files.size() == 1 && files.at(0).at("size") == 3 &&
                files.at(0).at("name") == "upload-Abc123.bin", "list fields or file type");
        RequestParser request;
        require(request.feed("GET /api/files/upload-Abc123 HTTP/1.1\r\nHost: localhost\r\n\r\n")
                    .state == ParseState::Done, "parse detail request");
        const auto response = file_catalog_response(request);
        require(response.status == 200 && nlohmann::json::parse(response.body) == files.at(0),
                "detail differs from list");
        std::cout << "PASS: file catalog JSON escaping, disk list and detail\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
// ===== DAY 18 ADDED END =====

```

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)

project(netdisk LANGUAGES CXX)

add_executable(netdisk src/main.cpp)
set(NETDISK_TARGETS netdisk)

# ===== DAY 18 ADDED BEGIN 查找并接入系统JSON依赖 =====
find_package(nlohmann_json 3.2.0 REQUIRED)
target_link_libraries(netdisk PRIVATE nlohmann_json::nlohmann_json)
# ===== DAY 18 ADDED END =====

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

    # ===== DAY 18 ADDED BEGIN 注册JSON目录单测与集成测试 =====
    add_executable(file_catalog_tests tests/day18_file_catalog.cpp)
    target_include_directories(file_catalog_tests PRIVATE src)
    target_link_libraries(file_catalog_tests PRIVATE nlohmann_json::nlohmann_json)
    list(APPEND NETDISK_TARGETS file_catalog_tests)
    add_test(NAME file_catalog_tests COMMAND file_catalog_tests)

    add_test(NAME file_catalog_integration_tests
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/day18_file_catalog.py"
            "$<TARGET_FILE:netdisk>"
    )
    set_tests_properties(file_catalog_integration_tests PROPERTIES
        TIMEOUT 30
        RUN_SERIAL TRUE
        SKIP_RETURN_CODE 77
    )
    # ===== DAY 18 ADDED END =====
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

同步以上四个文件以及tests/day18_file_catalog.py 保留tests/day16_download_integration.py 新脚本从中复用exchange和require
本次第一次增加JSON依赖 Ubuntu Debian虚拟机首次执行 其他发行版使用对应包管理器安装nlohmann/json开发包

```sh
sudo apt update
sudo apt install nlohmann-json3-dev
```

随后在Linux工程目录复用已有build 如果原来用build-debug统一替换 停止旧手动服务让8080空闲

```sh
cd ~/projects/netdisk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j2
ctest --test-dir build -R '^file_catalog_integration_tests$' -V
```

应实际运行file_catalog_integration_tests一项通过 输出PASS: file catalog integration finished, 5 groups 退出0 Linux无SKIP 没找到测试不算通过
五组覆盖空数组 上传列表详情下载一致 临时文件非法名称目录链接FIFO隐藏 HEAD长度非法ID方法正文策略 存储目录缺失500且后续health可用
自动测试使用独立临时目录 启动停止自己创建的进程 不使用工程data 不证明优雅退出

真实操作 在工程目录启动新版本服务

```sh
mkdir -p data/uploads data/objects
./build/netdisk
```

另一Linux终端执行一次列表

```sh
curl -i 'http://127.0.0.1:8080/api/files'
```

应200 application/json files数组中每个对象含id name size size为整数 没文件时files为空数组
本次不要求额外手动详情请求 自动测试已覆盖
CMake找不到nlohmann_json检查开发包是否安装 如果GET列表仍405检查新版本已构建并重启 500看file catalog failed日志 确认cwd data/objects存在权限正确和不超过1000个合法对象 ModuleNotFoundError检查第16单元脚本是否保留

## 实际证据

本机从官方v3.11.3下载单头文件到build-parser-check/deps供Windows编译 未将其作为工程源码同步 Linux按系统开发包接入
Windows g++ C++17 Wall Wextra pedantic编译运行file_catalog_tests通过 真实隔离目录的空列表二进制大小目录排除详情一致与特殊名称JSON往返通过
Python ast语法 导入第16单元辅助函数及Windows退出77通过 Linux新服务未编译 未执行集成测试和curl 用户虚拟机同步理解待反馈 第15至17单元Linux结果也未反馈 不回填通过
下一单元第19单元重命名与删除语义