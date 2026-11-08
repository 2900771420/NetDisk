#include "http_response.h"

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

std::string reply(const std::string& input, bool eof = false)
{
    RequestParser parser;
    auto state = parser.feed(input).state;
    if (eof && state == ParseState::NeedMore) state = parser.finish();
    require(state != ParseState::NeedMore, "test input is incomplete");
    const auto response = state == ParseState::Done
        ? route_request(parser) : parser_error_response(parser.error());
    return build_response(response, parser.line().method == "HEAD");
}

void check(const std::string& wire, int status, const std::string& body,
           const std::string& allow = {}, bool head_only = false)
{
    require(wire.find("HTTP/1.1 " + std::to_string(status) + " ") == 0,
            "wrong status");
    const auto end = wire.find("\r\n\r\n");
    require(end != std::string::npos, "missing header terminator");
    const auto head = wire.substr(0, end + 4);
    require(head.find("\r\nContent-Length: " + std::to_string(body.size()) + "\r\n")
                != std::string::npos, "wrong byte length");
    require(head.find("\r\nConnection: close\r\n") != std::string::npos,
            "missing connection policy");
    require(wire.substr(end + 4) == (head_only ? std::string{} : body),
            "wrong response body");
    if (allow.empty())
        require(head.find("\r\nAllow:") == std::string::npos, "unexpected Allow");
    else
        require(head.find("\r\nAllow: " + allow + "\r\n") != std::string::npos,
                "missing Allow");
    if (status >= 400)
        require(head.find("\r\nContent-Type: application/json\r\n") != std::string::npos,
                "error is not JSON");
}

int main()
{
    const std::string host = " HTTP/1.1\r\nHost: localhost\r\n";
    check(reply("GET /health" + host + "\r\n"), 200, "OK\n");
    check(reply("GET /health?probe=1" + host + "\r\n"), 200, "OK\n");
    check(reply("HEAD /health" + host + "\r\n"), 200, "OK\n", {}, true);
    const std::string binary("A\0B", 3);
    check(reply("POST /echo" + host + "Content-Length: 3\r\n\r\n" + binary),
          200, binary);
    const std::string not_found =
        R"({"error":{"code":"not_found","message":"Route not found"}})";
    const std::string wrong_method =
        R"({"error":{"code":"method_not_allowed","message":"Method not allowed"}})";
    const std::string bad =
        R"({"error":{"code":"bad_request","message":"Invalid request"}})";
    check(reply("GET /missing" + host + "\r\n"), 404, not_found);
    check(reply("HEAD /missing" + host + "\r\n"), 404, not_found, {}, true);
    check(reply("POST /health" + host + "\r\n"), 405, wrong_method, "GET, HEAD");
    check(reply("GET /echo" + host + "\r\n"), 405, wrong_method, "POST");
    check(reply("HEAD /echo" + host + "\r\n"), 405, wrong_method, "POST", true);
    check(reply("PUT /health" + host + "\r\n"), 501,
          R"({"error":{"code":"not_implemented","message":"Requested feature is not implemented"}})");
    for (const std::string& headers : {
             std::string("Content-Length: x\r\n"),
             std::string("Content-Length: 1\r\nContent-Length: 1\r\n"),
             std::string("Content-Length: 1\r\nTransfer-Encoding: chunked\r\n"),
             std::string("Transfer-Encoding: chunked\r\nContent-Length: 1\r\n"),
             std::string("Host: another\r\n"), std::string("Bad Name: x\r\n")})
    {
        check(reply("POST /echo" + host + headers + "\r\n"), 400, bad);
    }
    check(reply("GET /health HTTP/1.1\r\n\r\n"), 400, bad);
    check(reply("GET /health" + host + "Content-Length: 1\r\n\r\nx"), 400, bad);
    check(reply("POST /echo" + host + "Content-Length: 3\r\n\r\nx", true), 400, bad);
    check(reply("POST /echo" + host + "Content-Length: 65537\r\n\r\n"), 413,
          R"({"error":{"code":"body_too_large","message":"Body exceeds 65536 bytes"}})");
    check(reply("POST /echo" + host + "Expect: 100-continue\r\nContent-Length: 3\r\n\r\n"),
          417, R"({"error":{"code":"expectation_failed","message":"Expect is not supported"}})");
    check(reply("POST /echo" + host + "Transfer-Encoding: chunked\r\n\r\n"), 501,
          R"({"error":{"code":"not_implemented","message":"Requested feature is not implemented"}})");
    std::cout << "PASS: routes, JSON errors, Allow, HEAD and response lengths\n";
}
