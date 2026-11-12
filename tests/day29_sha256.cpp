// ===== DAY 29 ADDED BEGIN 验证流式SHA256与v1到v2迁移 =====
#include "file_metadata_store.h"
#include "metadata_database.h"
#include "sha256_hasher.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

struct TestDirectory
{
    std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::path root = previous / ("day29-test-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));

    TestDirectory()
    {
        require(std::filesystem::create_directory(root), "create isolated directory");
        std::filesystem::current_path(root);
        std::filesystem::create_directory("data");
    }

    ~TestDirectory()
    {
        std::error_code error;
        std::filesystem::current_path(previous, error);
        std::filesystem::remove_all(root, error);
    }
};

void execute(sqlite3* database, const char* sql)
{
    char* detail = nullptr;
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, &detail);
    if (result != SQLITE_OK)
    {
        const std::string message = detail == nullptr ? sqlite3_errmsg(database) : detail;
        sqlite3_free(detail);
        throw std::runtime_error(message);
    }
    sqlite3_free(detail);
}

int scalar_int(sqlite3* database, const char* sql)
{
    int value = -1;
    const auto callback = [](void* context, int count, char** values, char**) -> int
    {
        if (count != 1 || values[0] == nullptr) return 1;
        *static_cast<int*>(context) = std::atoi(values[0]);
        return 0;
    };
    char* detail = nullptr;
    const int result = sqlite3_exec(database, sql, callback, &value, &detail);
    if (result != SQLITE_OK)
    {
        const std::string message = detail == nullptr ? sqlite3_errmsg(database) : detail;
        sqlite3_free(detail);
        throw std::runtime_error(message);
    }
    sqlite3_free(detail);
    return value;
}

std::string digest_fragments(std::string_view input, std::size_t width)
{
    Sha256Hasher hasher;
    require(hasher.good(), "initialize SHA256 context");
    for (std::size_t offset = 0; offset < input.size(); offset += width)
        require(hasher.update(input.substr(offset, width)), "update SHA256 context");
    std::string output = "unchanged";
    require(hasher.finish(output), "finish SHA256 context");
    require(!hasher.update("x"), "accepted update after finalization");
    require(!hasher.finish(output), "accepted second finalization");
    return output;
}

void create_version_1_database()
{
    sqlite3* database = nullptr;
    require(sqlite3_open("data/netdisk.db", &database) == SQLITE_OK,
            "open version 1 fixture");
    try
    {
        execute(database, R"SQL(
BEGIN IMMEDIATE;
CREATE TABLE schema_info (
    singleton INTEGER NOT NULL PRIMARY KEY CHECK (singleton = 1),
    version INTEGER NOT NULL CHECK (version = 1)
);
INSERT INTO schema_info(singleton, version) VALUES(1, 1);
CREATE TABLE files (
    id TEXT NOT NULL PRIMARY KEY,
    name TEXT NOT NULL CHECK (length(CAST(name AS BLOB)) BETWEEN 1 AND 255),
    size INTEGER NOT NULL CHECK (size >= 0),
    state TEXT NOT NULL CHECK (state IN ('publishing', 'ready', 'deleting')),
    created_at INTEGER NOT NULL CHECK (created_at >= 0)
);
CREATE INDEX files_state_created_index ON files(state, created_at, id);
INSERT INTO files(id, name, size, state, created_at)
VALUES('upload-Old029', 'legacy.bin', 6, 'ready', 100);
PRAGMA user_version = 1;
COMMIT;
)SQL");
    }
    catch (...)
    {
        sqlite3_close(database);
        throw;
    }
    require(sqlite3_close(database) == SQLITE_OK, "close version 1 fixture");
}

int main()
{
    try
    {
        require(digest_fragments("", 1) ==
                    "e3b0c44298fc1c149afbf4c8996fb924"
                    "27ae41e4649b934ca495991b7852b855",
                "empty SHA256 differs");
        require(digest_fragments("abc", 1) ==
                    "ba7816bf8f01cfea414140de5dae2223"
                    "b00361a396177a9cb410ff61f20015ad",
                "fragmented abc SHA256 differs");
        const std::string binary("A\0B\r\nC", 6);
        require(digest_fragments(binary, 2) == digest_fragments(binary, binary.size()),
                "fragment boundaries changed SHA256");
        std::cout << "PASS: OpenSSL EVP SHA256 is binary safe and fragment independent\n";

        TestDirectory directory;
        create_version_1_database();
        MetadataDatabase database;
        require(database.open("data/netdisk.db"), "open legacy metadata");
        require(database.initialize_schema(), "migrate metadata to version 2");
        require(scalar_int(database.get(), "PRAGMA user_version;") == 2,
                "user version was not migrated");
        require(scalar_int(database.get(),
                "SELECT version FROM schema_info WHERE singleton = 1;") == 2,
                "schema info was not migrated");
        require(scalar_int(database.get(),
                "SELECT COUNT(*) FROM pragma_table_info('files') WHERE name='sha256';") == 1,
                "SHA256 column is missing");

        FileMetadataStore store(database.get());
        require(store.prepare(), "prepare migrated metadata store");
        FileMetadata legacy;
        require(store.find_ready("upload-Old029", legacy) == MetadataResult::Ok &&
                legacy.sha256.empty(), "legacy row did not retain unknown digest");

        const std::string digest = digest_fragments("new payload", 3);
        require(store.create_publishing("upload-New029", "new.bin", 11, 101, digest) ==
                    MetadataResult::Ok,
                "create publishing metadata with digest");
        require(store.mark_ready("upload-New029") == MetadataResult::Ok,
                "mark digest metadata ready");
        FileMetadata created;
        require(store.find_ready("upload-New029", created) == MetadataResult::Ok &&
                created.sha256 == digest, "persisted digest differs");
        require(store.create_publishing("upload-Bad029", "bad.bin", 1, 102,
                                        std::string(64, 'A')) == MetadataResult::Error,
                "uppercase digest was accepted");
        std::cout << "PASS: schema v2 preserves legacy rows and persists canonical SHA256\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
// ===== DAY 29 ADDED END =====
