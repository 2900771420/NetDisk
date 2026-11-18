#pragma once

#include <netdisk/http/range.h>
#include <netdisk/http/request.h>

#include <cstdint>
#include <string>
#include <string_view>

// ===== DAY 33A ADDED BEGIN 汇总HTTP响应与客户端下载响应头稳定接口 =====
struct HttpResponse
{
    int status;
    std::string_view reason;
    std::string_view content_type;
    std::string body;
    std::string_view allow = {};
    std::string file_id = {};
    std::string sha256 = {};
};

HttpResponse error_response(int status, std::string_view allow = {});
HttpResponse parser_error_response(RequestError error);
std::string build_response(const HttpResponse& response, bool head_only = false);

bool make_download_header(std::string_view id, std::uint64_t size,
                          std::string_view sha256, const ByteRange* range,
                          std::string& output);
bool make_range_not_satisfiable_response(std::uint64_t size,
                                         std::string_view sha256,
                                         bool head_only,
                                         std::string& output);

namespace download_client
{

enum class ContentRangeKind
{
    None,
    Selected,
    Unsatisfied
};

struct ResponseHead
{
    int status = 0;
    bool has_content_length = false;
    std::uint64_t content_length = 0;
    std::string etag;
    ContentRangeKind content_range_kind = ContentRangeKind::None;
    std::uint64_t range_start = 0;
    std::uint64_t range_end = 0;
    std::uint64_t total_size = 0;
};

bool parse_response_head(std::string_view wire, ResponseHead& output);
bool is_strong_sha256_etag(std::string_view etag) noexcept;

}
// ===== DAY 33A ADDED END =====
