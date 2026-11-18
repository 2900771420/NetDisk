// ===== DAY 33A ADDED BEGIN 实现统一HTTP请求增量解析状态机 =====
#include <netdisk/http/request.h>

#include <algorithm>
#include <limits>

FeedResult RequestLineParser::feed(std::string_view input)
{
    std::size_t used = 0;
    while (used < input.size() && state_ == ParseState::NeedMore)
    {
        const unsigned char ch = static_cast<unsigned char>(input[used++]);
        if (++bytes_ > max_line_bytes)
        {
            state_ = ParseState::Error;
            break;
        }
        if (saw_cr_)
        {
            state_ = ch == '\n' && parse_line()
                ? ParseState::Done : ParseState::Error;
            break;
        }
        if (ch == '\r')
            saw_cr_ = true;
        else if (ch < 32 || ch > 126)
            state_ = ParseState::Error;
        else
            buffer_.push_back(static_cast<char>(ch));
    }
    return {state_, used};
}

const RequestLine& RequestLineParser::line() const noexcept
{
    return line_;
}

bool RequestLineParser::is_token_char(unsigned char ch)
{
    return (ch >= 'A' && ch <= 'Z') ||
           (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9') ||
           std::string_view("!#$%&'*+-.^_`|~").find(
               static_cast<char>(ch)) != std::string_view::npos;
}

bool RequestLineParser::parse_line()
{
    const std::size_t first = buffer_.find(' ');
    if (first == std::string::npos || first == 0) return false;
    const std::size_t second = buffer_.find(' ', first + 1);
    if (second == std::string::npos || second == first + 1 ||
        buffer_.find(' ', second + 1) != std::string::npos)
        return false;

    RequestLine candidate{
        buffer_.substr(0, first),
        buffer_.substr(first + 1, second - first - 1),
        buffer_.substr(second + 1)
    };
    for (const unsigned char ch : candidate.method)
        if (!is_token_char(ch)) return false;
    if (candidate.target.front() != '/' ||
        candidate.target.find('#') != std::string::npos ||
        candidate.target.find('\\') != std::string::npos ||
        candidate.version != "HTTP/1.1")
        return false;
    line_ = std::move(candidate);
    return true;
}

FeedResult RequestHeadersParser::feed(std::string_view input)
{
    std::size_t used = 0;
    while (used < input.size() && state_ == ParseState::NeedMore)
    {
        const unsigned char ch = static_cast<unsigned char>(input[used++]);
        if (++bytes_ > 8192 || ++line_bytes_ > 4096)
        {
            state_ = ParseState::Error;
            break;
        }
        if (saw_cr_)
        {
            if (ch != '\n')
                state_ = ParseState::Error;
            else if (buffer_.empty())
                state_ = ParseState::Done;
            else if (!parse_field())
                state_ = ParseState::Error;
            buffer_.clear();
            saw_cr_ = false;
            line_bytes_ = 0;
        }
        else if (ch == '\r')
            saw_cr_ = true;
        else if ((ch < 32 && ch != '\t') || ch == 127)
            state_ = ParseState::Error;
        else
            buffer_.push_back(static_cast<char>(ch));
    }
    return {state_, used};
}

const std::vector<RequestHeadersParser::Field>&
RequestHeadersParser::fields() const noexcept
{
    return fields_;
}

bool RequestHeadersParser::is_token(unsigned char ch)
{
    return (ch >= 'A' && ch <= 'Z') ||
           (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9') ||
           std::string_view("!#$%&'*+-.^_`|~").find(
               static_cast<char>(ch)) != std::string_view::npos;
}

bool RequestHeadersParser::parse_field()
{
    const auto colon = buffer_.find(':');
    if (colon == std::string::npos || colon == 0 || fields_.size() >= 100)
        return false;
    std::string name = buffer_.substr(0, colon);
    for (char& ch : name)
    {
        if (!is_token(static_cast<unsigned char>(ch))) return false;
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    const auto begin = buffer_.find_first_not_of(" \t", colon + 1);
    const auto end = buffer_.find_last_not_of(" \t");
    const std::string value = begin == std::string::npos
        ? std::string{} : buffer_.substr(begin, end - begin + 1);
    fields_.emplace_back(std::move(name), value);
    return true;
}

bool parse_decimal_length(std::string_view text, std::uint64_t& output)
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

RequestParser::RequestParser(BodySink sink) : sink_(std::move(sink))
{
}

FeedResult RequestParser::feed(std::string_view input)
{
    if (state_ != ParseState::NeedMore) return {state_, 0};
    std::size_t used = 0;
    if (!line_done_)
    {
        const auto result = line_.feed(input);
        used += result.consumed;
        if (result.state == ParseState::Error)
        {
            error_ = RequestError::BadRequest;
            state_ = ParseState::Error;
            return {state_, used};
        }
        line_done_ = result.state == ParseState::Done;
    }
    if (line_done_ && !head_done_)
    {
        const auto result = headers_.feed(input.substr(used));
        used += result.consumed;
        if (result.state == ParseState::Error)
            error_ = RequestError::BadRequest;
        else if (result.state == ParseState::Done)
            error_ = validate_headers();
        if (error_ != RequestError::None)
        {
            state_ = ParseState::Error;
            return {state_, used};
        }
        head_done_ = result.state == ParseState::Done;
    }
    if (head_done_)
    {
        const auto count = std::min(input.size() - used, length_ - received_);
        if (count != 0 || length_ == 0)
        {
            const auto chunk = input.substr(used, count);
            const auto action = sink_ ? sink_(line_.line(), chunk) : BodyAction::Buffer;
            if (action == BodyAction::Failed)
            {
                error_ = RequestError::BodyWriteFailed;
                state_ = ParseState::Error;
                return {state_, used};
            }
            if (action == BodyAction::Buffer && count != 0)
                body_.append(chunk.data(), chunk.size());
        }
        received_ += count;
        used += count;
        if (received_ == length_) state_ = ParseState::Done;
    }
    return {state_, used};
}

ParseState RequestParser::finish() noexcept
{
    if (state_ == ParseState::NeedMore)
    {
        state_ = ParseState::Error;
        error_ = RequestError::BadRequest;
    }
    return state_;
}

RequestError RequestParser::error() const noexcept { return error_; }
const RequestLine& RequestParser::line() const noexcept { return line_.line(); }
const std::string& RequestParser::body() const noexcept { return body_; }

const std::vector<RequestHeadersParser::Field>&
RequestParser::headers() const noexcept
{
    return headers_.fields();
}

std::size_t RequestParser::body_size() const noexcept { return received_; }

RequestError RequestParser::validate_headers()
{
    std::size_t hosts = 0;
    bool has_length = false;
    bool has_encoding = false;
    bool has_expectation = false;
    std::uint64_t value = 0;
    for (const auto& field : headers_.fields())
    {
        if (field.first == "host")
        {
            ++hosts;
            if (field.second.empty()) return RequestError::BadRequest;
        }
        if (field.first == "transfer-encoding") has_encoding = true;
        if (field.first == "expect") has_expectation = true;
        if (field.first == "content-length")
        {
            if (has_length || !parse_decimal_length(field.second, value))
                return RequestError::BadRequest;
            has_length = true;
        }
    }
    if (hosts != 1) return RequestError::BadRequest;
    if (has_encoding)
        return has_length ? RequestError::BadRequest
                          : RequestError::UnsupportedTransferEncoding;
    if (has_expectation) return RequestError::UnsupportedExpectation;
    if (value > max_body_bytes) return RequestError::BodyTooLarge;
    length_ = static_cast<std::size_t>(value);
    return RequestError::None;
}
// ===== DAY 33A ADDED END =====
