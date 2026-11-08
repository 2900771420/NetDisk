#pragma once

#include "request_headers.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <functional>
#include <utility>

inline bool parse_decimal_length(std::string_view text, std::uint64_t& output)
{
    if (text.empty()) return false;
    std::uint64_t value = 0;
    for (char ch : text)
    {
        if (ch < '0' || ch > '9') return false;
        const auto digit = static_cast<std::uint64_t>(ch - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
        {
            return false;
        }
        value = value * 10 + digit;
    }
    output = value;
    return true;
}

enum class RequestError
{
    None,
    BadRequest,
    BodyTooLarge,
    UnsupportedExpectation,
    UnsupportedTransferEncoding,
    BodyWriteFailed
};

enum class BodyAction { Buffer, Written, Failed };
using BodySink = std::function<BodyAction(const RequestLine&, std::string_view)>;

class RequestParser
{
public:
    static constexpr std::size_t max_body_bytes = 64 * 1024;

    explicit RequestParser(BodySink sink = {}) : sink_(std::move(sink)) {}

    FeedResult feed(std::string_view input)
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
            {
                error_ = RequestError::BadRequest;
            }
            else if (result.state == ParseState::Done)
            {
                error_ = validate_headers();
            }
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

    ParseState finish() noexcept
    {
        if (state_ == ParseState::NeedMore)
        {
            state_ = ParseState::Error;
            error_ = RequestError::BadRequest;
        }
        return state_;
    }

    RequestError error() const noexcept
    {
        return error_;
    }

    const RequestLine& line() const noexcept
    {
        return line_.line();
    }

    const std::string& body() const noexcept
    {
        return body_;
    }

    std::size_t body_size() const noexcept { return received_; }

private:
    RequestError validate_headers()
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
                {
                    return RequestError::BadRequest;
                }
                has_length = true;
            }
        }
        if (hosts != 1) return RequestError::BadRequest;
        if (has_encoding)
        {
            return has_length ? RequestError::BadRequest
                              : RequestError::UnsupportedTransferEncoding;
        }
        if (has_expectation) return RequestError::UnsupportedExpectation;
        if (value > max_body_bytes) return RequestError::BodyTooLarge;
        length_ = static_cast<std::size_t>(value);
        return RequestError::None;
    }

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
