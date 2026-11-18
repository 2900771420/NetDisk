#include <netdisk/http/range.h>

#include <netdisk/crypto/sha256.h>

#include <algorithm>
#include <limits>

// ===== DAY 33A ADDED BEGIN 将范围解析与If Range实现移出头文件 =====
std::string_view trim_range_ows(std::string_view text) noexcept
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    return text;
}

bool parse_range_number(std::string_view text, std::uint64_t& output) noexcept
{
    if (text.empty()) return false;
    std::uint64_t value = 0;
    for (const char ch : text)
    {
        if (ch < '0' || ch > '9') return false;
        const auto digit = static_cast<std::uint64_t>(ch - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
            return false;
        value = value * 10 + digit;
    }
    output = value;
    return true;
}

ByteRangeResult parse_one_byte_range(std::string_view text,
                                     std::uint64_t full_size,
                                     ByteRange& output) noexcept
{
    const auto dash = text.find('-');
    if (dash == std::string_view::npos ||
        text.find('-', dash + 1) != std::string_view::npos)
        return ByteRangeResult::Invalid;

    const auto first_text = text.substr(0, dash);
    const auto last_text = text.substr(dash + 1);
    if (first_text.empty())
    {
        std::uint64_t suffix_length = 0;
        if (!parse_range_number(last_text, suffix_length))
            return ByteRangeResult::Invalid;
        if (suffix_length == 0 || full_size == 0)
            return ByteRangeResult::Unsatisfiable;
        ByteRange parsed;
        parsed.length = std::min(suffix_length, full_size);
        parsed.start = full_size - parsed.length;
        output = parsed;
        return ByteRangeResult::Satisfiable;
    }

    std::uint64_t first = 0;
    if (!parse_range_number(first_text, first))
        return ByteRangeResult::Invalid;
    if (last_text.empty())
    {
        if (first >= full_size) return ByteRangeResult::Unsatisfiable;
        output = {first, full_size - first};
        return ByteRangeResult::Satisfiable;
    }

    std::uint64_t last = 0;
    if (!parse_range_number(last_text, last) || last < first)
        return ByteRangeResult::Invalid;
    if (first >= full_size) return ByteRangeResult::Unsatisfiable;
    last = std::min(last, full_size - 1);
    output = {first, last - first + 1};
    return ByteRangeResult::Satisfiable;
}

ByteRangeResult parse_byte_range(std::string_view value,
                                 std::uint64_t full_size,
                                 ByteRange& output) noexcept
{
    constexpr std::size_t unit_size = 6;
    if (value.size() < unit_size || value[5] != '=' ||
        !((value[0] == 'b' || value[0] == 'B') &&
          (value[1] == 'y' || value[1] == 'Y') &&
          (value[2] == 't' || value[2] == 'T') &&
          (value[3] == 'e' || value[3] == 'E') &&
          (value[4] == 's' || value[4] == 'S')))
        return ByteRangeResult::Ignore;

    value.remove_prefix(unit_size);
    std::size_t item_count = 0;
    ByteRange parsed;
    ByteRangeResult single_result = ByteRangeResult::Invalid;
    while (true)
    {
        const auto comma = value.find(',');
        const auto item = trim_range_ows(value.substr(0, comma));
        if (item.empty()) return ByteRangeResult::Invalid;
        ByteRange current;
        const auto result = parse_one_byte_range(item, full_size, current);
        if (result == ByteRangeResult::Invalid) return result;
        ++item_count;
        if (item_count == 1)
        {
            parsed = current;
            single_result = result;
        }
        if (comma == std::string_view::npos) break;
        value.remove_prefix(comma + 1);
    }

    if (item_count != 1) return ByteRangeResult::Ignore;
    if (single_result == ByteRangeResult::Satisfiable) output = parsed;
    return single_result;
}

bool if_range_matches_strong_etag(std::string_view value,
                                  std::string_view sha256) noexcept
{
    if (!is_canonical_sha256(sha256) || value.size() != sha256.size() + 2)
        return false;
    return value.front() == '"' && value.back() == '"' &&
           value.substr(1, sha256.size()) == sha256;
}
// ===== DAY 33A ADDED END =====
