#pragma once

#include <sqlite3.h>

#include <cstdlib>
#include <limits>
#include <string>

class MetadataDatabase
{
public:
    MetadataDatabase() = default;
    MetadataDatabase(const MetadataDatabase&) = delete;
    MetadataDatabase& operator=(const MetadataDatabase&) = delete;

    ~MetadataDatabase() noexcept
    {
        close();
    }

    bool open(const char* path)
    {
        if (database_ != nullptr)
        {
            remember_error("open metadata database", SQLITE_MISUSE,
                           "connection is already open");
            return false;
        }

        sqlite3* opened = nullptr;
        const int result = sqlite3_open_v2(
            path, &opened, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
        database_ = opened;
        if (result != SQLITE_OK)
        {
            remember_error("open metadata database", result,
                           database_ == nullptr ? sqlite3_errstr(result)
                                                : sqlite3_errmsg(database_));
            close();
            return false;
        }

        int configure_result = sqlite3_extended_result_codes(database_, 1);
        if (configure_result == SQLITE_OK)
            configure_result = sqlite3_busy_timeout(database_, 3000);
        if (configure_result != SQLITE_OK)
        {
            remember_error("configure metadata database", configure_result,
                           sqlite3_errmsg(database_));
            close();
            return false;
        }

        clear_error();
        return true;
    }

    bool initialize_schema()
    {
        if (database_ == nullptr)
        {
            remember_error("initialize metadata schema", SQLITE_MISUSE,
                           "connection is not open");
            return false;
        }

        if (!execute("PRAGMA foreign_keys = ON;", "enable foreign keys"))
            return false;

        int version = 0;
        if (!read_integer("PRAGMA user_version;", "read schema version", version))
            return false;
        if (version != 0 && version != 1)
        {
            const std::string detail = "unsupported schema version " +
                std::to_string(version);
            remember_error("initialize metadata schema", SQLITE_ERROR, detail.c_str());
            return false;
        }

        static constexpr const char* schema = R"SQL(
BEGIN IMMEDIATE;
CREATE TABLE IF NOT EXISTS schema_info (
    singleton INTEGER NOT NULL PRIMARY KEY CHECK (singleton = 1),
    version INTEGER NOT NULL CHECK (version = 1)
);
INSERT OR IGNORE INTO schema_info(singleton, version) VALUES(1, 1);
CREATE TABLE IF NOT EXISTS files (
    id TEXT NOT NULL PRIMARY KEY,
    name TEXT NOT NULL CHECK (length(CAST(name AS BLOB)) BETWEEN 1 AND 255),
    size INTEGER NOT NULL CHECK (size >= 0),
    state TEXT NOT NULL CHECK (state IN ('publishing', 'ready', 'deleting')),
    created_at INTEGER NOT NULL CHECK (created_at >= 0)
);
CREATE INDEX IF NOT EXISTS files_state_created_index
    ON files(state, created_at, id);
PRAGMA user_version = 1;
COMMIT;
)SQL";

        if (!execute(schema, "initialize metadata schema"))
        {
            sqlite3_exec(database_, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }

        int stored_version = 0;
        if (!read_integer(
                "SELECT version FROM schema_info WHERE singleton = 1;",
                "verify schema version", stored_version))
            return false;
        if (stored_version != 1)
        {
            remember_error("verify schema version", SQLITE_ERROR,
                           "schema_info does not contain version 1");
            return false;
        }

        clear_error();
        return true;
    }

    sqlite3* get() const noexcept
    {
        return database_;
    }

    int error_code() const noexcept
    {
        return error_code_;
    }

    const std::string& error_message() const noexcept
    {
        return error_message_;
    }

private:
    bool read_integer(const char* sql, const char* operation, int& output)
    {
        struct IntegerResult
        {
            int value = 0;
            bool received = false;
        } result_value;
        const auto callback = [](void* context, int count, char** values, char**) -> int
        {
            auto& result = *static_cast<IntegerResult*>(context);
            if (count != 1 || values[0] == nullptr || result.received) return 1;
            char* end = nullptr;
            const long value = std::strtol(values[0], &end, 10);
            if (end == values[0] || *end != '\0' || value < 0 ||
                value > std::numeric_limits<int>::max())
                return 1;
            result.value = static_cast<int>(value);
            result.received = true;
            return 0;
        };

        char* detail = nullptr;
        const int result = sqlite3_exec(
            database_, sql, callback, &result_value, &detail);
        if (result != SQLITE_OK || !result_value.received)
        {
            const int code = result == SQLITE_OK ? SQLITE_ERROR : result;
            remember_error(operation, code, detail == nullptr
                ? (result == SQLITE_OK ? "query returned no integer" : sqlite3_errmsg(database_))
                : detail);
            sqlite3_free(detail);
            return false;
        }

        sqlite3_free(detail);
        output = result_value.value;
        return true;
    }

    bool execute(const char* sql, const char* operation)
    {
        char* detail = nullptr;
        const int result = sqlite3_exec(database_, sql, nullptr, nullptr, &detail);
        if (result == SQLITE_OK)
        {
            sqlite3_free(detail);
            return true;
        }

        remember_error(operation, result,
                       detail == nullptr ? sqlite3_errmsg(database_) : detail);
        sqlite3_free(detail);
        return false;
    }

    void remember_error(const char* operation, int code, const char* detail)
    {
        error_code_ = code;
        error_message_ = operation;
        error_message_ += ": ";
        error_message_ += detail == nullptr ? sqlite3_errstr(code) : detail;
    }

    void clear_error()
    {
        error_code_ = SQLITE_OK;
        error_message_.clear();
    }

    void close() noexcept
    {
        if (database_ == nullptr) return;
        sqlite3_close_v2(database_);
        database_ = nullptr;
    }

    sqlite3* database_ = nullptr;
    int error_code_ = SQLITE_OK;
    std::string error_message_;
};
