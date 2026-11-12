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

#ifdef __linux__
std::string read_file(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    require(input.is_open(), "open published file");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
#endif

int main()
{
    const std::string head = "POST /api/files HTTP/1.1\r\nHost: localhost\r\n";
    RequestParser response_parser;
    require(response_parser.feed(head + "Content-Length: 0\r\n\r\n").state
                == ParseState::Done, "parse empty upload");
    require(route_request(response_parser).status == 500, "201 before publication");
    std::string id = "upload-Abc123";
    // ===== DAY 29 MODIFIED BEGIN 发布响应要求同时拥有文件ID与摘要 =====
    std::string digest(64, 'a');
    const auto response = route_request(response_parser, id, digest);
    id.assign("changed");
    digest.assign("changed");
    const auto wire = build_response(response);
    require(wire.find("HTTP/1.1 201 Created\r\n") == 0 &&
            wire.find("\r\nX-File-Id: upload-Abc123\r\n") != std::string::npos &&
            wire.find("\r\nX-File-Sha256: " + std::string(64, 'a') + "\r\n") !=
                std::string::npos,
            "missing or borrowed publication identity");
    // ===== DAY 29 MODIFIED END =====
    require(wire.find("\r\nContent-Length: 20\r\n") != std::string::npos &&
            response.body == R"({"status":"created"})", "created response");
    RequestParser wrong_method;
    require(wrong_method.feed("GET /api/files HTTP/1.1\r\nHost: localhost\r\n\r\n").state
                == ParseState::Done, "parse method test");
    require(route_request(wrong_method).status == 405 &&
            route_request(wrong_method).allow == "POST", "upload method policy");
    std::cout << "PASS: publication response, owned file ID and method policy\n";

#ifdef __linux__
    namespace fs = std::filesystem;
    const auto previous = fs::current_path();
    char pattern[] = "day13-test-XXXXXX";
    const char* directory = ::mkdtemp(pattern);
    require(directory != nullptr, "create isolated test directory");
    const auto root = previous / directory;
    fs::current_path(root);
    fs::create_directories("data/uploads");
    fs::create_directories("data/objects");
    const std::string body("A\0B\r\n", 5);
    std::string temporary;
    std::string published;
    {
        TempUpload file;
        RequestParser parser([&](const RequestLine&, std::string_view chunk)
        {
            return file.append(chunk) ? BodyAction::Written : BodyAction::Failed;
        });
        require(parser.feed(head + "Content-Length: 5\r\n\r\nA").state
                    == ParseState::NeedMore && fs::is_empty("data/objects"),
                "partial upload became visible");
        const auto result = parser.feed(body.substr(1) + "NEXT");
        require(result.state == ParseState::Done && result.consumed == 4 &&
                parser.body().empty(), "request boundary or buffering");
        temporary = file.path();
        require(file.publish(parser.body_size()), "publish complete upload");
        published = "data/objects/" + file.id();
        require(!fs::exists(temporary) && read_file(published) == body,
                "publication changed bytes or left temporary name");
    }
    require(fs::exists(published) && read_file(published) == body,
            "destructor deleted published object");
    {
        TempUpload file;
        RequestParser parser([&](const RequestLine&, std::string_view chunk)
        {
            return file.append(chunk) ? BodyAction::Written : BodyAction::Failed;
        });
        require(parser.feed(head + "Content-Length: 5\r\n\r\nAB").state
                    == ParseState::NeedMore, "partial state");
        temporary = file.path();
        require(parser.finish() == ParseState::Error && !file.publish(5),
                "incomplete upload published");
    }
    require(!fs::exists(temporary), "partial file leaked");
    {
        TempUpload file;
        require(file.append("NEW"), "prepare collision");
        temporary = file.path();
        const auto destination = fs::path("data/objects") / fs::path(temporary).filename();
        {
            std::ofstream existing(destination, std::ios::binary);
            existing << "OLD";
            require(existing.good(), "prepare existing object");
        }
        require(!file.publish(3) && file.error() == EEXIST &&
                read_file(destination.string()) == "OLD", "existing object overwritten");
    }
    require(!fs::exists(temporary), "failed publication leaked temporary file");
    {
        TempUpload file;
        require(file.append({}) && file.publish(0), "publish empty file");
        published = "data/objects/" + file.id();
    }
    require(fs::exists(published) && fs::file_size(published) == 0,
            "empty object was not preserved");
    fs::current_path(previous);
    fs::remove_all(root);
    std::cout << "PASS: Linux complete publication, binary bytes, cleanup and no overwrite\n";
#else
    std::cout << "SKIP: Linux publication checks require Linux\n";
#endif
}
