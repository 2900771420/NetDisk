# 第九天 请求头增量解析

目标 在请求行后依次解析字段 遇到空行结束 不消费正文
新增 request_headers.h day09_request_headers.cpp 修改 main.cpp CMakeLists.txt 其余源码不变
字段名统一ASCII小写 值去除两端空格与TAB 中间冒号保留 重复字段不合并 空值允许
拒绝无冒号 非token字段名 冒号前空白 折叠行 裸LF 控制字符和DEL 值允许高位字节但不解码
请求头区域含CRLF上限8192字节 单行含CRLF上限4096字节 字段最多100个 请求行另计上限4096字节
feed借用string_view并复制需要保存的内容 返回本次consumed和状态 空输入不表示EOF
Done与Error为终态 后续feed消费零字节 fields返回内部容器的const引用 仅在Done后读取完整结果 解析器销毁后不可使用
main依次向两个解析器喂同一recv的数据 用请求行consumed移除前缀 recv返回零且未完成则失败
每连接独立解析器 生命周期在handle_client内 client_fd只是借用 外层UniqueFd负责关闭
服务要求单个非空Host 不完整验证authority语法 暂时拒绝任何Content-Length和Transfer-Encoding
当前只处理一次health然后关闭 不保存同次recv中的头后字节 下一单元增加正文缓冲与长度语义
失败仍记录日志并关闭 尚无400响应 无超时 串行阻塞
本机Windows g++ C++17 Wall Wextra pedantic编译并执行请求头测试通过 Linux构建与curl未执行

Linux虚拟机工程目录 沿用现有目录 若采用建议路径则为 ~/projects/netdisk
```sh
cd ~/projects/netdisk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j2
ctest --test-dir build -R '^request_headers_tests$' --output-on-failure
./build/netdisk
```
测试应实际运行1项并通过 服务器运行后另一个Linux终端执行
```sh
curl -i http://127.0.0.1:8080/health
```
预期200 Content-Length为3 Connection为close 正文OK加换行
无测试时检查BUILD_TESTING与文件位置 编译找不到头检查src 连接失败检查服务监听 日志定位Host或字段问题

以下为本次完整源码

## src\request_headers.h

```cpp
// ===== DAY 09 ADDED BEGIN 增量解析请求头并保留正文边界 =====
#pragma once

#include "request_line.h"

#include <utility>
#include <vector>

class RequestHeadersParser
{
public:
    using Field = std::pair<std::string, std::string>;

    FeedResult feed(std::string_view input)
    {
        std::size_t used = 0;
        while (used < input.size() && state_ == ParseState::NeedMore)
        {
            const unsigned char ch = static_cast<unsigned char>(input[used++]);
            if (++bytes_ > 8192 || ++line_bytes_ > 4096)
            {
                state_ = ParseState::Error;
                break;
            }
            if (saw_cr_)
            {
                if (ch != '\n')
                {
                    state_ = ParseState::Error;
                }
                else if (buffer_.empty())
                {
                    state_ = ParseState::Done;
                }
                else if (!parse_field())
                {
                    state_ = ParseState::Error;
                }
                buffer_.clear();
                saw_cr_ = false;
                line_bytes_ = 0;
            }
            else if (ch == '\r')
            {
                saw_cr_ = true;
            }
            else if ((ch < 32 && ch != '\t') || ch == 127)
            {
                state_ = ParseState::Error;
            }
            else
            {
                buffer_.push_back(static_cast<char>(ch));
            }
        }
        return {state_, used};
    }

    const std::vector<Field>& fields() const noexcept
    {
        return fields_;
    }

private:
    static bool is_token(unsigned char ch)
    {
        return (ch >= 'A' && ch <= 'Z') ||
               (ch >= 'a' && ch <= 'z') ||
               (ch >= '0' && ch <= '9') ||
               std::string_view("!#$%&'*+-.^_`|~").find(
                   static_cast<char>(ch)) != std::string_view::npos;
    }

    bool parse_field()
    {
        const auto colon = buffer_.find(':');
        if (colon == std::string::npos || colon == 0 || fields_.size() >= 100)
        {
            return false;
        }
        std::string name = buffer_.substr(0, colon);
        for (char& ch : name)
        {
            if (!is_token(static_cast<unsigned char>(ch)))
            {
                return false;
            }
            if (ch >= 'A' && ch <= 'Z')
            {
                ch = static_cast<char>(ch - 'A' + 'a');
            }
        }
        const auto begin = buffer_.find_first_not_of(" \t", colon + 1);
        const auto end = buffer_.find_last_not_of(" \t");
        const std::string value = begin == std::string::npos
            ? std::string{} : buffer_.substr(begin, end - begin + 1);
        fields_.emplace_back(std::move(name), value);
        return true;
    }

    ParseState state_ = ParseState::NeedMore;
    std::size_t bytes_ = 0;
    std::size_t line_bytes_ = 0;
    bool saw_cr_ = false;
    std::string buffer_;
    std::vector<Field> fields_;
};
// ===== DAY 09 ADDED END =====

```

## src\main.cpp

```cpp
#include "unique_fd.h"
#include "socket_io.h"
#include "request_line.h"
// ===== DAY 09 ADDED BEGIN 接入请求头解析器 =====
#include "request_headers.h"
// ===== DAY 09 ADDED END =====

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

std::string build_health_response()
{
    const std::string body = "OK\n";
    std::string response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: text/plain; charset=utf-8\r\n";
    response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    response += "Connection: close\r\n";
    response += "\r\n";
    response += body;
    return response;
}

bool handle_client(int client_fd)
{
    // ===== DAY 09 DELETED 删除原始头缓存及终止串搜索 改由解析器识别边界 =====
    // ===== DAY 09 MODIFIED BEGIN 请求行和请求头依次消费同一接收缓冲 =====
    RequestLineParser parser;
    RequestHeadersParser headers;
    bool line_done = false;
    ParseState head_state = ParseState::NeedMore;
    char buffer[1024];

    while (head_state == ParseState::NeedMore)
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
            std::cerr << "peer closed before request head completed\n";
            return false;
        }

        const std::size_t count = static_cast<std::size_t>(n);
        std::string_view remaining(buffer, count);
        if (!line_done)
        {
            const FeedResult result = parser.feed(remaining);
            if (result.state == ParseState::Error)
            {
                std::cerr << "invalid or unsupported request line\n";
                return false;
            }
            remaining.remove_prefix(result.consumed);
            line_done = result.state == ParseState::Done;
        }
        if (line_done)
        {
            const FeedResult result = headers.feed(remaining);
            head_state = result.state;
        }
        if (head_state == ParseState::Error)
        {
            std::cerr << "invalid or oversized request headers\n";
            return false;
        }
    }
    // ===== DAY 09 MODIFIED END =====

    // ===== DAY 09 ADDED BEGIN 检查单个非空Host并暂时拒绝消息长度字段 =====
    std::size_t hosts = 0;
    for (const auto& field : headers.fields())
    {
        if (field.first == "host")
        {
            ++hosts;
            if (field.second.empty())
            {
                std::cerr << "empty Host\n";
                return false;
            }
        }
        if (field.first == "content-length" || field.first == "transfer-encoding")
        {
            std::cerr << "request framing is not supported yet\n";
            return false;
        }
    }
    if (hosts != 1)
    {
        std::cerr << "exactly one Host is required\n";
        return false;
    }
    // ===== DAY 09 ADDED END =====

    const RequestLine& line = parser.line();
    std::cout << "method=" << line.method
              << " target=" << line.target
              << " version=" << line.version << '\n';
    if (line.method != "GET" || line.target != "/health")
    {
        std::cerr << "unsupported request line\n";
        return false;
    }

    const std::string response = build_health_response();
    return send_all(client_fd, response.data(), response.size());
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
        std::cout << (ok ? "request completed" : "request failed")
                  << std::endl;
    }
}

```

## tests\day09_request_headers.cpp

```cpp
// ===== DAY 09 ADDED BEGIN 验证分片字段边界和输入限制 =====
#include "request_headers.h"

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
    const std::string head = "HoSt: localhost\r\nX-Test:\t a:b \t\r\nX-Test:\r\n\r\n";
    for (std::size_t split = 0; split <= head.size(); ++split)
    {
        RequestHeadersParser parser;
        const auto first = parser.feed(std::string_view(head).substr(0, split));
        require(first.consumed == split, "first consumption");
        require(first.state == (split == head.size()
                    ? ParseState::Done : ParseState::NeedMore), "first state");
        const auto second = parser.feed(head.substr(split) + "BODY");
        require(second.state == ParseState::Done, "split failed");
        require(second.consumed == head.size() - split, "body consumed");
        const auto& fields = parser.fields();
        require(fields.size() == 3 && fields[0].first == "host" &&
                fields[0].second == "localhost" && fields[1].second == "a:b" &&
                fields[2].first == "x-test" && fields[2].second.empty(),
                "wrong fields");
        require(parser.feed("more").consumed == 0, "done not sticky");
    }
    RequestHeadersParser bytes;
    for (char ch : head)
    {
        require(bytes.feed(std::string_view(&ch, 1)).state != ParseState::Error,
                "byte feed failed");
    }
    require(bytes.feed("").state == ParseState::Done, "byte feed not done");
    for (const std::string& bad : {
             std::string("Bad Name: x\r\n"), std::string(": x\r\n"),
             std::string("Host : x\r\n"), std::string("Missing\r\n"),
             std::string(" folded: x\r\n"), std::string("X: a\n"),
             std::string("X: a\rZ"), std::string("X: \x7f\r\n"),
             std::string("X: a\0b\r\n", 9), std::string(4097, 'A')})
    {
        RequestHeadersParser parser;
        require(parser.feed(bad).state == ParseState::Error, "bad header accepted");
        require(parser.feed(head).consumed == 0, "error not sticky");
    }
    RequestHeadersParser empty;
    require(empty.feed("\r\nBODY").consumed == 2 && empty.fields().empty(),
            "empty section boundary");
    std::string many;
    for (int i = 0; i < 101; ++i) many += "X: a\r\n";
    RequestHeadersParser count;
    require(count.feed(many).state == ParseState::Error, "field count limit");
    const std::string line = "X:" + std::string(4092, 'a') + "\r\n";
    RequestHeadersParser exact;
    require(exact.feed(line + line).state == ParseState::NeedMore,
            "exact byte limit");
    require(exact.feed("\r\n").state == ParseState::Error, "total byte limit");
    std::cout << "PASS: split headers, fields, body boundary and limits\n";
}
// ===== DAY 09 ADDED END =====

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

    # ===== DAY 09 ADDED BEGIN 注册请求头增量解析测试 =====
    add_executable(request_headers_tests tests/day09_request_headers.cpp)
    target_include_directories(request_headers_tests PRIVATE src)
    list(APPEND NETDISK_TARGETS request_headers_tests)
    add_test(NAME request_headers_tests COMMAND request_headers_tests)
    # ===== DAY 09 ADDED END =====
endif()

foreach(target IN LISTS NETDISK_TARGETS)
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )
endforeach()

```