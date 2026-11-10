#include "file_catalog.h"
#include "metadata_database.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

struct TestDirectory
{
    std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::path root = previous / ("day24-test-" + std::to_string(
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

HttpResponse request(FileMetadataStore& store, std::string_view target)
{
    RequestParser parser;
    const std::string wire = "GET " + std::string(target) +
        " HTTP/1.1\r\nHost: localhost\r\n\r\n";
    require(parser.feed(wire).state == ParseState::Done, "parse pagination request");
    return file_catalog_response(parser, store);
}

std::vector<std::string> page_ids(const nlohmann::json& document)
{
    std::vector<std::string> ids;
    for (const auto& file : document.at("files"))
        ids.push_back(file.at("id").get<std::string>());
    return ids;
}

void add_file(FileMetadataStore& store, const std::string& id, std::int64_t created_at)
{
    const std::filesystem::path path = std::filesystem::path("data/objects") / id;
    std::ofstream file(path, std::ios::binary);
    file.put(id.back());
    require(file.good(), "write object fixture");
    file.close();
    require(store.create_publishing(id, id + ".bin", 1, created_at) ==
                MetadataResult::Ok &&
            store.mark_ready(id) == MetadataResult::Ok,
            "publish metadata fixture");
}

int main()
{
    try
    {
        TestDirectory directory;
        std::filesystem::create_directories("data/objects");
        MetadataDatabase database;
        require(database.open("data/netdisk.db") && database.initialize_schema(),
                "initialize metadata database");
        FileMetadataStore store(database.get());
        require(store.prepare(), "prepare metadata statements");

        const std::vector<std::string> expected{
            "upload-A00001", "upload-B00001", "upload-C00001",
            "upload-D00001", "upload-E00001"};
        add_file(store, expected[0], 100);
        add_file(store, expected[1], 100);
        add_file(store, expected[2], 101);
        add_file(store, expected[3], 101);
        add_file(store, expected[4], 102);

        const auto first = nlohmann::json::parse(
            request(store, "/api/files?limit=2").body);
        require(page_ids(first) == std::vector<std::string>{expected[0], expected[1]},
                "first page order");
        require(first.at("next_cursor") == "100.upload-B00001", "first cursor");

        // ===== DAY 27 MODIFIED BEGIN 分页删除夹具遵循deleting状态转换 =====
        require(store.mark_deleting(expected[0]) == MetadataResult::Ok &&
                std::filesystem::remove(
                    std::filesystem::path("data/objects") / expected[0]) &&
                store.erase_deleting(expected[0]) == MetadataResult::Ok,
                "delete item before cursor");
        // ===== DAY 27 MODIFIED END =====

        const auto second = nlohmann::json::parse(request(
            store, "/api/files?cursor=100.upload-B00001&limit=2").body);
        require(page_ids(second) == std::vector<std::string>{expected[2], expected[3]},
                "second page shifted after deletion");
        require(second.at("next_cursor") == "101.upload-D00001", "second cursor");

        const auto third = nlohmann::json::parse(request(
            store, "/api/files?limit=2&cursor=101.upload-D00001").body);
        require(page_ids(third) == std::vector<std::string>{expected[4]} &&
                third.find("next_cursor") == third.end(), "final page boundary");

        const std::string_view invalid[] = {
            "/api/files?limit=0",
            "/api/files?limit=101",
            "/api/files?limit=-1",
            "/api/files?limit=x",
            "/api/files?limit=1&limit=2",
            "/api/files?cursor=",
            "/api/files?cursor=-1.upload-A00001",
            "/api/files?cursor=100.bad-id",
            "/api/files?cursor=9223372036854775808.upload-A00001",
            "/api/files?cursor=100.upload-A00001&cursor=101.upload-B00001",
            "/api/files?offset=2",
            "/api/files?limit=2&"};
        for (const auto target : invalid)
            require(request(store, target).status == 400, "invalid pagination accepted");

        std::cout << "PASS: keyset pagination order, deletion stability and query bounds\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
