#include "file_catalog.h"
#include "metadata_database.h"

#include <chrono>
#include <fstream>
#include <iterator>

void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

struct TestDirectory
{
    std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::path root = previous / ("day19-test-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));

    TestDirectory()
    {
        require(std::filesystem::create_directory(root), "create isolated directory");
        std::filesystem::current_path(root);
    }

    ~TestDirectory()
    {
        std::error_code error;
        std::filesystem::current_path(previous, error);
        std::filesystem::remove_all(root, error);
    }
};

HttpResponse request(FileMetadataStore& store, std::string_view method,
                     const std::string& body = "")
{
    RequestParser parser;
    const std::string wire = std::string(method) +
        " /api/files/upload-Abc123 HTTP/1.1\r\nHost: localhost\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\n\r\n" + body;
    require(parser.feed(wire).state == ParseState::Done, "parse mutation request");
    return file_catalog_response(parser, store);
}

int main()
{
    try
    {
        TestDirectory directory;
        std::filesystem::create_directories("data/objects");
        const std::string path = "data/objects/upload-Abc123";
        {
            std::ofstream file(path, std::ios::binary);
            file.write("A\0B", 3);
            require(file.good(), "write fixture");
        }
        MetadataDatabase database;
        require(database.open("data/netdisk.db") && database.initialize_schema(),
                "initialize metadata database");
        FileMetadataStore store(database.get());
        require(store.prepare(), "prepare metadata statements");
        require(store.create_ready("upload-Abc123", "upload-Abc123.bin", 3, 1) ==
                MetadataResult::Ok, "insert fixture metadata");
        const std::string name = u8"笔记' SET state='deleting' -- 新版.txt";
        const auto renamed = request(
            store, "PATCH", nlohmann::json{{"name", name}}.dump());
        require(renamed.status == 200 && nlohmann::json::parse(renamed.body).at("name") == name,
                "rename result");
        require(list_file_objects(store).at("files").at(0).at("name") == name, "list name");
        {
            std::ifstream file(path, std::ios::binary);
            const std::string bytes{std::istreambuf_iterator<char>(file), {}};
            require(bytes == std::string("A\0B", 3), "rename changed bytes");
        }
        const std::string bad[] = {"", "..", "../escape", "bad\\name", "bad\nname",
                                   std::string(256, 'x')};
        for (const auto& value : bad)
            require(request(store, "PATCH", nlohmann::json{{"name", value}}.dump()).status == 400,
                    "invalid name accepted");
        require(request(store, "PATCH", "{").status == 400 &&
                request(store, "PATCH", R"({"name":123})").status == 400,
                "invalid JSON schema");
        require(nlohmann::json::parse(request(store, "GET").body).at("name") == name,
                "failed rename changed name");
        const auto removed = request(store, "DELETE");
        require(removed.status == 204 && build_response(removed) ==
                "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n", "204 framing");
        require(!std::filesystem::exists(path) &&
                list_file_objects(store).at("files").empty(), "delete left object or metadata");
        require(request(store, "DELETE").status == 404 &&
                request(store, "GET").status == 404,
                "deleted object remained visible");
        std::cout << "PASS: rename isolation, invalid input and deletion semantics\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
