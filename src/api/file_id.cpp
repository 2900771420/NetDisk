// ===== DAY 33A ADDED BEGIN 实现跨端文件标识规则 =====
#include <netdisk/api/file_id.h>

bool is_valid_file_id(std::string_view id) noexcept
{
    constexpr std::string_view prefix = "upload-";
    if (id.size() != prefix.size() + 6 ||
        id.substr(0, prefix.size()) != prefix) return false;
    for (const char ch : id.substr(prefix.size()))
    {
        const bool alnum = (ch >= 'a' && ch <= 'z') ||
                           (ch >= 'A' && ch <= 'Z') ||
                           (ch >= '0' && ch <= '9');
        if (!alnum) return false;
    }
    return true;
}
// ===== DAY 33A ADDED END =====
