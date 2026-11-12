#include "metadata_database.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

struct TestDirectory
{
    std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::path root = previous / ("day22-test-" + std::to_string(
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

int scalar_int(sqlite3* database, const char* sql)
{
    int value = -1;
    char* detail = nullptr;
    const auto callback = [](void* context, int count, char** values, char**) -> int
    {
        if (count != 1 || values[0] == nullptr) return 1;
        *static_cast<int*>(context) = std::atoi(values[0]);
        return 0;
    };
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

// ===== DAY 29 ADDED BEGIN 创建真实v1迁移夹具 =====
void execute_sql(sqlite3* database, const char* sql)
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
// ===== DAY 29 ADDED END =====

int main()
{
    try
    {
        TestDirectory directory;
        std::filesystem::create_directory("data");

        {
            MetadataDatabase database;
            require(database.open("data/netdisk.db"), "open new database");
            require(database.initialize_schema(), "initialize new schema");
            require(database.error_code() == SQLITE_OK, "success retained an error");
            require(scalar_int(database.get(), "PRAGMA foreign_keys;") == 1,
                    "foreign keys are disabled");
            // ===== DAY 29 MODIFIED BEGIN 当前新库schema版本升级为2 =====
            require(scalar_int(database.get(), "PRAGMA user_version;") == 2,
                    "unexpected schema version");
            require(scalar_int(database.get(),
                    "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' "
                    "AND name IN ('schema_info', 'files');") == 2,
                    "required tables are missing");
            require(scalar_int(database.get(),
                    "SELECT version FROM schema_info WHERE singleton = 1;") == 2,
                    "schema version row is missing");
            require(scalar_int(database.get(),
                    "SELECT COUNT(*) FROM pragma_table_info('files') WHERE name='sha256';") == 1,
                    "sha256 column is missing");
            // ===== DAY 29 MODIFIED END =====
        }

        {
            MetadataDatabase reopened;
            require(reopened.open("data/netdisk.db"), "reopen database");
            require(reopened.initialize_schema(), "repeat schema initialization");
            require(scalar_int(reopened.get(),
                    "SELECT COUNT(*) FROM schema_info;") == 1,
                    "schema initialization is not idempotent");
            // ===== DAY 29 MODIFIED BEGIN 使用版本3模拟未来schema =====
            require(sqlite3_exec(reopened.get(), "PRAGMA user_version = 3;",
                                 nullptr, nullptr, nullptr) == SQLITE_OK,
                    "prepare newer schema fixture");
            // ===== DAY 29 MODIFIED END =====
        }

        {
            MetadataDatabase newer;
            require(newer.open("data/netdisk.db"), "open newer schema fixture");
            require(!newer.initialize_schema(), "newer schema version accepted");
            require(newer.error_code() != SQLITE_OK &&
                    !newer.error_message().empty(), "version failure has no diagnostic");
            require(scalar_int(newer.get(), "PRAGMA user_version;") == 3,
                    "newer schema version was overwritten");
        }

        // ===== DAY 29 ADDED BEGIN 验证v1迁移保留旧行并新增NULL摘要 =====
        sqlite3* legacy_raw = nullptr;
        require(sqlite3_open("data/legacy.db", &legacy_raw) == SQLITE_OK,
                "open legacy fixture");
        execute_sql(legacy_raw, R"SQL(
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
)SQL");
        require(sqlite3_close(legacy_raw) == SQLITE_OK, "close legacy fixture");

        MetadataDatabase legacy;
        require(legacy.open("data/legacy.db"), "reopen legacy fixture");
        require(legacy.initialize_schema(), "migrate legacy schema");
        require(scalar_int(legacy.get(), "PRAGMA user_version;") == 2 &&
                scalar_int(legacy.get(),
                    "SELECT COUNT(*) FROM files WHERE id='upload-Old029' "
                    "AND sha256 IS NULL;") == 1,
                "legacy row or unknown digest was not preserved");
        // ===== DAY 29 ADDED END =====

        {
            std::ofstream blocked("blocked", std::ios::binary);
            blocked << "ordinary file";
        }
        MetadataDatabase open_failure;
        require(!open_failure.open("blocked/netdisk.db"), "invalid parent opened");
        require(open_failure.error_code() != SQLITE_OK &&
                !open_failure.error_message().empty(), "open failure has no diagnostic");

        {
            std::ofstream corrupt("corrupt.db", std::ios::binary);
            corrupt << "not a sqlite database";
        }
        MetadataDatabase schema_failure;
        require(schema_failure.open("corrupt.db"), "open corrupt fixture");
        require(!schema_failure.initialize_schema(), "corrupt schema initialized");
        require(schema_failure.error_code() != SQLITE_OK &&
                !schema_failure.error_message().empty(), "schema failure has no diagnostic");

        std::cout << "PASS: sqlite lifecycle, schema version and failure diagnostics\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
