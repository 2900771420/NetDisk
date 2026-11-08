#include "request_headers.h"

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
    const std::string head = "HoSt: localhost\r\nX-Test:\t a:b \t\r\nX-Test:\r\n\r\n";
    for (std::size_t split = 0; split <= head.size(); ++split)
    {
        RequestHeadersParser parser;
        const auto first = parser.feed(std::string_view(head).substr(0, split));
        require(first.consumed == split, "first consumption");
        require(first.state == (split == head.size()
                    ? ParseState::Done : ParseState::NeedMore), "first state");
        const auto second = parser.feed(head.substr(split) + "BODY");
        require(second.state == ParseState::Done, "split failed");
        require(second.consumed == head.size() - split, "body consumed");
        const auto& fields = parser.fields();
        require(fields.size() == 3 && fields[0].first == "host" &&
                fields[0].second == "localhost" && fields[1].second == "a:b" &&
                fields[2].first == "x-test" && fields[2].second.empty(),
                "wrong fields");
        require(parser.feed("more").consumed == 0, "done not sticky");
    }
    RequestHeadersParser bytes;
    for (char ch : head)
    {
        require(bytes.feed(std::string_view(&ch, 1)).state != ParseState::Error,
                "byte feed failed");
    }
    require(bytes.feed("").state == ParseState::Done, "byte feed not done");
    for (const std::string& bad : {
             std::string("Bad Name: x\r\n"), std::string(": x\r\n"),
             std::string("Host : x\r\n"), std::string("Missing\r\n"),
             std::string(" folded: x\r\n"), std::string("X: a\n"),
             std::string("X: a\rZ"), std::string("X: \x7f\r\n"),
             std::string("X: a\0b\r\n", 9), std::string(4097, 'A')})
    {
        RequestHeadersParser parser;
        require(parser.feed(bad).state == ParseState::Error, "bad header accepted");
        require(parser.feed(head).consumed == 0, "error not sticky");
    }
    RequestHeadersParser empty;
    require(empty.feed("\r\nBODY").consumed == 2 && empty.fields().empty(),
            "empty section boundary");
    std::string many;
    for (int i = 0; i < 101; ++i) many += "X: a\r\n";
    RequestHeadersParser count;
    require(count.feed(many).state == ParseState::Error, "field count limit");
    const std::string line = "X:" + std::string(4092, 'a') + "\r\n";
    RequestHeadersParser exact;
    require(exact.feed(line + line).state == ParseState::NeedMore,
            "exact byte limit");
    require(exact.feed("\r\n").state == ParseState::Error, "total byte limit");
    std::cout << "PASS: split headers, fields, body boundary and limits\n";
}
