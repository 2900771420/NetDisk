# 第十五天 文件ID与存储路径隔离

目标 集中校验对象ID 再构造固定目录下的对象路径 为下一单元下载复用
显示名称是元数据 ID是服务器生成的对象标识 存储路径由服务器推导
目前忽略name查询参数 不声称已保存显示名称
ID沿用upload-加六位ASCII字母数字 不允许点 斜杠 反斜杠 百分号 空白 NUL或额外后缀
make_object_path非法输入返回false且输出不变 成功输出拥有数据的string string_view只在同步调用中借用
该函数只检查格式 不访问磁盘 不证明文件存在 不提供鉴权 以服务工作目录和受控数据目录为前提
TempUpload发布前用统一函数构造路径 失败记录EINVAL link提交点和不覆盖语义保持不变
不做路径归一化或解码 不允许任意路径输入 不处理本机恶意用户修改数据目录的问题

本次文件完整版本如下 main.cpp http_response.h RequestParser 旧测试及其他源码保持不变

## src\object_path.h

```cpp
// ===== DAY 15 ADDED BEGIN 校验对象ID并集中构造存储路径 =====
#pragma once

#include <string>
#include <string_view>
#include <utility>

inline bool is_valid_file_id(std::string_view id) noexcept
{
    constexpr std::string_view prefix = "upload-";
    if (id.size() != prefix.size() + 6 ||
        id.substr(0, prefix.size()) != prefix) return false;
    for (char c : id.substr(prefix.size()))
    {
        const bool alnum = (c >= 'a' && c <= 'z') ||
                           (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9');
        if (!alnum) return false;
    }
    return true;
}

inline bool make_object_path(std::string_view id, std::string& output)
{
    if (!is_valid_file_id(id)) return false;
    std::string path = "data/objects/";
    path.append(id.data(), id.size());
    output = std::move(path);
    return true;
}
// ===== DAY 15 ADDED END =====

```

## src\temp_upload.h

```cpp
#pragma once

// ===== DAY 15 ADDED BEGIN 使用统一对象路径接口 =====
#include "object_path.h"
// ===== DAY 15 ADDED END =====
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <unistd.h>
#include <cstring>
#include <string>
#include <utility>

class TempUpload
{
public:
    TempUpload() = default;
    TempUpload(const TempUpload&) = delete;
    TempUpload& operator=(const TempUpload&) = delete;

    ~TempUpload() noexcept
    {
        const int saved = errno;
        if (fd_ >= 0 && ::close(fd_) == -1) std::perror("close temporary file");
        if (created_ && ::unlink(path_) == -1 && errno != ENOENT)
            std::perror("unlink temporary file");
        errno = saved;
    }

    bool append(std::string_view data)
    {
        if (error_ != 0) return false;
        if (!id_.empty()) return fail(EINVAL);
        if (!created_)
        {
            fd_ = ::mkstemp(path_);
            if (fd_ == -1) return fail(errno);
            created_ = true;
        }
        if (fd_ < 0) return fail(EBADF);
        std::size_t written = 0;
        while (written < data.size())
        {
            const ssize_t n = ::write(fd_, data.data() + written, data.size() - written);
            if (n == -1)
            {
                if (errno == EINTR) continue;
                return fail(errno);
            }
            if (n == 0) return fail(EIO);
            written += static_cast<std::size_t>(n);
            written_ += static_cast<std::size_t>(n);
        }
        return true;
    }

    bool close_file()
    {
        if (fd_ >= 0)
        {
            const int raw = fd_;
            fd_ = -1;
            if (::close(raw) == -1) return fail(errno);
        }
        return error_ == 0;
    }

    int error() const noexcept { return error_; }
    const char* path() const noexcept { return created_ ? path_ : ""; }

    bool publish(std::size_t expected)
    {
        if (error_ != 0) return false;
        if (!created_ || !id_.empty() || written_ != expected) return fail(EINVAL);
        if (!close_file()) return false;
        std::string candidate(std::strrchr(path_, '/') + 1);
        // ===== DAY 15 DELETED 删除未经校验的对象路径直接拼接 =====
        // ===== DAY 15 MODIFIED BEGIN 校验服务器生成的ID后构造正式路径 =====
        std::string destination;
        if (!make_object_path(candidate, destination)) return fail(EINVAL);
        // ===== DAY 15 MODIFIED END =====
        if (::link(path_, destination.c_str()) == -1) return fail(errno);
        id_ = std::move(candidate);
        if (::unlink(path_) == 0)
            created_ = false;
        else
            std::perror("unlink published temporary name");
        return true;
    }

    const std::string& id() const noexcept { return id_; }

private:
    bool fail(int error) noexcept
    {
        error_ = error;
        return false;
    }

    char path_[sizeof("data/uploads/upload-XXXXXX")] = "data/uploads/upload-XXXXXX";
    int fd_ = -1;
    int error_ = 0;
    bool created_ = false;
    std::size_t written_ = 0;
    std::string id_;
};

```

## tests\day15_object_path.cpp

```cpp
// ===== DAY 15 ADDED BEGIN 验证ID边界与路径隔离 =====
#include "object_path.h"

#include <cstdlib>
#include <iostream>

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
    std::string path;
    require(make_object_path("upload-Abc123", path) &&
            path == "data/objects/upload-Abc123", "valid object path");
    require(is_valid_file_id("upload-aZ09bY"), "ASCII ID alphabet");
    const std::string invalid[] = {
        "", "upload-", "upload-Abc12", "upload-Abc1234",
        "UPLOAD-Abc123", "../upload-Abc123", "/tmp/upload-Abc123",
        "C:\\upload-Abc123", "upload-../ab", "upload-abc/de",
        "upload-abc\\de", "upload-abc.de", "upload-abc de",
        "upload-%2e%2e", "upload-Abc123?name=x",
        std::string("upload-Ab\0xyz", 13), "upload-中文"
    };
    for (const auto& id : invalid)
    {
        path = "unchanged";
        require(!is_valid_file_id(id) && !make_object_path(id, path),
                "invalid ID accepted");
        require(path == "unchanged", "failure modified output");
    }
    std::cout << "PASS: object path isolation, 17 invalid IDs rejected\n";
}
// ===== DAY 15 ADDED END =====

```

## CMakeLists.txt

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

在~/projects/netdisk同步上述四个文件 保留原build目录

```sh
cd ~/projects/netdisk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j2
ctest --test-dir build -R '^object_path_tests$' -V
mkdir -p data/uploads data/objects
./build/netdisk
```

若之前用build-debug继续使用该目录 停止旧服务后启动新版本
测试必须实际运行一项 输出PASS: object path isolation, 17 invalid IDs rejected 且退出0

另一Linux终端执行一次真实上传

```sh
cd ~/projects/netdisk
curl -i --data-binary 'hello' 'http://127.0.0.1:8080/api/files?name=../../escape'
```

应201 X-File-Id为upload-加六位字母数字 正文为{"status":"created"}
用返回ID替换下面示例ID 检查对象正文为hello

```sh
cat data/objects/upload-Abc123
```

name参数仍忽略 ../../escape不会作为存储路径
本机Windows纯C++隔离测试通过 旧发布测试响应部分通过 Linux部分SKIP
Linux构建与真实上传待用户反馈 不重复第14单元整套集成测试
若500先看服务终端的finalize upload日志 确认cwd 数据目录和硬链接所在文件系统
下一单元按ID流式下载