// ===== DAY 31 ADDED BEGIN 验证单Range边界和206 416响应头 =====
// ===== DAY 33A MODIFIED BEGIN 使用合并后的HTTP模块公开接口 =====
#include <netdisk/http/range.h>
#include <netdisk/http/response.h>
#include <netdisk/http/request.h>
// ===== DAY 33A MODIFIED END =====

#include <cstdlib>
#include <iostream>
#include <limits>
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

void require_range(std::string_view value, std::uint64_t size,
                   std::uint64_t start, std::uint64_t length)
{
    ByteRange range{99, 88};
    require(parse_byte_range(value, size, range) == ByteRangeResult::Satisfiable,
            "range should be satisfiable");
    require(range.start == start && range.length == length,
            "range boundaries differ");
}

int main()
{
    require_range("bytes=2-5", 10, 2, 4);
    require_range("bytes=7-", 10, 7, 3);
    require_range("bytes=-4", 10, 6, 4);
    require_range("bytes=8-99", 10, 8, 2);
    require_range("bytes=-99", 10, 0, 10);
    require_range("bytes=0-0", 1, 0, 1);
    require_range("bytes= 1-2", 10, 1, 2);
    require_range("BYTES=3-4", 10, 3, 2);

    ByteRange unchanged{77, 66};
    require(parse_byte_range("bytes=10-", 10, unchanged) ==
                ByteRangeResult::Unsatisfiable,
            "start at size should be unsatisfiable");
    require(parse_byte_range("bytes=-0", 10, unchanged) ==
                ByteRangeResult::Unsatisfiable,
            "zero suffix should be unsatisfiable");
    require(parse_byte_range("bytes=0-", 0, unchanged) ==
                ByteRangeResult::Unsatisfiable,
            "empty representation should be unsatisfiable");
    require(unchanged.start == 77 && unchanged.length == 66,
            "failed parse changed output");

    const std::string invalid[] = {
        "bytes=", "bytes=-", "bytes=5-4", "bytes=a-4", "bytes=1-a",
        "bytes=1-2-3", "bytes=1 -2",
        "bytes=18446744073709551616-"
    };
    for (const auto& value : invalid)
        require(parse_byte_range(value, 10, unchanged) == ByteRangeResult::Invalid,
                "invalid byte range accepted");

    require(parse_byte_range("items=0-1", 10, unchanged) == ByteRangeResult::Ignore,
            "unknown range unit should be ignored");
    require(parse_byte_range("bytes=0-1, 4-5", 10, unchanged) ==
                ByteRangeResult::Ignore,
            "legal multiple ranges should be ignored");
    require(parse_byte_range("bytes=20-30, 40-50", 10, unchanged) ==
                ByteRangeResult::Ignore,
            "legal unsatisfied multiple ranges should be ignored");
    require(parse_byte_range("bytes=0-1, bad", 10, unchanged) ==
                ByteRangeResult::Invalid,
            "malformed multiple range accepted");

    constexpr std::string_view digest =
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad";
    ByteRange partial{2, 4};
    std::string wire;
    require(make_download_header(
                "upload-Abc123", 10, digest, &partial, wire),
            "partial header failed");
    require(wire ==
        "HTTP/1.1 206 Partial Content\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: 4\r\n"
        "Content-Disposition: attachment; filename=\"upload-Abc123.bin\"\r\n"
        "Accept-Ranges: bytes\r\n"
        "ETag: \"ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad\"\r\n"
        "Connection: close\r\n"
        "Content-Range: bytes 2-5/10\r\n\r\n",
        "partial header framing differs");

    partial = {9, 2};
    wire = "unchanged";
    require(!make_download_header(
                "upload-Abc123", 10, digest, &partial, wire) && wire == "unchanged",
            "out of bounds response range accepted");

    require(make_range_not_satisfiable_response(10, digest, false, wire),
            "416 response failed");
    require(wire.find("HTTP/1.1 416 Range Not Satisfiable\r\n") == 0 &&
            wire.find("Content-Range: bytes */10\r\n") != std::string::npos &&
            wire.find("range_not_satisfiable") != std::string::npos,
            "416 response fields differ");
    require(make_range_not_satisfiable_response(10, digest, true, wire) &&
            wire.substr(wire.find("\r\n\r\n") + 4).empty(),
            "HEAD 416 response included a body");

    RequestParser request;
    const std::string request_wire =
        "GET /api/files/upload-Abc123/content HTTP/1.1\r\n"
        "Host: localhost\r\nRange: bytes=2-5\r\nContent-Length: 0\r\n\r\n";
    const auto parsed = request.feed(request_wire);
    require(parsed.state == ParseState::Done && parsed.consumed == request_wire.size(),
            "request with Range did not parse");
    std::size_t range_headers = 0;
    for (const auto& field : request.headers())
        if (field.first == "range" && field.second == "bytes=2-5") ++range_headers;
    require(range_headers == 1, "normalized Range header was not retained");

    std::cout << "PASS: single byte ranges and 206 416 headers\n";
}
// ===== DAY 31 ADDED END =====
