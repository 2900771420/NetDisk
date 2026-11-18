#pragma once

#include <netdisk/http/response.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace download_client
{

enum class ResumeAction
{
    Reject,
    Replace,
    Append,
    Complete
};

struct ResumePlan
{
    ResumeAction action = ResumeAction::Reject;
    std::uint64_t total_size = 0;
};

ResumePlan make_resume_plan(const ResponseHead& response,
                            std::uint64_t local_size,
                            std::string_view saved_etag) noexcept;
bool make_download_request(std::string_view id, bool resume,
                           std::uint64_t local_size,
                           std::string_view saved_etag,
                           std::string& output);

// ===== DAY 34 ADDED BEGIN 请求Host跟随可配置服务端 =====
bool make_download_request(std::string_view id, bool resume,
                           std::uint64_t local_size,
                           std::string_view saved_etag,
                           std::string_view authority,
                           std::string& output);
// ===== DAY 34 ADDED END =====

}
