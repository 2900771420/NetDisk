// ===== DAY 26 ADDED BEGIN 验证publishing启动恢复与失败保留策略 =====
#include "metadata_database.h"
#include "publishing_recovery.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

struct TestDirectory
{
    std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::path root = previous / ("day26-test-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));

    TestDirectory()
    {
        require(std::filesystem::create_directory(root), "create isolated directory");
        std::filesystem::current_path(root);
        std::filesystem::create_directories("data/uploads");
        std::filesystem::create_directories("data/objects");
    }

    ~TestDirectory()
    {
        std::error_code error;
        std::filesystem::current_path(previous, error);
        std::filesystem::remove_all(root, error);
    }
};

std::filesystem::path temporary_path(std::string_view id)
{
    return std::filesystem::path("data/uploads") / std::string(id);
}

std::filesystem::path object_path(std::string_view id)
{
    return std::filesystem::path("data/objects") / std::string(id);
}

void write_file(const std::filesystem::path& path, std::string_view bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(output.good(), "write fixture file");
}

void create_publishing(FileMetadataStore& store, std::string_view id,
                       std::string_view payload, std::int64_t created_at)
{
    const std::string name = std::string(id) + ".bin";
    require(store.create_publishing(id, name, payload.size(), created_at) ==
                MetadataResult::Ok,
            "create publishing fixture");
}

void require_ready(FileMetadataStore& store, std::string_view id,
                   std::string_view payload)
{
    FileMetadata metadata;
    require(store.find_ready(id, metadata) == MetadataResult::Ok,
            "recovered metadata is not ready");
    require(metadata.size == payload.size(), "recovered metadata size changed");
    std::ifstream input(object_path(id), std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
    require(input.is_open() && bytes == payload, "recovered object bytes differ");
    require(!std::filesystem::exists(temporary_path(id)),
            "recovered temporary name remains");
}

int main()
{
    try
    {
        TestDirectory directory;
        MetadataDatabase database;
        require(database.open("data/netdisk.db") && database.initialize_schema(),
                "initialize metadata database");
        FileMetadataStore store(database.get());
        require(store.prepare(), "prepare metadata statements");

        constexpr std::string_view temp_id = "upload-Temp26";
        constexpr std::string_view object_id = "upload-Obj026";
        constexpr std::string_view linked_id = "upload-Both26";
        constexpr std::string_view missing_id = "upload-Miss26";
        constexpr std::string_view temp_payload = "temporary-only";
        constexpr std::string_view object_payload = "object-only";
        constexpr std::string_view linked_payload = "two-hard-links";

        create_publishing(store, temp_id, temp_payload, 100);
        write_file(temporary_path(temp_id), temp_payload);

        create_publishing(store, object_id, object_payload, 101);
        write_file(object_path(object_id), object_payload);

        create_publishing(store, linked_id, linked_payload, 102);
        write_file(temporary_path(linked_id), linked_payload);
        std::filesystem::create_hard_link(
            temporary_path(linked_id), object_path(linked_id));

        create_publishing(store, missing_id, "", 103);

        PublishingRecovery recovery(store);
        PublishingRecoveryReport report;
        require(recovery.run(report), "recover valid publishing fixtures");
        require(report.completed == 3 && report.cancelled == 1,
                "unexpected recovery report");
        require_ready(store, temp_id, temp_payload);
        require_ready(store, object_id, object_payload);
        require_ready(store, linked_id, linked_payload);
        FileMetadata metadata;
        require(store.find_ready(missing_id, metadata) == MetadataResult::NotFound,
                "missing publication became ready");
        std::vector<FileMetadata> publishing;
        require(store.list_publishing(publishing) == MetadataResult::Ok &&
                    publishing.empty(),
                "successful recovery left publishing rows");

        require(recovery.run(report) && report.completed == 0 && report.cancelled == 0,
                "second recovery run was not idempotent");

        constexpr std::string_view bad_size_id = "upload-BadSz6";
        create_publishing(store, bad_size_id, "0123456789", 104);
        write_file(temporary_path(bad_size_id), "short");
        require(!recovery.run(report) &&
                    recovery.error_message().find("size differs") != std::string::npos,
                "size mismatch did not stop recovery");
        require(store.find_ready(bad_size_id, metadata) == MetadataResult::NotFound &&
                    store.list_publishing(publishing) == MetadataResult::Ok &&
                    publishing.size() == 1 && publishing[0].id == bad_size_id,
                "failed recovery changed mismatched metadata");

        write_file(temporary_path(bad_size_id), "0123456789");
        require(recovery.run(report) && report.completed == 1 && report.cancelled == 0,
                "corrected publication did not recover on retry");
        require_ready(store, bad_size_id, "0123456789");

        constexpr std::string_view split_id = "upload-Split6";
        create_publishing(store, split_id, "ABC", 105);
        write_file(temporary_path(split_id), "ABC");
        write_file(object_path(split_id), "XYZ");
        require(!recovery.run(report) &&
                    recovery.error_message().find("different files") != std::string::npos,
                "conflicting names did not stop recovery");
        require(store.find_ready(split_id, metadata) == MetadataResult::NotFound &&
                    store.list_publishing(publishing) == MetadataResult::Ok &&
                    publishing.size() == 1 && publishing[0].id == split_id &&
                    std::filesystem::exists(temporary_path(split_id)) &&
                    std::filesystem::exists(object_path(split_id)),
                "conflicting recovery destroyed evidence or exposed data");

        std::cout << "PASS: publishing startup recovery completes cancel and retries safely\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
// ===== DAY 26 ADDED END =====
