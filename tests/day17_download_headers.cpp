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
