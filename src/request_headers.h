#pragma once

#include "request_line.h"

#include <utility>
#include <vector>

class RequestHeadersParser
{
public:
    using Field = std::pair<std::string, std::string>;

    FeedResult feed(std::string_view input)
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
                {
                    state_ = ParseState::Error;
                }
                else if (buffer_.empty())
                {
                    state_ = ParseState::Done;
                }
                else if (!parse_field())
                {
                    state_ = ParseState::Error;
                }
                buffer_.clear();
                saw_cr_ = false;
                line_bytes_ = 0;
            }
            else if (ch == '\r')
            {
                saw_cr_ = true;
            }
            else if ((ch < 32 && ch != '\t') || ch == 127)
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

    const std::vector<Field>& fields() const noexcept
    {
        return fields_;
    }

private:
    static bool is_token(unsigned char ch)
    {
        return (ch >= 'A' && ch <= 'Z') ||
               (ch >= 'a' && ch <= 'z') ||
               (ch >= '0' && ch <= '9') ||
               std::string_view("!#$%&'*+-.^_`|~").find(
                   static_cast<char>(ch)) != std::string_view::npos;
    }

    bool parse_field()
    {
        const auto colon = buffer_.find(':');
        if (colon == std::string::npos || colon == 0 || fields_.size() >= 100)
        {
            return false;
        }
        std::string name = buffer_.substr(0, colon);
        for (char& ch : name)
        {
            if (!is_token(static_cast<unsigned char>(ch)))
            {
                return false;
            }
            if (ch >= 'A' && ch <= 'Z')
            {
                ch = static_cast<char>(ch - 'A' + 'a');
            }
        }
        const auto begin = buffer_.find_first_not_of(" \t", colon + 1);
        const auto end = buffer_.find_last_not_of(" \t");
        const std::string value = begin == std::string::npos
            ? std::string{} : buffer_.substr(begin, end - begin + 1);
        fields_.emplace_back(std::move(name), value);
        return true;
    }

    ParseState state_ = ParseState::NeedMore;
    std::size_t bytes_ = 0;
    std::size_t line_bytes_ = 0;
    bool saw_cr_ = false;
    std::string buffer_;
    std::vector<Field> fields_;
};
