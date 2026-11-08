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
            require(scalar_int(database.get(), "PRAGMA user_version;") == 1,
                    "unexpected schema version");
            require(scalar_int(database.get(),
                    "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' "
                    "AND name IN ('schema_info', 'files');") == 2,
                    "required tables are missing");
            require(scalar_int(database.get(),
                    "SELECT version FROM schema_info WHERE singleton = 1;") == 1,
                    "schema version row is missing");
        }

        {
            MetadataDatabase reopened;
            require(reopened.open("data/netdisk.db"), "reopen database");
            require(reopened.initialize_schema(), "repeat schema initialization");
            require(scalar_int(reopened.get(),
                    "SELECT COUNT(*) FROM schema_info;") == 1,
                    "schema initialization is not idempotent");
            require(sqlite3_exec(reopened.get(), "PRAGMA user_version = 2;",
                                 nullptr, nullptr, nullptr) == SQLITE_OK,
                    "prepare newer schema fixture");
        }

        {
            MetadataDatabase newer;
            require(newer.open("data/netdisk.db"), "open newer schema fixture");
            require(!newer.initialize_schema(), "newer schema version accepted");
            require(newer.error_code() != SQLITE_OK &&
                    !newer.error_message().empty(), "version failure has no diagnostic");
            require(scalar_int(newer.get(), "PRAGMA user_version;") == 2,
                    "newer schema version was overwritten");
        }

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
