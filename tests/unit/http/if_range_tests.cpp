// ===== DAY 32 ADDED BEGIN 验证If Range只接受当前强ETag精确匹配 =====
// ===== DAY 33A MODIFIED BEGIN 使用合并后的HTTP模块公开接口 =====
#include <netdisk/http/range.h>
#include <netdisk/http/request.h>
// ===== DAY 33A MODIFIED END =====

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

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
    constexpr std::string_view digest =
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad";
    constexpr std::string_view current =
        "\"ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad\"";

    require(if_range_matches_strong_etag(current, digest),
            "current strong ETag did not match");
    require(!if_range_matches_strong_etag(
                "\"aa7816bf8f01cfea414140de5dae2223"
                "b00361a396177a9cb410ff61f20015ad\"", digest),
            "different strong ETag matched");
    require(!if_range_matches_strong_etag(
                "W/\"ba7816bf8f01cfea414140de5dae2223"
                "b00361a396177a9cb410ff61f20015ad\"", digest),
            "weak ETag matched a strong validator");
    require(!if_range_matches_strong_etag(digest, digest),
            "unquoted digest matched");
    require(!if_range_matches_strong_etag(
                "Sun, 06 Nov 1994 08:49:37 GMT", digest),
            "HTTP date matched an ETag");
    require(!if_range_matches_strong_etag(current, "ABC"),
            "invalid current digest matched");

    RequestParser request;
    const std::string wire =
        "GET /api/files/upload-Abc123/content HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Range: bytes=2-5\r\n"
        "If-Range: " + std::string(current) + "\r\n"
        "Content-Length: 0\r\n\r\n";
    const auto parsed = request.feed(wire);
    require(parsed.state == ParseState::Done && parsed.consumed == wire.size(),
            "request with If-Range did not parse");
    std::size_t count = 0;
    for (const auto& field : request.headers())
        if (field.first == "if-range" && field.second == current) ++count;
    require(count == 1, "normalized If-Range header was not retained");

    std::cout << "PASS: If-Range strong ETag matching and fallback inputs\n";
}
// ===== DAY 32 ADDED END =====
