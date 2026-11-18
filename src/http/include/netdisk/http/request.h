// ===== DAY 33A ADDED BEGIN 合并HTTP请求行 请求头与正文解析接口 =====
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

enum class ParseState
{
    NeedMore,
    Done,
    Error
};

struct RequestLine
{
    std::string method;
    std::string target;
    std::string version;
};

struct FeedResult
{
    ParseState state;
    std::size_t consumed;
};

class RequestLineParser
{
public:
    FeedResult feed(std::string_view input);
    const RequestLine& line() const noexcept;

private:
    static constexpr std::size_t max_line_bytes = 4096;
    static bool is_token_char(unsigned char ch);
    bool parse_line();

    ParseState state_ = ParseState::NeedMore;
    std::size_t bytes_ = 0;
    bool saw_cr_ = false;
    std::string buffer_;
    RequestLine line_;
};

class RequestHeadersParser
{
public:
    using Field = std::pair<std::string, std::string>;

    FeedResult feed(std::string_view input);
    const std::vector<Field>& fields() const noexcept;

private:
    static bool is_token(unsigned char ch);
    bool parse_field();

    ParseState state_ = ParseState::NeedMore;
    std::size_t bytes_ = 0;
    std::size_t line_bytes_ = 0;
    bool saw_cr_ = false;
    std::string buffer_;
    std::vector<Field> fields_;
};

bool parse_decimal_length(std::string_view text, std::uint64_t& output);

enum class RequestError
{
    None,
    BadRequest,
    BodyTooLarge,
    UnsupportedExpectation,
    UnsupportedTransferEncoding,
    BodyWriteFailed
};

enum class BodyAction
{
    Buffer,
    Written,
    Failed
};

using BodySink = std::function<BodyAction(const RequestLine&, std::string_view)>;

class RequestParser
{
public:
    static constexpr std::size_t max_body_bytes = 64 * 1024;

    explicit RequestParser(BodySink sink = {});

    FeedResult feed(std::string_view input);
    ParseState finish() noexcept;
    RequestError error() const noexcept;
    const RequestLine& line() const noexcept;
    const std::string& body() const noexcept;
    const std::vector<RequestHeadersParser::Field>& headers() const noexcept;
    std::size_t body_size() const noexcept;

private:
    RequestError validate_headers();

    ParseState state_ = ParseState::NeedMore;
    RequestError error_ = RequestError::None;
    RequestLineParser line_;
    RequestHeadersParser headers_;
    bool line_done_ = false;
    bool head_done_ = false;
    std::size_t length_ = 0;
    std::string body_;
    std::size_t received_ = 0;
    BodySink sink_;
};
// ===== DAY 33A ADDED END =====
