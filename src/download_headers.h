#pragma once

#include "object_path.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

// ===== DAY 30 ADDED BEGIN 强ETag只接受规范SHA256避免响应头注入 =====
inline bool is_canonical_sha256(std::string_view value) noexcept
{
    if (value.size() != 64) return false;
    for (const char ch : value)
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    return true;
}
// ===== DAY 30 ADDED END =====

// ===== DAY 30 MODIFIED BEGIN 下载响应使用持久化内容摘要构造强ETag =====
inline bool make_download_header(std::string_view id, std::uint64_t size,
                                  std::string_view sha256, std::string& output)
{
    if (!is_valid_file_id(id) || !is_canonical_sha256(sha256)) return false;
    const std::string filename = std::string(id) + ".bin";
    std::string header =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: " + std::to_string(size) + "\r\n"
        "Content-Disposition: attachment; filename=\"" + filename + "\"\r\n"
        "ETag: \"" + std::string(sha256) + "\"\r\n"
        "Connection: close\r\n\r\n";
    output = std::move(header);
    return true;
}
// ===== DAY 30 MODIFIED END =====
