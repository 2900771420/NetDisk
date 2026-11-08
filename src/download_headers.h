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
