#pragma once

#include <netdisk/http/request.h>
#include <netdisk/http/response.h>

#include <string_view>

class FileMetadataStore;

// ===== DAY 33A ADDED BEGIN 提取服务端路由与连接处理接口 =====
HttpResponse route_request(const RequestParser& request,
                           std::string_view published_id = {},
                           std::string_view published_sha256 = {});
bool is_upload(const RequestLine& line);
bool handle_client(int client_fd, FileMetadataStore& metadata);
// ===== DAY 33A ADDED END =====
