#pragma once

#include <sqlite3.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// ===== DAY 33A ADDED BEGIN 提取文件元数据仓库稳定接口 =====
struct FileMetadata
{
    std::string id;
    std::string name;
    std::uint64_t size = 0;
    std::int64_t created_at = 0;
    std::string sha256;
};

struct FilePageRequest
{
    std::size_t limit = 100;
    bool has_cursor = false;
    std::int64_t cursor_created_at = 0;
    std::string cursor_id;
};

enum class MetadataResult
{
    Ok,
    NotFound,
    Conflict,
    Error
};

class FileMetadataStore
{
public:
    explicit FileMetadataStore(sqlite3* database) noexcept;
    FileMetadataStore(const FileMetadataStore&) = delete;
    FileMetadataStore& operator=(const FileMetadataStore&) = delete;
    ~FileMetadataStore() noexcept;

    bool prepare();
    MetadataResult create_publishing(std::string_view id, std::string_view name,
                                     std::uint64_t size, std::int64_t created_at,
                                     std::string_view sha256 = {});
    MetadataResult mark_ready(std::string_view id);
    MetadataResult cancel_publishing(std::string_view id);
    MetadataResult list_publishing(std::vector<FileMetadata>& output);
    MetadataResult mark_deleting(std::string_view id);
    MetadataResult erase_deleting(std::string_view id);
    MetadataResult list_deleting(std::vector<FileMetadata>& output);
    MetadataResult list_ready(const FilePageRequest& page,
                              std::vector<FileMetadata>& output,
                              bool& has_more);
    MetadataResult find_ready(std::string_view id, FileMetadata& output);
    MetadataResult rename_ready(std::string_view id, std::string_view name);
    MetadataResult set_sha256_if_missing(std::string_view id, std::uint64_t size,
                                         std::string_view sha256);
    int error_code() const noexcept;
    const std::string& error_message() const noexcept;

private:
    class StatementReset;

    static int primary_code(int code) noexcept;
    bool prepare_statement(sqlite3_stmt*& statement, const char* sql,
                           const char* operation);
    bool start_statement(sqlite3_stmt* statement, const char* operation);
    bool bind_text(sqlite3_stmt* statement, int index, std::string_view value,
                   const char* operation);
    bool bind_nullable_text(sqlite3_stmt* statement, int index,
                            std::string_view value, const char* operation);
    bool bind_integer(sqlite3_stmt* statement, int index, sqlite3_int64 value,
                      const char* operation);
    bool execute_command(const char* sql, const char* operation);
    bool begin_transaction(const char* operation);
    bool commit_transaction(const char* operation);
    void rollback_transaction() noexcept;
    MetadataResult change_state(sqlite3_stmt* statement, std::string_view id,
                                const char* operation,
                                const char* begin_operation,
                                const char* commit_operation);
    bool read_row(sqlite3_stmt* statement, FileMetadata& output,
                  const char* operation);
    void remember_error(const char* operation, int code, const char* detail);
    void clear_error();

    sqlite3* database_ = nullptr;
    sqlite3_stmt* create_ = nullptr;
    sqlite3_stmt* ready_ = nullptr;
    sqlite3_stmt* cancel_ = nullptr;
    sqlite3_stmt* list_publishing_ = nullptr;
    sqlite3_stmt* mark_deleting_ = nullptr;
    sqlite3_stmt* erase_deleting_ = nullptr;
    sqlite3_stmt* list_deleting_ = nullptr;
    sqlite3_stmt* list_ = nullptr;
    sqlite3_stmt* find_ = nullptr;
    sqlite3_stmt* rename_ = nullptr;
    sqlite3_stmt* set_digest_ = nullptr;
    bool prepared_ = false;
    int error_code_ = SQLITE_OK;
    std::string error_message_;
};
// ===== DAY 33A ADDED END =====
