#pragma once

#include <netdisk/http/request.h>
#include <netdisk/http/response.h>
#include <netdisk/metadata/file_metadata_store.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>

// ===== DAY 33A ADDED BEGIN 提取文件目录请求处理接口 =====
bool valid_display_name(std::string_view name);
nlohmann::json make_file_json(std::string_view id, std::string_view name,
                              std::uintmax_t size);
nlohmann::json make_file_json(const FileMetadata& file);
bool stored_object_is_regular(std::string_view id);
bool parse_nonnegative_int64(std::string_view text, std::int64_t& output);
bool parse_file_page(std::string_view target, FilePageRequest& page);
std::string make_file_cursor(const FileMetadata& file);
nlohmann::json list_file_objects(FileMetadataStore& store,
                                 const FilePageRequest& page);
nlohmann::json list_file_objects(FileMetadataStore& store);
bool is_file_catalog_request(const RequestLine& line);
HttpResponse file_catalog_response(const RequestParser& request,
                                   FileMetadataStore& store);
// ===== DAY 33A ADDED END =====
