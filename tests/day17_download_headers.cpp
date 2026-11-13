#include "download_headers.h"

#include <cstdlib>
#include <iostream>
#include <limits>
// ===== DAY 30 ADDED BEGIN 显式声明摘要视图依赖 =====
#include <string_view>
// ===== DAY 30 ADDED END =====

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
    // ===== DAY 30 ADDED BEGIN 下载头测试使用规范持久摘要 =====
    constexpr std::string_view digest =
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad";
    // ===== DAY 30 ADDED END =====
    std::string header;
    // ===== DAY 30 MODIFIED BEGIN 精确验证强ETag响应头 =====
    require(make_download_header("upload-Abc123", 5, digest, header), "build header");
    require(header ==
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: 5\r\n"
        "Content-Disposition: attachment; filename=\"upload-Abc123.bin\"\r\n"
        "ETag: \"ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad\"\r\n"
        "Connection: close\r\n\r\n", "exact header framing and filename");
    require(make_download_header("upload-Abc123", 0, digest, header) &&
            header.find("Content-Length: 0\r\n") != std::string::npos, "empty file");
    require(make_download_header("upload-Abc123",
                std::numeric_limits<std::uint64_t>::max(), digest, header) &&
            header.find("Content-Length: 18446744073709551615\r\n") != std::string::npos,
            "64 bit length formatting");
    const std::string invalid[] = {
        "", "../upload-Abc123", "upload-Abc12\"", "upload-Abc123\r\nX-Test: yes",
        "upload-Abc123?name=x", std::string("upload-Ab\0xyz", 13)
    };
    for (const auto& id : invalid)
    {
        header = "unchanged";
        require(!make_download_header(id, 5, digest, header) && header == "unchanged",
                "unsafe ID accepted or output changed");
    }
    for (const std::string invalid_digest :
         {std::string(), std::string(63, 'a'), std::string(64, 'A'),
          std::string(64, 'g'), std::string(63, 'a') + "\r"})
    {
        header = "unchanged";
        require(!make_download_header("upload-Abc123", 5, invalid_digest, header) &&
                header == "unchanged", "unsafe digest accepted or output changed");
    }
    // ===== DAY 30 MODIFIED END =====
    std::cout << "PASS: download headers include a canonical strong ETag\n";
}
