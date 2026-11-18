// ===== DAY 33 ADDED BEGIN 验证断点客户端只按已校验响应状态写文件 =====
// ===== DAY 33A MODIFIED BEGIN 客户端只依赖client与HTTP公开接口 =====
#include <netdisk/client/download_protocol.h>
#include <netdisk/http/response.h>
// ===== DAY 33A MODIFIED END =====

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace
{

void require(bool ok, const char* message)
{
    if (!ok)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::string make_head(int status, std::string_view reason,
                      std::uint64_t length, std::string_view etag,
                      std::string_view content_range = {})
{
    std::string wire = "HTTP/1.1 " + std::to_string(status) + " " +
                       std::string(reason) + "\r\n";
    wire += "Content-Length: " + std::to_string(length) + "\r\n";
    wire += "ETag: " + std::string(etag) + "\r\n";
    if (!content_range.empty())
        wire += "Content-Range: " + std::string(content_range) + "\r\n";
    wire += "Connection: close\r\n\r\n";
    return wire;
}

}

int main()
{
    constexpr std::string_view etag =
        "\"ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad\"";
    constexpr std::string_view other_etag =
        "\"aa7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad\"";

    std::string request;
    require(download_client::make_download_request(
                "upload-Abc123", false, 0, {}, request),
            "fresh request was not built");
    require(request.find("Range:") == std::string::npos &&
            request.find("If-Range:") == std::string::npos,
            "fresh request unexpectedly contains resume fields");
    require(download_client::make_download_request(
                "upload-Abc123", true, 4, etag, request),
            "resume request was not built");
    require(request.find("Range: bytes=4-\r\n") != std::string::npos &&
            request.find("If-Range: " + std::string(etag) + "\r\n") !=
                std::string::npos,
            "resume request fields differ");
    require(!download_client::make_download_request(
                "../object", true, 4, etag, request),
            "unsafe file ID was accepted");

    download_client::ResponseHead response;
    require(download_client::parse_response_head(
                make_head(206, "Partial Content", 6, etag,
                          "bytes 4-9/10"), response),
            "valid 206 response did not parse");
    auto plan = download_client::make_resume_plan(response, 4, etag);
    require(plan.action == download_client::ResumeAction::Append &&
            plan.total_size == 10,
            "matching 206 did not choose append");
    require(download_client::make_resume_plan(response, 4, other_etag).action ==
                download_client::ResumeAction::Reject,
            "206 with a different saved ETag was accepted");
    require(download_client::make_resume_plan(response, 3, etag).action ==
                download_client::ResumeAction::Reject,
            "206 with a different local offset was accepted");

    require(download_client::parse_response_head(
                make_head(200, "OK", 10, other_etag), response),
            "valid 200 response did not parse");
    plan = download_client::make_resume_plan(response, 4, etag);
    require(plan.action == download_client::ResumeAction::Replace &&
            plan.total_size == 10,
            "200 did not choose full replacement");

    require(download_client::parse_response_head(
                make_head(416, "Range Not Satisfiable", 80, etag,
                          "bytes */10"), response),
            "valid 416 response did not parse");
    plan = download_client::make_resume_plan(response, 10, etag);
    require(plan.action == download_client::ResumeAction::Complete &&
            plan.total_size == 10,
            "matching 416 did not recognize a complete local file");
    require(download_client::make_resume_plan(response, 9, etag).action ==
                download_client::ResumeAction::Reject,
            "416 with an incomplete local file was accepted");

    const std::string duplicate_length =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 10\r\n"
        "Content-Length: 10\r\n"
        "ETag: " + std::string(etag) + "\r\n\r\n";
    require(!download_client::parse_response_head(duplicate_length, response),
            "duplicate Content-Length was accepted");
    const std::string transfer_encoded =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 10\r\n"
        "Transfer-Encoding: chunked\r\n"
        "ETag: " + std::string(etag) + "\r\n\r\n";
    require(!download_client::parse_response_head(transfer_encoded, response),
            "unsupported Transfer-Encoding was accepted");
    require(!download_client::parse_response_head(
                make_head(206, "Partial Content", 6, etag,
                          "bytes 4-10/10"), response),
            "out of bounds Content-Range was accepted");
    require(download_client::parse_response_head(
                make_head(206, "Partial Content", 5, etag,
                          "bytes 4-9/10"), response) &&
            download_client::make_resume_plan(response, 4, etag).action ==
                download_client::ResumeAction::Reject,
            "Content-Length and Content-Range mismatch was accepted");

    std::cout << "PASS: resume client separates 206 append 200 replace and 416 complete\n";
}
// ===== DAY 33 ADDED END =====
