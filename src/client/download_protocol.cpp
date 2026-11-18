#include <netdisk/client/download_protocol.h>

#include <netdisk/api/file_id.h>

#include <utility>

namespace download_client
{
namespace
{

// ===== DAY 34 ADDED BEGIN 拒绝会改变Host请求头边界的字符 =====
bool is_valid_authority(std::string_view authority) noexcept
{
    if (authority.empty() || authority.size() > 255) return false;
    for (const unsigned char c : authority)
    {
        if (c < 0x21 || c > 0x7e || c == '/' || c == '?' || c == '#')
            return false;
    }
    return true;
}
// ===== DAY 34 ADDED END =====

}

ResumePlan make_resume_plan(const ResponseHead& response,
                            std::uint64_t local_size,
                            std::string_view saved_etag) noexcept
{
    ResumePlan plan;
    if (!response.has_content_length || !is_strong_sha256_etag(response.etag))
        return plan;
    if (response.status == 200)
    {
        if (response.content_range_kind != ContentRangeKind::None) return plan;
        plan.action = ResumeAction::Replace;
        plan.total_size = response.content_length;
        return plan;
    }
    if (!is_strong_sha256_etag(saved_etag) || response.etag != saved_etag)
        return plan;
    if (response.status == 206)
    {
        if (response.content_range_kind != ContentRangeKind::Selected ||
            response.range_start != local_size || response.total_size == 0 ||
            local_size >= response.total_size ||
            response.range_end != response.total_size - 1 ||
            response.content_length != response.total_size - local_size)
            return plan;
        plan.action = ResumeAction::Append;
        plan.total_size = response.total_size;
        return plan;
    }
    if (response.status == 416 &&
        response.content_range_kind == ContentRangeKind::Unsatisfied &&
        local_size == response.total_size)
    {
        plan.action = ResumeAction::Complete;
        plan.total_size = response.total_size;
    }
    return plan;
}

// ===== DAY 34 MODIFIED BEGIN 旧接口保持默认Host兼容行为 =====
bool make_download_request(std::string_view id, bool resume,
                           std::uint64_t local_size,
                           std::string_view saved_etag,
                           std::string& output)
{
    return make_download_request(id, resume, local_size, saved_etag,
                                 "127.0.0.1:8080", output);
}
// ===== DAY 34 MODIFIED END =====

// ===== DAY 34 ADDED BEGIN 使用已校验的可配置Host构造请求 =====
bool make_download_request(std::string_view id, bool resume,
                           std::uint64_t local_size,
                           std::string_view saved_etag,
                           std::string_view authority,
                           std::string& output)
{
    if (!is_valid_file_id(id) || !is_valid_authority(authority)) return false;
    if (resume && (local_size == 0 || !is_strong_sha256_etag(saved_etag)))
        return false;
    std::string request =
        "GET /api/files/" + std::string(id) + "/content HTTP/1.1\r\n"
        "Host: " + std::string(authority) + "\r\n";
    if (resume)
    {
        request += "Range: bytes=" + std::to_string(local_size) + "-\r\n";
        request += "If-Range: " + std::string(saved_etag) + "\r\n";
    }
    request += "Content-Length: 0\r\nConnection: close\r\n\r\n";
    output = std::move(request);
    return true;
}
// ===== DAY 34 ADDED END =====
}
