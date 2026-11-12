#pragma once

#include "request_parser.h"

struct HttpResponse
{
    int status;
    std::string_view reason;
    std::string_view content_type;
    std::string body;
    std::string_view allow = {};
    std::string file_id = {};
    // ===== DAY 29 ADDED BEGIN 上传成功响应携带内容摘要 =====
    std::string sha256 = {};
    // ===== DAY 29 ADDED END =====
};

inline HttpResponse error_response(int status, std::string_view allow = {})
{
    switch (status)
    {
    case 400:
        return {400, "Bad Request", "application/json",
            R"({"error":{"code":"bad_request","message":"Invalid request"}})"};
    case 404:
        return {404, "Not Found", "application/json",
            R"({"error":{"code":"not_found","message":"Route not found"}})"};
    case 405:
        return {405, "Method Not Allowed", "application/json",
            R"({"error":{"code":"method_not_allowed","message":"Method not allowed"}})", allow};
    case 413:
        return {413, "Content Too Large", "application/json",
            R"({"error":{"code":"body_too_large","message":"Body exceeds 65536 bytes"}})"};
    case 417:
        return {417, "Expectation Failed", "application/json",
            R"({"error":{"code":"expectation_failed","message":"Expect is not supported"}})"};
    case 501:
        return {501, "Not Implemented", "application/json",
            R"({"error":{"code":"not_implemented","message":"Requested feature is not implemented"}})"};
    default:
        return {500, "Internal Server Error", "application/json",
            R"({"error":{"code":"internal_error","message":"Internal server error"}})"};
    }
}

inline HttpResponse parser_error_response(RequestError error)
{
    switch (error)
    {
    case RequestError::BodyTooLarge: return error_response(413);
    case RequestError::UnsupportedExpectation: return error_response(417);
    case RequestError::UnsupportedTransferEncoding: return error_response(501);
    case RequestError::BodyWriteFailed: return error_response(500);
    default: return error_response(400);
    }
}

// ===== DAY 29 MODIFIED BEGIN 正式上传必须同时提供文件ID和SHA256 =====
inline HttpResponse route_request(const RequestParser& request,
                                  std::string_view published_id = {},
                                  std::string_view published_sha256 = {})
{
    const auto& line = request.line();
    if (line.method != "GET" && line.method != "HEAD" && line.method != "POST" &&
        line.method != "PATCH" && line.method != "DELETE")
    {
        return error_response(501);
    }
    const std::string_view target(line.target);
    const auto path = target.substr(0, target.find('?'));
    if (path == "/health")
    {
        if (line.method != "GET" && line.method != "HEAD")
        {
            return error_response(405, "GET, HEAD");
        }
        if (request.body_size() != 0) return error_response(400);
        return {200, "OK", "text/plain; charset=utf-8", "OK\n"};
    }
    if (path == "/echo")
    {
        if (line.method != "POST") return error_response(405, "POST");
        return {200, "OK", "application/octet-stream", request.body()};
    }
    if (path == "/dev/upload")
    {
        if (line.method != "POST") return error_response(405, "POST");
        return {200, "OK", "application/json", R"({"status":"temporary_received"})"};
    }
    if (path == "/api/files")
    {
        if (line.method != "POST") return error_response(405, "POST");
        if (published_id.empty() || published_sha256.empty()) return error_response(500);
        HttpResponse response{201, "Created", "application/json", R"({"status":"created"})"};
        response.file_id = published_id;
        response.sha256 = published_sha256;
        return response;
    }
    return error_response(404);
}
// ===== DAY 29 MODIFIED END =====

inline std::string build_response(const HttpResponse& response, bool head_only = false)
{
    if (response.status == 204)
        return "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n";
    std::string wire = "HTTP/1.1 " + std::to_string(response.status) + " " +
        std::string(response.reason) + "\r\n";
    wire += "Content-Type: " + std::string(response.content_type) + "\r\n";
    wire += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
    wire += "Connection: close\r\n";
    if (!response.allow.empty()) wire += "Allow: " + std::string(response.allow) + "\r\n";
    if (!response.file_id.empty()) wire += "X-File-Id: " + response.file_id + "\r\n";
    // ===== DAY 29 ADDED BEGIN 序列化服务端计算的摘要响应头 =====
    if (!response.sha256.empty()) wire += "X-File-Sha256: " + response.sha256 + "\r\n";
    // ===== DAY 29 ADDED END =====
    wire += "\r\n";
    if (!head_only) wire.append(response.body);
    return wire;
}
