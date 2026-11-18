#pragma once

#include <netdisk/http/request.h>

#include <string_view>

class FileMetadataStore;

// ===== DAY 33A ADDED BEGIN 提取文件内容下载处理接口 =====
bool match_file_content(std::string_view target, std::string_view& id);
bool send_file_content(int client_fd, const RequestParser& request,
                       std::string_view id,
                       FileMetadataStore& metadata_store);
// ===== DAY 33A ADDED END =====
