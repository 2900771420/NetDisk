#include <netdisk/storage/object_path.h>

#include <netdisk/api/file_id.h>

#include <utility>

// ===== DAY 33A ADDED BEGIN 将对象路径构造实现移出头文件 =====
bool make_object_path(std::string_view id, std::string& output)
{
    if (!is_valid_file_id(id)) return false;
    std::string path = "data/objects/";
    path.append(id.data(), id.size());
    output = std::move(path);
    return true;
}
// ===== DAY 33A ADDED END =====
