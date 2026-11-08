#pragma once

#include <cstddef>
#include <string>
#include <string_view>

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
    FeedResult feed(std::string_view input)
    {
        std::size_t used = 0;
        while (used < input.size() && state_ == ParseState::NeedMore)
        {
            const unsigned char ch =
                static_cast<unsigned char>(input[used++]);

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
            {
                saw_cr_ = true;
            }
            else if (ch < 32 || ch > 126)
            {
                state_ = ParseState::Error;
            }
            else
            {
                buffer_.push_back(static_cast<char>(ch));
            }
        }
        return {state_, used};
    }

    const RequestLine& line() const noexcept
    {
        return line_;
    }

private:
    static constexpr std::size_t max_line_bytes = 4096;

    static bool is_token_char(unsigned char ch)
    {
        return (ch >= 'A' && ch <= 'Z') ||
               (ch >= 'a' && ch <= 'z') ||
               (ch >= '0' && ch <= '9') ||
               std::string_view("!#$%&'*+-.^_`|~").find(
                   static_cast<char>(ch)) != std::string_view::npos;
    }

    bool parse_line()
    {
        const std::size_t first = buffer_.find(' ');
        if (first == std::string::npos || first == 0)
        {
            return false;
        }
        const std::size_t second = buffer_.find(' ', first + 1);
        if (second == std::string::npos || second == first + 1 ||
            buffer_.find(' ', second + 1) != std::string::npos)
        {
            return false;
        }

        RequestLine candidate{
            buffer_.substr(0, first),
            buffer_.substr(first + 1, second - first - 1),
            buffer_.substr(second + 1)
        };

        for (unsigned char ch : candidate.method)
        {
            if (!is_token_char(ch))
            {
                return false;
            }
        }
        if (candidate.target.front() != '/' ||
            candidate.target.find('#') != std::string::npos ||
            candidate.target.find('\\') != std::string::npos ||
            candidate.version != "HTTP/1.1")
        {
            return false;
        }

        line_ = candidate;
        return true;
    }

    ParseState state_ = ParseState::NeedMore;
    std::size_t bytes_ = 0;
    bool saw_cr_ = false;
    std::string buffer_;
    RequestLine line_;
};
