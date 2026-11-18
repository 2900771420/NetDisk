#pragma once

#include <cstdint>
#include <string_view>

// ===== DAY 33A ADDED BEGIN 合并字节范围与If Range公共接口 =====
struct ByteRange
{
    std::uint64_t start = 0;
    std::uint64_t length = 0;
};

enum class ByteRangeResult
{
    Satisfiable,
    Unsatisfiable,
    Ignore,
    Invalid
};

std::string_view trim_range_ows(std::string_view text) noexcept;
bool parse_range_number(std::string_view text, std::uint64_t& output) noexcept;
ByteRangeResult parse_one_byte_range(std::string_view text,
                                     std::uint64_t full_size,
                                     ByteRange& output) noexcept;
ByteRangeResult parse_byte_range(std::string_view value,
                                 std::uint64_t full_size,
                                 ByteRange& output) noexcept;
bool if_range_matches_strong_etag(std::string_view value,
                                  std::string_view sha256) noexcept;
// ===== DAY 33A ADDED END =====
