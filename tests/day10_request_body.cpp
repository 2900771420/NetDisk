#include "request_parser.h"

#include <cstdlib>
#include <iostream>

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
    const std::string body("A\0B\r\n", 5);
    const std::string head =
        "POST /echo HTTP/1.1\r\nHost: localhost\r\ncOnTeNt-LeNgTh:\t5 \r\n\r\n";
    const std::string request = head + body;
    const std::string next = "GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n";
    for (std::size_t split = 0; split <= request.size(); ++split)
    {
        RequestParser parser;
        const auto first = parser.feed(std::string_view(request).substr(0, split));
        require(first.consumed == split, "first consumption");
        require(first.state == (split == request.size()
                    ? ParseState::Done : ParseState::NeedMore), "first state");
        const auto second = parser.feed(request.substr(split) + next);
        require(second.state == ParseState::Done, "split request failed");
        require(second.consumed == request.size() - split, "next request consumed");
        require(parser.body() == body && parser.line().target == "/echo",
                "body or target changed");
        require(parser.feed(next).consumed == 0 && parser.finish() == ParseState::Done,
                "done not sticky");
    }
    RequestParser bytes;
    for (char ch : request)
    {
        require(bytes.feed(std::string_view(&ch, 1)).state != ParseState::Error,
                "byte feed failed");
    }
    require(bytes.finish() == ParseState::Done && bytes.body() == body,
            "byte feed body changed");
    for (std::size_t cut = 0; cut < request.size(); ++cut)
    {
        RequestParser truncated;
        require(truncated.feed(std::string_view(request).substr(0, cut)).state
                    == ParseState::NeedMore, "truncated state");
        require(truncated.finish() == ParseState::Error, "premature EOF accepted");
        require(truncated.feed(request).consumed == 0, "EOF error not sticky");
    }
    const std::string base = "POST /echo HTTP/1.1\r\nHost: localhost\r\n";
    for (const std::string& field : {
             std::string("Content-Length:\r\n"), std::string("Content-Length: -1\r\n"),
             std::string("Content-Length: +1\r\n"), std::string("Content-Length: 1x\r\n"),
             std::string("Content-Length: 1 0\r\n"), std::string("Content-Length: 1, 1\r\n"),
             std::string("Content-Length: 18446744073709551616\r\n"),
             std::string("Content-Length: 65537\r\n"),
             std::string("Content-Length: 1\r\nContent-Length: 1\r\n"),
             std::string("Content-Length: 1\r\nContent-Length: 2\r\n"),
             std::string("Transfer-Encoding: chunked\r\n"),
             std::string("Content-Length: 1\r\nTransfer-Encoding: chunked\r\n"),
             std::string("Expect: 100-continue\r\nContent-Length: 1\r\n"),
             std::string("Host: another\r\n"), std::string("Bad Name: x\r\n")})
    {
        RequestParser invalid;
        require(invalid.feed(base + field + "\r\n").state == ParseState::Error,
                "invalid headers accepted");
        require(invalid.feed(request).consumed == 0, "error not sticky");
    }
    RequestParser no_host;
    require(no_host.feed("GET /health HTTP/1.1\r\n\r\n").state == ParseState::Error,
            "missing Host accepted");
    for (const std::string& field : {std::string{}, std::string("Content-Length: 000\r\n")})
    {
        RequestParser empty;
        const std::string zero = base + field + "\r\n";
        const auto result = empty.feed(zero + next);
        require(result.state == ParseState::Done && result.consumed == zero.size() &&
                empty.body().empty(), "zero body boundary");
    }
    std::uint64_t value = 7;
    require(parse_decimal_length("18446744073709551615", value) &&
            value == std::numeric_limits<std::uint64_t>::max(), "uint64 maximum");
    require(!parse_decimal_length("18446744073709551616", value) &&
            value == std::numeric_limits<std::uint64_t>::max(), "overflow changed output");
    RequestParser limit;
    const std::string full = base + "Content-Length: 65536\r\n\r\n" +
        std::string(RequestParser::max_body_bytes, 'x');
    const auto result = limit.feed(full + next);
    require(result.state == ParseState::Done && result.consumed == full.size() &&
            limit.body().size() == RequestParser::max_body_bytes, "body size limit");
    std::cout << "PASS: request splits, binary body, length limits and EOF\n";
}
