// ===== DAY 27 ADDED BEGIN 验证deleting状态与启动删除恢复 =====
#include "deletion_recovery.h"
#include "metadata_database.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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
    std::filesystem::path root = previous / ("day27-test-" + std::to_string(
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

std::filesystem::path object_path(std::string_view id)
{
    return std::filesystem::path("data/objects") / id;
}

void write_file(const std::filesystem::path& path, std::string_view bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(file.good(), "write object fixture");
}

void create_ready(FileMetadataStore& store, std::string_view id,
                  std::string_view payload, std::int64_t created_at,
                  bool create_object = true)
{
    if (create_object) write_file(object_path(id), payload);
    require(store.create_publishing(id, std::string(id) + ".bin",
                payload.size(), created_at) == MetadataResult::Ok &&
            store.mark_ready(id) == MetadataResult::Ok,
            "create ready fixture");
}

bool has_deleting(FileMetadataStore& store, std::string_view id)
{
    std::vector<FileMetadata> deleting;
    require(store.list_deleting(deleting) == MetadataResult::Ok,
            "list deleting fixtures");
    for (const auto& file : deleting)
        if (file.id == id) return true;
    return false;
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

        constexpr std::string_view object_id = "upload-DelObj";
        constexpr std::string_view missing_id = "upload-DelMis";
        constexpr std::string_view ready_id = "upload-Keep27";
        create_ready(store, object_id, "delete object", 100);
        create_ready(store, missing_id, "already absent", 101, false);
        create_ready(store, ready_id, "keep ready", 102);

        require(store.mark_deleting(object_id) == MetadataResult::Ok &&
                store.mark_deleting(missing_id) == MetadataResult::Ok,
                "mark interrupted deletions");
        FileMetadata metadata;
        require(store.find_ready(object_id, metadata) == MetadataResult::NotFound &&
                store.find_ready(missing_id, metadata) == MetadataResult::NotFound &&
                store.find_ready(ready_id, metadata) == MetadataResult::Ok,
                "deleting rows were still public or ready row was hidden");

        DeletionRecovery recovery(store);
        DeletionRecoveryReport report;
        require(recovery.run(report) && report.completed == 2 &&
                    report.objects_removed == 1,
                "startup recovery did not finish both deletion phases");
        require(!std::filesystem::exists(object_path(object_id)) &&
                !has_deleting(store, object_id) && !has_deleting(store, missing_id),
                "successful recovery left deleting data");
        require(std::filesystem::is_regular_file(object_path(ready_id)) &&
                store.find_ready(ready_id, metadata) == MetadataResult::Ok,
                "recovery removed an unrelated ready object");

        require(recovery.run(report) && report.completed == 0 &&
                    report.objects_removed == 0,
                "second deletion recovery run was not idempotent");

        constexpr std::string_view mismatch_id = "upload-BadD27";
        create_ready(store, mismatch_id, "1234", 103, false);
        write_file(object_path(mismatch_id), "xx");
        require(store.mark_deleting(mismatch_id) == MetadataResult::Ok,
                "mark mismatched deletion");
        require(!recovery.run(report) &&
                    recovery.error_message().find("size differs") != std::string::npos,
                "size mismatch did not stop deletion recovery");
        require(std::filesystem::is_regular_file(object_path(mismatch_id)) &&
                has_deleting(store, mismatch_id),
                "failed recovery destroyed mismatch evidence");

        write_file(object_path(mismatch_id), "1234");
        require(recovery.run(report) && report.completed == 1 &&
                    report.objects_removed == 1 &&
                !std::filesystem::exists(object_path(mismatch_id)) &&
                !has_deleting(store, mismatch_id),
                "corrected deleting object did not recover on retry");

        std::cout << "PASS: deleting recovery hides retries and preserves ready objects\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
// ===== DAY 27 ADDED END =====
