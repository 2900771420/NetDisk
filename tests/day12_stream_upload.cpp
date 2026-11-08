#include "http_response.h"

#include <cstdlib>
#include <iostream>

#ifdef __linux__
#include "temp_upload.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#endif

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
    const std::string prefix = "POST /dev/upload HTTP/1.1\r\nHost: localhost\r\n";
    const std::string body("A\0B\r\n", 5);
    const std::string request = prefix + "Content-Length: 5\r\n\r\n" + body;
    for (std::size_t split = 0; split <= request.size(); ++split)
    {
        std::string written;
        RequestParser parser([&](const RequestLine&, std::string_view chunk)
        {
            written.append(chunk.data(), chunk.size());
            return BodyAction::Written;
        });
        const auto first = parser.feed(std::string_view(request).substr(0, split));
        require(first.consumed == split, "first consumption");
        const auto last = parser.feed(request.substr(split) + "NEXT");
        require(last.state == ParseState::Done && last.consumed == request.size() - split,
                "split or trailing boundary");
        require(written == body && parser.body().empty() && parser.body_size() == 5,
                "streamed bytes were lost or cached");
    }
    std::size_t total = 0;
    RequestParser many([&](const RequestLine&, std::string_view chunk)
    {
        require(chunk.size() <= 1024, "oversized sink chunk");
        total += chunk.size();
        return BodyAction::Written;
    });
    require(many.feed(prefix + "Content-Length: 65536\r\n\r\n").state
                == ParseState::NeedMore, "header unexpectedly completed body");
    for (int i = 0; i < 64; ++i)
    {
        const auto result = many.feed(std::string(1024, 'x'));
        require(result.state == (i == 63 ? ParseState::Done : ParseState::NeedMore),
                "chunk progress");
        require(many.body().empty(), "whole body buffered");
    }
    require(total == 65536 && many.body_size() == total, "streamed length");
    int empty_calls = 0;
    RequestParser empty([&](const RequestLine&, std::string_view chunk)
    {
        require(chunk.empty(), "zero body has bytes");
        ++empty_calls;
        return BodyAction::Written;
    });
    require(empty.feed(prefix + "Content-Length: 0\r\n\r\n").state == ParseState::Done &&
            empty_calls == 1, "empty upload was not initialized");
    RequestParser failed([](const RequestLine&, std::string_view)
    {
        return BodyAction::Failed;
    });
    require(failed.feed(request).state == ParseState::Error &&
            failed.error() == RequestError::BodyWriteFailed &&
            parser_error_response(failed.error()).status == 500, "sink failure");
    require(failed.feed(request).consumed == 0, "failed sink retried");
    std::cout << "PASS: stream chunks, binary boundary, empty body and sink failure\n";

#ifdef __linux__
    std::filesystem::create_directories("data/uploads");
    std::string path;
    {
        TempUpload file;
        RequestParser parser([&](const RequestLine&, std::string_view chunk)
        {
            return file.append(chunk) ? BodyAction::Written : BodyAction::Failed;
        });
        require(parser.feed(request).state == ParseState::Done, "real file write");
        path = file.path();
        require(file.close_file(), "close real file");
        std::ifstream input(path, std::ios::binary);
        require(input.is_open(), "open test result");
        const std::string actual{std::istreambuf_iterator<char>(input),
                                 std::istreambuf_iterator<char>()};
        require(actual == body, "real file content changed");
    }
    require(!std::filesystem::exists(path), "completed temporary file leaked");
    {
        TempUpload file;
        RequestParser parser([&](const RequestLine&, std::string_view chunk)
        {
            return file.append(chunk) ? BodyAction::Written : BodyAction::Failed;
        });
        require(parser.feed(prefix + "Content-Length: 5\r\n\r\nAB").state
                    == ParseState::NeedMore, "partial body state");
        path = file.path();
        require(parser.finish() == ParseState::Error, "partial file accepted");
    }
    require(!std::filesystem::exists(path), "partial temporary file leaked");
    {
        TempUpload file;
        require(file.append({}) && std::filesystem::file_size(file.path()) == 0,
                "empty real file");
        path = file.path();
    }
    require(!std::filesystem::exists(path), "empty temporary file leaked");
    std::cout << "PASS: Linux temporary file bytes and scope cleanup\n";
#else
    std::cout << "SKIP: Linux temporary file checks require Linux\n";
#endif
}
