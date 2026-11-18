#pragma once

#include <sqlite3.h>

#include <string>

// ===== DAY 33A ADDED BEGIN 提取元数据库稳定接口 =====
class MetadataDatabase
{
public:
    MetadataDatabase() = default;
    MetadataDatabase(const MetadataDatabase&) = delete;
    MetadataDatabase& operator=(const MetadataDatabase&) = delete;
    ~MetadataDatabase() noexcept;

    bool open(const char* path);
    bool initialize_schema();
    sqlite3* get() const noexcept;
    int error_code() const noexcept;
    const std::string& error_message() const noexcept;

private:
    bool read_integer(const char* sql, const char* operation, int& output);
    bool execute(const char* sql, const char* operation);
    void remember_error(const char* operation, int code, const char* detail);
    void clear_error();
    void close() noexcept;

    sqlite3* database_ = nullptr;
    int error_code_ = SQLITE_OK;
    std::string error_message_;
};
// ===== DAY 33A ADDED END =====
