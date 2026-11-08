# 第十九天 重命名与删除语义

目标 PATCH /api/files/{id}只改显示名称 DELETE同路径真正移除对象 成功204 后续详情下载404且列表不显示
名称本次保存在进程内static map 重启恢复ID加.bin 删除会改变磁盘 重启不恢复删除对象 不提前增加数据库 下载建议名仍ID加.bin 未实现中文filename星号编码
当前串行服务 不宣称并发删除下载 不保证断电持久化 无回收站

## 请求与结果

PATCH正文仅允许一个name字符串字段 例如{"name":"课程笔记.txt"} 成功200返回id name size 内容和路径都不变 不调用磁盘rename
DELETE不接受正文 成功204 没有Content-Length或正文 对不存在对象返回404 重复删除结果仍为不存在
已有方法在不允许的资源返回405 未实现PUT仍501

## 实现与接口

函数局部static map拥有ID和UTF8名称 跨串行请求存活 进程结束销毁 返回map引用不转移所有权 不是持久化 不是线程安全业务实现 最多1000个名称覆盖
valid_display_name检查1至255字节 非字符数 拒绝纯空格 点双点 ASCII控制字符DEL 斜杠反斜杠 UTF8有效性由JSON解析保证 支持中文和引号 名称不用于磁盘路径
parse(body,nullptr,false)对语法失败返回discarded 通过is_object字段数量find和is_string检查结构 无效JSON UTF8 名称返回400 不改旧名称
PATCH先读取合法普通对象 再校验JSON 构造返回JSON正文后写map 因此缺失对象404即使正文也有问题 同名允许 因为身份由ID区分
DELETE先准备ID字符串 再filesystem remove正式路径 返回true成功 false表示没有移除目标 异常由外层捕获500 删除成功才清名称 不使用递归删除 不删除目录或符号链接
remove成功是删除提交点 响应丢失也不回滚已删除对象 不做fsync不保证断电持久化
read_file_info统一读取名称覆盖 所以列表和详情一致 无覆盖时ID加.bin
build_response为204直接生成状态行Connection close和空行 不发送正文 Content-Length或Transfer-Encoding 204要求参考RFC9110 15.3.5与8.6
修改file_download.h只为已实现PATCH DELETE返回405而非501 未改变读文件或发送逻辑

## 变化文件

修改src/file_catalog.h src/http_response.h src/file_download.h CMakeLists.txt tests/day18_file_catalog.py
新增tests/day19_file_mutation.cpp tests/day19_file_mutation.py
main.cpp RequestParser 上传和ID模块 download_headers.h保持不变 Python实现不在对话展示

## 完整C++与CMake

### src\file_catalog.h

```cpp
#pragma once

#include "http_response.h"
#include "object_path.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
// ===== DAY 19 ADDED BEGIN 保存本进程的显示名称覆盖值 =====
#include <map>

inline std::map<std::string, std::string>& display_names()
{
    static std::map<std::string, std::string> names;
    return names;
}

inline bool valid_display_name(std::string_view name)
{
    if (name.empty() || name.size() > 255 || name == "." || name == ".." ||
        name.find_first_not_of(' ') == std::string_view::npos) return false;
    for (unsigned char ch : name)
        if (ch < 32 || ch == 127 || ch == '/' || ch == '\\') return false;
    return true;
}
// ===== DAY 19 ADDED END =====

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
    // ===== DAY 19 MODIFIED BEGIN 列表与详情读取同一份显示名称 =====
    const auto name = display_names().find(std::string(id));
    output = make_file_json(id, name == display_names().end()
        ? std::string(id) + ".bin" : name->second, fs::file_size(path));
    // ===== DAY 19 MODIFIED END =====
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
    // ===== DAY 19 MODIFIED BEGIN 列表只读 详情允许修改和删除 =====
    const bool listing = path == "/api/files";
    const bool changing = line.method == "PATCH" || line.method == "DELETE";
    if (line.method != "GET" && line.method != "HEAD" && !(changing && !listing))
    {
        const bool known = changing || line.method == "POST";
        return error_response(known ? 405 : 501,
            listing ? "GET, HEAD, POST" : "GET, HEAD, PATCH, DELETE");
    }
    if (line.method != "PATCH" && request.body_size() != 0) return error_response(400);
    // ===== DAY 19 MODIFIED END =====
    try
    {
        nlohmann::json document;
        if (listing)
            document = list_file_objects();
        else
        {
            const auto id = path.substr(std::string_view("/api/files/").size());
            std::string object_path;
            if (!make_object_path(id, object_path)) return error_response(400);
            if (!read_file_info(object_path, id, document))
                return {404, "Not Found", "application/json",
                    R"({"error":{"code":"not_found","message":"File not found"}})"};
            // ===== DAY 19 ADDED BEGIN 修改名称不改变ID 删除成功后清除名称 =====
            if (line.method == "PATCH")
            {
                const auto patch = nlohmann::json::parse(request.body(), nullptr, false);
                if (!patch.is_object() || patch.size() != 1 || patch.find("name") == patch.end() ||
                    !patch.at("name").is_string()) return error_response(400);
                const std::string name = patch.at("name").get<std::string>();
                if (!valid_display_name(name)) return error_response(400);
                const std::string key(id);
                auto& names = display_names();
                if (names.find(key) == names.end() && names.size() == 1000)
                    return error_response(500);
                document["name"] = name;
                std::string body = document.dump();
                names.insert_or_assign(key, name);
                return {200, "OK", "application/json", std::move(body)};
            }
            if (line.method == "DELETE")
            {
                const std::string key(id);
                if (!std::filesystem::remove(object_path)) return error_response(404);
                display_names().erase(key);
                return {204, "No Content", "", ""};
            }
            // ===== DAY 19 ADDED END =====
        }
        return {200, "OK", "application/json", document.dump()};
    }
    catch (const std::exception& error)
    {
        std::cerr << "file catalog failed: " << error.what() << '\n';
        return error_response(500);
    }
}

```

### src\http_response.h

```cpp
#pragma once

#include "request_parser.h"

struct HttpResponse
{
    int status;
    std::string_view reason;
    std::string_view content_type;
    std::string body;
    std::string_view allow = {};
    std::string file_id = {};
};

inline HttpResponse error_response(int status, std::string_view allow = {})
{
    switch (status)
    {
    case 400:
        return {400, "Bad Request", "application/json",
            R"({"error":{"code":"bad_request","message":"Invalid request"}})"};
    case 404:
        return {404, "Not Found", "application/json",
            R"({"error":{"code":"not_found","message":"Route not found"}})"};
    case 405:
        return {405, "Method Not Allowed", "application/json",
            R"({"error":{"code":"method_not_allowed","message":"Method not allowed"}})", allow};
    case 413:
        return {413, "Content Too Large", "application/json",
            R"({"error":{"code":"body_too_large","message":"Body exceeds 65536 bytes"}})"};
    case 417:
        return {417, "Expectation Failed", "application/json",
            R"({"error":{"code":"expectation_failed","message":"Expect is not supported"}})"};
    case 501:
        return {501, "Not Implemented", "application/json",
            R"({"error":{"code":"not_implemented","message":"Requested feature is not implemented"}})"};
    default:
        return {500, "Internal Server Error", "application/json",
            R"({"error":{"code":"internal_error","message":"Internal server error"}})"};
    }
}

inline HttpResponse parser_error_response(RequestError error)
{
    switch (error)
    {
    case RequestError::BodyTooLarge: return error_response(413);
    case RequestError::UnsupportedExpectation: return error_response(417);
    case RequestError::UnsupportedTransferEncoding: return error_response(501);
    case RequestError::BodyWriteFailed: return error_response(500);
    default: return error_response(400);
    }
}

inline HttpResponse route_request(const RequestParser& request, std::string_view published_id = {})
{
    const auto& line = request.line();
    // ===== DAY 19 MODIFIED BEGIN 区分已实现的方法与资源不允许的方法 =====
    if (line.method != "GET" && line.method != "HEAD" && line.method != "POST" &&
        line.method != "PATCH" && line.method != "DELETE")
    // ===== DAY 19 MODIFIED END =====
    {
        return error_response(501);
    }
    const std::string_view target(line.target);
    const auto path = target.substr(0, target.find('?'));
    if (path == "/health")
    {
        if (line.method != "GET" && line.method != "HEAD")
        {
            return error_response(405, "GET, HEAD");
        }
        if (request.body_size() != 0) return error_response(400);
        return {200, "OK", "text/plain; charset=utf-8", "OK\n"};
    }
    if (path == "/echo")
    {
        if (line.method != "POST") return error_response(405, "POST");
        return {200, "OK", "application/octet-stream", request.body()};
    }
    if (path == "/dev/upload")
    {
        if (line.method != "POST") return error_response(405, "POST");
        return {200, "OK", "application/json", R"({"status":"temporary_received"})"};
    }
    if (path == "/api/files")
    {
        if (line.method != "POST") return error_response(405, "POST");
        if (published_id.empty()) return error_response(500);
        HttpResponse response{201, "Created", "application/json", R"({"status":"created"})"};
        response.file_id = published_id;
        return response;
    }
    return error_response(404);
}

inline std::string build_response(const HttpResponse& response, bool head_only = false)
{
    // ===== DAY 19 ADDED BEGIN 204不发送正文或Content-Length =====
    if (response.status == 204)
        return "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n";
    // ===== DAY 19 ADDED END =====
    std::string wire = "HTTP/1.1 " + std::to_string(response.status) + " " +
        std::string(response.reason) + "\r\n";
    wire += "Content-Type: " + std::string(response.content_type) + "\r\n";
    wire += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
    wire += "Connection: close\r\n";
    if (!response.allow.empty()) wire += "Allow: " + std::string(response.allow) + "\r\n";
    if (!response.file_id.empty()) wire += "X-File-Id: " + response.file_id + "\r\n";
    wire += "\r\n";
    if (!head_only) wire.append(response.body);
    return wire;
}

```

### src\file_download.h

```cpp
#pragma once

#include "http_response.h"
#include "object_path.h"
#include "socket_io.h"
#include "unique_fd.h"
#include "download_headers.h"

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
    {
        // ===== DAY 19 MODIFIED BEGIN 已实现的写方法在下载资源返回405 =====
        const bool known = method == "POST" || method == "PATCH" || method == "DELETE";
        return send_error(known ? 405 : 501, "GET, HEAD");
        // ===== DAY 19 MODIFIED END =====
    }
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
    std::string header;
    if (!make_download_header(id, remaining, header)) return send_error(500);
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

### tests\day19_file_mutation.cpp

```cpp
// ===== DAY 19 ADDED BEGIN 验证改名内容不变与删除204边界 =====
#include "file_catalog.h"

#include <chrono>
#include <fstream>
#include <iterator>

void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

struct TestDirectory
{
    std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::path root = previous / ("day19-test-" + std::to_string(
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

HttpResponse request(std::string_view method, const std::string& body = "")
{
    RequestParser parser;
    const std::string wire = std::string(method) +
        " /api/files/upload-Abc123 HTTP/1.1\r\nHost: localhost\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\n\r\n" + body;
    require(parser.feed(wire).state == ParseState::Done, "parse mutation request");
    return file_catalog_response(parser);
}

int main()
{
    try
    {
        TestDirectory directory;
        std::filesystem::create_directories("data/objects");
        const std::string path = "data/objects/upload-Abc123";
        {
            std::ofstream file(path, std::ios::binary);
            file.write("A\0B", 3);
            require(file.good(), "write fixture");
        }
        const std::string name = u8"笔记\"新版.txt";
        const auto renamed = request("PATCH", nlohmann::json{{"name", name}}.dump());
        require(renamed.status == 200 && nlohmann::json::parse(renamed.body).at("name") == name,
                "rename result");
        require(list_file_objects().at("files").at(0).at("name") == name, "list name");
        {
            std::ifstream file(path, std::ios::binary);
            const std::string bytes{std::istreambuf_iterator<char>(file), {}};
            require(bytes == std::string("A\0B", 3), "rename changed bytes");
        }
        const std::string bad[] = {"", "..", "../escape", "bad\\name", "bad\nname",
                                   std::string(256, 'x')};
        for (const auto& value : bad)
            require(request("PATCH", nlohmann::json{{"name", value}}.dump()).status == 400,
                    "invalid name accepted");
        require(request("PATCH", "{").status == 400 &&
                request("PATCH", R"({"name":123})").status == 400, "invalid JSON schema");
        require(nlohmann::json::parse(request("GET").body).at("name") == name,
                "failed rename changed name");
        const auto removed = request("DELETE");
        require(removed.status == 204 && build_response(removed) ==
                "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n", "204 framing");
        require(!std::filesystem::exists(path) && display_names().empty() &&
                list_file_objects().at("files").empty(), "delete left object or name");
        require(request("DELETE").status == 404 && request("GET").status == 404,
                "deleted object remained visible");
        std::cout << "PASS: rename isolation, invalid input and deletion semantics\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
// ===== DAY 19 ADDED END =====

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

    # ===== DAY 19 ADDED BEGIN 注册重命名删除测试 =====
    add_executable(file_mutation_tests tests/day19_file_mutation.cpp)
    target_include_directories(file_mutation_tests PRIVATE src)
    target_link_libraries(file_mutation_tests PRIVATE nlohmann_json::nlohmann_json)
    list(APPEND NETDISK_TARGETS file_mutation_tests)
    add_test(NAME file_mutation_tests COMMAND file_mutation_tests)

    add_test(NAME file_mutation_integration_tests
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/day19_file_mutation.py"
            "$<TARGET_FILE:netdisk>"
    )
    set_tests_properties(file_mutation_integration_tests PROPERTIES
        TIMEOUT 30
        RUN_SERIAL TRUE
        SKIP_RETURN_CODE 77
    )
    # ===== DAY 19 ADDED END =====
endif()

foreach(target IN LISTS NETDISK_TARGETS)
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )
endforeach()

```
## Linux验证

同步上述五个文件及tests/day19_file_mutation.py 更新tests/day18_file_catalog.py中的Allow预期 合计七个文件 保留第16单元脚本 无新依赖
先停止旧手动服务让8080空闲 在Linux工程目录执行 复用原build 若使用build-debug统一替换

```sh
cd ~/projects/netdisk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j2
ctest --test-dir build -R '^file_mutation_integration_tests$' -V
```

应实际运行一项通过 输出PASS: file mutation integration finished, 6 groups 退出0 Linux无SKIP 没找到测试不算通过
六组 两对象同名合法且ID字节不变 非法JSON与名称拒绝且旧名称保留 列表详情下载一致 方法和DELETE非空体规则 删除204正确且后续404 重启只丢名称不恢复删除对象
自动测试使用独立临时目录 两次启动同一临时目录内自己创建的服务 停止重启不代表优雅退出或断电测试

真实操作 启动新服务

```sh
mkdir -p data/uploads data/objects
./build/netdisk
```

另一Linux终端 用已有真实ID替换示例

```sh
file_id='upload-Abc123'
curl -i -X PATCH -H 'Content-Type: application/json' \
  --data-binary '{"name":"课程笔记.txt"}' \
  "http://127.0.0.1:8080/api/files/${file_id}"
```

应200 JSON中id不变 name变成课程笔记.txt size不变 当前不自动同步名称到下载建议头 无需手动删除真实文件 自动测试已删除自己的测试对象
400检查JSON结构 名称字节数 控制字符和分隔符 404检查ID对象存在 501检查新进程是否已重启 500查看file catalog failed权限日志及名称表容量

## 实际验证

Windows g++ C++17 Wall Wextra pedantic编译运行file_mutation_tests通过 因修改公共响应与目录逻辑重新编译运行http_response_tests和file_catalog_tests通过
两个变化Python脚本ast 导入和Windows退出77通过 socketpair验证DELETE辅助接受正确204 拒绝204长度头与正文 400错误长度检查通过
Linux新服务未编译 未实际执行集成测试或curl 用户虚拟机同步理解待反馈 第15至18单元Linux反馈仍未提供 不回填通过
下一步第20单元文件错误与客户端中断