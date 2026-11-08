#include "request_line.h"

#include <cstdlib>
#include <iostream>
#include <string>

void require(bool ok, const char* message)
{
    if (!ok)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    const std::string line = "GET /health HTTP/1.1\r\n";
    const std::string headers = "Host: localhost\r\n\r\n";

    for (std::size_t split = 0; split <= line.size(); ++split)
    {
        RequestLineParser parser;
        const auto first = parser.feed(std::string_view(line).substr(0, split));
        require(first.consumed == split, "first chunk consumption");
        require(first.state == (split == line.size()
                    ? ParseState::Done : ParseState::NeedMore),
                "state before complete line");
        const std::string rest = line.substr(split) + headers;
        const auto second = parser.feed(rest);
        require(second.state == ParseState::Done, "split line failed");
        require(second.consumed == line.size() - split, "headers consumed");
        require(parser.line().method == "GET" &&
                parser.line().target == "/health" &&
                parser.line().version == "HTTP/1.1", "wrong fields");
    }

    RequestLineParser byte_parser;
    for (char ch : line)
    {
        require(byte_parser.feed(std::string_view(&ch, 1)).state
                    != ParseState::Error, "byte feed failed");
    }
    require(byte_parser.feed("").state == ParseState::Done, "not done");

    for (const char* bad : {
             "GET  /health HTTP/1.1\r\n",
             "GET /health HTTP/1.1\n",
             "GET /health HTTP/1.1\rX",
             "GET /health HTTP/1.0\r\n"})
    {
        RequestLineParser parser;
        require(parser.feed(bad).state == ParseState::Error, "bad line accepted");
        require(parser.feed(line).consumed == 0, "error state not sticky");
    }

    RequestLineParser limited;
    require(limited.feed(std::string(4097, 'G')).state == ParseState::Error,
            "line size limit missing");

    RequestLineParser method_parser;
    require(method_parser.feed("POST /upload HTTP/1.1\r\n").state
                == ParseState::Done, "parser mixed with routing");

    std::cout << "PASS: split request line, fields, boundaries and errors\n";
}
