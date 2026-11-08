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
