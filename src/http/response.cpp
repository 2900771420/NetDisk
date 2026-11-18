#include <netdisk/http/response.h>

#include <netdisk/api/file_id.h>
#include <netdisk/crypto/sha256.h>

#include <cstddef>
#include <limits>
#include <utility>

// ===== DAY 33A ADDED BEGIN 将HTTP响应构造与解析实现移出头文件 =====
HttpResponse error_response(int status, std::string_view allow)
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

HttpResponse parser_error_response(RequestError error)
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

std::string build_response(const HttpResponse& response, bool head_only)
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
    if (!response.sha256.empty()) wire += "X-File-Sha256: " + response.sha256 + "\r\n";
    wire += "\r\n";
    if (!head_only) wire.append(response.body);
    return wire;
}

bool make_download_header(std::string_view id, std::uint64_t size,
                          std::string_view sha256, const ByteRange* range,
                          std::string& output)
{
    if (!is_valid_file_id(id) || !is_canonical_sha256(sha256)) return false;
    if (range != nullptr &&
        (range->length == 0 || range->start >= size ||
         range->length > size - range->start)) return false;

    const bool partial = range != nullptr;
    const std::uint64_t content_length = partial ? range->length : size;
    const std::string filename = std::string(id) + ".bin";
    std::string header = partial ? "HTTP/1.1 206 Partial Content\r\n"
                                 : "HTTP/1.1 200 OK\r\n";
    header +=
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: " + std::to_string(content_length) + "\r\n"
        "Content-Disposition: attachment; filename=\"" + filename + "\"\r\n"
        "Accept-Ranges: bytes\r\n"
        "ETag: \"" + std::string(sha256) + "\"\r\n"
        "Connection: close\r\n";
    if (partial)
    {
        const auto end = range->start + range->length - 1;
        header += "Content-Range: bytes " + std::to_string(range->start) + "-" +
                  std::to_string(end) + "/" + std::to_string(size) + "\r\n";
    }
    header += "\r\n";
    output = std::move(header);
    return true;
}

bool make_range_not_satisfiable_response(std::uint64_t size,
                                         std::string_view sha256,
                                         bool head_only,
                                         std::string& output)
{
    if (!is_canonical_sha256(sha256)) return false;
    constexpr std::string_view body =
        R"({"error":{"code":"range_not_satisfiable","message":"Range cannot be satisfied"}})";
    std::string response =
        "HTTP/1.1 416 Range Not Satisfiable\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Content-Range: bytes */" + std::to_string(size) + "\r\n"
        "Accept-Ranges: bytes\r\n"
        "ETag: \"" + std::string(sha256) + "\"\r\n"
        "Connection: close\r\n\r\n";
    if (!head_only) response.append(body.data(), body.size());
    output = std::move(response);
    return true;
}

namespace download_client
{
namespace
{
std::string_view trim_ows(std::string_view value) noexcept
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
        value.remove_suffix(1);
    return value;
}

bool parse_decimal(std::string_view text, std::uint64_t& output) noexcept
{
    if (text.empty()) return false;
    std::uint64_t value = 0;
    for (const char ch : text)
    {
        if (ch < '0' || ch > '9') return false;
        const auto digit = static_cast<std::uint64_t>(ch - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
            return false;
        value = value * 10 + digit;
    }
    output = value;
    return true;
}

bool ascii_equal_ignore_case(std::string_view left,
                             std::string_view right) noexcept
{
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        char a = left[index];
        char b = right[index];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

bool is_header_name_char(char ch) noexcept
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') || ch == '-';
}

bool parse_content_range(std::string_view value, ResponseHead& output) noexcept
{
    constexpr std::string_view unit = "bytes ";
    if (value.size() < unit.size() ||
        !ascii_equal_ignore_case(value.substr(0, unit.size()), unit))
        return false;
    value.remove_prefix(unit.size());

    if (value.size() > 2 && value[0] == '*' && value[1] == '/')
    {
        std::uint64_t total = 0;
        if (!parse_decimal(value.substr(2), total)) return false;
        output.content_range_kind = ContentRangeKind::Unsatisfied;
        output.total_size = total;
        return true;
    }

    const auto dash = value.find('-');
    const auto slash = value.find('/');
    if (dash == std::string_view::npos || slash == std::string_view::npos ||
        dash == 0 || slash <= dash + 1 || slash + 1 >= value.size() ||
        value.find('-', dash + 1) != std::string_view::npos ||
        value.find('/', slash + 1) != std::string_view::npos)
        return false;

    std::uint64_t start = 0;
    std::uint64_t end = 0;
    std::uint64_t total = 0;
    if (!parse_decimal(value.substr(0, dash), start) ||
        !parse_decimal(value.substr(dash + 1, slash - dash - 1), end) ||
        !parse_decimal(value.substr(slash + 1), total) ||
        start > end || total == 0 || end >= total)
        return false;

    output.content_range_kind = ContentRangeKind::Selected;
    output.range_start = start;
    output.range_end = end;
    output.total_size = total;
    return true;
}
}

bool parse_response_head(std::string_view wire, ResponseHead& output)
{
    if (wire.size() < 4 || wire.substr(wire.size() - 4) != "\r\n\r\n")
        return false;

    ResponseHead parsed;
    const auto status_end = wire.find("\r\n");
    if (status_end == std::string_view::npos) return false;
    const auto status_line = wire.substr(0, status_end);
    if (status_line.size() < 13 || status_line.substr(0, 9) != "HTTP/1.1 " ||
        status_line[9] < '0' || status_line[9] > '9' ||
        status_line[10] < '0' || status_line[10] > '9' ||
        status_line[11] < '0' || status_line[11] > '9' ||
        status_line[12] != ' ')
        return false;
    parsed.status = (status_line[9] - '0') * 100 +
                    (status_line[10] - '0') * 10 +
                    (status_line[11] - '0');

    bool has_etag = false;
    bool has_content_range = false;
    std::size_t cursor = status_end + 2;
    while (true)
    {
        const auto line_end = wire.find("\r\n", cursor);
        if (line_end == std::string_view::npos) return false;
        if (line_end == cursor)
        {
            if (line_end + 2 != wire.size()) return false;
            break;
        }

        const auto line = wire.substr(cursor, line_end - cursor);
        const auto colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) return false;
        const auto name = line.substr(0, colon);
        for (const char ch : name)
            if (!is_header_name_char(ch)) return false;
        auto value = trim_ows(line.substr(colon + 1));
        for (const char ch : value)
            if ((static_cast<unsigned char>(ch) < 32 && ch != '\t') || ch == 127)
                return false;

        if (ascii_equal_ignore_case(name, "Content-Length"))
        {
            if (parsed.has_content_length ||
                !parse_decimal(value, parsed.content_length))
                return false;
            parsed.has_content_length = true;
        }
        else if (ascii_equal_ignore_case(name, "ETag"))
        {
            if (has_etag) return false;
            parsed.etag.assign(value.data(), value.size());
            has_etag = true;
        }
        else if (ascii_equal_ignore_case(name, "Content-Range"))
        {
            if (has_content_range || !parse_content_range(value, parsed))
                return false;
            has_content_range = true;
        }
        else if (ascii_equal_ignore_case(name, "Transfer-Encoding"))
        {
            return false;
        }
        cursor = line_end + 2;
    }

    output = std::move(parsed);
    return true;
}

bool is_strong_sha256_etag(std::string_view etag) noexcept
{
    return etag.size() == 66 && etag.front() == '"' && etag.back() == '"' &&
           is_canonical_sha256(etag.substr(1, 64));
}
}
// ===== DAY 33A ADDED END =====
